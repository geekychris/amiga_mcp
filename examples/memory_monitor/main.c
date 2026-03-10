#include <exec/types.h>
#include <exec/memory.h>
#include <exec/execbase.h>
#include <intuition/intuition.h>
#include <graphics/gfx.h>
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

extern struct ExecBase *SysBase;

#define VERSION "1.0"

#define WIN_W 500
#define WIN_H 250
#define MAX_SAMPLES 100

/* Bar chart area */
#define CHART_X 10
#define CHART_Y 80
#define CHART_W 480
#define CHART_H 140

/* --- Static buffers --- */

static LONG chip_free = 0;
static LONG fast_free = 0;
static LONG chip_total = 0;
static LONG fast_total = 0;
static LONG chip_largest = 0;
static LONG fast_largest = 0;
static LONG chip_fragments = 0;
static LONG fast_fragments = 0;
static ULONG sample_count = 0;
static LONG update_interval = 25;  /* ticks between samples */

/* Ring buffer of chip_free samples */
static LONG sample_ring[MAX_SAMPLES];
static LONG sample_index = 0;

static LONG running = 1;
static LONG bridge_ok = 0;

static char text_buf[256];

/* Leaked allocations tracker (for leak_test hook) */
#define MAX_LEAKS 64
static APTR leaked_ptrs[MAX_LEAKS];
static LONG leak_count = 0;

/* ---- Memory Walking ---- */

/* Count fragments and find largest block for a given memory attribute.
 * Must be called under Forbid()/Permit(). Keep it fast. */
static void count_memory(ULONG attrs, LONG *out_free, LONG *out_total,
                         LONG *out_largest, LONG *out_fragments)
{
    struct MemHeader *mh;
    struct MemChunk *mc;
    LONG free_sum = 0;
    LONG total_sum = 0;
    LONG largest = 0;
    LONG frags = 0;

    Forbid();

    for (mh = (struct MemHeader *)SysBase->MemList.lh_Head;
         mh->mh_Node.ln_Succ != NULL;
         mh = (struct MemHeader *)mh->mh_Node.ln_Succ)
    {
        if (mh->mh_Attributes & attrs) {
            /* Total = upper - lower */
            total_sum += (LONG)((UBYTE *)mh->mh_Upper - (UBYTE *)mh->mh_Lower);

            /* Walk free list */
            for (mc = mh->mh_First; mc != NULL; mc = mc->mc_Next) {
                LONG sz = (LONG)mc->mc_Bytes;
                free_sum += sz;
                frags++;
                if (sz > largest) {
                    largest = sz;
                }
            }
        }
    }

    Permit();

    *out_free = free_sum;
    *out_total = total_sum;
    *out_largest = largest;
    *out_fragments = frags;
}

static void update_stats(void)
{
    count_memory(MEMF_CHIP, &chip_free, &chip_total, &chip_largest, &chip_fragments);
    count_memory(MEMF_FAST, &fast_free, &fast_total, &fast_largest, &fast_fragments);

    /* Store sample in ring buffer */
    sample_ring[sample_index] = chip_free;
    sample_index = (sample_index + 1) % MAX_SAMPLES;
    sample_count++;
}

/* ---- Drawing ---- */

static void draw_text_stats(struct RastPort *rp)
{
    /* Clear text area */
    SetAPen(rp, 0);
    RectFill(rp, 4, 12, WIN_W - 4, CHART_Y - 2);

    SetAPen(rp, 1);

    /* Row 1: Chip free / total */
    sprintf(text_buf, "CHIP: %ldK / %ldK free",
            (long)(chip_free / 1024), (long)(chip_total / 1024));
    Move(rp, 10, 24);
    Text(rp, text_buf, strlen(text_buf));
    sprintf(text_buf, "Lrg: %ldK  Frags: %ld",
            (long)(chip_largest / 1024), (long)chip_fragments);
    Move(rp, 270, 24);
    Text(rp, text_buf, strlen(text_buf));

    /* Row 2: Fast free / total */
    sprintf(text_buf, "FAST: %ldK / %ldK free",
            (long)(fast_free / 1024), (long)(fast_total / 1024));
    Move(rp, 10, 36);
    Text(rp, text_buf, strlen(text_buf));
    sprintf(text_buf, "Lrg: %ldK  Frags: %ld",
            (long)(fast_largest / 1024), (long)fast_fragments);
    Move(rp, 270, 36);
    Text(rp, text_buf, strlen(text_buf));

    /* Row 3: Usage % and samples */
    if (chip_total > 0 && fast_total > 0) {
        LONG chip_pct = (chip_free * 100) / chip_total;
        LONG fast_pct = (fast_free * 100) / fast_total;
        sprintf(text_buf, "Use: Chip %ld%%  Fast %ld%%",
                (long)(100 - chip_pct), (long)(100 - fast_pct));
    } else {
        sprintf(text_buf, "Use: --");
    }
    Move(rp, 10, 48);
    Text(rp, text_buf, strlen(text_buf));
    sprintf(text_buf, "Samples: %lu  Leaks: %ld",
            (unsigned long)sample_count, (long)leak_count);
    Move(rp, 270, 48);
    Text(rp, text_buf, strlen(text_buf));
}

