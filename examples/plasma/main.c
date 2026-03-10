#include <exec/types.h>
#include <intuition/intuition.h>
#include <intuition/screens.h>
#include <graphics/gfxbase.h>
#include <graphics/rastport.h>
#include <graphics/view.h>
#include <proto/exec.h>
#include <proto/intuition.h>
#include <proto/graphics.h>
#include <proto/dos.h>

#include <stdio.h>
#include <string.h>

#include "bridge_client.h"

struct IntuitionBase *IntuitionBase = NULL;
struct GfxBase *GfxBase = NULL;

/* Screen dimensions */
#define SCR_W 320
#define SCR_H 256
#define NUM_COLORS 16
#define NUM_BITPLANES 4
#define NUM_BANDS (SCR_H / (SCR_H / NUM_COLORS))  /* 16 bands */
#define BAND_HEIGHT (SCR_H / NUM_COLORS)           /* 16 pixels per band */

/* Sine table - 256 entries, values 0-255 */
static UBYTE sine_table[256];

/* Palette storage for SetRGB4 calls: R, G, B per color */
static UBYTE pal_r[NUM_COLORS];
static UBYTE pal_g[NUM_COLORS];
static UBYTE pal_b[NUM_COLORS];

/* Registered variables */
static LONG speed = 1;
static LONG palette_offset = 0;
static LONG red_freq = 3;
static LONG green_freq = 5;
static LONG blue_freq = 7;
static LONG brightness = 15;
static ULONG frame_count = 0;

/* Simple pseudo-random number generator */
static ULONG rng_state = 12345;

static ULONG simple_rand(void)
{
    rng_state = rng_state * 1103515245UL + 12345UL;
    return (rng_state >> 16) & 0x7FFF;
}

/* Initialize sine table with values 0-255 */
static void init_sine_table(void)
{
    int i;
    /* Approximate sine using a parabolic curve in quadrants.
     * Maps 0-255 input to 0-255 output (shifted sine, no negatives). */
    for (i = 0; i < 64; i++) {
        /* First quadrant: 0 to peak (0->128->255) */
        LONG val = (i * 4);  /* 0..252 */
        sine_table[i] = (UBYTE)(128 + (val * 127 / 252));
    }
    for (i = 64; i < 128; i++) {
        /* Second quadrant: peak back to mid */
        LONG val = ((127 - i) * 4);
        sine_table[i] = (UBYTE)(128 + (val * 127 / 252));
    }
    for (i = 128; i < 192; i++) {
        /* Third quadrant: mid down to trough */
        LONG val = ((i - 128) * 4);
        sine_table[i] = (UBYTE)(128 - (val * 128 / 252));
    }
    for (i = 192; i < 256; i++) {
        /* Fourth quadrant: trough back to mid */
        LONG val = ((255 - i) * 4);
        sine_table[i] = (UBYTE)(128 - (val * 128 / 252));
    }
}

/* Generate palette based on current offset and frequencies */
static void generate_palette(LONG offset)
{
    int i;
    LONG bright = brightness;
    if (bright < 0) bright = 0;
    if (bright > 15) bright = 15;

    for (i = 0; i < NUM_COLORS; i++) {
        LONG idx_r = ((i * red_freq * 16) + offset) & 0xFF;
        LONG idx_g = ((i * green_freq * 16) + offset * 2) & 0xFF;
        LONG idx_b = ((i * blue_freq * 16) + offset * 3) & 0xFF;

        /* sine_table gives 0-255, scale to 0-brightness */
        pal_r[i] = (UBYTE)((sine_table[idx_r] * bright) / 255);
        pal_g[i] = (UBYTE)((sine_table[idx_g] * bright) / 255);
        pal_b[i] = (UBYTE)((sine_table[idx_b] * bright) / 255);
    }
}

/* Apply palette to screen viewport */
static void apply_palette(struct ViewPort *vp)
{
    int i;
    for (i = 0; i < NUM_COLORS; i++) {
        SetRGB4(vp, (long)i, (long)pal_r[i], (long)pal_g[i], (long)pal_b[i]);
    }
}

