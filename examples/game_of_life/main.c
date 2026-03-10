/*
 * Conway's Game of Life for Amiga
 * Uses AmigaBridge for remote monitoring and control.
 */

#include <exec/types.h>
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

#define GRID_W 80
#define GRID_H 60
#define CELL_SIZE 5
#define WIN_W (GRID_W * CELL_SIZE)
#define WIN_H (GRID_H * CELL_SIZE)

/* Double-buffered grids */
static UBYTE grid_cur[GRID_H][GRID_W];
static UBYTE grid_next[GRID_H][GRID_W];

/* Bridge variables */
static ULONG generation = 0;
static LONG population = 0;
static LONG speed = 5;       /* delay in ticks */
static LONG running_sim = 1; /* 1=running, 0=paused */
static LONG grid_w = GRID_W;
static LONG grid_h = GRID_H;

static LONG app_running = 1;
static LONG bridge_ok = 0;

/* Simple pseudo-random number generator (no malloc needed) */
static ULONG rng_state = 12345;

static ULONG rng_next(void)
{
    rng_state ^= rng_state << 13;
    rng_state ^= rng_state >> 17;
    rng_state ^= rng_state << 5;
    return rng_state;
}

static void rng_seed(ULONG seed)
{
    rng_state = seed ? seed : 1;
}

/* Count live neighbors with wrapping */
static int count_neighbors(int x, int y)
{
    int count = 0;
    int dx, dy, nx, ny;

    for (dy = -1; dy <= 1; dy++) {
        for (dx = -1; dx <= 1; dx++) {
            if (dx == 0 && dy == 0) continue;
            nx = (x + dx + GRID_W) % GRID_W;
            ny = (y + dy + GRID_H) % GRID_H;
            if (grid_cur[ny][nx]) count++;
        }
    }
    return count;
}

/* Advance one generation */
static void step_generation(void)
{
    int x, y, n;

    population = 0;
    for (y = 0; y < GRID_H; y++) {
        for (x = 0; x < GRID_W; x++) {
            n = count_neighbors(x, y);
            if (grid_cur[y][x]) {
                /* alive: survive with 2 or 3 neighbors */
                grid_next[y][x] = (n == 2 || n == 3) ? 1 : 0;
            } else {
                /* dead: birth with exactly 3 neighbors */
                grid_next[y][x] = (n == 3) ? 1 : 0;
            }
            if (grid_next[y][x]) population++;
        }
    }
    generation++;
}

/* Swap grids by copying next into cur */
static void swap_grids(void)
{
    memcpy(grid_cur, grid_next, GRID_W * GRID_H);
}

/* Draw only cells that changed between cur and next */
static void draw_changes(struct RastPort *rp)
{
    int x, y;

    for (y = 0; y < GRID_H; y++) {
        for (x = 0; x < GRID_W; x++) {
            if (grid_next[y][x] != grid_cur[y][x]) {
                SetAPen(rp, grid_next[y][x] ? 1 : 0);
                RectFill(rp,
                    x * CELL_SIZE, y * CELL_SIZE,
                    x * CELL_SIZE + CELL_SIZE - 1,
                    y * CELL_SIZE + CELL_SIZE - 1);
            }
        }
    }
}

/* Draw entire grid (used for initial draw and after clear/randomize) */
static void draw_full(struct RastPort *rp)
{
    int x, y;

    /* Clear background */
    SetAPen(rp, 0);
    RectFill(rp, 0, 0, WIN_W - 1, WIN_H - 1);

    /* Draw live cells */
    SetAPen(rp, 1);
    for (y = 0; y < GRID_H; y++) {
        for (x = 0; x < GRID_W; x++) {
            if (grid_cur[y][x]) {
                RectFill(rp,
                    x * CELL_SIZE, y * CELL_SIZE,
                    x * CELL_SIZE + CELL_SIZE - 1,
                    y * CELL_SIZE + CELL_SIZE - 1);
            }
        }
    }
}

/* Clear grid */
static void clear_grid(void)
{
    memset(grid_cur, 0, GRID_W * GRID_H);
    memset(grid_next, 0, GRID_W * GRID_H);
    generation = 0;
    population = 0;
}

/* Fill grid with random cells */
static void randomize_grid(int density)
{
    int x, y;

    if (density < 1) density = 1;
    if (density > 100) density = 100;

    population = 0;
    for (y = 0; y < GRID_H; y++) {
        for (x = 0; x < GRID_W; x++) {
            grid_cur[y][x] = ((int)(rng_next() % 100) < density) ? 1 : 0;
            if (grid_cur[y][x]) population++;
        }
    }
    generation = 0;
}

/* Place a glider at given grid coordinates */
static void place_glider(int cx, int cy)
{
    /* Standard glider pattern:
     *  .#.
     *  ..#
     *  ###
     */
    static const int offsets[][2] = {
        {1, 0}, {2, 1}, {0, 2}, {1, 2}, {2, 2}
    };
    int i, gx, gy;

    for (i = 0; i < 5; i++) {
        gx = (cx + offsets[i][0]) % GRID_W;
        gy = (cy + offsets[i][1]) % GRID_H;
        grid_cur[gy][gx] = 1;
    }
}

