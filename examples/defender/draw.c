/*
 * Defender - All rendering
 * Terrain, entities, scanner, HUD, title/gameover screens
 */
#include <proto/graphics.h>
#include <graphics/gfx.h>
#include <graphics/rastport.h>
#include <string.h>
#include "game.h"
#include "draw.h"

/* 5x7 bitmap font */
static const UBYTE font_5x7[96][7] = {
    {0x00,0x00,0x00,0x00,0x00,0x00,0x00}, /* space */
    {0x04,0x04,0x04,0x04,0x04,0x00,0x04}, /* ! */
    {0x0A,0x0A,0x00,0x00,0x00,0x00,0x00}, /* " */
    {0x0A,0x1F,0x0A,0x0A,0x1F,0x0A,0x00}, /* # */
    {0x04,0x0F,0x14,0x0E,0x05,0x1E,0x04}, /* $ */
    {0x18,0x19,0x02,0x04,0x08,0x13,0x03}, /* % */
    {0x08,0x14,0x14,0x08,0x15,0x12,0x0D}, /* & */
    {0x04,0x04,0x00,0x00,0x00,0x00,0x00}, /* ' */
    {0x02,0x04,0x08,0x08,0x08,0x04,0x02}, /* ( */
    {0x08,0x04,0x02,0x02,0x02,0x04,0x08}, /* ) */
    {0x00,0x04,0x15,0x0E,0x15,0x04,0x00}, /* * */
    {0x00,0x04,0x04,0x1F,0x04,0x04,0x00}, /* + */
    {0x00,0x00,0x00,0x00,0x00,0x04,0x08}, /* , */
    {0x00,0x00,0x00,0x1F,0x00,0x00,0x00}, /* - */
    {0x00,0x00,0x00,0x00,0x00,0x00,0x04}, /* . */
    {0x01,0x01,0x02,0x04,0x08,0x10,0x10}, /* / */
    {0x0E,0x11,0x13,0x15,0x19,0x11,0x0E}, /* 0 */
    {0x04,0x0C,0x04,0x04,0x04,0x04,0x0E}, /* 1 */
    {0x0E,0x11,0x01,0x06,0x08,0x10,0x1F}, /* 2 */
    {0x0E,0x11,0x01,0x06,0x01,0x11,0x0E}, /* 3 */
    {0x02,0x06,0x0A,0x12,0x1F,0x02,0x02}, /* 4 */
    {0x1F,0x10,0x1E,0x01,0x01,0x11,0x0E}, /* 5 */
    {0x06,0x08,0x10,0x1E,0x11,0x11,0x0E}, /* 6 */
    {0x1F,0x01,0x02,0x04,0x08,0x08,0x08}, /* 7 */
    {0x0E,0x11,0x11,0x0E,0x11,0x11,0x0E}, /* 8 */
    {0x0E,0x11,0x11,0x0F,0x01,0x02,0x0C}, /* 9 */
    {0x00,0x00,0x04,0x00,0x00,0x04,0x00}, /* : */
    {0x00,0x00,0x04,0x00,0x00,0x04,0x08}, /* ; */
    {0x02,0x04,0x08,0x10,0x08,0x04,0x02}, /* < */
    {0x00,0x00,0x1F,0x00,0x1F,0x00,0x00}, /* = */
    {0x08,0x04,0x02,0x01,0x02,0x04,0x08}, /* > */
    {0x0E,0x11,0x01,0x06,0x04,0x00,0x04}, /* ? */
    {0x0E,0x11,0x17,0x15,0x17,0x10,0x0E}, /* @ */
    {0x0E,0x11,0x11,0x1F,0x11,0x11,0x11}, /* A */
    {0x1E,0x11,0x11,0x1E,0x11,0x11,0x1E}, /* B */
    {0x0E,0x11,0x10,0x10,0x10,0x11,0x0E}, /* C */
    {0x1E,0x11,0x11,0x11,0x11,0x11,0x1E}, /* D */
    {0x1F,0x10,0x10,0x1E,0x10,0x10,0x1F}, /* E */
    {0x1F,0x10,0x10,0x1E,0x10,0x10,0x10}, /* F */
    {0x0E,0x11,0x10,0x17,0x11,0x11,0x0E}, /* G */
    {0x11,0x11,0x11,0x1F,0x11,0x11,0x11}, /* H */
    {0x0E,0x04,0x04,0x04,0x04,0x04,0x0E}, /* I */
    {0x07,0x02,0x02,0x02,0x02,0x12,0x0C}, /* J */
    {0x11,0x12,0x14,0x18,0x14,0x12,0x11}, /* K */
    {0x10,0x10,0x10,0x10,0x10,0x10,0x1F}, /* L */
    {0x11,0x1B,0x15,0x15,0x11,0x11,0x11}, /* M */
    {0x11,0x19,0x15,0x13,0x11,0x11,0x11}, /* N */
    {0x0E,0x11,0x11,0x11,0x11,0x11,0x0E}, /* O */
    {0x1E,0x11,0x11,0x1E,0x10,0x10,0x10}, /* P */
    {0x0E,0x11,0x11,0x11,0x15,0x12,0x0D}, /* Q */
    {0x1E,0x11,0x11,0x1E,0x14,0x12,0x11}, /* R */
    {0x0E,0x11,0x10,0x0E,0x01,0x11,0x0E}, /* S */
    {0x1F,0x04,0x04,0x04,0x04,0x04,0x04}, /* T */
    {0x11,0x11,0x11,0x11,0x11,0x11,0x0E}, /* U */
    {0x11,0x11,0x11,0x11,0x0A,0x0A,0x04}, /* V */
    {0x11,0x11,0x11,0x15,0x15,0x1B,0x11}, /* W */
    {0x11,0x11,0x0A,0x04,0x0A,0x11,0x11}, /* X */
    {0x11,0x11,0x0A,0x04,0x04,0x04,0x04}, /* Y */
    {0x1F,0x01,0x02,0x04,0x08,0x10,0x1F}, /* Z */
    {0x0E,0x08,0x08,0x08,0x08,0x08,0x0E}, /* [ */
    {0x10,0x10,0x08,0x04,0x02,0x01,0x01}, /* \ */
    {0x0E,0x02,0x02,0x02,0x02,0x02,0x0E}, /* ] */
    {0x04,0x0A,0x11,0x00,0x00,0x00,0x00}, /* ^ */
    {0x00,0x00,0x00,0x00,0x00,0x00,0x1F}, /* _ */
    {0x08,0x04,0x00,0x00,0x00,0x00,0x00}, /* ` */
    {0x00,0x00,0x0E,0x01,0x0F,0x11,0x0F}, /* a */
    {0x10,0x10,0x1E,0x11,0x11,0x11,0x1E}, /* b */
    {0x00,0x00,0x0E,0x10,0x10,0x10,0x0E}, /* c */
    {0x01,0x01,0x0F,0x11,0x11,0x11,0x0F}, /* d */
    {0x00,0x00,0x0E,0x11,0x1F,0x10,0x0E}, /* e */
    {0x06,0x08,0x1C,0x08,0x08,0x08,0x08}, /* f */
    {0x00,0x00,0x0F,0x11,0x0F,0x01,0x0E}, /* g */
    {0x10,0x10,0x1E,0x11,0x11,0x11,0x11}, /* h */
    {0x04,0x00,0x0C,0x04,0x04,0x04,0x0E}, /* i */
    {0x02,0x00,0x06,0x02,0x02,0x12,0x0C}, /* j */
    {0x10,0x10,0x12,0x14,0x18,0x14,0x12}, /* k */
    {0x0C,0x04,0x04,0x04,0x04,0x04,0x0E}, /* l */
    {0x00,0x00,0x1A,0x15,0x15,0x15,0x15}, /* m */
    {0x00,0x00,0x1E,0x11,0x11,0x11,0x11}, /* n */
    {0x00,0x00,0x0E,0x11,0x11,0x11,0x0E}, /* o */
    {0x00,0x00,0x1E,0x11,0x1E,0x10,0x10}, /* p */
    {0x00,0x00,0x0F,0x11,0x0F,0x01,0x01}, /* q */
    {0x00,0x00,0x16,0x19,0x10,0x10,0x10}, /* r */
    {0x00,0x00,0x0F,0x10,0x0E,0x01,0x1E}, /* s */
    {0x08,0x08,0x1C,0x08,0x08,0x09,0x06}, /* t */
    {0x00,0x00,0x11,0x11,0x11,0x11,0x0F}, /* u */
    {0x00,0x00,0x11,0x11,0x11,0x0A,0x04}, /* v */
    {0x00,0x00,0x11,0x11,0x15,0x15,0x0A}, /* w */
    {0x00,0x00,0x11,0x0A,0x04,0x0A,0x11}, /* x */
    {0x00,0x00,0x11,0x11,0x0F,0x01,0x0E}, /* y */
    {0x00,0x00,0x1F,0x02,0x04,0x08,0x1F}, /* z */
    {0x02,0x04,0x04,0x08,0x04,0x04,0x02}, /* { */
    {0x04,0x04,0x04,0x04,0x04,0x04,0x04}, /* | */
    {0x08,0x04,0x04,0x02,0x04,0x04,0x08}, /* } */
    {0x00,0x00,0x08,0x15,0x02,0x00,0x00}, /* ~ */
    {0x00,0x00,0x00,0x00,0x00,0x00,0x00}, /* DEL */
};

