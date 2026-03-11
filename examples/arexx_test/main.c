/*
 * arexx_test - ARexx port test application
 *
 * Creates a public ARexx port "AREXX_TEST" and responds to commands:
 *   HELLO           - Returns a greeting
 *   SAY <text>      - Displays text in the window, returns OK
 *   GETCOUNT        - Returns the current loop counter
 *   GETTIME         - Returns uptime in seconds
 *   ADD <a> <b>     - Returns a + b
 *   REVERSE <text>  - Returns text reversed
 *   COLOR <pen>     - Changes text color (0-7), returns old color
 *   QUIT            - Closes the app
 *   Any other       - Returns "UNKNOWN COMMAND"
 */

#include <exec/types.h>
#include <exec/ports.h>
#include <intuition/intuition.h>
#include <graphics/gfx.h>
#include <graphics/rastport.h>
#include <rexx/storage.h>
#include <rexx/rxslib.h>
#include <rexx/errors.h>
#include <proto/exec.h>
#include <proto/intuition.h>
#include <proto/graphics.h>
#include <proto/rexxsyslib.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "bridge_client.h"

struct IntuitionBase *IntuitionBase = NULL;
struct GfxBase *GfxBase = NULL;

#define PORT_NAME "AREXX_TEST"
#define VERSION   "1.0"

static struct MsgPort *rexxPort = NULL;
static LONG loop_count = 0;
static LONG running = 1;
static LONG bridge_ok = 0;
static LONG start_tick = 0;
static LONG text_pen = 1;
static char last_cmd[128] = "(none)";
static char last_result[128] = "";
static char display_line1[80] = "ARexx port: " PORT_NAME;
static char display_line2[80] = "Waiting for commands...";

/*
 * Redraw the window content
 */
static void redraw(struct Window *win)
{
    struct RastPort *rp = win->RPort;
    int len;

    /* Clear content area */
    SetAPen(rp, 0);
    RectFill(rp, 4, 14, win->Width - 20, win->Height - 4);

    /* Line 1: port name */
    SetAPen(rp, 3);  /* orange/white */
    Move(rp, 8, 26);
    len = strlen(display_line1);
    Text(rp, display_line1, len);

    /* Line 2: last command/status */
    SetAPen(rp, (ULONG)text_pen);
    Move(rp, 8, 40);
    len = strlen(display_line2);
    Text(rp, display_line2, len);

    /* Line 3: last ARexx command */
    SetAPen(rp, 1);
    Move(rp, 8, 54);
    {
        static char tmp[128];
        sprintf(tmp, "Last: %s", last_cmd);
        len = strlen(tmp);
        if (len > 50) len = 50;
        Text(rp, tmp, len);
    }

    /* Line 4: result */
    Move(rp, 8, 68);
    {
        static char tmp[128];
        sprintf(tmp, "Result: %s", last_result);
        len = strlen(tmp);
        if (len > 50) len = 50;
        Text(rp, tmp, len);
    }

    /* Line 5: counter */
    SetAPen(rp, 2);
    Move(rp, 8, 82);
    {
        static char tmp[64];
        sprintf(tmp, "Loops: %ld  Commands: %ld", (long)loop_count, (long)0);
        len = strlen(tmp);
        Text(rp, tmp, len);
    }
}

/*
 * Handle a single ARexx message.
 * Returns 1 if QUIT was requested.
 */
