/*
 * Moon Patrol - Drawing functions
 * Parallax backgrounds, terrain, buggy, enemies, HUD, title screen
 * Includes 5x7 bitmap font for text rendering
 */
#include <proto/graphics.h>
#include <graphics/gfx.h>
#include <graphics/rastport.h>
#include <string.h>
#include "game.h"
#include "draw.h"

/* 5x7 bitmap font (A-Z, 0-9, space, punctuation) */
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
                    /* Use RectFill even for single pixels - never WritePixel in loops */
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
    while (*s) {
        draw_char(rp, x, y, *s, scale);
        x += (5 + 1) * scale;
        s++;
    }
}

WORD string_pixel_width(const char *s, WORD scale)
{
    WORD len = 0;
    while (*s) { len++; s++; }
    if (len == 0) return 0;
    return len * 6 * scale - scale;
}

/* Format a long integer to string (avoids sprintf %d issues on AmigaOS) */
static void long_to_str(char *buf, long val)
{
    char *p = buf;
    int started = 0;
    long dv;

    if (val < 0) {
        *p++ = '-';
        val = -val;
    }

    for (dv = 1000000000L; dv > 0; dv /= 10) {
        int digit = (int)(val / dv);
        val -= (long)digit * dv;
        if (digit || started || dv == 1) {
            *p++ = (char)('0' + digit);
            started = 1;
        }
    }
    *p = '\0';
}

/* ---- Mountain drawing using simple polygons ---- */

/* Approximate sine: input 0-255 maps to 0-255-0 (half wave) */
static WORD approx_sin(WORD phase)
{
    /* Triangle approximation: 0-64=rise, 64-192=fall, 192-256=rise */
    phase = phase & 255;
    if (phase < 64) return (WORD)(phase * 4);
    if (phase < 192) return (WORD)(255 - (phase - 64) * 2);
    return (WORD)(-255 + (phase - 192) * 4);
}

static void draw_stars(struct RastPort *rp)
{
    /* Fixed star positions - very fast, just a few RectFills */
    ULONG seed = 42;
    WORD i;

    SetAPen(rp, COL_WHITE);
    for (i = 0; i < 30; i++) {
        WORD sx, sy;
        seed = seed * 1103515245UL + 12345UL;
        sx = (WORD)((seed >> 16) % SCREEN_W);
        sy = (WORD)((seed >> 8) % 80);  /* stars only in top portion */
        RectFill(rp, sx, sy, sx, sy);
    }
}

static void draw_far_mountains(struct RastPort *rp, LONG scroll_x)
{
    WORD x;
    WORD offset = (WORD)((scroll_x / 400) % 640);  /* 0.25x parallax speed */

    SetAPen(rp, COL_DKPURPLE);
    for (x = 0; x < SCREEN_W; x += 2) {
        WORD phase = (WORD)((x + offset) * 3 / 2);
        WORD peak = approx_sin(phase);
        WORD h = 120 + 20 - (peak * 40 / 255);
        if (h < 90) h = 90;
        if (h > 140) h = 140;
        RectFill(rp, x, h, x + 1, 160);
    }
}

static void draw_near_mountains(struct RastPort *rp, LONG scroll_x)
{
    WORD x;
    WORD offset = (WORD)((scroll_x / 200) % 640);  /* 0.5x parallax speed */

    SetAPen(rp, COL_PURPLE);
    for (x = 0; x < SCREEN_W; x += 2) {
        WORD phase = (WORD)((x + offset) * 5 / 2);
        WORD peak = approx_sin(phase);
        WORD h = 100 + 30 - (peak * 60 / 255);
        if (h < 80) h = 80;
        if (h > 160) h = 160;
        RectFill(rp, x, h, x + 1, 170);
    }
}

/* ---- Terrain drawing ---- */