static void draw_char(struct RastPort *rp, WORD x, WORD y, char c, WORD scale)
{
    WORD idx, row, col;
    UBYTE bits;

    if (c < 32 || c > 127) return;
    idx = c - 32;

    for (row = 0; row < 7; row++) {
        bits = font_5x7[idx][row];
        for (col = 0; col < 5; col++) {
            if (bits & (0x10 >> col)) {
                if (scale == 1) {
                    RectFill(rp, x + col, y + row, x + col, y + row);
                } else {
                    RectFill(rp, x + col * scale, y + row * scale,
                             x + col * scale + scale - 1,
                             y + row * scale + scale - 1);
                }
            }
        }
    }
}

void draw_string_at(struct RastPort *rp, WORD x, WORD y, const char *s, WORD scale)
{
    WORD len = 0;
    const char *p = s;
    (void)scale; /* scale ignored - using system font */
    while (*p) { len++; p++; }
    Move(rp, x, y + 6); /* +6 for baseline */
    Text(rp, (STRPTR)s, len);
}

WORD string_pixel_width(const char *s, WORD scale)
{
    WORD len = 0;
    (void)scale;
    while (*s) { len++; s++; }
    return len * 8; /* system font is 8px wide */
}

/* ---- Stars (far parallax layer) ---- */

