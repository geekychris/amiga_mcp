/*
 * input_inject.c - Input event injection for AmigaBridge
 *
 * Uses AmigaOS input.device to inject keyboard and mouse events.
 * The input device is lazily opened on first use and kept open
 * until cleanup.
 *
 * Injection is performed with SendIO + a bounded CheckIO/Delay poll loop
 * instead of a blocking DoIO. This is critical: when an injected
 * left-button-down lands on an Intuition window drag/size gadget (or a
 * Workbench icon), Intuition enters a modal drag loop that stalls the
 * input.device handler chain until the *physical* mouse button is
 * released. A blocking DoIO on the next event would then hang the
 * single-threaded daemon forever (no heartbeat, no TCP). We instead poll
 * for completion for a bounded number of ticks, then abandon a stuck
 * request, keep the daemon alive, and refuse further injection until the
 * stuck request drains. (An earlier timer.device + dual-port Wait() design
 * had an AbortIO double-reply race that spuriously timed out ~every other
 * event; this poll loop is race-free.)
 *
 * Mouse move and button events also carry the currently-held button
 * qualifiers (IEQUALIFIER_*BUTTON) so that in-application drags
 * (scrollbars, sliders, text selection) are recognised as "button held
 * while moving".
 *
 * Keyboard qualifier keys (Shift/Ctrl/Alt/Amiga) are tracked the same way.
 * input.device passes an injected IND_WRITEEVENT through to Intuition with
 * its ie_Qualifier as-supplied - it does NOT re-derive qualifiers from
 * preceding qualifier-key events the way real keyboard.device does. So
 * injecting a Ctrl-down rawkey followed by F1 is not enough on its own: the
 * F1 event must itself carry IEQUALIFIER_CONTROL or the app sees a bare F1.
 * We therefore maintain g_keyQuals: a qualifier-key down sets its bit, a
 * key-up clears it, and the bit is OR'd into every subsequent injected
 * event. Without this, Ctrl/Alt/Shift bindings are silently dropped.
 */

#include <exec/types.h>
#include <exec/memory.h>
#include <exec/io.h>
#include <devices/input.h>
#include <devices/inputevent.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#include "bridge_internal.h"

/* Bounded wait for a single injected event to be processed. A normal event
 * completes in well under one tick; only an event swallowed by a modal
 * Intuition drag loop stalls. We poll completion with CheckIO and Delay()
 * (1 tick = 20ms) up to this many ticks, then give up and mark the
 * input.device wedged. This is deliberately race-free: no timer.device
 * request, no dual-port Wait(), so there is no abort/double-reply hazard. */
#define INJECT_POLL_TICKS 15   /* ~300 ms max bounded stall */

static struct MsgPort *inputPort = NULL;
static struct IOStdReq *inputReq = NULL;
static BOOL inputOpen = FALSE;

/* TRUE while a previously-injected event is still stuck in the input.device
 * (Intuition modal drag). We refuse new injections until it drains. */
static BOOL inputWedged = FALSE;

/* Currently-held mouse button qualifier bits (IEQUALIFIER_LEFTBUTTON, ...). */
static UWORD g_heldQuals = 0;

/* Currently-held keyboard qualifier bits (IEQUALIFIER_CONTROL/SHIFT/ALT/...).
 * Updated as qualifier keys are injected down/up, OR'd into every event. */
static UWORD g_keyQuals = 0;

/*
 * key_qualifier_bit - Map an Amiga raw key code to its IEQUALIFIER bit.
 * Returns 0 for non-qualifier keys.
 */
static UWORD key_qualifier_bit(UWORD raw)
{
    switch (raw) {
        case 0x60: return IEQUALIFIER_LSHIFT;
        case 0x61: return IEQUALIFIER_RSHIFT;
        case 0x62: return IEQUALIFIER_CAPSLOCK;
        case 0x63: return IEQUALIFIER_CONTROL;
        case 0x64: return IEQUALIFIER_LALT;
        case 0x65: return IEQUALIFIER_RALT;
        case 0x66: return IEQUALIFIER_LCOMMAND;
        case 0x67: return IEQUALIFIER_RCOMMAND;
        default:   return 0;
    }
}

static int ensure_input_open(void)
{
    if (inputOpen) return 0;

    inputPort = CreateMsgPort();
    if (!inputPort) return -1;

    inputReq = (struct IOStdReq *)CreateIORequest(inputPort, sizeof(struct IOStdReq));
    if (!inputReq) {
        DeleteMsgPort(inputPort);
        inputPort = NULL;
        return -1;
    }

    if (OpenDevice((CONST_STRPTR)"input.device", 0,
                   (struct IORequest *)inputReq, 0) != 0) {
        DeleteIORequest((struct IORequest *)inputReq);
        DeleteMsgPort(inputPort);
        inputReq = NULL;
        inputPort = NULL;
        return -1;
    }

    inputOpen = TRUE;
    return 0;
}

