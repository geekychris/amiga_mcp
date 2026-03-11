/*
 * input_inject.c - Input event injection for AmigaBridge
 *
 * Uses AmigaOS input.device to inject keyboard and mouse events.
 * The input device is lazily opened on first use and kept open
 * until cleanup.
 */

#include <exec/types.h>
#include <exec/memory.h>
#include <exec/io.h>
#include <devices/input.h>
#include <devices/inputevent.h>
#include <proto/exec.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#include "bridge_internal.h"

static struct MsgPort *inputPort = NULL;
static struct IOStdReq *inputReq = NULL;
static BOOL inputOpen = FALSE;

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
    const char *p;
    BOOL isUp = FALSE;

    if (!args || !args[0]) {
        protocol_send_raw("ERR|INPUTKEY|Missing arguments (rawkey_hex|up/down)");
        return;
    }

    if (ensure_input_open() != 0) {
        protocol_send_raw("ERR|INPUTKEY|Cannot open input.device");
        return;
    }

    rawkey = (UWORD)strtoul(args, NULL, 16);
    p = strchr(args, '|');
    if (p && (p[1] == 'u' || p[1] == 'U')) {
        isUp = TRUE;
    }

    memset(&ie, 0, sizeof(ie));
    ie.ie_Class = IECLASS_RAWKEY;
    ie.ie_Code = rawkey;
    if (isUp) ie.ie_Code |= IECODE_UP_PREFIX;
    ie.ie_Qualifier = 0;

    inputReq->io_Command = IND_WRITEEVENT;
    inputReq->io_Data = (APTR)&ie;
    inputReq->io_Length = sizeof(struct InputEvent);
    DoIO((struct IORequest *)inputReq);

    sprintf(linebuf, "OK|INPUTKEY|Key %lx %s injected",
        (unsigned long)rawkey, isUp ? "up" : "down");
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

    if (!args || !args[0]) {
        protocol_send_raw("ERR|INPUTMOVE|Missing arguments (dx|dy)");
        return;
    }

    if (ensure_input_open() != 0) {
        protocol_send_raw("ERR|INPUTMOVE|Cannot open input.device");
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
    ie.ie_X = (WORD)dx;
    ie.ie_Y = (WORD)dy;

    inputReq->io_Command = IND_WRITEEVENT;
    inputReq->io_Data = (APTR)&ie;
    inputReq->io_Length = sizeof(struct InputEvent);
    DoIO((struct IORequest *)inputReq);

    sprintf(linebuf, "OK|INPUTMOVE|Mouse moved %ld,%ld",
        (long)dx, (long)dy);
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
    const char *p;
    const char *btnName = "left";

    if (!args || !args[0]) {
        protocol_send_raw("ERR|INPUTCLICK|Missing arguments (left/right/middle|up/down)");
        return;
    }

    if (ensure_input_open() != 0) {
        protocol_send_raw("ERR|INPUTCLICK|Cannot open input.device");
        return;
    }

    if (args[0] == 'r' || args[0] == 'R') {
        code = IECODE_RBUTTON;
        btnName = "right";
    } else if (args[0] == 'm' || args[0] == 'M') {
        code = IECODE_MBUTTON;
        btnName = "middle";
    }

    p = strchr(args, '|');
    if (p && (p[1] == 'u' || p[1] == 'U')) {
        code |= IECODE_UP_PREFIX;
    }

    memset(&ie, 0, sizeof(ie));
    ie.ie_Class = IECLASS_RAWMOUSE;
    ie.ie_Code = code;
    ie.ie_X = 0;
    ie.ie_Y = 0;

    inputReq->io_Command = IND_WRITEEVENT;
    inputReq->io_Data = (APTR)&ie;
    inputReq->io_Length = sizeof(struct InputEvent);
    DoIO((struct IORequest *)inputReq);

    sprintf(linebuf, "OK|INPUTCLICK|Button %s %s",
        btnName, (code & IECODE_UP_PREFIX) ? "up" : "down");
    protocol_send_raw(linebuf);
}

/*
 * input_cleanup - Close input.device and free resources
 */
void input_cleanup(void)
{
    if (inputOpen) {
        CloseDevice((struct IORequest *)inputReq);
        inputOpen = FALSE;
    }
    if (inputReq) {
        DeleteIORequest((struct IORequest *)inputReq);
        inputReq = NULL;
    }
    if (inputPort) {
        DeleteMsgPort(inputPort);
        inputPort = NULL;
    }
}