static void draw_chart(struct RastPort *rp)
{
    LONG i, idx, val;
    LONG bar_w, bar_h;
    LONG max_val;
    LONG num_samples;
    LONG x;

    /* Clear chart area */
    SetAPen(rp, 0);
    RectFill(rp, CHART_X, CHART_Y, CHART_X + CHART_W - 1, CHART_Y + CHART_H - 1);

    /* Draw border */
    SetAPen(rp, 1);
    Move(rp, CHART_X, CHART_Y);
    Draw(rp, CHART_X, CHART_Y + CHART_H - 1);
    Draw(rp, CHART_X + CHART_W - 1, CHART_Y + CHART_H - 1);

    /* Label */
    Move(rp, CHART_X + 5, CHART_Y + 10);
    sprintf(text_buf, "Chip Free History");
    Text(rp, text_buf, strlen(text_buf));

    /* Determine how many samples we have */
    num_samples = (LONG)sample_count;
    if (num_samples > MAX_SAMPLES) num_samples = MAX_SAMPLES;
    if (num_samples < 2) return;

    /* Find max value for scaling */
    max_val = 0;
    for (i = 0; i < num_samples; i++) {
        idx = (sample_index - num_samples + i + MAX_SAMPLES) % MAX_SAMPLES;
        if (sample_ring[idx] > max_val) {
            max_val = sample_ring[idx];
        }
    }
    if (max_val == 0) max_val = 1;

    /* Draw bars */
    bar_w = (CHART_W - 4) / MAX_SAMPLES;
    if (bar_w < 1) bar_w = 1;

    SetAPen(rp, 3); /* Color 3 for bars */

    for (i = 0; i < num_samples; i++) {
        idx = (sample_index - num_samples + i + MAX_SAMPLES) % MAX_SAMPLES;
        val = sample_ring[idx];

        bar_h = (val * (CHART_H - 16)) / max_val;
        if (bar_h < 1) bar_h = 1;

        x = CHART_X + 2 + i * bar_w;

        RectFill(rp, x, CHART_Y + CHART_H - 1 - bar_h,
                 x + bar_w - 1, CHART_Y + CHART_H - 2);
    }

    /* Draw scale labels */
    SetAPen(rp, 1);
    sprintf(text_buf, "%ldK", (long)(max_val / 1024));
    Move(rp, CHART_X + CHART_W - 50, CHART_Y + 10);
    Text(rp, text_buf, strlen(text_buf));
}

/* ---- Hook Callbacks ---- */

static int hook_snapshot(const char *args, char *buf, int bufSize)
{
    (void)args;
    sprintf(buf,
        "CHIP: %ld/%ld free (largest %ld, %ld frags) | "
        "FAST: %ld/%ld free (largest %ld, %ld frags) | "
        "Samples: %lu",
        (long)chip_free, (long)chip_total, (long)chip_largest, (long)chip_fragments,
        (long)fast_free, (long)fast_total, (long)fast_largest, (long)fast_fragments,
        (unsigned long)sample_count);
    return 0;
}

static int hook_alloc_test(const char *args, char *buf, int bufSize)
{
    LONG size = 0;
    APTR mem;
    LONG i;
    LONG before_chip, before_fast;
    LONG after_chip, after_fast;
    LONG dummy1, dummy2, dummy3;

    /* Parse size from args */
    if (args && args[0]) {
        for (i = 0; args[i] >= '0' && args[i] <= '9'; i++) {
            size = size * 10 + (args[i] - '0');
        }
    }
    if (size <= 0) size = 4096;

    before_chip = AvailMem(MEMF_CHIP);
    before_fast = AvailMem(MEMF_FAST);

    /* Allocate and immediately free */
    mem = AllocMem(size, MEMF_ANY);
    if (mem) {
        FreeMem(mem, size);
        after_chip = AvailMem(MEMF_CHIP);
        after_fast = AvailMem(MEMF_FAST);
        sprintf(buf, "OK: alloc+free %ld bytes. Chip delta: %ld, Fast delta: %ld",
                (long)size, (long)(after_chip - before_chip),
                (long)(after_fast - before_fast));
    } else {
        sprintf(buf, "FAIL: could not allocate %ld bytes", (long)size);
    }

    AB_I("alloc_test: %s", buf);
    return 0;
}

static int hook_leak_test(const char *args, char *buf, int bufSize)
{
    LONG size = 0;
    APTR mem;
    LONG i;

    /* Parse size from args */
    if (args && args[0]) {
        for (i = 0; args[i] >= '0' && args[i] <= '9'; i++) {
            size = size * 10 + (args[i] - '0');
        }
    }
    if (size <= 0) size = 4096;

    if (leak_count >= MAX_LEAKS) {
        sprintf(buf, "FAIL: leak tracker full (%ld slots)", (long)MAX_LEAKS);
        return -1;
    }

    mem = AllocMem(size, MEMF_ANY);
    if (mem) {
        leaked_ptrs[leak_count] = mem;
        leak_count++;
        sprintf(buf, "LEAKED %ld bytes at %p (total leaks: %ld)",
                (long)size, mem, (long)leak_count);
        AB_W("leak_test: deliberately leaked %ld bytes at %p", (long)size, mem);
    } else {
        sprintf(buf, "FAIL: could not allocate %ld bytes", (long)size);
    }

    return 0;
}

