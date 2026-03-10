#include <exec/types.h>
#include <intuition/intuition.h>
#include <intuition/gadgetclass.h>
#include <graphics/gfx.h>
#include <graphics/rastport.h>
#include <dos/dos.h>
#include <dos/datetime.h>
#include <proto/exec.h>
#include <proto/intuition.h>
#include <proto/graphics.h>
#include <proto/dos.h>

#include <stdio.h>
#include <string.h>

#include "bridge_client.h"

#define VERSION "1.0"
#define IO_BUF_SIZE 32768
#define RESULTS_BUF_SIZE 1024
#define TEMP_FILENAME_SIZE 128
#define BAR_MAX_WIDTH 240
#define BAR_HEIGHT 14

#define WIN_W 420
#define WIN_H 220

/* Gadget IDs */
#define GID_PATH    1
#define GID_WRITE   2
#define GID_READ    3
#define GID_ALL     4

struct IntuitionBase *IntuitionBase = NULL;
struct GfxBase *GfxBase = NULL;

/* Static I/O buffer - 32KB */
static UBYTE io_buffer[IO_BUF_SIZE];

/* State */
static char test_path[128] = "RAM:";
static LONG block_size = 4096;
static LONG test_size = 65536;
static LONG read_speed = 0;
static LONG write_speed = 0;
static char status_msg[128] = "Ready";
static LONG running = 1;
static LONG bridge_ok = 0;

static char results_buf[RESULTS_BUF_SIZE];

static struct Window *win = NULL;
static LONG needs_redraw = 1;

/* ---- Gadget definitions ---- */

/* String gadget for path */
static UBYTE path_undobuf[128];
static struct StringInfo path_sinfo = {
    (UBYTE *)NULL, path_undobuf, 0, 128, 0, 0, 0, 0, 0, 0, NULL, 0L, NULL
};

/* Border for string gadget */
static SHORT str_border_xy[] = { -2,-2, 201,-2, 201,10, -2,10, -2,-2 };
static struct Border str_border = { 0,0, 1,0, JAM1, 5, str_border_xy, NULL };

static struct IntuiText btn_write_txt = { 1,0, JAM1, 4,2, NULL, (UBYTE *)"Write", NULL };
static struct IntuiText btn_read_txt  = { 1,0, JAM1, 6,2, NULL, (UBYTE *)"Read",  NULL };
static struct IntuiText btn_all_txt   = { 1,0, JAM1, 4,2, NULL, (UBYTE *)"Both",  NULL };

/* Button borders */
static SHORT btn_border_xy[] = { 0,0, 59,0, 59,13, 0,13, 0,0 };
static struct Border btn_border = { 0,0, 1,0, JAM1, 5, btn_border_xy, NULL };

static struct Gadget gad_all = {
    NULL, 240, 28, 60, 14,
    GFLG_GADGHCOMP, GACT_RELVERIFY, GTYP_BOOLGADGET,
    (APTR)&btn_border, NULL, &btn_all_txt, 0, NULL, GID_ALL, NULL
};
static struct Gadget gad_read = {
    &gad_all, 170, 28, 60, 14,
    GFLG_GADGHCOMP, GACT_RELVERIFY, GTYP_BOOLGADGET,
    (APTR)&btn_border, NULL, &btn_read_txt, 0, NULL, GID_READ, NULL
};
static struct Gadget gad_write = {
    &gad_read, 100, 28, 60, 14,
    GFLG_GADGHCOMP, GACT_RELVERIFY, GTYP_BOOLGADGET,
    (APTR)&btn_border, NULL, &btn_write_txt, 0, NULL, GID_WRITE, NULL
};
static struct Gadget gad_path = {
    &gad_write, 50, 15, 200, 10,
    GFLG_GADGHCOMP, GACT_RELVERIFY, GTYP_STRGADGET,
    (APTR)&str_border, NULL, NULL, 0, (APTR)&path_sinfo, GID_PATH, NULL
};