#define NUM_STARS 25
static WORD star_wx[NUM_STARS];
static WORD star_wy[NUM_STARS];
static WORD star_color[NUM_STARS];
static WORD stars_init = 0;

static void init_stars(void)
{
    WORD i;
    ULONG rng = 99999;
    for (i = 0; i < NUM_STARS; i++) {
        rng = rng * 1103515245 + 12345;
        star_wx[i] = (WORD)((rng >> 16) % WORLD_W);
        rng = rng * 1103515245 + 12345;
        star_wy[i] = (WORD)(PLAY_TOP + (rng >> 16) % (PLAY_BOT - PLAY_TOP - 20));
        star_color[i] = (rng & 1) ? COL_STAR_DIM : COL_STAR_BLUE;
    }
    stars_init = 1;
}

static void draw_stars(struct RastPort *rp, GameState *gs)
{
    WORD i;
    LONG vx_quarter;

    if (!stars_init) init_stars();

    vx_quarter = FROM_FP(gs->viewport_x) / 4; /* 1/4 speed parallax */

    /* Draw dim stars first, then blue stars - 2 SetAPen calls instead of 50 */
    SetAPen(rp, COL_STAR_DIM);
    for (i = 0; i < NUM_STARS; i++) {
        if (star_color[i] != COL_STAR_DIM) continue;
        {
            WORD sx = ((star_wx[i] - (WORD)vx_quarter) % SCREEN_W + SCREEN_W) % SCREEN_W;
            WORD sy = star_wy[i];
            RectFill(rp, sx, sy, sx, sy);
        }
    }
    SetAPen(rp, COL_STAR_BLUE);
    for (i = 0; i < NUM_STARS; i++) {
        if (star_color[i] != COL_STAR_BLUE) continue;
        {
            WORD sx = ((star_wx[i] - (WORD)vx_quarter) % SCREEN_W + SCREEN_W) % SCREEN_W;
            WORD sy = star_wy[i];
            RectFill(rp, sx, sy, sx, sy);
        }
    }
}

/* ---- Terrain drawing (fast: single pass, batched spans) ---- */

static void draw_terrain(struct RastPort *rp, GameState *gs)
{
    WORD sx;
    LONG vx_px = FROM_FP(gs->viewport_x);
    WORD heights[SCREEN_W];
    WORD prev_h, span_start;

    /* Pre-compute heights in one pass */
    for (sx = 0; sx < SCREEN_W; sx++) {
        WORD wx = (WORD)(((LONG)sx + vx_px) % WORLD_W);
        if (wx < 0) wx += WORLD_W;
        heights[sx] = gs->terrain_h[wx];
    }

    /* Draw terrain as batched spans - body and surface together.
     * For each span of same-height columns, draw body then surface
     * in one go to avoid flicker. */
    prev_h = heights[0];
    span_start = 0;
    for (sx = 1; sx <= SCREEN_W; sx++) {
        WORD cur_h = (sx < SCREEN_W) ? heights[sx] : -999;
        if (cur_h != prev_h) {
            /* Draw this span: surface + body */
            SetAPen(rp, COL_TERRAIN_LT);
            RectFill(rp, span_start, prev_h, sx - 1, prev_h);
            SetAPen(rp, COL_TERRAIN);
            RectFill(rp, span_start, prev_h + 1, sx - 1, prev_h + 6);
            span_start = sx;
            prev_h = cur_h;
        }
    }
}

