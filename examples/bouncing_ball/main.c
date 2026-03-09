#include <exec/types.h>
#include <intuition/intuition.h>
#include <graphics/gfxbase.h>
#include <graphics/rastport.h>
#include <proto/exec.h>
#include <proto/intuition.h>
#include <proto/graphics.h>
#include <proto/dos.h>

#include <stdio.h>
#include <string.h>

#include "bridge_client.h"

struct IntuitionBase *IntuitionBase = NULL;
struct GfxBase *GfxBase = NULL;

/* Ball state - all registered as debug variables */
static LONG ball_x = 160;
static LONG ball_y = 100;
static LONG ball_dx = 3;
static LONG ball_dy = 2;
static LONG ball_size = 10;
static ULONG frame_count = 0;

#define WIN_W 320
#define WIN_H 200

/* Custom command handler */
static void cmd_handler(ULONG id, const char *data)
{
    if (strcmp(data, "reset") == 0) {
        ball_x = WIN_W / 2;
        ball_y = WIN_H / 2;
        ball_dx = 3;
        ball_dy = 2;
        AB_I("Ball position reset");
        ab_cmd_respond(id, "ok", "position reset");
    } else {
        AB_W("Unknown command: %s", data);
        ab_cmd_respond(id, "err", "unknown command");
    }
}

static void draw_ball(struct RastPort *rp, LONG x, LONG y, LONG size, UBYTE color)
{
    SetAPen(rp, color);
    RectFill(rp, x - size/2, y - size/2, x + size/2, y + size/2);
}

int main(void)
{
    struct Window *win;
    struct IntuiMessage *msg;
    struct RastPort *rp;
    ULONG class;
    BOOL running = TRUE;
    LONG inner_w, inner_h;

    IntuitionBase = (struct IntuitionBase *)OpenLibrary((CONST_STRPTR)"intuition.library", 36);
    if (!IntuitionBase) return 1;

    GfxBase = (struct GfxBase *)OpenLibrary((CONST_STRPTR)"graphics.library", 36);
    if (!GfxBase) {
        CloseLibrary((struct Library *)IntuitionBase);
        return 1;
    }

    /* Connect to AmigaBridge daemon (also sets task name) */
    if (ab_init("bouncing_ball") != 0) {
        printf("Bridge: NOT FOUND\n");
    } else {
        printf("Bridge: CONNECTED\n");
    }

    AB_I("Bouncing Ball starting");

    /* Register all ball variables for remote inspection */
    ab_register_var("ball_x", AB_TYPE_I32, &ball_x);
    ab_register_var("ball_y", AB_TYPE_I32, &ball_y);
    ab_register_var("ball_dx", AB_TYPE_I32, &ball_dx);
    ab_register_var("ball_dy", AB_TYPE_I32, &ball_dy);
    ab_register_var("ball_size", AB_TYPE_I32, &ball_size);
    ab_register_var("frame_count", AB_TYPE_U32, &frame_count);

    ab_set_cmd_handler(cmd_handler);

    win = OpenWindowTags(NULL,
        WA_Left, 50,
        WA_Top, 30,
        WA_Width, WIN_W,
        WA_Height, WIN_H,
        WA_Title, (ULONG)"Bouncing Ball",
        WA_CloseGadget, TRUE,
        WA_DragBar, TRUE,
        WA_DepthGadget, TRUE,
        WA_IDCMP, IDCMP_CLOSEWINDOW,
        WA_Activate, TRUE,
        WA_GimmeZeroZero, TRUE,
        TAG_DONE);

    if (!win) {
        AB_E("Failed to open window");
        ab_cleanup();
        CloseLibrary((struct Library *)GfxBase);
        CloseLibrary((struct Library *)IntuitionBase);
        return 1;
    }

    rp = win->RPort;
    inner_w = win->GZZWidth;
    inner_h = win->GZZHeight;

    AB_I("Window opened: %ldx%ld", inner_w, inner_h);

    while (running) {
        /* Check for close */
        while ((msg = (struct IntuiMessage *)GetMsg(win->UserPort))) {
            class = msg->Class;
            ReplyMsg((struct Message *)msg);
            if (class == IDCMP_CLOSEWINDOW) {
                running = FALSE;
            }
        }

        if (!running) break;

        /* Erase old ball */
        draw_ball(rp, ball_x, ball_y, ball_size, 0);

        /* Update position */
        ball_x += ball_dx;
        ball_y += ball_dy;

        /* Bounce off walls */
        if (ball_x - ball_size/2 <= 0 || ball_x + ball_size/2 >= inner_w) {
            ball_dx = -ball_dx;
            ball_x += ball_dx;
        }
        if (ball_y - ball_size/2 <= 0 || ball_y + ball_size/2 >= inner_h) {
            ball_dy = -ball_dy;
            ball_y += ball_dy;
        }

        /* Draw new ball */
        draw_ball(rp, ball_x, ball_y, ball_size, 3);

        frame_count++;

        /* Push variables every 60 frames */
        if ((frame_count % 60) == 0) {
            ab_push_var("ball_x");
            ab_push_var("ball_y");
            ab_push_var("frame_count");
            ab_heartbeat();
        }

        /* Log bounces (less frequent than every frame) */
        if ((frame_count % 300) == 0) {
            AB_D("Frame %lu pos=(%ld,%ld)", frame_count, ball_x, ball_y);
        }

        /* Poll for commands from bridge daemon */
        ab_poll();

        /* Check for CTRL-C break signal */
        if (SetSignal(0L, SIGBREAKF_CTRL_C) & SIGBREAKF_CTRL_C) {
            AB_I("Break signal received");
            running = FALSE;
        }

        /* ~20fps delay */
        Delay(3);
    }

    AB_I("Bouncing Ball shutting down");

    CloseWindow(win);
    ab_cleanup();
    CloseLibrary((struct Library *)GfxBase);
    CloseLibrary((struct Library *)IntuitionBase);

    return 0;
}