static void draw_terrain(struct RastPort *rp, GameState *gs)
{
    WORD x;
    WORD scroll_px = (WORD)(gs->scroll_x / 100);

    /* Draw ground surface */
    for (x = 0; x < SCREEN_W; x++) {
        WORD tidx = (WORD)((scroll_px + x) % TERRAIN_LEN);
        WORD th;
        WORD ttype;

        if (tidx < 0) tidx += TERRAIN_LEN;
        th = gs->terrain_h[tidx];
        ttype = gs->terrain[tidx];

        switch (ttype) {
            case TERRAIN_FLAT:
            case TERRAIN_HILL:
                /* Ground surface */
                SetAPen(rp, COL_BROWN);
                RectFill(rp, x, th, x, th + 2);
                SetAPen(rp, COL_DKBROWN);
                RectFill(rp, x, th + 3, x, SCREEN_H - 25);
                break;

            case TERRAIN_CRATER_SM:
            case TERRAIN_CRATER_LG:
                /* Gap in ground - just darker below */
                SetAPen(rp, COL_BLACK);
                RectFill(rp, x, GROUND_Y, x, GROUND_Y + 15);
                SetAPen(rp, COL_DKBROWN);
                RectFill(rp, x, GROUND_Y + 16, x, SCREEN_H - 25);
                break;

            case TERRAIN_ROCK:
                /* Rock on ground */
                SetAPen(rp, COL_BROWN);
                RectFill(rp, x, th, x, th + 2);
                SetAPen(rp, COL_DKBROWN);
                RectFill(rp, x, th + 3, x, SCREEN_H - 25);
                /* Rock body */
                SetAPen(rp, COL_DKGRAY);
                RectFill(rp, x, th - 10, x, th - 1);
                break;

            case TERRAIN_MINE:
                /* Ground + mine on top */
                SetAPen(rp, COL_BROWN);
                RectFill(rp, x, th, x, th + 2);
                SetAPen(rp, COL_DKBROWN);
                RectFill(rp, x, th + 3, x, SCREEN_H - 25);
                /* Mine dot */
                if ((x & 3) < 2) {
                    SetAPen(rp, COL_RED);
                    RectFill(rp, x, th - 4, x, th - 1);
                }
                break;
        }
    }
}

/* ---- Entity drawing ---- */

static void draw_buggy(struct RastPort *rp, GameState *gs)
{
    Buggy *b = &gs->buggy;
    WORD bx, by;

    if (!b->alive) return;

    bx = b->x;
    by = b->y;

    /* Buggy body */
    SetAPen(rp, COL_YELLOW);
    RectFill(rp, bx + 2, by + 1, bx + 21, by + 5);

    /* Roof/top */
    SetAPen(rp, COL_YELLOW);
    RectFill(rp, bx + 6, by - 2, bx + 17, by);

    /* Windshield */
    SetAPen(rp, COL_CYAN);
    RectFill(rp, bx + 16, by - 1, bx + 18, by);

    /* Gun barrel (forward) */
    SetAPen(rp, COL_ORANGE);
    RectFill(rp, bx + 22, by + 2, bx + 24, by + 3);

    /* Gun barrel (upward) */
    RectFill(rp, bx + 11, by - 5, bx + 12, by - 2);

    /* Wheels */
    SetAPen(rp, COL_GRAY);
    if (b->wheel_frame) {
        /* Frame 1 */
        RectFill(rp, bx + 2, by + 6, bx + 6, by + 10);
        RectFill(rp, bx + 17, by + 6, bx + 21, by + 10);
        /* Spoke detail */
        SetAPen(rp, COL_DKGRAY);
        RectFill(rp, bx + 4, by + 7, bx + 4, by + 9);
        RectFill(rp, bx + 19, by + 7, bx + 19, by + 9);
    } else {
        /* Frame 0 */
        RectFill(rp, bx + 2, by + 6, bx + 6, by + 10);
        RectFill(rp, bx + 17, by + 6, bx + 21, by + 10);
        /* Spoke detail */
        SetAPen(rp, COL_DKGRAY);
        RectFill(rp, bx + 3, by + 8, bx + 5, by + 8);
        RectFill(rp, bx + 18, by + 8, bx + 20, by + 8);
    }

    /* Undercarriage */
    SetAPen(rp, COL_DKGRAY);
    RectFill(rp, bx + 7, by + 6, bx + 16, by + 7);
}

