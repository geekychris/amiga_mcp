/* SPDX-License-Identifier: MIT */
/* Copyright (c) 2026 Chris Collins <chris@hitorro.com> */
/*
 * claude_busy: a small banner on the Workbench screen that says Claude is
 * testing on this Amiga, so the person at the machine knows to leave it be.
 *
 *   claude_busy [message]     show the banner (one at a time)
 *   claude_busy OFF           take it down
 *
 * It never activates its window, so keys injected by a test still reach the
 * game, and it comes back to the front every two seconds in case a game's
 * window covered it. Its close gadget takes it down too.
 * Builds for classic AmigaOS (68k) and AmigaOS 4 (PPC).
 */
#include <exec/types.h>
#include <exec/ports.h>
#include <devices/timer.h>
#include <dos/dos.h>
#include <intuition/intuition.h>
#include <intuition/screens.h>
#include <graphics/gfx.h>
#include <graphics/view.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/intuition.h>
#include <proto/graphics.h>
#include <string.h>

#ifndef __PPC__
struct IntuitionBase *IntuitionBase;
struct GfxBase *GfxBase;
#endif

#define PORTNAME "CLAUDE_BUSY"
#define TITLE    "CLAUDE IS TESTING"
#define DEFMSG   "Please don't use the Amiga until this goes away"

static const char ver[] = "$VER: claude_busy 1.0 (01.11.2026)";

static void stop_other(void)
{
    struct MsgPort *p;
    Forbid();
    if ((p = FindPort(PORTNAME)) != NULL)
        Signal(p->mp_SigTask, SIGBREAKF_CTRL_C);
    Permit();
}

static void draw(struct Window *win, int w, int h, int tw, int mw, int fh,
                 LONG red, LONG white, LONG yellow, const char *msg)
{
    struct RastPort *rp = win->RPort;
    int x0 = win->BorderLeft, y0 = win->BorderTop;
    SetAPen(rp, red);
    RectFill(rp, x0, y0, x0 + w - 1, y0 + h - 1);
    SetDrMd(rp, JAM1);
    SetAPen(rp, yellow);
    Move(rp, x0 + (w - tw) / 2, y0 + 6 + rp->TxBaseline);
    Text(rp, (STRPTR)TITLE, strlen(TITLE));
    SetAPen(rp, white);
    Move(rp, x0 + (w - mw) / 2, y0 + 12 + fh + rp->TxBaseline);
    Text(rp, (STRPTR)msg, strlen(msg));
}