/* ---- Scanner / Minimap ---- */

static void draw_scanner(struct RastPort *rp, GameState *gs)
{
    WORD i, sx;
    LONG vx_px = FROM_FP(gs->viewport_x);
    WORD view_left, view_right;

    /* Scanner background */
    SetAPen(rp, COL_SCANNER_BG);
    RectFill(rp, 0, 0, SCREEN_W - 1, SCANNER_H - 1);

    /* Scanner border */
    SetAPen(rp, COL_SCANNER_BRD);
    RectFill(rp, 0, SCANNER_H - 1, SCREEN_W - 1, SCANNER_H - 1);

    /* Terrain in scanner - batched: draw every 2nd pixel, batch same-height spans */
    SetAPen(rp, COL_TERRAIN);
    {
        WORD span_start = 0, prev_sy = -1;
        for (sx = 0; sx <= SCREEN_W; sx += 2) {
            WORD sy = -1;
            if (sx < SCREEN_W) {
                WORD wx = (WORD)((LONG)sx * WORLD_W / SCREEN_W);
                WORD ty = gs->terrain_h[wx];
                sy = (WORD)((LONG)(ty - PLAY_TOP) * (SCANNER_H - 4) / (PLAY_BOT - PLAY_TOP)) + 2;
                if (sy < 2 || sy >= SCANNER_H - 1) sy = -1;
            }
            if (sy != prev_sy) {
                if (prev_sy >= 0)
                    RectFill(rp, span_start, prev_sy, sx - 1, SCANNER_H - 2);
                span_start = sx;
                prev_sy = sy;
            }
        }
    }

    /* Viewport bracket */
    view_left = (WORD)(vx_px * SCREEN_W / WORLD_W);
    if (view_left < 0) view_left += SCREEN_W;
    view_right = view_left + SCREEN_W * SCREEN_W / WORLD_W;

    SetAPen(rp, COL_WHITE);
    /* Left edge */
    RectFill(rp, view_left % SCREEN_W, 0, view_left % SCREEN_W, SCANNER_H - 2);
    /* Right edge */
    RectFill(rp, view_right % SCREEN_W, 0, view_right % SCREEN_W, SCANNER_H - 2);

    /* Humans as green dots */
    SetAPen(rp, COL_HUMAN);
    for (i = 0; i < MAX_HUMANS; i++) {
        Human *h = &gs->humans[i];
        if (h->state == HUMAN_DEAD) continue;
        sx = (WORD)(FROM_FP(h->wx) * SCREEN_W / WORLD_W);
        WORD sy = (WORD)((LONG)(FROM_FP(h->wy) - PLAY_TOP) * (SCANNER_H - 4) / (PLAY_BOT - PLAY_TOP)) + 2;
        if (sx >= 0 && sx < SCREEN_W && sy >= 1 && sy < SCANNER_H - 1)
            RectFill(rp, sx, sy, sx, sy);
    }

    /* Enemies as colored dots */
    for (i = 0; i < MAX_ENEMIES; i++) {
        Enemy *e = &gs->enemies[i];
        if (!e->active) continue;
        sx = (WORD)(FROM_FP(e->wx) * SCREEN_W / WORLD_W);
        WORD sy = (WORD)((LONG)(FROM_FP(e->wy) - PLAY_TOP) * (SCANNER_H - 4) / (PLAY_BOT - PLAY_TOP)) + 2;
        WORD col;
        switch (e->type) {
        case ENT_LANDER:  col = COL_LANDER;  break;
        case ENT_MUTANT:  col = COL_MUTANT;  break;
        case ENT_BAITER:  col = COL_BAITER;  break;
        case ENT_BOMBER:  col = COL_BOMBER;  break;
        case ENT_POD:     col = COL_POD;     break;
        case ENT_SWARMER: col = COL_SWARMER; break;
        default:          col = COL_WHITE;   break;
        }
        SetAPen(rp, col);
        if (sx >= 0 && sx < SCREEN_W && sy >= 1 && sy < SCANNER_H - 1)
            RectFill(rp, sx, sy, sx, sy);
    }

    /* Ship as white dot */
    if (gs->ship.alive) {
        sx = (WORD)(FROM_FP(gs->ship.wx) * SCREEN_W / WORLD_W);
        WORD sy = (WORD)((LONG)(FROM_FP(gs->ship.wy) - PLAY_TOP) * (SCANNER_H - 4) / (PLAY_BOT - PLAY_TOP)) + 2;
        SetAPen(rp, COL_WHITE);
        if (sx >= 0 && sx < SCREEN_W && sy >= 1 && sy < SCANNER_H - 1)
            RectFill(rp, sx - 1, sy, sx + 1, sy);
    }
}