static void draw_bullets_all(struct RastPort *rp, GameState *gs)
{
    WORD i;

    SetAPen(rp, COL_ORANGE);

    /* Forward bullets */
    for (i = 0; i < MAX_FWD_BULLETS; i++) {
        Bullet *b = &gs->fwd_bullets[i];
        if (!b->active) continue;
        RectFill(rp, b->x, b->y, b->x + 3, b->y + 1);
    }

    /* Upward bullets */
    for (i = 0; i < MAX_UP_BULLETS; i++) {
        Bullet *b = &gs->up_bullets[i];
        if (!b->active) continue;
        RectFill(rp, b->x, b->y, b->x + 1, b->y + 3);
    }
}

static void draw_enemies_all(struct RastPort *rp, GameState *gs)
{
    WORD i;

    for (i = 0; i < MAX_ENEMIES; i++) {
        Enemy *e = &gs->enemies[i];
        if (!e->active) continue;

        switch (e->type) {
            case ENEMY_UFO_SM:
                /* Small UFO - pulsing between cyan and white */
                SetAPen(rp, (e->flash & 8) ? COL_WHITE : COL_CYAN);
                RectFill(rp, e->x + 2, e->y + 2, e->x + 9, e->y + 5);
                /* Dome */
                SetAPen(rp, COL_CYAN);
                RectFill(rp, e->x + 4, e->y, e->x + 7, e->y + 1);
                /* Bottom */
                SetAPen(rp, COL_DKGRAY);
                RectFill(rp, e->x, e->y + 4, e->x + 11, e->y + 6);
                break;

            case ENEMY_UFO_LG:
                /* Large UFO */
                SetAPen(rp, (e->flash & 8) ? COL_WHITE : COL_CYAN);
                RectFill(rp, e->x + 3, e->y + 3, e->x + 16, e->y + 8);
                /* Dome */
                SetAPen(rp, COL_CYAN);
                RectFill(rp, e->x + 6, e->y, e->x + 13, e->y + 2);
                /* Bottom */
                SetAPen(rp, COL_DKGRAY);
                RectFill(rp, e->x, e->y + 7, e->x + 19, e->y + 10);
                /* Lights */
                SetAPen(rp, COL_RED);
                RectFill(rp, e->x + 2, e->y + 9, e->x + 3, e->y + 10);
                RectFill(rp, e->x + 16, e->y + 9, e->x + 17, e->y + 10);
                break;

            case ENEMY_METEOR:
                /* Meteor - pink/orange */
                SetAPen(rp, COL_PINK);
                RectFill(rp, e->x + 1, e->y + 1, e->x + 6, e->y + 6);
                SetAPen(rp, COL_ORANGE);
                RectFill(rp, e->x + 2, e->y + 2, e->x + 5, e->y + 5);
                /* Trail */
                SetAPen(rp, COL_RED);
                RectFill(rp, e->x + 7, e->y - 2, e->x + 8, e->y);
                break;
        }
    }
}

static void draw_bombs_all(struct RastPort *rp, GameState *gs)
{
    WORD i;

    for (i = 0; i < MAX_BOMBS; i++) {
        Bomb *b = &gs->bombs[i];
        if (!b->active) continue;

        SetAPen(rp, COL_BLUE);
        RectFill(rp, b->x - 1, b->y - 1, b->x + 2, b->y + 2);
        SetAPen(rp, COL_WHITE);
        RectFill(rp, b->x, b->y, b->x, b->y);
    }
}