/*
 * inject_event - Send one InputEvent to input.device without ever blocking
 * the daemon indefinitely.
 *
 * Sends the event async (SendIO) and polls for completion with CheckIO,
 * sleeping one tick between checks, up to INJECT_POLL_TICKS. A normal event
 * completes on the first check (no Delay at all). An event swallowed by a
 * modal Intuition window drag/size loop never completes until the physical
 * button is released, so after the bounded poll we AbortIO and, if that
 * doesn't reclaim it, mark the input.device wedged and refuse further
 * injection until it drains.
 *
 * Returns:
 *    0  event injected and processed
 *   -1  timed out (input.device stuck in a modal Intuition drag)
 *   -2  refused: a prior injection is still wedged; release the physical button
 *   -3  could not open input.device
 */
static int inject_event(struct InputEvent *ie)
{
    int i;

    if (ensure_input_open() != 0) return -3;

    /* If a prior event is still stuck, see whether it has finally drained
     * (e.g. the user released the physical button). If not, refuse. */
    if (inputWedged) {
        if (CheckIO((struct IORequest *)inputReq)) {
            WaitIO((struct IORequest *)inputReq);
            inputWedged = FALSE;
        } else {
            return -2;
        }
    }

    inputReq->io_Command = IND_WRITEEVENT;
    inputReq->io_Data = (APTR)ie;
    inputReq->io_Length = sizeof(struct InputEvent);
    SendIO((struct IORequest *)inputReq);

    /* Fast path: most events are done immediately. Only poll/sleep if not. */
    for (i = 0; i < INJECT_POLL_TICKS; i++) {
        if (CheckIO((struct IORequest *)inputReq)) {
            WaitIO((struct IORequest *)inputReq);
            return 0;
        }
        Delay(1);  /* 1 tick = 20 ms */
    }

    /* Still pending: stuck in a modal drag. Try to abort; if that reclaims
     * it we are clean, otherwise leave it pending (do NOT WaitIO - that could
     * block forever) and refuse further injection until it drains. */
    AbortIO((struct IORequest *)inputReq);
    if (CheckIO((struct IORequest *)inputReq)) {
        WaitIO((struct IORequest *)inputReq);
        return -1;
    }
    inputWedged = TRUE;
    return -1;
}

/* Map an inject_event() status to a trailing note for the OK/ERR line. */
static const char *inject_note(int rc)
{
    switch (rc) {
        case 0:  return "";
        case -1: return " (timeout: input.device busy - modal window drag? release physical mouse button)";
        case -2: return " (refused: input still wedged - release physical mouse button on the Amiga)";
        default: return " (error: cannot open input.device)";
    }
}

/*
 * input_handle_key - Inject a keyboard event
 *
 * Args format: rawkey_hex|updown
 *   rawkey_hex: Amiga raw key code in hex (e.g. "45" for Escape, "44" for Return)
 *   updown: "down" or "up"
 */
void input_handle_key(const char *args)
{
    static char linebuf[256];
    struct InputEvent ie;
    UWORD rawkey;
    UWORD qbit;
    const char *p;
    BOOL isUp = FALSE;
    int rc;

    if (!args || !args[0]) {
        protocol_send_raw("ERR|INPUTKEY|Missing arguments (rawkey_hex|up/down)");
        return;
    }

    rawkey = (UWORD)strtoul(args, NULL, 16);
    p = strchr(args, '|');
    if (p && (p[1] == 'u' || p[1] == 'U')) {
        isUp = TRUE;
    }

    /* If this is a qualifier key, update the held-qualifier state so that it
     * (and subsequent keys) carry the modifier. Caps Lock is a toggle: it
     * flips on key-down and is unaffected by key-up; the others are momentary
     * (down sets, up clears). */
    qbit = key_qualifier_bit(rawkey);
    if (qbit) {
        if (qbit == IEQUALIFIER_CAPSLOCK) {
            if (!isUp) g_keyQuals ^= IEQUALIFIER_CAPSLOCK;
        } else if (isUp) {
            g_keyQuals &= ~qbit;
        } else {
            g_keyQuals |= qbit;
        }
    }

    memset(&ie, 0, sizeof(ie));
    ie.ie_Class = IECLASS_RAWKEY;
    ie.ie_Code = rawkey;
    if (isUp) ie.ie_Code |= IECODE_UP_PREFIX;
    ie.ie_Qualifier = g_heldQuals | g_keyQuals;

    rc = inject_event(&ie);
    sprintf(linebuf, "%s|INPUTKEY|Key %lx %s%s",
        rc == 0 ? "OK" : "ERR",
        (unsigned long)rawkey, isUp ? "up" : "down", inject_note(rc));
    protocol_send_raw(linebuf);
}