/* ---- Entity drawing helpers ---- */

static void draw_ship(struct RastPort *rp, GameState *gs)
{
    Ship *s = &gs->ship;
    WORD sx, sy;

    if (!s->alive) return;

    /* Blink during invulnerability */
    if (s->invuln_timer > 0 && (gs->frame & 3) < 2)
        return;

    sx = world_to_screen_x(s->wx, gs->viewport_x);
    sy = (WORD)FROM_FP(s->wy);

    if (sx < -20 || sx > SCREEN_W + 20) return;

    if (s->facing > 0) {
        /* Ship facing right:
         *   ##====>
         *  ########
         *   ##====>
         */
        SetAPen(rp, COL_SHIP_BODY);
        RectFill(rp, sx + 2, sy + 1, sx + 13, sy + 5);  /* main body */
        SetAPen(rp, COL_SHIP_WING);
        RectFill(rp, sx, sy + 2, sx + 1, sy + 4);        /* rear */
        SetAPen(rp, COL_WHITE);
        RectFill(rp, sx + 14, sy + 2, sx + 15, sy + 3);  /* nose */

        /* Thrust flame */
        if (s->thrust_on && (gs->frame & 1)) {
            SetAPen(rp, COL_THRUST);
            RectFill(rp, sx - 4, sy + 2, sx - 1, sy + 4);
        }
    } else {
        /* Ship facing left (mirrored) */
        SetAPen(rp, COL_SHIP_BODY);
        RectFill(rp, sx + 3, sy + 1, sx + 14, sy + 5);
        SetAPen(rp, COL_SHIP_WING);
        RectFill(rp, sx + 15, sy + 2, sx + 16, sy + 4);
        SetAPen(rp, COL_WHITE);
        RectFill(rp, sx + 1, sy + 2, sx + 2, sy + 3);

        if (s->thrust_on && (gs->frame & 1)) {
            SetAPen(rp, COL_THRUST);
            RectFill(rp, sx + 17, sy + 2, sx + 20, sy + 4);
        }
    }
}

static void draw_enemies(struct RastPort *rp, GameState *gs)
{
    WORD i;
    for (i = 0; i < MAX_ENEMIES; i++) {
        Enemy *e = &gs->enemies[i];
        WORD sx, sy;
        if (!e->active) continue;

        sx = world_to_screen_x(e->wx, gs->viewport_x);
        sy = (WORD)FROM_FP(e->wy);

        if (sx < -20 || sx > SCREEN_W + 20) continue;

        switch (e->type) {
        case ENT_LANDER:
            /* Green UFO shape */
            SetAPen(rp, COL_LANDER);
            RectFill(rp, sx + 2, sy, sx + 7, sy + 2);     /* dome */
            SetAPen(rp, COL_LANDER2);
            RectFill(rp, sx, sy + 3, sx + 9, sy + 5);     /* body */
            RectFill(rp, sx + 2, sy + 6, sx + 7, sy + 7); /* legs */
            /* Animate: flash */
            if (e->anim_frame & 8) {
                SetAPen(rp, COL_WHITE);
                RectFill(rp, sx + 4, sy + 1, sx + 5, sy + 1);
            }
            break;

        case ENT_MUTANT:
            /* Magenta aggressive shape */
            SetAPen(rp, COL_MUTANT);
            RectFill(rp, sx + 1, sy, sx + 8, sy + 7);
            SetAPen(rp, COL_WHITE);
            RectFill(rp, sx + 3, sy + 2, sx + 3, sy + 2); /* eye */
            RectFill(rp, sx + 6, sy + 2, sx + 6, sy + 2); /* eye */
            break;

        case ENT_BAITER:
            /* Blue elongated shape */
            SetAPen(rp, COL_BAITER);
            RectFill(rp, sx, sy + 1, sx + 13, sy + 4);
            SetAPen(rp, COL_WHITE);
            RectFill(rp, sx + 1, sy, sx + 12, sy);
            RectFill(rp, sx + 1, sy + 5, sx + 12, sy + 5);
            break;

        case ENT_BOMBER:
            /* Red bomber */
            SetAPen(rp, COL_BOMBER);
            RectFill(rp, sx + 1, sy, sx + 10, sy + 6);
            SetAPen(rp, COL_EXPL_ORG);
            RectFill(rp, sx + 3, sy + 2, sx + 8, sy + 4);
            break;

        case ENT_POD:
            /* Purple round-ish */
            SetAPen(rp, COL_POD);
            RectFill(rp, sx + 2, sy, sx + 5, sy);
            RectFill(rp, sx + 1, sy + 1, sx + 6, sy + 5);
            RectFill(rp, sx + 2, sy + 6, sx + 5, sy + 6);
            break;

        case ENT_SWARMER:
            /* Small orange dot */
            SetAPen(rp, COL_SWARMER);
            RectFill(rp, sx, sy, sx + 3, sy + 3);
            break;
        }
    }
}