/* Fill screen with horizontal color bands */
static void draw_bands(struct RastPort *rp)
{
    int i;
    for (i = 0; i < NUM_COLORS; i++) {
        LONG y0 = i * BAND_HEIGHT;
        LONG y1 = y0 + BAND_HEIGHT - 1;
        if (y1 >= SCR_H) y1 = SCR_H - 1;
        SetAPen(rp, (long)i);
        RectFill(rp, 0L, y0, (long)(SCR_W - 1), y1);
    }
}

/* Hook: randomize frequencies */
static int hook_randomize(const char *args, char *resultBuf, int bufSize)
{
    red_freq = (LONG)(simple_rand() % 10) + 1;
    green_freq = (LONG)(simple_rand() % 10) + 1;
    blue_freq = (LONG)(simple_rand() % 10) + 1;
    sprintf(resultBuf, "Randomized: r=%ld g=%ld b=%ld",
            (long)red_freq, (long)green_freq, (long)blue_freq);
    resultBuf[bufSize - 1] = '\0';
    return 0;
}

/* Hook: send screen to back (reveal other screens) */
static struct Screen *g_scr = NULL;

static int hook_screen_to_back(const char *args, char *resultBuf, int bufSize)
{
    if (g_scr) ScreenToBack(g_scr);
    strncpy(resultBuf, "Screen sent to back", bufSize - 1);
    resultBuf[bufSize - 1] = '\0';
    return 0;
}

/* Hook: bring screen to front */
static int hook_screen_to_front(const char *args, char *resultBuf, int bufSize)
{
    if (g_scr) ScreenToFront(g_scr);
    strncpy(resultBuf, "Screen brought to front", bufSize - 1);
    resultBuf[bufSize - 1] = '\0';
    return 0;
}

/* Hook: reset to defaults */
static int hook_reset(const char *args, char *resultBuf, int bufSize)
{
    speed = 1;
    palette_offset = 0;
    red_freq = 3;
    green_freq = 5;
    blue_freq = 7;
    brightness = 15;
    strncpy(resultBuf, "Reset to defaults", bufSize - 1);
    resultBuf[bufSize - 1] = '\0';
    return 0;
}

