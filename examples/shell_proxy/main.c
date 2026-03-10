#include <exec/types.h>
#include <intuition/intuition.h>
#include <graphics/gfx.h>
#include <graphics/rastport.h>
#include <dos/dos.h>
#include <dos/dostags.h>
#include <proto/exec.h>
#include <proto/intuition.h>
#include <proto/graphics.h>
#include <proto/dos.h>

#include <stdio.h>
#include <string.h>

#include "bridge_client.h"

struct IntuitionBase *IntuitionBase = NULL;
struct GfxBase *GfxBase = NULL;

#define VERSION "1.0"

#define OUTPUT_BUF_SIZE 1024
#define CMD_BUF_SIZE    512
#define PATH_BUF_SIZE   256

/* Static buffers - no malloc */
static ULONG command_count = 0;
static LONG last_exit_code = 0;
static char last_command[128] = "";
static char status_msg[64] = "Ready";
static char output_buf[OUTPUT_BUF_SIZE] = "";
static char cmd_buf[CMD_BUF_SIZE] = "";
static char path_buf[PATH_BUF_SIZE] = "";
static char line_buf[256] = "";
static LONG running = 1;
static LONG bridge_ok = 0;

/* Display state */
static char prev_cmd_display[128] = "";
static ULONG prev_count = 0;

/* ---- Helper: read a file into buffer ---- */
static LONG read_file_into(const char *path, char *buf, LONG bufSize)
{
    BPTR fh;
    LONG total = 0;
    LONG n;

    buf[0] = '\0';
    fh = Open((CONST_STRPTR)path, MODE_OLDFILE);
    if (!fh) return 0;

    while (total < bufSize - 1) {
        n = Read(fh, buf + total, bufSize - 1 - total);
        if (n <= 0) break;
        total += n;
    }
    buf[total] = '\0';
    Close(fh);
    return total;
}

/* Escape newlines and pipes in output for serial protocol */
static void escape_output(const char *src, char *dst, int dstSize)
{
    int si = 0, di = 0;
    while (src[si] && di < dstSize - 2) {
        if (src[si] == '\n') {
            dst[di++] = '\\';
            dst[di++] = 'n';
        } else if (src[si] == '|') {
            dst[di++] = '\\';
            dst[di++] = '|';
        } else if (src[si] == '\r') {
            /* skip CR */
        } else {
            dst[di++] = src[si];
        }
        si++;
    }
    dst[di] = '\0';
}

/* ---- Hook: exec ---- */
static int hook_exec(const char *args, char *resultBuf, int bufSize)
{
    BPTR nil_in;
    BPTR out_fh;

    if (!args || args[0] == '\0') {
        strncpy(resultBuf, "ERROR: no command given", bufSize - 1);
        resultBuf[bufSize - 1] = '\0';
        return -1;
    }

    /* Save last command */
    strncpy(last_command, args, sizeof(last_command) - 1);
    last_command[sizeof(last_command) - 1] = '\0';
    command_count++;

    /* Open NIL: as input so Execute doesn't wait for interactive input */
    nil_in = Open((CONST_STRPTR)"NIL:", MODE_OLDFILE);
    if (!nil_in) {
        strncpy(resultBuf, "ERROR: cannot open NIL:", bufSize - 1);
        resultBuf[bufSize - 1] = '\0';
        last_exit_code = -1;
        return -1;
    }

    /* Open output capture file */
    out_fh = Open((CONST_STRPTR)"T:bridge_out", MODE_NEWFILE);
    if (!out_fh) {
        Close(nil_in);
        strncpy(resultBuf, "ERROR: cannot create T:bridge_out", bufSize - 1);
        resultBuf[bufSize - 1] = '\0';
        last_exit_code = -1;
        return -1;
    }

    /* Use SystemTags - works without CLI structure, cleaner than Execute() */
    last_exit_code = SystemTags(args,
        SYS_Input, nil_in,
        SYS_Output, out_fh,
        TAG_DONE);
    Close(nil_in);
    Close(out_fh);

    /* Read captured output */
    read_file_into("T:bridge_out", output_buf, OUTPUT_BUF_SIZE);

    /* Copy to result, escaping newlines for serial protocol */
    if (output_buf[0] == '\0') {
        if (last_exit_code == 0) {
            strncpy(resultBuf, "(no output)", bufSize - 1);
        } else {
            strncpy(resultBuf, "ERROR: command failed", bufSize - 1);
        }
        resultBuf[bufSize - 1] = '\0';
    } else {
        escape_output(output_buf, resultBuf, bufSize);
    }

    sprintf(status_msg, "Ran %ld cmds", (long)command_count);
    return (last_exit_code == 0) ? 0 : -1;
}

/* ---- Hook: cd ---- */
static int hook_cd(const char *args, char *resultBuf, int bufSize)
{
    BPTR lock;
    BPTR old;

    if (!args || args[0] == '\0') {
        strncpy(resultBuf, "ERROR: no path given", bufSize - 1);
        resultBuf[bufSize - 1] = '\0';
        return -1;
    }

    lock = Lock((CONST_STRPTR)args, ACCESS_READ);
    if (!lock) {
        sprintf(resultBuf, "ERROR: cannot lock \"%s\"", args);
        return -1;
    }

    old = CurrentDir(lock);
    UnLock(old);

    /* Get new path name */
    if (NameFromLock(lock, (UBYTE *)path_buf, PATH_BUF_SIZE)) {
        strncpy(resultBuf, path_buf, bufSize - 1);
        resultBuf[bufSize - 1] = '\0';
    } else {
        strncpy(resultBuf, args, bufSize - 1);
        resultBuf[bufSize - 1] = '\0';
    }

    return 0;
}