static void draw_explosions_all(struct RastPort *rp, GameState *gs)
{
    WORD i;

    for (i = 0; i < MAX_EXPLOSIONS; i++) {
        Explosion *e = &gs->explosions[i];
        WORD r, x1, y1, x2, y2;
        if (!e->active) continue;

        r = e->radius;

        /* Outer ring */
        SetAPen(rp, (e->life > 6) ? COL_WHITE : COL_ORANGE);
        x1 = e->x - r;
        y1 = e->y - r;
        x2 = e->x + r;
        y2 = e->y + r;
        if (x1 < 0) x1 = 0;
        if (y1 < 0) y1 = 0;
        if (x2 >= SCREEN_W) x2 = SCREEN_W - 1;
        if (y2 >= SCREEN_H) y2 = SCREEN_H - 1;
        if (x1 <= x2 && y1 <= y2)
            RectFill(rp, x1, y1, x2, y2);

        /* Inner core */
        if (r > 3) {
            SetAPen(rp, (e->life > 8) ? COL_BRYELLOW : COL_RED);
            x1 = e->x - r / 2;
            y1 = e->y - r / 2;
            x2 = e->x + r / 2;
            y2 = e->y + r / 2;
            if (x1 < 0) x1 = 0;
            if (y1 < 0) y1 = 0;
            if (x2 >= SCREEN_W) x2 = SCREEN_W - 1;
            if (y2 >= SCREEN_H) y2 = SCREEN_H - 1;
            if (x1 <= x2 && y1 <= y2)
                RectFill(rp, x1, y1, x2, y2);
        }
    }
}

/* ---- HUD ---- */

void draw_hud(struct RastPort *rp, GameState *gs)
{
    char buf[32];
    WORD i;

    /* Score */
    SetAPen(rp, COL_BRYELLOW);
    draw_string_at(rp, 4, 2, "SCORE", 1);
    long_to_str(buf, (long)gs->score);
    draw_string_at(rp, 40, 2, buf, 1);

    /* Lives */
    SetAPen(rp, COL_WHITE);
    draw_string_at(rp, 4, 12, "LIVES", 1);
    for (i = 0; i < gs->lives && i < 5; i++) {
        SetAPen(rp, COL_YELLOW);
        RectFill(rp, 40 + i * 10, 12, 46 + i * 10, 17);
    }

    /* Checkpoint section indicator */
    {
        char sec[6];
        sec[0] = 'A' + gs->checkpoint_cur;
        sec[1] = '-';
        sec[2] = 'A' + gs->checkpoint_cur + 1;
        if (gs->checkpoint_cur + 1 >= 26) sec[2] = 'A';
        sec[3] = '\0';

        SetAPen(rp, COL_GREEN);
        draw_string_at(rp, SCREEN_W / 2 - 12, 2, sec, 1);
    }

    /* Checkpoint progress bar */
    {
        WORD bar_w = 60;
        WORD progress = (WORD)(bar_w - (gs->checkpoint_dist * bar_w / CHECKPOINT_DIST));
        if (progress < 0) progress = 0;
        if (progress > bar_w) progress = bar_w;

        SetAPen(rp, COL_DKGRAY);
        RectFill(rp, SCREEN_W / 2 - 30, 12, SCREEN_W / 2 + 30, 14);
        if (progress > 0) {
            SetAPen(rp, COL_GREEN);
            RectFill(rp, SCREEN_W / 2 - 30, 12,
                     SCREEN_W / 2 - 30 + progress, 14);
        }
    }

    /* High score area */
    SetAPen(rp, COL_WHITE);
    draw_string_at(rp, SCREEN_W - 72, 2, "HI", 1);
    /* Just show current score as high score placeholder */
    long_to_str(buf, (long)gs->score);
    draw_string_at(rp, SCREEN_W - 56, 2, buf, 1);
}

/* ---- Full game frame ---- */

void draw_game(struct RastPort *rp, GameState *gs)
{
    /* Clear screen */
    SetRast(rp, COL_BLACK);

    /* Parallax layers (back to front) */
    draw_stars(rp);
    draw_far_mountains(rp, gs->scroll_x);
    draw_near_mountains(rp, gs->scroll_x);

    /* Terrain */
    draw_terrain(rp, gs);

    /* Game objects */
    draw_bombs_all(rp, gs);
    draw_enemies_all(rp, gs);
    draw_bullets_all(rp, gs);
    draw_buggy(rp, gs);
    draw_explosions_all(rp, gs);

    /* HUD */
    draw_hud(rp, gs);
}

/* ---- Title screen ---- */

