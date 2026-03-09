#include <exec/types.h>
#include <intuition/intuition.h>
#include <graphics/gfx.h>
#include <graphics/rastport.h>
#include <proto/exec.h>
#include <proto/intuition.h>
#include <proto/graphics.h>

#include <stdio.h>
#include <string.h>

#include "bridge_client.h"

struct IntuitionBase *IntuitionBase = NULL;
struct GfxBase *GfxBase = NULL;

#define VERSION "2.0-bridge"

static LONG loop_count = 0;
static LONG running = 1;
static LONG bridge_ok = 0;
static char display_msg[256] = "Waiting for message...";
static char prev_msg[256] = "";

/* Custom command handler */
static void cmd_handler(ULONG id, const char *data)
{
    AB_I("Received command %lu: %s", id, data);
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

    printf("hello_world v%s\n", VERSION);

    /* Connect to AmigaBridge daemon */
    if (ab_init("hello_world") != 0) {
        printf("  Bridge: NOT FOUND (is amiga-bridge running?)\n");
        bridge_ok = 0;
    } else {
        printf("  Bridge: CONNECTED\n");
        bridge_ok = 1;
    }

    AB_I("Hello World v%s starting up", VERSION);

    /* Register variables for remote inspection */
    ab_register_var("loop_count", AB_TYPE_I32, &loop_count);
    ab_register_var("running", AB_TYPE_I32, &running);
    ab_register_var("message", AB_TYPE_STR, display_msg);
    ab_set_cmd_handler(cmd_handler);

    /* Open a simple window */
    win = OpenWindowTags(NULL,
        WA_Left, 50,
        WA_Top, 50,
        WA_Width, 320,
        WA_Height, 100,
        WA_Title, (ULONG)(bridge_ok ?
            "Hello Amiga v2.0 [Bridge: OK]" :
            "Hello Amiga v2.0 [Bridge: OFF]"),
        WA_CloseGadget, TRUE,
        WA_DragBar, TRUE,
        WA_DepthGadget, TRUE,
        WA_IDCMP, IDCMP_CLOSEWINDOW,
        WA_Activate, TRUE,
        TAG_DONE);

    if (!win) {
        AB_E("Failed to open window");
        ab_cleanup();
        CloseLibrary((struct Library *)IntuitionBase);
        return 1;
    }

    AB_I("Window opened successfully");

    /* Main loop */
    while (running) {
        /* Check for window messages */
        while ((msg = (struct IntuiMessage *)GetMsg(win->UserPort))) {
            class = msg->Class;
            ReplyMsg((struct Message *)msg);

            if (class == IDCMP_CLOSEWINDOW) {
                AB_I("Close window requested");
                running = 0;
            }
        }

        loop_count++;

        /* Log every 100 iterations */
        if ((loop_count % 100) == 0) {
            AB_D("Loop iteration %ld", loop_count);
        }

        /* Heartbeat every 500 iterations */
        if ((++hb_counter % 500) == 0) {
            ab_heartbeat();
        }

        /* Poll for commands from bridge daemon */
        ab_poll();

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

    AB_I("Hello World shutting down");

    CloseWindow(win);
    ab_cleanup();
    CloseLibrary((struct Library *)GfxBase);
    CloseLibrary((struct Library *)IntuitionBase);

    return 0;
}