/* ---- Hook: type ---- */
static int hook_type(const char *args, char *resultBuf, int bufSize)
{
    LONG n;

    if (!args || args[0] == '\0') {
        strncpy(resultBuf, "ERROR: no file given", bufSize - 1);
        resultBuf[bufSize - 1] = '\0';
        return -1;
    }

    n = read_file_into(args, resultBuf, bufSize);
    if (n == 0) {
        BPTR fh = Open((CONST_STRPTR)args, MODE_OLDFILE);
        if (!fh) {
            sprintf(resultBuf, "ERROR: cannot open \"%s\"", args);
            return -1;
        }
        Close(fh);
        /* File exists but is empty */
        strncpy(resultBuf, "(empty file)", bufSize - 1);
        resultBuf[bufSize - 1] = '\0';
    }

    return 0;
}

/* ---- Hook: info ---- */
static int hook_info(const char *args, char *resultBuf, int bufSize)
{
    /* Use exec hook to run the INFO command */
    return hook_exec("Info", resultBuf, bufSize);
}

/* ---- Hook: which ---- */
static int hook_which(const char *args, char *resultBuf, int bufSize)
{
    if (!args || args[0] == '\0') {
        strncpy(resultBuf, "ERROR: no command name given", bufSize - 1);
        resultBuf[bufSize - 1] = '\0';
        return -1;
    }

    /* Build "Which <command>" and run it via exec */
    sprintf(line_buf, "Which %s", args);
    return hook_exec(line_buf, resultBuf, bufSize);
}

/* ---- Window drawing ---- */
static void redraw_status(struct Window *win)
{
    struct RastPort *rp = win->RPort;
    char disp[160];

    SetAPen(rp, 0);
    RectFill(rp, 6, 14, 294, 70);

    SetAPen(rp, 1);

    sprintf(disp, "Commands: %ld", (long)command_count);
    Move(rp, 10, 26);
    Text(rp, disp, strlen(disp));

    if (last_command[0] != '\0') {
        sprintf(disp, "Last: %s", last_command);
        /* Truncate for display */
        if (strlen(disp) > 38) {
            disp[35] = '.';
            disp[36] = '.';
            disp[37] = '.';
            disp[38] = '\0';
        }
        Move(rp, 10, 40);
        Text(rp, disp, strlen(disp));
    }

    sprintf(disp, "Exit: %ld  %s", (long)last_exit_code, status_msg);
    Move(rp, 10, 54);
    Text(rp, disp, strlen(disp));
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

    printf("shell_proxy v%s\n", VERSION);

    /* Connect to AmigaBridge daemon */
    if (ab_init("shell_proxy") != 0) {
        printf("  Bridge: NOT FOUND (is amiga-bridge running?)\n");
        bridge_ok = 0;
    } else {
        printf("  Bridge: CONNECTED\n");
        bridge_ok = 1;
    }

    AB_I("Shell Proxy v%s starting up", VERSION);

    /* Register variables */
    ab_register_var("command_count", AB_TYPE_U32, &command_count);
    ab_register_var("last_command", AB_TYPE_STR, last_command);
    ab_register_var("last_exit_code", AB_TYPE_I32, &last_exit_code);
    ab_register_var("status_msg", AB_TYPE_STR, status_msg);

    /* Register hooks */
    ab_register_hook("exec", "Execute an AmigaDOS command, return output", hook_exec);
    ab_register_hook("cd", "Change current directory (arg: path)", hook_cd);
    ab_register_hook("type", "Read and return file contents (arg: filepath)", hook_type);
    ab_register_hook("info", "Return volume/device info (like INFO command)", hook_info);
    ab_register_hook("which", "Find a command in the path (arg: command name)", hook_which);

    /* Register memory region for last output */
    ab_register_memregion("last_output", output_buf, OUTPUT_BUF_SIZE,
                          "Last command output buffer");

    /* Open status window */
    win = OpenWindowTags(NULL,
        WA_Left, 20,
        WA_Top, 20,
        WA_Width, 300,
        WA_Height, 80,
        WA_Title, (ULONG)(bridge_ok ?
            "Shell Proxy v1.0 [Bridge: OK]" :
            "Shell Proxy v1.0 [Bridge: OFF]"),
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

    AB_I("Window opened, ready for commands");
    strncpy(status_msg, "Ready", sizeof(status_msg) - 1);
    redraw_status(win);

    /* Main loop */
    while (running) {
        /* Check for window close */
        while ((msg = (struct IntuiMessage *)GetMsg(win->UserPort))) {
            class = msg->Class;
            ReplyMsg((struct Message *)msg);

            if (class == IDCMP_CLOSEWINDOW) {
                AB_I("Close window requested");
                running = 0;
            }
        }

        /* Check CTRL-C */
        if (SetSignal(0L, SIGBREAKF_CTRL_C) & SIGBREAKF_CTRL_C) {
            AB_I("CTRL-C received");
            running = 0;
        }

        /* Heartbeat every 500 iterations */
        if ((++hb_counter % 500) == 0) {
            ab_heartbeat();
        }

        /* Poll for commands/hooks from bridge daemon */
        ab_poll();

        /* Redraw if state changed */
        if (command_count != prev_count ||
            strcmp(last_command, prev_cmd_display) != 0) {
            redraw_status(win);
            prev_count = command_count;
            strncpy(prev_cmd_display, last_command, sizeof(prev_cmd_display) - 1);
            prev_cmd_display[sizeof(prev_cmd_display) - 1] = '\0';
        }

        /* Small delay */
        Delay(5);
    }

    AB_I("Shell Proxy shutting down");

    CloseWindow(win);
    ab_cleanup();
    CloseLibrary((struct Library *)GfxBase);
    CloseLibrary((struct Library *)IntuitionBase);

    return 0;
}