static int handle_rexx_msg(struct RexxMsg *rmsg)
{
    static char resultstr[256];
    const char *cmd;
    int quit = 0;

    cmd = (const char *)rmsg->rm_Args[0];
    if (!cmd) cmd = "";

    /* Copy for display */
    strncpy(last_cmd, cmd, sizeof(last_cmd) - 1);
    last_cmd[sizeof(last_cmd) - 1] = '\0';

    AB_I("ARexx cmd: %s", cmd);

    /* Parse command */
    if (strncmp(cmd, "HELLO", 5) == 0 ||
        strncmp(cmd, "hello", 5) == 0) {
        strcpy(resultstr, "Hello from ARexx Test App!");
        rmsg->rm_Result1 = 0;
        rmsg->rm_Result2 = (LONG)CreateArgstring(
            (CONST_STRPTR)resultstr, strlen(resultstr));
    }
    else if (strncmp(cmd, "SAY ", 4) == 0 ||
             strncmp(cmd, "say ", 4) == 0) {
        const char *text = cmd + 4;
        strncpy(display_line2, text, sizeof(display_line2) - 1);
        display_line2[sizeof(display_line2) - 1] = '\0';
        strcpy(resultstr, "OK");
        rmsg->rm_Result1 = 0;
        rmsg->rm_Result2 = (LONG)CreateArgstring(
            (CONST_STRPTR)resultstr, strlen(resultstr));
    }
    else if (strncmp(cmd, "GETCOUNT", 8) == 0 ||
             strncmp(cmd, "getcount", 8) == 0) {
        sprintf(resultstr, "%ld", (long)loop_count);
        rmsg->rm_Result1 = 0;
        rmsg->rm_Result2 = (LONG)CreateArgstring(
            (CONST_STRPTR)resultstr, strlen(resultstr));
    }
    else if (strncmp(cmd, "GETTIME", 7) == 0 ||
             strncmp(cmd, "gettime", 7) == 0) {
        ULONG now = *(volatile ULONG *)0xDFF004;  /* VPOSR/VHPOSR - rough */
        /* Use a simpler approach: count loops * delay */
        sprintf(resultstr, "%ld", (long)(loop_count / 10));
        rmsg->rm_Result1 = 0;
        rmsg->rm_Result2 = (LONG)CreateArgstring(
            (CONST_STRPTR)resultstr, strlen(resultstr));
    }
    else if (strncmp(cmd, "ADD ", 4) == 0 ||
             strncmp(cmd, "add ", 4) == 0) {
        long a = 0, b = 0;
        const char *p = cmd + 4;
        a = strtol(p, (char **)&p, 10);
        while (*p == ' ') p++;
        b = strtol(p, NULL, 10);
        sprintf(resultstr, "%ld", a + b);
        rmsg->rm_Result1 = 0;
        rmsg->rm_Result2 = (LONG)CreateArgstring(
            (CONST_STRPTR)resultstr, strlen(resultstr));
    }
    else if (strncmp(cmd, "REVERSE ", 8) == 0 ||
             strncmp(cmd, "reverse ", 8) == 0) {
        const char *text = cmd + 8;
        int tlen = strlen(text);
        int i;
        if (tlen > (int)(sizeof(resultstr) - 1))
            tlen = (int)(sizeof(resultstr) - 1);
        for (i = 0; i < tlen; i++) {
            resultstr[i] = text[tlen - 1 - i];
        }
        resultstr[tlen] = '\0';
        rmsg->rm_Result1 = 0;
        rmsg->rm_Result2 = (LONG)CreateArgstring(
            (CONST_STRPTR)resultstr, strlen(resultstr));
    }
    else if (strncmp(cmd, "COLOR ", 6) == 0 ||
             strncmp(cmd, "color ", 6) == 0) {
        long old_pen = text_pen;
        long new_pen = strtol(cmd + 6, NULL, 10);
        if (new_pen >= 0 && new_pen <= 7) {
            text_pen = (LONG)new_pen;
        }
        sprintf(resultstr, "%ld", old_pen);
        rmsg->rm_Result1 = 0;
        rmsg->rm_Result2 = (LONG)CreateArgstring(
            (CONST_STRPTR)resultstr, strlen(resultstr));
    }
    else if (strncmp(cmd, "QUIT", 4) == 0 ||
             strncmp(cmd, "quit", 4) == 0) {
        strcpy(resultstr, "Goodbye!");
        rmsg->rm_Result1 = 0;
        rmsg->rm_Result2 = (LONG)CreateArgstring(
            (CONST_STRPTR)resultstr, strlen(resultstr));
        quit = 1;
    }
    else {
        strcpy(resultstr, "UNKNOWN COMMAND");
        rmsg->rm_Result1 = 10;  /* RC=10 = error */
        rmsg->rm_Result2 = 0;
    }

    /* Save result for display */
    strncpy(last_result, resultstr, sizeof(last_result) - 1);
    last_result[sizeof(last_result) - 1] = '\0';

    /* Reply to the message */
    ReplyMsg((struct Message *)rmsg);

    return quit;
}