/* ---- Main ---- */

int main(void)
{
    struct Window *win;
    struct IntuiMessage *imsg;
    ULONG class;
    LONG tick_counter = 0;
    LONG hb_counter = 0;
    ULONG signals;

    IntuitionBase = (struct IntuitionBase *)OpenLibrary((CONST_STRPTR)"intuition.library", 36);
    if (!IntuitionBase) return 1;

    GfxBase = (struct GfxBase *)OpenLibrary((CONST_STRPTR)"graphics.library", 36);
    if (!GfxBase) {
        CloseLibrary((struct Library *)IntuitionBase);
        return 1;
    }

    printf("memory_monitor v%s\n", VERSION);

    /* Connect to bridge */
    if (ab_init("memory_monitor") != 0) {
        printf("  Bridge: NOT FOUND\n");
        bridge_ok = 0;
    } else {
        printf("  Bridge: CONNECTED\n");
        bridge_ok = 1;
    }

    AB_I("Memory Monitor v%s starting", VERSION);

    /* Register variables */
    ab_register_var("chip_free", AB_TYPE_I32, &chip_free);
    ab_register_var("fast_free", AB_TYPE_I32, &fast_free);
    ab_register_var("chip_total", AB_TYPE_I32, &chip_total);
    ab_register_var("fast_total", AB_TYPE_I32, &fast_total);
    ab_register_var("chip_largest", AB_TYPE_I32, &chip_largest);
    ab_register_var("fast_largest", AB_TYPE_I32, &fast_largest);
    ab_register_var("chip_fragments", AB_TYPE_I32, &chip_fragments);
    ab_register_var("fast_fragments", AB_TYPE_I32, &fast_fragments);
    ab_register_var("sample_count", AB_TYPE_U32, &sample_count);
    ab_register_var("update_interval", AB_TYPE_I32, &update_interval);

    /* Register hooks */
    ab_register_hook("snapshot", "Return formatted memory stats summary", hook_snapshot);
    ab_register_hook("alloc_test", "Allocate and free N bytes (arg: size)", hook_alloc_test);
    ab_register_hook("leak_test", "Allocate N bytes without freeing (arg: size)", hook_leak_test);

    /* Register memory region: the sample ring buffer */
    ab_register_memregion("chip_samples", (APTR)sample_ring,
                          sizeof(sample_ring),
                          "Ring buffer of last 100 chip_free values (LONGs)");

    /* Clear ring buffer */
    memset(sample_ring, 0, sizeof(sample_ring));
    memset(leaked_ptrs, 0, sizeof(leaked_ptrs));

    /* Get initial stats */
    update_stats();

    /* Open window */
    win = OpenWindowTags(NULL,
        WA_Left, 20,
        WA_Top, 20,
        WA_Width, WIN_W,
        WA_Height, WIN_H,
        WA_Title, (ULONG)(bridge_ok ?
            "Memory Monitor v1.0 [Bridge: OK]" :
            "Memory Monitor v1.0 [Bridge: OFF]"),
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

    AB_I("Window opened, entering main loop");

    /* Initial draw */
    draw_text_stats(win->RPort);
    draw_chart(win->RPort);

    /* Main loop */
    while (running) {
        /* Check CTRL-C */
        signals = SetSignal(0L, 0L);
        if (signals & SIGBREAKF_CTRL_C) {
            AB_I("CTRL-C received");
            running = 0;
            break;
        }

        /* Process window messages */
        while ((imsg = (struct IntuiMessage *)GetMsg(win->UserPort))) {
            class = imsg->Class;
            ReplyMsg((struct Message *)imsg);

            if (class == IDCMP_CLOSEWINDOW) {
                AB_I("Close gadget pressed");
                running = 0;
            }
        }

        if (!running) break;

        tick_counter++;

        /* Update stats at configured interval */
        if (tick_counter >= update_interval) {
            tick_counter = 0;
            update_stats();
            draw_text_stats(win->RPort);
            draw_chart(win->RPort);

            /* Push key vars periodically */
            ab_push_var("chip_free");
            ab_push_var("fast_free");
            ab_push_var("chip_fragments");
            ab_push_var("fast_fragments");
        }

        /* Heartbeat every 500 ticks */
        if ((++hb_counter % 500) == 0) {
            ab_heartbeat();
        }

        /* Poll bridge for commands */
        ab_poll();

        /* ~20fps delay */
        Delay(1);
    }

    AB_I("Memory Monitor shutting down (leaks: %ld)", (long)leak_count);

    /* Note: deliberately leaked memory is NOT freed - that's the point.
     * In a real app you'd track and free everything. */

    CloseWindow(win);
    ab_cleanup();
    CloseLibrary((struct Library *)GfxBase);
    CloseLibrary((struct Library *)IntuitionBase);

    printf("memory_monitor exited. Deliberate leaks: %ld\n", (long)leak_count);
    return 0;
}