void draw_title(struct RastPort *rp, WORD frame)
{
    WORD cx = SCREEN_W / 2;
    WORD w;

    SetRast(rp, COL_BLACK);

    /* Stars */
    draw_stars(rp);

    /* Title: MOON PATROL */
    SetAPen(rp, COL_CYAN);
    w = string_pixel_width("MOON PATROL", 3);
    draw_string_at(rp, cx - w / 2, 40, "MOON PATROL", 3);

    /* Subtitle */
    SetAPen(rp, COL_PURPLE);
    w = string_pixel_width("LUNAR DEFENSE FORCE", 1);
    draw_string_at(rp, cx - w / 2, 75, "LUNAR DEFENSE FORCE", 1);

    /* Draw a buggy sprite on title */
    SetAPen(rp, COL_YELLOW);
    RectFill(rp, cx - 12, 100, cx + 12, 106);
    RectFill(rp, cx - 6, 96, cx + 6, 99);
    /* Wheels */
    SetAPen(rp, COL_GRAY);
    RectFill(rp, cx - 10, 107, cx - 6, 111);
    RectFill(rp, cx + 6, 107, cx + 10, 111);
    /* Gun */
    SetAPen(rp, COL_ORANGE);
    RectFill(rp, cx + 12, 101, cx + 16, 103);
    RectFill(rp, cx, 92, cx + 2, 96);

    /* Ground line */
    SetAPen(rp, COL_BROWN);
    RectFill(rp, 0, 112, SCREEN_W - 1, 114);

    /* Controls */
    SetAPen(rp, COL_WHITE);
    w = string_pixel_width("CONTROLS", 2);
    draw_string_at(rp, cx - w / 2, 130, "CONTROLS", 2);

    SetAPen(rp, COL_GREEN);
    draw_string_at(rp, 40, 150, "LEFT/RIGHT   SPEED", 1);
    draw_string_at(rp, 40, 160, "UP/SPACE     JUMP", 1);
    draw_string_at(rp, 40, 170, "FIRE/A       SHOOT", 1);
    draw_string_at(rp, 40, 180, "ESC          QUIT", 1);

    SetAPen(rp, COL_PURPLE);
    draw_string_at(rp, 40, 196, "JOYSTICK PORT 2 SUPPORTED", 1);

    /* Blinking prompt */
    if ((frame / 20) & 1) {
        SetAPen(rp, COL_BRYELLOW);
        w = string_pixel_width("PRESS FIRE TO START", 2);
        draw_string_at(rp, cx - w / 2, 220, "PRESS FIRE TO START", 2);
    }
}

/* ---- Game over screen ---- */

void draw_gameover(struct RastPort *rp, LONG score)
{
    WORD cx = SCREEN_W / 2;
    WORD w;
    char buf[32];

    SetAPen(rp, COL_RED);
    w = string_pixel_width("GAME OVER", 3);
    draw_string_at(rp, cx - w / 2, SCREEN_H / 2 - 30, "GAME OVER", 3);

    SetAPen(rp, COL_BRYELLOW);
    long_to_str(buf, (long)score);
    w = string_pixel_width(buf, 2);
    draw_string_at(rp, cx - w / 2, SCREEN_H / 2 + 10, buf, 2);
}

/* ---- Checkpoint celebration ---- */

void draw_checkpoint(struct RastPort *rp, WORD checkpoint)
{
    WORD cx = SCREEN_W / 2;
    WORD w;
    char msg[20];

    msg[0] = 'P';
    msg[1] = 'O';
    msg[2] = 'I';
    msg[3] = 'N';
    msg[4] = 'T';
    msg[5] = ' ';
    msg[6] = 'A' + checkpoint;
    msg[7] = '\0';

    SetAPen(rp, COL_GREEN);
    w = string_pixel_width(msg, 2);
    draw_string_at(rp, cx - w / 2, 30, msg, 2);

    SetAPen(rp, COL_BRYELLOW);
    w = string_pixel_width("1000 BONUS", 1);
    draw_string_at(rp, cx - w / 2, 50, "1000 BONUS", 1);
}