static void draw_humans(struct RastPort *rp, GameState *gs)
{
    WORD i;
    for (i = 0; i < MAX_HUMANS; i++) {
        Human *h = &gs->humans[i];
        WORD sx, sy;
        if (h->state == HUMAN_DEAD) continue;

        sx = world_to_screen_x(h->wx, gs->viewport_x);
        sy = (WORD)FROM_FP(h->wy);

        if (sx < -10 || sx > SCREEN_W + 10) continue;

        /* Simple stick figure */
        SetAPen(rp, COL_HUMAN);
        RectFill(rp, sx + 1, sy, sx + 2, sy);         /* head */
        RectFill(rp, sx + 1, sy + 1, sx + 2, sy + 3); /* body */
        SetAPen(rp, COL_HUMAN2);
        RectFill(rp, sx, sy + 2, sx, sy + 2);          /* arm L */
        RectFill(rp, sx + 3, sy + 2, sx + 3, sy + 2);  /* arm R */
        RectFill(rp, sx, sy + 4, sx, sy + 5);          /* leg L */
        RectFill(rp, sx + 3, sy + 4, sx + 3, sy + 5);  /* leg R */
    }
}

static void draw_bullets(struct RastPort *rp, GameState *gs)
{
    WORD i;

    /* Player lasers */
    SetAPen(rp, COL_LASER);
    for (i = 0; i < MAX_PLAYER_BULLETS; i++) {
        Bullet *b = &gs->player_bullets[i];
        WORD sx, sy;
        if (!b->active) continue;
        sx = world_to_screen_x(b->wx, gs->viewport_x);
        sy = (WORD)FROM_FP(b->wy);
        if (sx >= -10 && sx < SCREEN_W + 10) {
            RectFill(rp, sx, sy, sx + 7, sy + 1);
        }
    }

    /* Enemy bullets */
    SetAPen(rp, COL_EXPL_ORG);
    for (i = 0; i < MAX_ENEMY_BULLETS; i++) {
        Bullet *b = &gs->enemy_bullets[i];
        WORD sx, sy;
        if (!b->active) continue;
        sx = world_to_screen_x(b->wx, gs->viewport_x);
        sy = (WORD)FROM_FP(b->wy);
        if (sx >= -10 && sx < SCREEN_W + 10) {
            RectFill(rp, sx, sy, sx + 3, sy + 3);
        }
    }

    /* Mines */
    SetAPen(rp, COL_MINE);
    for (i = 0; i < MAX_MINES; i++) {
        Mine *m = &gs->mines[i];
        WORD sx, sy;
        if (!m->active) continue;
        sx = world_to_screen_x(m->wx, gs->viewport_x);
        sy = (WORD)FROM_FP(m->wy);
        if (sx >= -10 && sx < SCREEN_W + 10) {
            RectFill(rp, sx, sy, sx + 4, sy + 4);
            SetAPen(rp, COL_EXPL_YEL);
            RectFill(rp, sx + 1, sy + 1, sx + 3, sy + 3);
            SetAPen(rp, COL_MINE);
        }
    }
}

static void draw_particles(struct RastPort *rp, GameState *gs)
{
    WORD i;
    for (i = 0; i < MAX_PARTICLES; i++) {
        Particle *p = &gs->particles[i];
        if (p->life > 0 && p->x >= 0 && p->x < SCREEN_W &&
            p->y >= PLAY_TOP && p->y < PLAY_BOT) {
            SetAPen(rp, p->color);
            RectFill(rp, p->x, p->y, p->x + 1, p->y + 1);
        }
    }
}

