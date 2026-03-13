#include "copper.h"

#include <exec/memory.h>
#include <graphics/gfxbase.h>
#include <graphics/gfxmacros.h>
#include <hardware/custom.h>
#include <proto/exec.h>
#include <proto/graphics.h>

extern struct Custom custom;

static struct UCopList *sky_ucl = NULL;
static struct UCopList *rainbow_ucl = NULL;

/* Sky gradient colors */
static UWORD sky_colors[COPPER_LINES];

/* Rainbow hue table - 7 brightness levels for each of 256 hue positions */
static void hue_to_rgb(WORD hue, WORD bright, UWORD *out)
{
    /* hue 0-255, bright 0-6 (0=brightest) */
    WORD r, g, b;
    WORD sector = hue / 43;  /* 0-5 */
    WORD frac = (hue % 43) * 6;  /* 0-252 */
    WORD scale = 15 - (bright * 2);
    if (scale < 1) scale = 1;

    switch (sector) {
        case 0: r = scale; g = (scale * frac) >> 8; b = 0; break;
        case 1: r = (scale * (255 - frac)) >> 8; g = scale; b = 0; break;
        case 2: r = 0; g = scale; b = (scale * frac) >> 8; break;
        case 3: r = 0; g = (scale * (255 - frac)) >> 8; b = scale; break;
        case 4: r = (scale * frac) >> 8; g = 0; b = scale; break;
        default: r = scale; g = 0; b = (scale * (255 - frac)) >> 8; break;
    }
    if (r > 0xF) r = 0xF;
    if (g > 0xF) g = 0xF;
    if (b > 0xF) b = 0xF;
    *out = (r << 8) | (g << 4) | b;
}

static void build_gradient(void)
{
    WORD i;
    for (i = 0; i < COPPER_LINES; i++) {
        WORD t = (i * 256) / COPPER_LINES;
        WORD r = (0x0 * (256 - t) + 0x3 * t) >> 8;
        WORD g = (0x0 * (256 - t) + 0x5 * t) >> 8;
        WORD b = (0x5 * (256 - t) + 0xB * t) >> 8;
        if (r > 0xF) r = 0xF;
        if (g > 0xF) g = 0xF;
        if (b > 0xF) b = 0xF;
        sky_colors[i] = (r << 8) | (g << 4) | b;
    }
}

static struct UCopList *build_sky_only(void)
{
    struct UCopList *ucl;
    WORD i;

    ucl = (struct UCopList *)AllocMem(sizeof(struct UCopList), MEMF_PUBLIC | MEMF_CLEAR);
    if (!ucl) return NULL;

    CINIT(ucl, COPPER_LINES * 3);
    for (i = 0; i < COPPER_LINES; i++) {
        CWAIT(ucl, SKY_TOP_Y + i, 0);
        CMOVE(ucl, custom.color[0], sky_colors[i]);
    }
    CEND(ucl);
    return ucl;
}

static struct UCopList *build_rainbow(void)
{
    struct UCopList *ucl;
    WORD i, c;

    ucl = (struct UCopList *)AllocMem(sizeof(struct UCopList), MEMF_PUBLIC | MEMF_CLEAR);
    if (!ucl) return NULL;

    /* Sky gradient + 7 rainbow color changes per line = 8 CMOVEs per line */
    CINIT(ucl, COPPER_LINES * 20);
    for (i = 0; i < COPPER_LINES; i++) {
        UWORD rainbow_col;
        WORD hue = (i * 3) & 0xFF;  /* Cycle hue over screen height */

        CWAIT(ucl, SKY_TOP_Y + i, 0);
        CMOVE(ucl, custom.color[0], sky_colors[i]);

        /* Set colors 1-7 (red shades) to rainbow at this scanline */
        for (c = 0; c < 7; c++) {
            hue_to_rgb(hue, c, &rainbow_col);
            CMOVE(ucl, custom.color[1 + c], rainbow_col);
        }
    }
    CEND(ucl);
    return ucl;
}

void copper_init(struct ViewPort *vp)
{
    build_gradient();

    sky_ucl = build_sky_only();
    rainbow_ucl = build_rainbow();

    if (sky_ucl) {
        Forbid();
        vp->UCopIns = sky_ucl;
        Permit();
        RethinkDisplay();
    }
}

void copper_set_rainbow(struct ViewPort *vp, BOOL enable)
{
    struct UCopList *target = enable ? rainbow_ucl : sky_ucl;
    if (target) {
        Forbid();
        vp->UCopIns = target;
        Permit();
        RethinkDisplay();
    }
}

void copper_cleanup(void)
{
    /* UCopLists freed by CloseScreen / FreeVPortCopLists */
    sky_ucl = NULL;
    rainbow_ucl = NULL;
}
