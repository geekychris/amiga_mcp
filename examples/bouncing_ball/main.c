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

#include "debug.h"

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
#define BORDER_L 4
#define BORDER_T 11
#define BORDER_R 4
#define BORDER_B 4

/* Custom command handler */
static void cmd_handler(ULONG id, const char *data)
{
    char response[128];
    int len;

    if (strcmp(data, "reset") == 0) {
        ball_x = WIN_W / 2;
        ball_y = WIN_H / 2;
        ball_dx = 3;
        ball_dy = 2;
        DBG_I("Ball position reset");
        len = snprintf(response, sizeof(response), "CMD|%lu|ok|position reset\n", id);
    } else {
        len = snprintf(response, sizeof(response), "CMD|%lu|err|unknown command: %s\n", id, data);
    }
    /* Response is sent via log for now */
    (void)len;
    (void)response;
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

    if (dbg_init(9600) != 0) {
        /* Continue without debug */
    }

    DBG_I("Bouncing Ball starting");

    /* Register all ball variables for remote inspection */
    dbg_register_var("ball_x", DBG_TYPE_I32, &ball_x);
    dbg_register_var("ball_y", DBG_TYPE_I32, &ball_y);
    dbg_register_var("ball_dx", DBG_TYPE_I32, &ball_dx);
    dbg_register_var("ball_dy", DBG_TYPE_I32, &ball_dy);
    dbg_register_var("ball_size", DBG_TYPE_I32, &ball_size);
    dbg_register_var("frame_count", DBG_TYPE_U32, &frame_count);

    dbg_set_cmd_handler(cmd_handler);

    win = OpenWindowTags(NULL,
        WA_Left, 50,
        WA_Top, 30,
        WA_Width, WIN_W,
        WA_Height, WIN_H,
        WA_Title, (ULONG)"Bouncing Ball - Debug Demo",
        WA_CloseGadget, TRUE,
        WA_DragBar, TRUE,
        WA_DepthGadget, TRUE,
        WA_IDCMP, IDCMP_CLOSEWINDOW,
        WA_Activate, TRUE,
        WA_GimmeZeroZero, TRUE,
        TAG_DONE);

    if (!win) {
        DBG_E("Failed to open window");
        dbg_cleanup();
        CloseLibrary((struct Library *)GfxBase);
        CloseLibrary((struct Library *)IntuitionBase);
        return 1;
    }

    rp = win->RPort;
    inner_w = win->GZZWidth;
    inner_h = win->GZZHeight;

    DBG_I("Window opened: inner %ldx%ld", inner_w, inner_h);

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
            DBG_D("Bounce X at %ld", ball_x);
        }
        if (ball_y - ball_size/2 <= 0 || ball_y + ball_size/2 >= inner_h) {
            ball_dy = -ball_dy;
            ball_y += ball_dy;
            DBG_D("Bounce Y at %ld", ball_y);
        }

        /* Draw new ball */
        draw_ball(rp, ball_x, ball_y, ball_size, 3);

        frame_count++;

        /* Heartbeat every 60 frames */
        if ((frame_count % 60) == 0) {
            dbg_heartbeat();
        }

        /* Poll for debug commands */
        dbg_poll();

        /* ~20fps delay */
        Delay(3);
    }

    DBG_I("Bouncing Ball shutting down");

    CloseWindow(win);
    dbg_cleanup();
    CloseLibrary((struct Library *)GfxBase);
    CloseLibrary((struct Library *)IntuitionBase);

    return 0;
}