/*
 * input_handle_mouse_move - Inject a relative mouse movement
 *
 * Args format: dx|dy
 */
void input_handle_mouse_move(const char *args)
{
    static char linebuf[256];
    struct InputEvent ie;
    LONG dx, dy;
    const char *p;
    int rc;

    if (!args || !args[0]) {
        protocol_send_raw("ERR|INPUTMOVE|Missing arguments (dx|dy)");
        return;
    }

    dx = strtol(args, NULL, 10);
    p = strchr(args, '|');
    if (!p) {
        protocol_send_raw("ERR|INPUTMOVE|Missing dy");
        return;
    }
    dy = strtol(p + 1, NULL, 10);

    memset(&ie, 0, sizeof(ie));
    ie.ie_Class = IECLASS_RAWMOUSE;
    ie.ie_Code = IECODE_NOBUTTON;
    /* Carry held-button qualifiers so Intuition/apps see "moving while the
     * button is held" (required for drags to track), plus any held keyboard
     * qualifiers (e.g. Shift-drag selection). */
    ie.ie_Qualifier = g_heldQuals | g_keyQuals;
    ie.ie_X = (WORD)dx;
    ie.ie_Y = (WORD)dy;

    rc = inject_event(&ie);
    sprintf(linebuf, "%s|INPUTMOVE|Mouse moved %ld,%ld%s",
        rc == 0 ? "OK" : "ERR", (long)dx, (long)dy, inject_note(rc));
    protocol_send_raw(linebuf);
}

/*
 * input_handle_mouse_button - Inject a mouse button event
 *
 * Args format: button|updown
 *   button: "left", "right", or "middle"
 *   updown: "down" or "up"
 */
void input_handle_mouse_button(const char *args)
{
    static char linebuf[256];
    struct InputEvent ie;
    UWORD code = IECODE_LBUTTON;
    UWORD qualBit = IEQUALIFIER_LEFTBUTTON;
    const char *p;
    const char *btnName = "left";
    BOOL isUp = FALSE;
    int rc;

    if (!args || !args[0]) {
        protocol_send_raw("ERR|INPUTCLICK|Missing arguments (left/right/middle|up/down)");
        return;
    }

    if (args[0] == 'r' || args[0] == 'R') {
        code = IECODE_RBUTTON;
        qualBit = IEQUALIFIER_RBUTTON;
        btnName = "right";
    } else if (args[0] == 'm' || args[0] == 'M') {
        code = IECODE_MBUTTON;
        qualBit = IEQUALIFIER_MIDBUTTON;
        btnName = "middle";
    }

    p = strchr(args, '|');
    if (p && (p[1] == 'u' || p[1] == 'U')) {
        code |= IECODE_UP_PREFIX;
        isUp = TRUE;
    }

    /* Update held-button state. A button-down event carries the button in
     * the qualifier; a button-up event reflects the cleared state. */
    if (isUp)
        g_heldQuals &= ~qualBit;
    else
        g_heldQuals |= qualBit;

    memset(&ie, 0, sizeof(ie));
    ie.ie_Class = IECLASS_RAWMOUSE;
    ie.ie_Code = code;
    ie.ie_Qualifier = g_heldQuals | g_keyQuals;
    ie.ie_X = 0;
    ie.ie_Y = 0;

    rc = inject_event(&ie);
    sprintf(linebuf, "%s|INPUTCLICK|Button %s %s%s",
        rc == 0 ? "OK" : "ERR",
        btnName, isUp ? "up" : "down", inject_note(rc));
    protocol_send_raw(linebuf);
}

/*
 * input_cleanup - Close input.device and free resources
 */
void input_cleanup(void)
{
    if (inputOpen) {
        /* Don't WaitIO a possibly-wedged request; just abort and reclaim
         * what we can. */
        if (!inputWedged) {
            CloseDevice((struct IORequest *)inputReq);
        }
        inputOpen = FALSE;
    }
    if (inputReq && !inputWedged) {
        DeleteIORequest((struct IORequest *)inputReq);
        inputReq = NULL;
    }
    if (inputPort && !inputWedged) {
        DeleteMsgPort(inputPort);
        inputPort = NULL;
    }
}
