/*
 * System Monitor - Amiga Debug Library Demo
 *
 * Demonstrates all features of the debug library:
 * - Multi-level logging (DEBUG, INFO, WARN, ERROR)
 * - Variable registration and remote inspection
 * - Heartbeat with memory stats
 * - Memory dump on request
 * - Custom EXEC commands
 * - Polling for host commands
 *
 * This app monitors Amiga system state and exposes it
 * via the debug serial link for the MCP server to read.
 */

#include <exec/types.h>
#include <exec/memory.h>
#include <exec/execbase.h>
#include <intuition/intuition.h>
#include <graphics/rastport.h>
#include <graphics/text.h>
#include <proto/exec.h>
#include <proto/intuition.h>
#include <proto/graphics.h>
#include <proto/dos.h>

#include <stdio.h>
#include <string.h>

#include "debug.h"

struct IntuitionBase *IntuitionBase = NULL;
struct GfxBase *GfxBase = NULL;
extern struct ExecBase *SysBase;

/* Monitored state - all exposed as debug variables */
static ULONG frame = 0;
static LONG chip_free = 0;
static LONG fast_free = 0;
static LONG chip_largest = 0;
static LONG fast_largest = 0;
static LONG task_count = 0;
static LONG cpu_usage = 0;       /* simulated 0-100 */
static char status_msg[64] = "starting";

/* Display state */
#define WIN_W 400
#define WIN_H 200
#define BAR_X 10
#define BAR_W 380
#define BAR_H 12
#define TEXT_Y 20

/* Custom command handler */
static void cmd_handler(ULONG id, const char *data)
{
    char response[256];
    int len;

    if (strcmp(data, "snapshot") == 0) {
        /* Send all variables at once */
        dbg_send_var("chip_free");
        dbg_send_var("fast_free");
        dbg_send_var("task_count");
        dbg_send_var("cpu_usage");
        dbg_send_var("status_msg");
        len = snprintf(response, sizeof(response),
                      "CMD|%lu|ok|Snapshot sent: chip=%ld fast=%ld tasks=%ld cpu=%ld%%\n",
                      id, chip_free, fast_free, task_count, cpu_usage);
        extern int serial_write(const char *buf, int len);
        serial_write(response, len);
    }
    else if (strcmp(data, "memmap") == 0) {
        /* Send a dump of low chip memory (system vectors) */
        dbg_send_mem((APTR)0x00000004, 64);
        len = snprintf(response, sizeof(response),
                      "CMD|%lu|ok|Memory map sent (ExecBase pointer region)\n", id);
        extern int serial_write(const char *buf, int len);
        serial_write(response, len);
    }
    else if (strncmp(data, "msg ", 4) == 0) {
        strncpy(status_msg, data + 4, sizeof(status_msg) - 1);
        status_msg[sizeof(status_msg) - 1] = '\0';
        DBG_I("Status message set to: %s", status_msg);
        len = snprintf(response, sizeof(response),
                      "CMD|%lu|ok|Status message updated\n", id);
        extern int serial_write(const char *buf, int len);
        serial_write(response, len);
    }
    else {
        len = snprintf(response, sizeof(response),
                      "CMD|%lu|err|Unknown command: %s (try: snapshot, memmap, msg <text>)\n",
                      id, data);
        extern int serial_write(const char *buf, int len);
        serial_write(response, len);
    }
}

static LONG count_tasks(void)
{
    struct Node *node;
    LONG count = 0;

    Disable();
    for (node = SysBase->TaskReady.lh_Head;
         node->ln_Succ; node = node->ln_Succ) {
        count++;
    }
    for (node = SysBase->TaskWait.lh_Head;
         node->ln_Succ; node = node->ln_Succ) {
        count++;
    }
    count++; /* current task */
    Enable();

    return count;
}

static void update_stats(void)
{
    chip_free = (LONG)AvailMem(MEMF_CHIP);
    fast_free = (LONG)AvailMem(MEMF_FAST);
    chip_largest = (LONG)AvailMem(MEMF_CHIP | MEMF_LARGEST);
    fast_largest = (LONG)AvailMem(MEMF_FAST | MEMF_LARGEST);
    task_count = count_tasks();
}

static void draw_bar(struct RastPort *rp, LONG y, LONG value, LONG max,
                     UBYTE color, const char *label)
{
    LONG bar_len;
    char text[80];

    if (max <= 0) max = 1;
    bar_len = (value * BAR_W) / max;
    if (bar_len > BAR_W) bar_len = BAR_W;
    if (bar_len < 0) bar_len = 0;

    /* Background */
    SetAPen(rp, 0);
    RectFill(rp, BAR_X, y, BAR_X + BAR_W, y + BAR_H);

    /* Bar */
    SetAPen(rp, color);
    if (bar_len > 0) {
        RectFill(rp, BAR_X, y, BAR_X + bar_len, y + BAR_H);
    }

    /* Label */
    snprintf(text, sizeof(text), "%s: %ld / %ld", label, value, max);
    SetAPen(rp, 1);
    Move(rp, BAR_X, y + BAR_H - 2);
    Text(rp, (CONST_STRPTR)text, strlen(text));
}