int main(void)
{
    struct Screen *scr = NULL;
    struct Window *win = NULL;
    struct IntuiMessage *msg;
    struct RastPort *rp;
    struct ViewPort *vp;
    ULONG class;
    BOOL running = TRUE;

    IntuitionBase = (struct IntuitionBase *)OpenLibrary((CONST_STRPTR)"intuition.library", 36);
    if (!IntuitionBase) return 1;

    GfxBase = (struct GfxBase *)OpenLibrary((CONST_STRPTR)"graphics.library", 36);
    if (!GfxBase) {
        CloseLibrary((struct Library *)IntuitionBase);
        return 1;
    }

    /* Connect to AmigaBridge daemon */
    if (ab_init("plasma") != 0) {
        printf("Bridge: NOT FOUND\n");
    } else {
        printf("Bridge: CONNECTED\n");
    }

    AB_I("Plasma demo starting");

    /* Register variables */
    ab_register_var("speed", AB_TYPE_I32, &speed);
    ab_register_var("palette_offset", AB_TYPE_I32, &palette_offset);
    ab_register_var("red_freq", AB_TYPE_I32, &red_freq);
    ab_register_var("green_freq", AB_TYPE_I32, &green_freq);
    ab_register_var("blue_freq", AB_TYPE_I32, &blue_freq);
    ab_register_var("brightness", AB_TYPE_I32, &brightness);
    ab_register_var("frame_count", AB_TYPE_U32, &frame_count);

    /* Register hooks */
    ab_register_hook("randomize", "Randomize color frequencies", hook_randomize);
    ab_register_hook("reset", "Reset all parameters to defaults", hook_reset);
    ab_register_hook("to_back", "Send screen to back", hook_screen_to_back);
    ab_register_hook("to_front", "Bring screen to front", hook_screen_to_front);

    /* Register palette memory region for inspection */
    ab_register_memregion("palette_rgb", pal_r, sizeof(pal_r) + sizeof(pal_g) + sizeof(pal_b),
                          "Palette data: 16 R, 16 G, 16 B values");

    /* Initialize sine table */
    init_sine_table();

    /* Open custom screen: 320x256, 4 bitplanes = 16 colors */
    scr = OpenScreenTags(NULL,
        SA_Width, SCR_W,
        SA_Height, SCR_H,
        SA_Depth, NUM_BITPLANES,
        SA_Title, (ULONG)"Plasma Demo",
        SA_Type, CUSTOMSCREEN,
        SA_Quiet, TRUE,
        SA_ShowTitle, FALSE,
        TAG_DONE);

    if (!scr) {
        AB_E("Failed to open screen");
        ab_cleanup();
        CloseLibrary((struct Library *)GfxBase);
        CloseLibrary((struct Library *)IntuitionBase);
        return 1;
    }

    /* Open a backdrop window on the screen for IDCMP (close gadget) */
    win = OpenWindowTags(NULL,
        WA_CustomScreen, (ULONG)scr,
        WA_Left, 0,
        WA_Top, 0,
        WA_Width, SCR_W,
        WA_Height, SCR_H,
        WA_Backdrop, TRUE,
        WA_Borderless, TRUE,
        WA_Activate, TRUE,
        WA_IDCMP, IDCMP_CLOSEWINDOW | IDCMP_RAWKEY,
        WA_RMBTrap, TRUE,
        TAG_DONE);

    if (!win) {
        AB_E("Failed to open window");
        CloseScreen(scr);
        ab_cleanup();
        CloseLibrary((struct Library *)GfxBase);
        CloseLibrary((struct Library *)IntuitionBase);
        return 1;
    }

    g_scr = scr;
    rp = &scr->RastPort;
    vp = &scr->ViewPort;

    /* Draw the color bands once */
    draw_bands(rp);

    AB_I("Screen opened: %ldx%ld, %ld colors", (long)SCR_W, (long)SCR_H, (long)NUM_COLORS);

    /* Main loop */
    while (running) {
        /* Check for window messages */
        while ((msg = (struct IntuiMessage *)GetMsg(win->UserPort))) {
            class = msg->Class;
            ReplyMsg((struct Message *)msg);
            if (class == IDCMP_CLOSEWINDOW) {
                running = FALSE;
            }
        }

        if (!running) break;

        /* Shift palette offset by speed */
        palette_offset += speed;

        /* Generate and apply new palette */
        generate_palette(palette_offset);
        apply_palette(vp);

        frame_count++;

        /* Push variables every 60 frames */
        if ((frame_count % 60) == 0) {
            ab_push_var("speed");
            ab_push_var("palette_offset");
            ab_push_var("red_freq");
            ab_push_var("green_freq");
            ab_push_var("blue_freq");
            ab_push_var("brightness");
            ab_push_var("frame_count");
            ab_heartbeat();
        }

        /* Log status periodically */
        if ((frame_count % 300) == 0) {
            AB_D("Frame %lu offset=%ld r=%ld g=%ld b=%ld",
                 (unsigned long)frame_count, (long)palette_offset,
                 (long)red_freq, (long)green_freq, (long)blue_freq);
        }

        /* Poll for commands from bridge daemon */
        ab_poll();

        /* Check for CTRL-C break signal */
        if (SetSignal(0L, SIGBREAKF_CTRL_C) & SIGBREAKF_CTRL_C) {
            AB_I("Break signal received");
            running = FALSE;
        }

        /* ~50fps: 1 tick delay */
        Delay(1L);
    }

    AB_I("Plasma demo shutting down");

    CloseWindow(win);
    CloseScreen(scr);
    ab_cleanup();
    CloseLibrary((struct Library *)GfxBase);
    CloseLibrary((struct Library *)IntuitionBase);

    return 0;
}