/* Forward declarations */
static void redraw_window(void);

/* ---- Benchmark functions ---- */

static void fill_pattern(void)
{
    LONG i;
    for (i = 0; i < IO_BUF_SIZE; i++) {
        io_buffer[i] = (UBYTE)(i & 0xFF);
    }
}

static LONG elapsed_ticks(struct DateStamp *start, struct DateStamp *end)
{
    LONG days = end->ds_Days - start->ds_Days;
    LONG mins = end->ds_Minute - start->ds_Minute;
    LONG ticks = end->ds_Tick - start->ds_Tick;
    return (days * 24L * 60L * 50L * 60L) + (mins * 60L * 50L) + ticks;
}

static void build_temp_path(char *buf, int bufsize)
{
    LONG pathlen;
    strncpy(buf, test_path, bufsize - 1);
    buf[bufsize - 1] = '\0';
    pathlen = strlen(buf);
    if (pathlen > 0 && buf[pathlen - 1] != ':' && buf[pathlen - 1] != '/') {
        if (pathlen < bufsize - 1) {
            buf[pathlen] = '/';
            buf[pathlen + 1] = '\0';
        }
    }
    strncat(buf, "diskbench.tmp", bufsize - strlen(buf) - 1);
}

static LONG do_write_test(void)
{
    char filepath[TEMP_FILENAME_SIZE];
    struct DateStamp ds_start, ds_end;
    BPTR fh;
    LONG written = 0;
    LONG chunk, ticks;

    build_temp_path(filepath, sizeof(filepath));
    fill_pattern();

    sprintf(status_msg, "Writing %ld bytes...", (long)test_size);
    needs_redraw = 1;
    if (win) redraw_window();

    fh = Open((CONST_STRPTR)filepath, MODE_NEWFILE);
    if (!fh) {
        sprintf(status_msg, "Error: cannot open %s", filepath);
        needs_redraw = 1;
        return 0;
    }

    DateStamp(&ds_start);
    while (written < test_size) {
        chunk = test_size - written;
        if (chunk > block_size) chunk = block_size;
        if (chunk > IO_BUF_SIZE) chunk = IO_BUF_SIZE;
        if (Write(fh, io_buffer, chunk) != chunk) {
            sprintf(status_msg, "Write error at %ld", (long)written);
            Close(fh);
            DeleteFile((CONST_STRPTR)filepath);
            needs_redraw = 1;
            return 0;
        }
        written += chunk;
    }
    Close(fh);
    DateStamp(&ds_end);

    ticks = elapsed_ticks(&ds_start, &ds_end);
    if (ticks < 1) ticks = 1;
    write_speed = (written * 50L) / ticks;
    return write_speed;
}

static LONG do_read_test(void)
{
    char filepath[TEMP_FILENAME_SIZE];
    struct DateStamp ds_start, ds_end;
    BPTR fh;
    LONG total_read = 0;
    LONG chunk, got, ticks;

    build_temp_path(filepath, sizeof(filepath));

    sprintf(status_msg, "Reading %ld bytes...", (long)test_size);
    needs_redraw = 1;
    if (win) redraw_window();

    fh = Open((CONST_STRPTR)filepath, MODE_OLDFILE);
    if (!fh) {
        do_write_test();
        fh = Open((CONST_STRPTR)filepath, MODE_OLDFILE);
        if (!fh) {
            sprintf(status_msg, "Error: cannot open %s", filepath);
            needs_redraw = 1;
            return 0;
        }
    }

    DateStamp(&ds_start);
    while (total_read < test_size) {
        chunk = test_size - total_read;
        if (chunk > block_size) chunk = block_size;
        if (chunk > IO_BUF_SIZE) chunk = IO_BUF_SIZE;
        got = Read(fh, io_buffer, chunk);
        if (got <= 0) break;
        total_read += got;
    }
    Close(fh);
    DateStamp(&ds_end);

    DeleteFile((CONST_STRPTR)filepath);

    ticks = elapsed_ticks(&ds_start, &ds_end);
    if (ticks < 1) ticks = 1;
    read_speed = (total_read * 50L) / ticks;
    return read_speed;
}

