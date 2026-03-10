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

#define VERSION "1.0"

static LONG loop_count = 0;
static LONG running = 1;
static LONG bridge_ok = 0;

/* Hook example: called remotely via bridge */
static int hook_status(const char *args, char *result, int bufsize)
{
    sprintf(result, "loop_count=%ld running=%ld", (long)loop_count, (long)running);
    return 0;
}

int main(void)
{
    struct Window *win;
    struct IntuiMessage *msg;
    ULONG class;
    ULONG signals;
    LONG hb_counter = 0;

    IntuitionBase = (struct IntuitionBase *)OpenLibrary((CONST_STRPTR)"intuition.library", 36);
    if (!IntuitionBase) return 1;

    GfxBase = (struct GfxBase *)OpenLibrary((CONST_STRPTR)"graphics.library", 36);
    if (!GfxBase) {
        CloseLibrary((struct Library *)IntuitionBase);
        return 1;
    }

    printf("foo v%s\n", VERSION);

    /* Connect to AmigaBridge daemon */
    if (ab_init("foo") != 0) {
        printf("  Bridge: NOT FOUND (is amiga-bridge running?)\n");
        bridge_ok = 0;
    } else {
        printf("  Bridge: CONNECTED\n");
        bridge_ok = 1;
    }

    AB_I("foo v%s starting", VERSION);

    /* Register variables for remote inspection */
    ab_register_var("loop_count", AB_TYPE_I32, &loop_count);
    ab_register_var("running", AB_TYPE_I32, &running);

    /* Register hooks for remote control */
    ab_register_hook("status", "Get current status", hook_status);

    /* Open window */
    win = OpenWindowTags(NULL,
        WA_Left, 50,
        WA_Top, 50,
        WA_Width, 320,
        WA_Height, 150,
        WA_Title, (ULONG)(bridge_ok ?
            "foo v1.0 [Bridge: OK]" :
            "foo v1.0 [Bridge: OFF]"),
        WA_CloseGadget, TRUE,
        WA_DragBar, TRUE,
        WA_DepthGadget, TRUE,
        WA_IDCMP, IDCMP_CLOSEWINDOW,
        WA_Activate, TRUE,
        TAG_DONE);

    if (!win) {
        AB_E("Failed to open window");
        ab_cleanup();
        CloseLibrary((struct Library *)GfxBase);
        CloseLibrary((struct Library *)IntuitionBase);
        return 1;
    }

    AB_I("Window opened successfully");

    /* Main loop */
    while (running) {
        /* Check CTRL-C */
        signals = SetSignal(0L, 0L);
        if (signals & SIGBREAKF_CTRL_C) {
            AB_I("CTRL-C received");
            running = 0;
            break;
        }

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

        /* Log every 500 iterations */
        if ((loop_count % 500) == 0) {
            AB_D("Loop iteration %ld", (long)loop_count);
        }

        /* Heartbeat every 500 iterations */
        if ((++hb_counter % 500) == 0) {
            ab_heartbeat();
        }

        /* Poll for commands from bridge daemon */
        ab_poll();

        /* TODO: Add your rendering/logic here */

        Delay(5);  /* ~10fps */
    }

    AB_I("foo shutting down");

    CloseWindow(win);
    ab_cleanup();
    CloseLibrary((struct Library *)GfxBase);
    CloseLibrary((struct Library *)IntuitionBase);

    printf("foo finished.\n");
    return 0;
}