/* Place a Gosper glider gun at top-left area */
static void place_gosper_gun(void)
{
    /* Gosper glider gun - 36x9 pattern, place at (1,1) */
    static const int cells[][2] = {
        /* Left square */
        {1,5},{1,6},{2,5},{2,6},
        /* Left part */
        {11,5},{11,6},{11,7},
        {12,4},{12,8},
        {13,3},{13,9},
        {14,3},{14,9},
        {15,6},
        {16,4},{16,8},
        {17,5},{17,6},{17,7},
        {18,6},
        /* Right part */
        {21,3},{21,4},{21,5},
        {22,3},{22,4},{22,5},
        {23,2},{23,6},
        {25,1},{25,2},{25,6},{25,7},
        /* Right square */
        {35,3},{35,4},{36,3},{36,4}
    };
    int i, num;

    num = (int)(sizeof(cells) / sizeof(cells[0]));
    for (i = 0; i < num; i++) {
        int gx = cells[i][0];
        int gy = cells[i][1];
        if (gx >= 0 && gx < GRID_W && gy >= 0 && gy < GRID_H) {
            grid_cur[gy][gx] = 1;
        }
    }
}

/* Parse an integer from a string, return default if empty/null */
static int parse_int(const char *s, int def)
{
    int val = 0;
    int neg = 0;

    if (!s || *s == '\0') return def;
    if (*s == '-') { neg = 1; s++; }
    while (*s >= '0' && *s <= '9') {
        val = val * 10 + (*s - '0');
        s++;
    }
    return neg ? -val : val;
}

/* ---- Hook callbacks ---- */

static struct Window *g_win = NULL; /* for redraw after hooks */

static int hook_clear(const char *args, char *buf, int bufSize)
{
    (void)args;
    clear_grid();
    if (g_win) draw_full(g_win->RPort);
    AB_I("Grid cleared");
    strncpy(buf, "OK", (size_t)(bufSize - 1));
    return 0;
}

static int hook_randomize(const char *args, char *buf, int bufSize)
{
    int density = parse_int(args, 30);
    randomize_grid(density);
    if (g_win) draw_full(g_win->RPort);
    AB_I("Randomized grid, density=%ld%%", (long)density);
    sprintf(buf, "OK density=%ld", (long)density);
    return 0;
}

static int hook_glider(const char *args, char *buf, int bufSize)
{
    int gx = GRID_W / 2;
    int gy = GRID_H / 2;

    if (args && *args) {
        /* Parse "x,y" */
        const char *comma;
        gx = parse_int(args, gx);
        comma = args;
        while (*comma && *comma != ',') comma++;
        if (*comma == ',') {
            gy = parse_int(comma + 1, gy);
        }
    }

    place_glider(gx, gy);
    if (g_win) draw_full(g_win->RPort);
    AB_I("Placed glider at %ld,%ld", (long)gx, (long)gy);
    sprintf(buf, "OK at %ld,%ld", (long)gx, (long)gy);
    return 0;
}

static int hook_gosper(const char *args, char *buf, int bufSize)
{
    (void)args;
    clear_grid();
    place_gosper_gun();
    if (g_win) draw_full(g_win->RPort);
    AB_I("Placed Gosper glider gun");
    strncpy(buf, "OK", (size_t)(bufSize - 1));
    return 0;
}

static int hook_toggle_pause(const char *args, char *buf, int bufSize)
{
    (void)args;
    running_sim = running_sim ? 0 : 1;
    AB_I("Simulation %s", running_sim ? "resumed" : "paused");
    sprintf(buf, "OK %s", running_sim ? "running" : "paused");
    return 0;
}