static void update_results(void)
{
    sprintf(results_buf,
        "Path: %s  Block: %ld  Size: %ld\n"
        "Write: %ld KB/s  Read: %ld KB/s\n",
        test_path, (long)block_size, (long)test_size,
        (long)(write_speed / 1024L), (long)(read_speed / 1024L));
}

/* Sync path from string gadget into test_path */
static void sync_path_from_gadget(void)
{
    if (path_sinfo.Buffer) {
        strncpy(test_path, (char *)path_sinfo.Buffer, sizeof(test_path) - 1);
        test_path[sizeof(test_path) - 1] = '\0';
    }
}

/* ---- Drawing ---- */

static void draw_bar(struct RastPort *rp, LONG x, LONG y,
                     LONG value, LONG max_val, LONG color)
{
    LONG bar_w;
    SetAPen(rp, 0);
    RectFill(rp, x, y, x + BAR_MAX_WIDTH, y + BAR_HEIGHT - 1);
    if (value > 0 && max_val > 0) {
        bar_w = (value * BAR_MAX_WIDTH) / max_val;
        if (bar_w < 1) bar_w = 1;
        if (bar_w > BAR_MAX_WIDTH) bar_w = BAR_MAX_WIDTH;
        SetAPen(rp, color);
        RectFill(rp, x, y, x + bar_w, y + BAR_HEIGHT - 1);
    }
}

static void redraw_window(void)
{
    struct RastPort *rp;
    char line[80];
    LONG max_speed;
    LONG y;

    if (!win) return;
    rp = win->RPort;

    /* Clear area below gadgets */
    SetAPen(rp, 0);
    RectFill(rp, 4, 46, WIN_W - 6, WIN_H - 4);

    SetAPen(rp, 1);

    /* Path label */
    Move(rp, 10, 23);
    Text(rp, "Path:", 5);

    /* Status line */
    y = 58;
    Move(rp, 10, y);
    Text(rp, status_msg, strlen(status_msg));

    /* Results area */
    y = 78;
    sprintf(line, "Block: %ld  Size: %ld bytes",
            (long)block_size, (long)test_size);
    Move(rp, 10, y);
    Text(rp, line, strlen(line));

    /* Write speed + bar */
    y = 100;
    sprintf(line, "Write: %ld KB/s", (long)(write_speed / 1024L));
    Move(rp, 10, y);
    Text(rp, line, strlen(line));
    draw_bar(rp, 160, y - 8, write_speed,
             (write_speed > read_speed ? write_speed : read_speed) > 0 ?
             (write_speed > read_speed ? write_speed : read_speed) : 1024, 3);

    /* Read speed + bar */
    y = 120;
    sprintf(line, "Read:  %ld KB/s", (long)(read_speed / 1024L));
    Move(rp, 10, y);
    Text(rp, line, strlen(line));
    max_speed = write_speed > read_speed ? write_speed : read_speed;
    if (max_speed < 1024) max_speed = 1024;
    draw_bar(rp, 160, y - 8, read_speed, max_speed, 2);

    /* Detailed bytes/sec */
    if (write_speed > 0 || read_speed > 0) {
        y = 146;
        sprintf(line, "W: %ld B/s  R: %ld B/s",
                (long)write_speed, (long)read_speed);
        Move(rp, 10, y);
        Text(rp, line, strlen(line));

        if (write_speed > 0 && read_speed > 0) {
            LONG ratio = (read_speed * 100L) / write_speed;
            y = 162;
            sprintf(line, "R/W ratio: %ld.%02ld",
                    (long)(ratio / 100L), (long)(ratio % 100L));
            Move(rp, 10, y);
            Text(rp, line, strlen(line));
        }
    }

    needs_redraw = 0;
}