int main(int argc, char **argv)
{
    char msg[160];
    struct MsgPort *port = NULL, *tport = NULL;
    struct timerequest *tr = NULL;
    struct Screen *scr = NULL;
    struct Window *win = NULL;
    LONG red = -1, white = -1, yellow = -1;
    int i, tw, mw, w, h, fh, timer_open = 0;
    int rc = RETURN_FAIL;

    (void)ver;
    if (argc > 1 && (strcmp(argv[1], "OFF") == 0 || strcmp(argv[1], "off") == 0)) {
        stop_other();
        return RETURN_OK;
    }
    msg[0] = 0;
    for (i = 1; i < argc; i++) {
        if (strlen(msg) + strlen(argv[i]) + 2 >= sizeof(msg)) break;
        if (i > 1) strcat(msg, " ");
        strcat(msg, argv[i]);
    }
    if (!msg[0]) strcpy(msg, DEFMSG);

#ifndef __PPC__
    IntuitionBase = (struct IntuitionBase *)OpenLibrary("intuition.library", 39);
    GfxBase = (struct GfxBase *)OpenLibrary("graphics.library", 39);
    if (!IntuitionBase || !GfxBase) goto out;
#endif

    /* one banner at a time: a new one replaces the old */
    stop_other();
    if (!(port = CreateMsgPort())) goto out;
    port->mp_Node.ln_Name = (char *)PORTNAME;
    port->mp_Node.ln_Pri = 0;
    AddPort(port);

    if (!(tport = CreateMsgPort())) goto out;
    if (!(tr = (struct timerequest *)CreateIORequest(tport, sizeof(*tr)))) goto out;
    if (OpenDevice((STRPTR)"timer.device", UNIT_VBLANK, (struct IORequest *)tr, 0) != 0) goto out;
    timer_open = 1;

    if (!(scr = LockPubScreen(NULL))) goto out;
    fh = scr->RastPort.TxHeight;
    tw = TextLength(&scr->RastPort, (STRPTR)TITLE, strlen(TITLE));
    mw = TextLength(&scr->RastPort, (STRPTR)msg, strlen(msg));
    w = (tw > mw ? tw : mw) + 24;
    h = fh * 2 + 18;
    red = ObtainBestPen(scr->ViewPort.ColorMap, 0xC0000000, 0x10000000, 0x10000000, TAG_DONE);
    white = ObtainBestPen(scr->ViewPort.ColorMap, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, TAG_DONE);
    yellow = ObtainBestPen(scr->ViewPort.ColorMap, 0xFFFFFFFF, 0xE0000000, 0x20000000, TAG_DONE);

    win = OpenWindowTags(NULL,
        WA_PubScreen, (ULONG)scr,
        WA_Title, (ULONG)"Claude",
        WA_InnerWidth, w, WA_InnerHeight, h,
        WA_Left, scr->Width - w - 40, WA_Top, scr->BarHeight + 2,
        WA_DragBar, TRUE, WA_DepthGadget, TRUE, WA_CloseGadget, TRUE,
        WA_MaxWidth, ~0UL, WA_MaxHeight, ~0UL,
        WA_Activate, FALSE, WA_RMBTrap, TRUE, WA_SimpleRefresh, TRUE,
        WA_IDCMP, IDCMP_CLOSEWINDOW | IDCMP_REFRESHWINDOW | IDCMP_NEWSIZE,
        TAG_DONE);
    if (!win) {
        if (red >= 0) ReleasePen(scr->ViewPort.ColorMap, red);
        if (white >= 0) ReleasePen(scr->ViewPort.ColorMap, white);
        if (yellow >= 0) ReleasePen(scr->ViewPort.ColorMap, yellow);
        UnlockPubScreen(NULL, scr);
        goto out;
    }
    UnlockPubScreen(NULL, scr);

    {   /* size it for the font the window really draws with */
        struct RastPort *rp = win->RPort;
        int nw, nh;
        fh = rp->TxHeight;
        tw = TextLength(rp, (STRPTR)TITLE, strlen(TITLE));
        mw = TextLength(rp, (STRPTR)msg, strlen(msg));
        nw = (tw > mw ? tw : mw) + 24;
        nh = fh * 2 + 18;
        if (nw != w || nh != h) {
            int ww = nw + win->BorderLeft + win->BorderRight;
            w = nw; h = nh;
            ChangeWindowBox(win, win->WScreen->Width - ww - 40, win->TopEdge,
                            ww, nh + win->BorderTop + win->BorderBottom);
        }
    }

    for (;;) {
        struct IntuiMessage *im;
        ULONG sigs;
        int done = 0;

        draw(win, w, h, tw, mw, fh, red, white, yellow, msg);

        tr->tr_node.io_Command = TR_ADDREQUEST;
        tr->tr_time.tv_secs = 2;
        tr->tr_time.tv_micro = 0;
        SendIO((struct IORequest *)tr);
        sigs = Wait(SIGBREAKF_CTRL_C | (1UL << win->UserPort->mp_SigBit) | (1UL << tport->mp_SigBit));
        if (!CheckIO((struct IORequest *)tr)) AbortIO((struct IORequest *)tr);
        WaitIO((struct IORequest *)tr);

        while ((im = (struct IntuiMessage *)GetMsg(win->UserPort)) != NULL) {
            if (im->Class == IDCMP_CLOSEWINDOW) done = 1;
            else if (im->Class == IDCMP_REFRESHWINDOW) {
                BeginRefresh(win);
                draw(win, w, h, tw, mw, fh, red, white, yellow, msg);
                EndRefresh(win, TRUE);
            }
            ReplyMsg((struct Message *)im);
        }
        if (done || (sigs & SIGBREAKF_CTRL_C)) break;
        if (sigs & (1UL << tport->mp_SigBit)) WindowToFront(win);
    }
    rc = RETURN_OK;

out:
    if (win) {
        struct Screen *s = win->WScreen;
        CloseWindow(win);
        if (red >= 0) ReleasePen(s->ViewPort.ColorMap, red);
        if (white >= 0) ReleasePen(s->ViewPort.ColorMap, white);
        if (yellow >= 0) ReleasePen(s->ViewPort.ColorMap, yellow);
    }
    if (timer_open) CloseDevice((struct IORequest *)tr);
    if (tr) DeleteIORequest((struct IORequest *)tr);
    if (tport) DeleteMsgPort(tport);
    if (port) { RemPort(port); DeleteMsgPort(port); }
#ifndef __PPC__
    if (GfxBase) CloseLibrary((struct Library *)GfxBase);
    if (IntuitionBase) CloseLibrary((struct Library *)IntuitionBase);
#endif
    return rc;
}