int main(void)
{
    struct Window *win;
    struct IntuiMessage *imsg;
    struct RexxMsg *rmsg;
    ULONG class;
    ULONG winSig, rexxSig, signals;
    int redraw_counter = 0;

    IntuitionBase = (struct IntuitionBase *)OpenLibrary(
        (CONST_STRPTR)"intuition.library", 36);
    if (!IntuitionBase) return 1;

    GfxBase = (struct GfxBase *)OpenLibrary(
        (CONST_STRPTR)"graphics.library", 36);
    if (!GfxBase) {
        CloseLibrary((struct Library *)IntuitionBase);
        return 1;
    }

    printf("arexx_test v%s\n", VERSION);

    /* Connect to AmigaBridge daemon */
    if (ab_init("arexx_test") != 0) {
        printf("  Bridge: NOT FOUND (standalone mode)\n");
        bridge_ok = 0;
    } else {
        printf("  Bridge: CONNECTED\n");
        bridge_ok = 1;
    }

    AB_I("ARexx Test App v%s starting", VERSION);

    /* Register variables for bridge inspection */
    ab_register_var("loop_count", AB_TYPE_I32, &loop_count);
    ab_register_var("running", AB_TYPE_I32, &running);
    ab_register_var("last_cmd", AB_TYPE_STR, last_cmd);
    ab_register_var("text_pen", AB_TYPE_I32, &text_pen);

    /* Create public ARexx port */
    rexxPort = CreateMsgPort();
    if (!rexxPort) {
        printf("ERROR: Could not create ARexx port\n");
        ab_cleanup();
        CloseLibrary((struct Library *)GfxBase);
        CloseLibrary((struct Library *)IntuitionBase);
        return 1;
    }
    rexxPort->mp_Node.ln_Name = PORT_NAME;
    rexxPort->mp_Node.ln_Pri = 0;
    AddPort(rexxPort);
    printf("  ARexx port: %s (active)\n", PORT_NAME);
    AB_I("ARexx port '%s' created", PORT_NAME);

    /* Open window */
    win = OpenWindowTags(NULL,
        WA_Left, 100,
        WA_Top, 100,
        WA_Width, 400,
        WA_Height, 100,
        WA_Title, (ULONG)"ARexx Test v" VERSION " [" PORT_NAME "]",
        WA_CloseGadget, TRUE,
        WA_DragBar, TRUE,
        WA_DepthGadget, TRUE,
        WA_IDCMP, IDCMP_CLOSEWINDOW,
        WA_Activate, TRUE,
        TAG_DONE);

    if (!win) {
        AB_E("Failed to open window");
        RemPort(rexxPort);
        DeleteMsgPort(rexxPort);
        ab_cleanup();
        CloseLibrary((struct Library *)GfxBase);
        CloseLibrary((struct Library *)IntuitionBase);
        return 1;
    }

    winSig = 1UL << win->UserPort->mp_SigBit;
    rexxSig = 1UL << rexxPort->mp_SigBit;

    AB_I("Window opened, entering main loop");
    redraw(win);

    /* Main loop using Wait() for efficiency */
    while (running) {
        signals = Wait(winSig | rexxSig | SIGBREAKF_CTRL_C);

        /* CTRL-C */
        if (signals & SIGBREAKF_CTRL_C) {
            AB_I("CTRL-C received");
            running = 0;
            break;
        }

        /* Window messages */
        if (signals & winSig) {
            while ((imsg = (struct IntuiMessage *)GetMsg(win->UserPort))) {
                class = imsg->Class;
                ReplyMsg((struct Message *)imsg);
                if (class == IDCMP_CLOSEWINDOW) {
                    AB_I("Close window requested");
                    running = 0;
                }
            }
        }

        /* ARexx messages */
        if (signals & rexxSig) {
            while ((rmsg = (struct RexxMsg *)GetMsg(rexxPort))) {
                if (handle_rexx_msg(rmsg)) {
                    running = 0;
                }
                redraw(win);
            }
        }

        loop_count++;

        /* Periodic redraw */
        if ((++redraw_counter % 50) == 0) {
            redraw(win);
        }

        /* Poll bridge */
        ab_poll();
    }

    AB_I("ARexx Test App shutting down");

    /* Drain remaining ARexx messages before removing port */
    Forbid();
    {
        struct RexxMsg *rm;
        while ((rm = (struct RexxMsg *)GetMsg(rexxPort))) {
            rm->rm_Result1 = 20;  /* RC=20 = severe error */
            rm->rm_Result2 = 0;
            ReplyMsg((struct Message *)rm);
        }
        RemPort(rexxPort);
    }
    Permit();
    DeleteMsgPort(rexxPort);

    CloseWindow(win);
    ab_cleanup();
    CloseLibrary((struct Library *)GfxBase);
    CloseLibrary((struct Library *)IntuitionBase);

    printf("arexx_test: done\n");
    return 0;
}