/* ---- Hook callbacks ---- */

static int hook_run_write(const char *args, char *result, int bufsize)
{
    LONG speed;
    if (args && args[0]) {
        strncpy(test_path, args, sizeof(test_path) - 1);
        test_path[sizeof(test_path) - 1] = '\0';
    }
    speed = do_write_test();
    sprintf(status_msg, "Write: %ld KB/s", (long)(speed / 1024L));
    update_results();
    needs_redraw = 1;
    sprintf(result, "Write speed: %ld bytes/sec (%ld KB/s)",
            (long)speed, (long)(speed / 1024L));
    return 0;
}

static int hook_run_read(const char *args, char *result, int bufsize)
{
    LONG speed;
    if (args && args[0]) {
        strncpy(test_path, args, sizeof(test_path) - 1);
        test_path[sizeof(test_path) - 1] = '\0';
    }
    speed = do_read_test();
    sprintf(status_msg, "Read: %ld KB/s", (long)(speed / 1024L));
    update_results();
    needs_redraw = 1;
    sprintf(result, "Read speed: %ld bytes/sec (%ld KB/s)",
            (long)speed, (long)(speed / 1024L));
    return 0;
}

static int hook_run_all(const char *args, char *result, int bufsize)
{
    LONG ws, rs;
    if (args && args[0]) {
        strncpy(test_path, args, sizeof(test_path) - 1);
        test_path[sizeof(test_path) - 1] = '\0';
    }
    ws = do_write_test();
    rs = do_read_test();
    sprintf(status_msg, "Done: W=%ld R=%ld KB/s",
            (long)(ws / 1024L), (long)(rs / 1024L));
    update_results();
    needs_redraw = 1;
    sprintf(result, "Write: %ld bytes/sec (%ld KB/s), Read: %ld bytes/sec (%ld KB/s)",
            (long)ws, (long)(ws / 1024L), (long)rs, (long)(rs / 1024L));
    return 0;
}

static int hook_set_path(const char *args, char *result, int bufsize)
{
    if (!args || !args[0]) {
        sprintf(result, "Current path: %s", test_path);
        return 0;
    }
    strncpy(test_path, args, sizeof(test_path) - 1);
    test_path[sizeof(test_path) - 1] = '\0';
    sprintf(status_msg, "Path: %s", test_path);
    needs_redraw = 1;
    sprintf(result, "Path set to: %s", test_path);
    return 0;
}

/* ---- Main ---- */

