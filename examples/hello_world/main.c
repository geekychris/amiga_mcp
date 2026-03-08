#include <exec/types.h>
#include <intuition/intuition.h>
#include <graphics/gfx.h>
#include <graphics/rastport.h>
#include <proto/exec.h>
#include <proto/intuition.h>
#include <proto/graphics.h>

#include <stdio.h>
#include <string.h>

#include "debug.h"

struct IntuitionBase *IntuitionBase = NULL;
struct GfxBase *GfxBase = NULL;

static LONG loop_count = 0;
static LONG running = 1;
static char display_msg[256] = "Waiting for message...";
static char prev_msg[256] = "";

/* Custom command handler */
static void cmd_handler(ULONG id, const char *data)
{
    DBG_I("Received command %lu: %s", id, data);
}

int main(void)
{
    struct Window *win;
    struct IntuiMessage *msg;
    ULONG class;
    int hb_counter = 0;

    IntuitionBase = (struct IntuitionBase *)OpenLibrary((CONST_STRPTR)"intuition.library", 36);
    if (!IntuitionBase) return 1;

    GfxBase = (struct GfxBase *)OpenLibrary((CONST_STRPTR)"graphics.library", 36);
    if (!GfxBase) {
        CloseLibrary((struct Library *)IntuitionBase);
        return 1;
    }

    /* Initialize debug serial link */
    if (dbg_init(9600) != 0) {
        /* Debug init failed - continue without debug */
        printf("Warning: debug init failed\n");
    }

    DBG_I("Hello World starting up");

    /* Register variables for remote inspection */
    dbg_register_var("loop_count", DBG_TYPE_I32, &loop_count);
    dbg_register_var("running", DBG_TYPE_I32, &running);
    dbg_register_var("message", DBG_TYPE_STR, display_msg);
    dbg_set_cmd_handler(cmd_handler);

    /* Open a simple window */
    win = OpenWindowTags(NULL,
        WA_Left, 50,
        WA_Top, 50,
        WA_Width, 320,
        WA_Height, 100,
        WA_Title, (ULONG)"Hello Amiga - Debug Demo",
        WA_CloseGadget, TRUE,
        WA_DragBar, TRUE,
        WA_DepthGadget, TRUE,
        WA_IDCMP, IDCMP_CLOSEWINDOW,
        WA_Activate, TRUE,
        TAG_DONE);

    if (!win) {
        DBG_E("Failed to open window");
        dbg_cleanup();
        CloseLibrary((struct Library *)IntuitionBase);
        return 1;
    }

    DBG_I("Window opened successfully");

    /* Main loop */
    while (running) {
        /* Check for window messages */
        while ((msg = (struct IntuiMessage *)GetMsg(win->UserPort))) {
            class = msg->Class;
            ReplyMsg((struct Message *)msg);

            if (class == IDCMP_CLOSEWINDOW) {
                DBG_I("Close window requested");
                running = 0;
            }
        }

        loop_count++;

        /* Log every 100 iterations */
        if ((loop_count % 100) == 0) {
            DBG_D("Loop iteration %ld", loop_count);
        }

        /* Heartbeat every 500 iterations */
        if ((++hb_counter % 500) == 0) {
            dbg_heartbeat();
        }

        /* Poll for debug commands from host */
        dbg_poll();

        /* Redraw message if it changed */
        if (strcmp(display_msg, prev_msg) != 0) {
            struct RastPort *rp = win->RPort;
            SetAPen(rp, 0);  /* background */
            RectFill(rp, 10, 20, 300, 40);
            SetAPen(rp, 1);  /* foreground */
            Move(rp, 10, 35);
            Text(rp, display_msg, strlen(display_msg));
            strncpy(prev_msg, display_msg, sizeof(prev_msg) - 1);
        }

        /* Small delay (~10fps) */
        Delay(5);
    }

    DBG_I("Hello World shutting down");

    CloseWindow(win);
    dbg_cleanup();
    CloseLibrary((struct Library *)GfxBase);
    CloseLibrary((struct Library *)IntuitionBase);

    return 0;
}