static void draw_explosions(struct RastPort *rp, GameState *gs)
{
    WORD i;
    for (i = 0; i < MAX_EXPLOSIONS; i++) {
        Explosion *e = &gs->explosions[i];
        WORD sx, sy, r;
        if (!e->active) continue;

        sx = world_to_screen_x(e->wx, gs->viewport_x);
        sy = (WORD)FROM_FP(e->wy);
        r = e->radius;

        if (sx < -r || sx > SCREEN_W + r) continue;

        /* Draw expanding ring */
        SetAPen(rp, e->life > 10 ? COL_EXPL_YEL : (e->life > 5 ? COL_EXPL_ORG : COL_EXPL_RED));
        {
            WORD x1 = sx - r, y1 = sy - r;
            WORD x2 = sx + r, y2 = sy + r;
            if (x1 < 0) x1 = 0;
            if (y1 < PLAY_TOP) y1 = PLAY_TOP;
            if (x2 >= SCREEN_W) x2 = SCREEN_W - 1;
            if (y2 >= PLAY_BOT) y2 = PLAY_BOT - 1;
            if (x1 <= x2 && y1 <= y2) {
                /* Outer ring */
                RectFill(rp, x1, sy, x2, sy);
                RectFill(rp, sx, y1, sx, y2);
                /* Fill center for early frames */
                if (e->life > 8) {
                    WORD ir = r / 2;
                    WORD ix1 = sx - ir, iy1 = sy - ir;
                    WORD ix2 = sx + ir, iy2 = sy + ir;
                    if (ix1 < 0) ix1 = 0;
                    if (iy1 < PLAY_TOP) iy1 = PLAY_TOP;
                    if (ix2 >= SCREEN_W) ix2 = SCREEN_W - 1;
                    if (iy2 >= PLAY_BOT) iy2 = PLAY_BOT - 1;
                    SetAPen(rp, COL_EXPL_YEL);
                    if (ix1 <= ix2 && iy1 <= iy2)
                        RectFill(rp, ix1, iy1, ix2, iy2);
                }
            }
        }
    }
}

/* ---- HUD (uses system font Move/Text for speed) ---- */

static void hud_text(struct RastPort *rp, WORD x, WORD y, const char *s)
{
    WORD len = 0;
    const char *p = s;
    while (*p) { len++; p++; }
    Move(rp, x, y + 6); /* +6 for baseline offset */
    Text(rp, (STRPTR)s, len);
}

static void draw_hud(struct RastPort *rp, GameState *gs)
{
    char buf[32];
    WORD i;

    /* Score */
    SetAPen(rp, COL_HUD);
    SetBPen(rp, COL_BLACK);
    sprintf(buf, "%07ld", (long)gs->score);
    hud_text(rp, 4, HUD_Y, buf);

    /* HI score */
    sprintf(buf, "HI %07ld", (long)gs->hiscore);
    hud_text(rp, 80, HUD_Y, buf);

    /* Lives (small rectangles) */
    SetAPen(rp, COL_SHIP_BODY);
    for (i = 0; i < gs->lives && i < 5; i++) {
        WORD lx = 200 + i * 10;
        RectFill(rp, lx, HUD_Y + 1, lx + 5, HUD_Y + 5);
    }

    /* Smart bombs */
    SetAPen(rp, COL_EXPL_YEL);
    for (i = 0; i < gs->ship.smart_bombs && i < 5; i++) {
        WORD bx = 260 + i * 8;
        RectFill(rp, bx, HUD_Y + 1, bx + 4, HUD_Y + 5);
    }

    /* Level + Humans on second line */
    SetAPen(rp, COL_HUD);
    sprintf(buf, "W%ld H%ld", (long)gs->level, (long)gs->humans_alive);
    hud_text(rp, 4, HUD_Y + 9, buf);
}

/* ---- Composite draw ---- */

void draw_all(struct RastPort *rp, GameState *gs)
{
    /* Clear */
    SetRast(rp, COL_BLACK);

    /* Parallax layers back-to-front */
    draw_stars(rp, gs);

    /* Terrain (skip if planet destroyed) */
    if (!gs->planet_destroyed)
        draw_terrain(rp, gs);

    /* Entities */
    draw_humans(rp, gs);
    draw_enemies(rp, gs);
    draw_bullets(rp, gs);
    draw_ship(rp, gs);
    draw_particles(rp, gs);
    draw_explosions(rp, gs);

    /* HUD overlay */
    draw_scanner(rp, gs);
    draw_hud(rp, gs);
}

/* ---- Screens ---- */