static void draw_text(struct RastPort *rp, LONG y, const char *text)
{
    LONG len = strlen(text);
    /* Clear line */
    SetAPen(rp, 0);
    RectFill(rp, BAR_X, y - 8, BAR_X + BAR_W, y + 2);
    /* Draw text */
    SetAPen(rp, 1);
    Move(rp, BAR_X, y);
    Text(rp, (CONST_STRPTR)text, len);
}

int main(void)
{
    struct Window *win;
    struct IntuiMessage *msg;
    struct RastPort *rp;
    ULONG class;
    BOOL loop = TRUE;
    char buf[80];
    LONG prev_chip = 0;
    LONG prev_tasks = 0;

    IntuitionBase = (struct IntuitionBase *)
        OpenLibrary((CONST_STRPTR)"intuition.library", 36);
    if (!IntuitionBase) return 1;

    GfxBase = (struct GfxBase *)
        OpenLibrary((CONST_STRPTR)"graphics.library", 36);
    if (!GfxBase) {
        CloseLibrary((struct Library *)IntuitionBase);
        return 1;
    }

    /* Initialize debug link */
    if (dbg_init(9600) != 0) {
        printf("Debug init failed - continuing without debug\n");
    }

    DBG_I("System Monitor starting");

    /* Register all variables */
    dbg_register_var("frame", DBG_TYPE_U32, &frame);
    dbg_register_var("chip_free", DBG_TYPE_I32, &chip_free);
    dbg_register_var("fast_free", DBG_TYPE_I32, &fast_free);
    dbg_register_var("chip_largest", DBG_TYPE_I32, &chip_largest);
    dbg_register_var("fast_largest", DBG_TYPE_I32, &fast_largest);
    dbg_register_var("task_count", DBG_TYPE_I32, &task_count);
    dbg_register_var("cpu_usage", DBG_TYPE_I32, &cpu_usage);
    dbg_register_var("status_msg", DBG_TYPE_STR, status_msg);

    dbg_set_cmd_handler(cmd_handler);

    /* Get initial stats for display scale */
    update_stats();
    prev_chip = chip_free;
    prev_tasks = task_count;

    DBG_I("Initial chip: %ld, fast: %ld, tasks: %ld",
           chip_free, fast_free, task_count);

    /* Open window */
    win = OpenWindowTags(NULL,
        WA_Left, 20,
        WA_Top, 20,
        WA_Width, WIN_W,
        WA_Height, WIN_H,
        WA_Title, (ULONG)"System Monitor - Debug Demo",
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
    DBG_I("Window opened successfully");
    strncpy(status_msg, "running", sizeof(status_msg));

    /* Main loop */
    while (loop) {
        /* Check window messages */
        while ((msg = (struct IntuiMessage *)GetMsg(win->UserPort))) {
            class = msg->Class;
            ReplyMsg((struct Message *)msg);
            if (class == IDCMP_CLOSEWINDOW) {
                loop = FALSE;
                DBG_I("Close requested");
            }
        }
        if (!loop) break;

        /* Update system stats */
        update_stats();

        /* Simulate CPU usage (varies randomly) */
        cpu_usage += ((LONG)(frame * 7 + 13) % 11) - 5;
        if (cpu_usage < 0) cpu_usage = 0;
        if (cpu_usage > 100) cpu_usage = 100;

        frame++;

        /* Draw UI */
        snprintf(buf, sizeof(buf), "Frame: %lu  Status: %s", frame, status_msg);
        draw_text(rp, TEXT_Y, buf);

        draw_bar(rp, TEXT_Y + 10, chip_free, 512000, 2, "Chip");
        draw_bar(rp, TEXT_Y + 30, fast_free, 1048576, 3, "Fast");
        draw_bar(rp, TEXT_Y + 50, cpu_usage, 100, 1, "CPU%");

        snprintf(buf, sizeof(buf), "Tasks: %ld  Chip largest: %ld  Fast largest: %ld",
                 task_count, chip_largest, fast_largest);
        draw_text(rp, TEXT_Y + 80, buf);

        /* Log significant changes */
        if (chip_free < prev_chip - 10000) {
            DBG_W("Chip memory dropped: %ld -> %ld (-%ld)",
                   prev_chip, chip_free, prev_chip - chip_free);
        }
        if (task_count != prev_tasks) {
            DBG_I("Task count changed: %ld -> %ld", prev_tasks, task_count);
        }
        prev_chip = chip_free;
        prev_tasks = task_count;

        /* Heartbeat every 60 frames */
        if (frame % 60 == 0) {
            dbg_heartbeat();
        }

        /* Detailed stats log every 300 frames */
        if (frame % 300 == 0) {
            DBG_I("Stats: chip=%ld fast=%ld tasks=%ld cpu=%ld%%",
                   chip_free, fast_free, task_count, cpu_usage);
        }

        /* Poll for debug commands from host */
        dbg_poll();

        /* ~10fps update rate */
        Delay(5);
    }

    strncpy(status_msg, "shutting down", sizeof(status_msg));
    DBG_I("System Monitor shutting down");

    CloseWindow(win);
    dbg_cleanup();
    CloseLibrary((struct Library *)GfxBase);
    CloseLibrary((struct Library *)IntuitionBase);

    return 0;
}