int main(void)
{
    struct Window *win;
    struct IntuiMessage *imsg;
    ULONG class;
    UWORD code;
    WORD mx, my;
    int hb_counter = 0;
    ULONG tick_seed;
    static char title_buf[80];

    IntuitionBase = (struct IntuitionBase *)OpenLibrary(
        (CONST_STRPTR)"intuition.library", 36);
    if (!IntuitionBase) return 1;

    GfxBase = (struct GfxBase *)OpenLibrary(
        (CONST_STRPTR)"graphics.library", 36);
    if (!GfxBase) {
        CloseLibrary((struct Library *)IntuitionBase);
        return 1;
    }

    printf("Game of Life v1.0\n");

    /* Connect to AmigaBridge */
    if (ab_init("game_of_life") != 0) {
        printf("  Bridge: NOT FOUND\n");
        bridge_ok = 0;
    } else {
        printf("  Bridge: CONNECTED\n");
        bridge_ok = 1;
    }

    AB_I("Game of Life starting");

    /* Register variables */
    ab_register_var("generation", AB_TYPE_U32, &generation);
    ab_register_var("population", AB_TYPE_I32, &population);
    ab_register_var("speed", AB_TYPE_I32, &speed);
    ab_register_var("running", AB_TYPE_I32, &running_sim);
    ab_register_var("grid_w", AB_TYPE_I32, &grid_w);
    ab_register_var("grid_h", AB_TYPE_I32, &grid_h);

    /* Register hooks */
    ab_register_hook("clear", "Clear all cells", hook_clear);
    ab_register_hook("randomize", "Random fill (arg: density%)", hook_randomize);
    ab_register_hook("glider", "Place glider (arg: x,y)", hook_glider);
    ab_register_hook("gosper", "Place Gosper glider gun", hook_gosper);
    ab_register_hook("toggle_pause", "Toggle pause/resume", hook_toggle_pause);

    /* Register memory region for grid data */
    ab_register_memregion("grid", (APTR)grid_cur, GRID_W * GRID_H,
                          "Game of Life grid (80x60 bytes)");

    /* Seed RNG from system tick */
    tick_seed = (ULONG)FindTask(NULL);
    rng_seed(tick_seed);

    /* Initialize with random field */
    randomize_grid(30);

    /* Open window with GimmeZeroZero */
    win = OpenWindowTags(NULL,
        WA_Left, 50,
        WA_Top, 30,
        WA_InnerWidth, WIN_W,
        WA_InnerHeight, WIN_H,
        WA_Title, (ULONG)"Game of Life",
        WA_CloseGadget, TRUE,
        WA_DragBar, TRUE,
        WA_DepthGadget, TRUE,
        WA_GimmeZeroZero, TRUE,
        WA_IDCMP, IDCMP_CLOSEWINDOW | IDCMP_MOUSEBUTTONS,
        WA_Activate, TRUE,
        TAG_DONE);

    if (!win) {
        AB_E("Failed to open window");
        ab_cleanup();
        CloseLibrary((struct Library *)GfxBase);
        CloseLibrary((struct Library *)IntuitionBase);
        return 1;
    }

    g_win = win;

    AB_I("Window opened (%ldx%ld, %ldx%ld cells)",
         (long)WIN_W, (long)WIN_H, (long)GRID_W, (long)GRID_H);

    /* Initial full draw */
    draw_full(win->RPort);

    /* Main loop */
    while (app_running) {
        /* Check for window messages */
        while ((imsg = (struct IntuiMessage *)GetMsg(win->UserPort))) {
            class = imsg->Class;
            code = imsg->Code;
            mx = imsg->MouseX;
            my = imsg->MouseY;
            ReplyMsg((struct Message *)imsg);

            if (class == IDCMP_CLOSEWINDOW) {
                AB_I("Close gadget pressed");
                app_running = 0;
            }
            else if (class == IDCMP_MOUSEBUTTONS && code == SELECTDOWN) {
                /* Toggle cell under mouse when paused */
                if (!running_sim) {
                    int cx = mx / CELL_SIZE;
                    int cy = my / CELL_SIZE;
                    if (cx >= 0 && cx < GRID_W && cy >= 0 && cy < GRID_H) {
                        grid_cur[cy][cx] = grid_cur[cy][cx] ? 0 : 1;
                        SetAPen(win->RPort, grid_cur[cy][cx] ? 1 : 0);
                        RectFill(win->RPort,
                            cx * CELL_SIZE, cy * CELL_SIZE,
                            cx * CELL_SIZE + CELL_SIZE - 1,
                            cy * CELL_SIZE + CELL_SIZE - 1);

                        /* Recalculate population */
                        {
                            int px, py;
                            population = 0;
                            for (py = 0; py < GRID_H; py++)
                                for (px = 0; px < GRID_W; px++)
                                    if (grid_cur[py][px]) population++;
                        }
                    }
                }
            }
        }

        /* Check for CTRL-C */
        if (SetSignal(0L, SIGBREAKF_CTRL_C) & SIGBREAKF_CTRL_C) {
            AB_I("CTRL-C received");
            app_running = 0;
        }

        /* Simulation step */
        if (running_sim && app_running) {
            step_generation();
            draw_changes(win->RPort);
            swap_grids();

            /* Update title periodically */
            if ((generation % 10) == 0) {
                sprintf(title_buf, "Game of Life - Gen %lu  Pop %ld",
                        (unsigned long)generation, (long)population);
                SetWindowTitles(win, title_buf, (UBYTE *)~0);
            }
        }

        /* Heartbeat every 500 iterations */
        if ((++hb_counter % 500) == 0) {
            ab_heartbeat();
        }

        /* Poll bridge for commands */
        ab_poll();

        /* Delay */
        if (speed > 0) {
            Delay(speed);
        } else {
            Delay(1);
        }
    }

    AB_I("Game of Life shutting down (gen=%lu, pop=%ld)",
         (unsigned long)generation, (long)population);

    g_win = NULL;
    CloseWindow(win);
    ab_cleanup();
    CloseLibrary((struct Library *)GfxBase);
    CloseLibrary((struct Library *)IntuitionBase);

    return 0;
}