void draw_title(struct RastPort *rp, GameState *gs)
{
    WORD blink = (gs->frame / 15) & 1;

    SetRast(rp, COL_BLACK);
    draw_stars(rp, gs);

    SetBPen(rp, COL_BLACK);

    /* Title */
    SetAPen(rp, COL_LASER);
    draw_string_at(rp, 96, 40, "DEFENDER", 1);

    SetAPen(rp, COL_HUD);
    draw_string_at(rp, 80, 60, "AMIGA EDITION", 1);

    /* Controls */
    SetAPen(rp, COL_TERRAIN_LT);
    draw_string_at(rp, 30, 95, "WASD/JOYSTICK    MOVE", 1);
    draw_string_at(rp, 30, 110, "SPACE/FIRE       SHOOT", 1);
    draw_string_at(rp, 30, 125, "Z                SMART BOMB", 1);
    draw_string_at(rp, 30, 140, "X                HYPERSPACE", 1);

    SetAPen(rp, COL_STAR_DIM);
    draw_string_at(rp, 20, 160, "SHOOT LANDERS TO SAVE HUMANS!", 1);

    /* Prompt */
    if (blink) {
        SetAPen(rp, COL_EXPL_YEL);
        draw_string_at(rp, 80, 185, "PRESS FIRE TO START", 1);
    }

    /* Hi score */
    SetAPen(rp, COL_HUD);
    {
        char buf[32];
        sprintf(buf, "HI SCORE  %07ld", (long)gs->hiscore);
        draw_string_at(rp, 80, 220, buf, 1);
    }
}

void draw_gameover(struct RastPort *rp, GameState *gs)
{
    char buf[32];

    SetBPen(rp, COL_BLACK);
    SetAPen(rp, COL_EXPL_RED);
    draw_string_at(rp, 96, 90, "GAME OVER", 1);

    SetAPen(rp, COL_WHITE);
    sprintf(buf, "SCORE  %07ld", (long)gs->score);
    draw_string_at(rp, 96, 120, buf, 1);

    SetAPen(rp, COL_HUD);
    sprintf(buf, "WAVE  %ld", (long)gs->level);
    draw_string_at(rp, 120, 150, buf, 1);

    if ((gs->frame / 15) & 1) {
        SetAPen(rp, COL_EXPL_YEL);
        draw_string_at(rp, 96, 180, "PRESS FIRE", 1);
    }
}

void draw_hiscore_table(struct RastPort *rp, ScoreTable *st)
{
    WORD i;
    char buf[32];

    SetBPen(rp, COL_BLACK);
    SetAPen(rp, COL_LASER);
    draw_string_at(rp, 88, 20, "HIGH SCORES", 1);

    for (i = 0; i < st->count && i < MAX_HISCORES; i++) {
        WORD y = 50 + i * 14;
        SetAPen(rp, (i == 0) ? COL_EXPL_YEL : COL_HUD);
        sprintf(buf, "%ld. %-7s %07ld", (long)(i + 1), st->entries[i].name, (long)st->entries[i].score);
        draw_string_at(rp, 60, y, buf, 1);
    }
}

void draw_hiscore_entry(struct RastPort *rp, GameState *gs, char *name, WORD cursor)
{
    char buf[32];

    SetBPen(rp, COL_BLACK);
    SetAPen(rp, COL_EXPL_YEL);
    draw_string_at(rp, 72, 80, "NEW HIGH SCORE!", 1);

    SetAPen(rp, COL_WHITE);
    sprintf(buf, "%07ld", (long)gs->score);
    draw_string_at(rp, 112, 110, buf, 1);

    SetAPen(rp, COL_HUD);
    draw_string_at(rp, 80, 140, "ENTER YOUR NAME", 1);

    SetAPen(rp, COL_WHITE);
    draw_string_at(rp, 128, 165, name, 1);

    /* Cursor blink */
    if ((gs->frame / 8) & 1) {
        WORD cx_pos = 128 + cursor * 8;
        SetAPen(rp, COL_LASER);
        RectFill(rp, cx_pos, 174, cx_pos + 7, 175);
    }

    SetAPen(rp, COL_TERRAIN_LT);
    draw_string_at(rp, 30, 200, "UP/DN LETTER  L/R MOVE  FIRE OK", 1);
}

void draw_level_message(struct RastPort *rp, WORD level, const char *msg)
{
    char buf[32];

    SetBPen(rp, COL_BLACK);
    SetAPen(rp, COL_EXPL_YEL);
    sprintf(buf, "WAVE %ld", (long)level);
    draw_string_at(rp, 120, 100, buf, 1);

    SetAPen(rp, COL_WHITE);
    {
        WORD w = string_pixel_width(msg, 1);
        draw_string_at(rp, 160 - w / 2, 125, msg, 1);
    }
}