int main(void)
{
    struct IntuiMessage *msg;
    ULONG class;
    UWORD gadid;
    struct Gadget *gad;
    LONG hb_counter = 0;
    ULONG signals;

    IntuitionBase = (struct IntuitionBase *)OpenLibrary((CONST_STRPTR)"intuition.library", 36);
    if (!IntuitionBase) return 1;

    GfxBase = (struct GfxBase *)OpenLibrary((CONST_STRPTR)"graphics.library", 36);
    if (!GfxBase) {
        CloseLibrary((struct Library *)IntuitionBase);
        return 1;
    }

    printf("disk_benchmark v%s\n", VERSION);

    /* Connect to AmigaBridge daemon */
    if (ab_init("disk_benchmark") != 0) {
        printf("  Bridge: NOT FOUND (is amiga-bridge running?)\n");
        bridge_ok = 0;
    } else {
        printf("  Bridge: CONNECTED\n");
        bridge_ok = 1;
    }

    AB_I("Disk Benchmark v%s starting", VERSION);

    /* Register variables */
    ab_register_var("test_path", AB_TYPE_STR, test_path);
    ab_register_var("block_size", AB_TYPE_I32, &block_size);
    ab_register_var("test_size", AB_TYPE_I32, &test_size);
    ab_register_var("read_speed", AB_TYPE_I32, &read_speed);
    ab_register_var("write_speed", AB_TYPE_I32, &write_speed);
    ab_register_var("status_msg", AB_TYPE_STR, status_msg);

    /* Register hooks */
    ab_register_hook("run_read", "Run read benchmark (optional arg: path)", hook_run_read);
    ab_register_hook("run_write", "Run write benchmark (optional arg: path)", hook_run_write);
    ab_register_hook("run_all", "Run both read and write tests", hook_run_all);
    ab_register_hook("set_path", "Change test path (arg: path)", hook_set_path);

    /* Register results memory region */
    update_results();
    ab_register_memregion("results", results_buf, RESULTS_BUF_SIZE,
                          "Formatted benchmark results");

    /* Set up string gadget buffer to point at test_path */
    path_sinfo.Buffer = (UBYTE *)test_path;
    path_sinfo.MaxChars = sizeof(test_path);

    /* Open window with gadgets */
    win = OpenWindowTags(NULL,
        WA_Left, 80,
        WA_Top, 40,
        WA_Width, WIN_W,
        WA_Height, WIN_H,
        WA_Title, (ULONG)"Disk Benchmark v1.0",
        WA_Gadgets, (ULONG)&gad_path,
        WA_CloseGadget, TRUE,
        WA_DragBar, TRUE,
        WA_DepthGadget, TRUE,
        WA_IDCMP, IDCMP_CLOSEWINDOW | IDCMP_GADGETUP,
        WA_Activate, TRUE,
        TAG_DONE);

    if (!win) {
        AB_E("Failed to open window");
        ab_cleanup();
        CloseLibrary((struct Library *)GfxBase);
        CloseLibrary((struct Library *)IntuitionBase);
        return 1;
    }

    redraw_window();

    /* Main loop */
    while (running) {
        signals = SetSignal(0L, 0L);
        if (signals & SIGBREAKF_CTRL_C) {
            running = 0;
            break;
        }

        while ((msg = (struct IntuiMessage *)GetMsg(win->UserPort))) {
            class = msg->Class;
            gad = (struct Gadget *)msg->IAddress;
            ReplyMsg((struct Message *)msg);

            if (class == IDCMP_CLOSEWINDOW) {
                running = 0;
            } else if (class == IDCMP_GADGETUP && gad) {
                gadid = gad->GadgetID;
                switch (gadid) {
                case GID_PATH:
                    sync_path_from_gadget();
                    sprintf(status_msg, "Path: %s", test_path);
                    needs_redraw = 1;
                    break;
                case GID_WRITE:
                    sync_path_from_gadget();
                    do_write_test();
                    sprintf(status_msg, "Write: %ld KB/s",
                            (long)(write_speed / 1024L));
                    update_results();
                    needs_redraw = 1;
                    break;
                case GID_READ:
                    sync_path_from_gadget();
                    do_read_test();
                    sprintf(status_msg, "Read: %ld KB/s",
                            (long)(read_speed / 1024L));
                    update_results();
                    needs_redraw = 1;
                    break;
                case GID_ALL:
                    sync_path_from_gadget();
                    do_write_test();
                    do_read_test();
                    sprintf(status_msg, "Done: W=%ld R=%ld KB/s",
                            (long)(write_speed / 1024L),
                            (long)(read_speed / 1024L));
                    update_results();
                    needs_redraw = 1;
                    break;
                }
            }
        }

        ab_poll();

        if (needs_redraw) {
            redraw_window();
        }

        if ((++hb_counter % 500) == 0) {
            ab_heartbeat();
        }

        Delay(3);
    }

    AB_I("Disk Benchmark shutting down");

    CloseWindow(win);
    win = NULL;

    ab_cleanup();
    CloseLibrary((struct Library *)GfxBase);
    CloseLibrary((struct Library *)IntuitionBase);

    printf("disk_benchmark finished.\n");
    return 0;
}
