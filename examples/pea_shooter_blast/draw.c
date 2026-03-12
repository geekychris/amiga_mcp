/*
 * Pea Shooter Blast - Drawing functions
 * Tile engine with smooth scrolling, sprite rendering
 */
#include <proto/graphics.h>
#include <graphics/gfx.h>
#include <graphics/rastport.h>
#include <string.h>
#include "game.h"
#include "draw.h"

/* External level data */
extern UBYTE level_maps[3][MAP_H][MAP_W];
extern WORD g_current_level;

/* Color indices (matching palette in main.c) */
#define COL_BG          0
#define COL_WHITE       1
#define COL_TANK_BODY   2   /* green */
#define COL_TANK_TURRET 3   /* light green */
#define COL_TANK_TREAD  13  /* dark grey */
#define COL_BULLET      10  /* yellow */
#define COL_ENEMY       8   /* red */
#define COL_ENEMY2      9   /* orange */
#define COL_BOSS        11  /* magenta */
#define COL_POWERUP_W   10  /* yellow */
#define COL_POWERUP_H   4   /* bright green */
#define COL_GROUND      6   /* brown */
#define COL_BRICK       9   /* orange-brown */
#define COL_ROCK        5   /* grey */
#define COL_METAL       7   /* light grey */
#define COL_PIPE        3   /* cyan */
#define COL_SPIKE       8   /* red */
#define COL_LADDER      14  /* yellow-orange */
#define COL_GATE        15  /* bright */
#define COL_DIRT        12  /* dark brown */
#define COL_PLATFORM    6   /* brown */
#define COL_HUD_TEXT    1   /* white */
#define COL_HUD_BAR     4   /* green */
#define COL_HUD_LOW     8   /* red */

/* 5x7 bitmap font (from asteroids, reused) */
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
                    WritePixel(rp, x + col, y + row);
                } else {
                    RectFill(rp, x + col * scale, y + row * scale,
                             x + col * scale + scale - 1,
                             y + row * scale + scale - 1);
                }
            }
        }
    }
}

static void draw_string(struct RastPort *rp, WORD x, WORD y,
                         const char *s, WORD scale)
{
    while (*s) {
        draw_char(rp, x, y, *s, scale);
        x += (5 + 1) * scale;
        s++;
    }
}

static WORD string_width(const char *s, WORD scale)
{
    WORD len = 0;
    while (*s) { len++; s++; }
    return len * 6 * scale - scale;
}

/* --- Tile drawing --- */

static void draw_tile(struct RastPort *rp, WORD sx, WORD sy, UBYTE type)
{
    WORD i;

    switch (type) {
        case TILE_EMPTY:
            /* Already cleared */
            break;

        case TILE_GROUND:
            SetAPen(rp, COL_GROUND);
            RectFill(rp, sx, sy, sx + TILE_W - 1, sy + TILE_H - 1);
            /* Surface detail */
            SetAPen(rp, COL_GROUND + 1);
            Move(rp, sx, sy);
            Draw(rp, sx + TILE_W - 1, sy);
            /* Speckle */
            for (i = 0; i < 4; i++) {
                WORD px = sx + 2 + (i * 4);
                WORD py = sy + 4 + (i & 1) * 6;
                WritePixel(rp, px, py);
            }
            break;

        case TILE_BRICK:
            SetAPen(rp, COL_BRICK);
            RectFill(rp, sx, sy, sx + TILE_W - 1, sy + TILE_H - 1);
            /* Brick lines */
            SetAPen(rp, COL_BG);
            Move(rp, sx, sy + 7);
            Draw(rp, sx + TILE_W - 1, sy + 7);
            Move(rp, sx + 7, sy);
            Draw(rp, sx + 7, sy + 6);
            Move(rp, sx + 3, sy + 8);
            Draw(rp, sx + 3, sy + TILE_H - 1);
            Move(rp, sx + 11, sy + 8);
            Draw(rp, sx + 11, sy + TILE_H - 1);
            break;

        case TILE_ROCK:
            SetAPen(rp, COL_ROCK);
            RectFill(rp, sx, sy, sx + TILE_W - 1, sy + TILE_H - 1);
            /* Rocky texture */
            SetAPen(rp, COL_ROCK - 1);
            WritePixel(rp, sx + 3, sy + 2);
            WritePixel(rp, sx + 8, sy + 5);
            WritePixel(rp, sx + 12, sy + 9);
            WritePixel(rp, sx + 5, sy + 11);
            SetAPen(rp, COL_ROCK + 1);
            WritePixel(rp, sx + 6, sy + 3);
            WritePixel(rp, sx + 10, sy + 7);
            WritePixel(rp, sx + 2, sy + 13);
            break;

        case TILE_PLATFORM:
            SetAPen(rp, COL_PLATFORM);
            RectFill(rp, sx, sy, sx + TILE_W - 1, sy + 3);
            SetAPen(rp, COL_PLATFORM - 1);
            Move(rp, sx, sy + 4);
            Draw(rp, sx + TILE_W - 1, sy + 4);
            break;

        case TILE_LADDER:
            SetAPen(rp, COL_LADDER);
            /* Side rails */
            Move(rp, sx + 3, sy);
            Draw(rp, sx + 3, sy + TILE_H - 1);
            Move(rp, sx + 12, sy);
            Draw(rp, sx + 12, sy + TILE_H - 1);
            /* Rungs */
            Move(rp, sx + 3, sy + 4);
            Draw(rp, sx + 12, sy + 4);
            Move(rp, sx + 3, sy + 10);
            Draw(rp, sx + 12, sy + 10);
            break;

        case TILE_SPIKES:
            SetAPen(rp, COL_SPIKE);
            for (i = 0; i < 4; i++) {
                WORD bx = sx + i * 4;
                Move(rp, bx + 2, sy);
                Draw(rp, bx, sy + TILE_H - 1);
                Draw(rp, bx + 3, sy + TILE_H - 1);
                Draw(rp, bx + 2, sy);
            }
            break;

        case TILE_PIPE_H:
            SetAPen(rp, COL_PIPE);
            RectFill(rp, sx, sy + 4, sx + TILE_W - 1, sy + 11);
            SetAPen(rp, COL_PIPE + 1);
            Move(rp, sx, sy + 4);
            Draw(rp, sx + TILE_W - 1, sy + 4);
            SetAPen(rp, COL_PIPE - 1);
            Move(rp, sx, sy + 11);
            Draw(rp, sx + TILE_W - 1, sy + 11);
            break;

        case TILE_PIPE_V:
            SetAPen(rp, COL_PIPE);
            RectFill(rp, sx + 4, sy, sx + 11, sy + TILE_H - 1);
            SetAPen(rp, COL_PIPE + 1);
            Move(rp, sx + 4, sy);
            Draw(rp, sx + 4, sy + TILE_H - 1);
            break;

        case TILE_DIRT:
            SetAPen(rp, COL_DIRT);
            RectFill(rp, sx, sy, sx + TILE_W - 1, sy + TILE_H - 1);
            break;

        case TILE_METAL:
            SetAPen(rp, COL_METAL);
            RectFill(rp, sx, sy, sx + TILE_W - 1, sy + TILE_H - 1);
            /* Panel lines */
            SetAPen(rp, COL_METAL - 1);
            Move(rp, sx, sy);
            Draw(rp, sx + TILE_W - 1, sy);
            Move(rp, sx, sy);
            Draw(rp, sx, sy + TILE_H - 1);
            /* Rivet dots */
            SetAPen(rp, COL_METAL + 1);
            WritePixel(rp, sx + 2, sy + 2);
            WritePixel(rp, sx + 13, sy + 2);
            WritePixel(rp, sx + 2, sy + 13);
            WritePixel(rp, sx + 13, sy + 13);
            break;

        case TILE_GATE:
            SetAPen(rp, COL_GATE);
            RectFill(rp, sx + 2, sy, sx + 13, sy + TILE_H - 1);
            SetAPen(rp, COL_WHITE);
            Move(rp, sx + 2, sy);
            Draw(rp, sx + 2, sy + TILE_H - 1);
            Move(rp, sx + 13, sy);
            Draw(rp, sx + 13, sy + TILE_H - 1);
            /* Arrow pointing right */
            SetAPen(rp, COL_BG);
            Move(rp, sx + 5, sy + 4);
            Draw(rp, sx + 10, sy + 7);
            Draw(rp, sx + 5, sy + 11);
            break;

        case TILE_POWERUP:
            /* Powerups are spawned as entities, tile cleared */
            break;
    }
}

/* Draw a single tile column at bitmap position */
void draw_tile_column(struct RastPort *rp, WORD map_col, WORD bitmap_col)
{
    WORD row;
    WORD sx = bitmap_col * TILE_W;

    for (row = 0; row < MAP_H; row++) {
        WORD sy = row * TILE_H;
        UBYTE type;

        /* Clear tile area first */
        SetAPen(rp, COL_BG);
        RectFill(rp, sx, sy, sx + TILE_W - 1, sy + TILE_H - 1);

        if (map_col >= 0 && map_col < MAP_W) {
            type = level_maps[g_current_level][row][map_col];
            draw_tile(rp, sx, sy, type);
        }
    }
}

/* Draw all visible tiles */
void draw_all_tiles(struct RastPort *rp, ScrollState *sc)
{
    WORD col;
    for (col = 0; col < TILES_X; col++) {
        WORD map_col = sc->tile_col + col;
        draw_tile_column(rp, map_col, col);
    }
}

/* Clear visible area */
void draw_clear(struct RastPort *rp, ScrollState *sc)
{
    SetAPen(rp, COL_BG);
    RectFill(rp, 0, 0, BITMAP_W - 1, SCREEN_H - 1);
}

/* --- Entity drawing --- */

/* World-to-screen conversion */
#define W2S_X(wx, sc) (FIX_INT(wx) - (sc)->pixel_x)
#define W2S_Y(wy)     (FIX_INT(wy))

void draw_tank(struct RastPort *rp, Tank *tank, ScrollState *sc)
{
    WORD sx, sy;

    if (!tank->alive) return;

    /* Blink during invulnerability */
    if (tank->invuln_timer > 0 && (tank->invuln_timer & 4))
        return;

    sx = W2S_X(tank->x, sc);
    sy = W2S_Y(tank->y);

    if (sx < -TANK_W || sx >= SCREEN_W) return;

    /* Tank body */
    SetAPen(rp, COL_TANK_BODY);
    RectFill(rp, sx + 1, sy + 2, sx + 14, sy + 9);

    /* Turret */
    SetAPen(rp, COL_TANK_TURRET);
    RectFill(rp, sx + 4, sy, sx + 11, sy + 4);

    /* Gun barrel */
    if (tank->facing == 0) {
        /* Facing right */
        RectFill(rp, sx + 11, sy + 1, sx + 15, sy + 3);
    } else {
        /* Facing left */
        RectFill(rp, sx, sy + 1, sx + 4, sy + 3);
    }

    /* Treads */
    SetAPen(rp, COL_TANK_TREAD);
    RectFill(rp, sx, sy + 10, sx + 15, sy + 13);
    /* Tread detail */
    SetAPen(rp, COL_BG);
    {
        WORD t_off = tank->anim_frame * 2;
        WORD tx;
        for (tx = sx + t_off; tx < sx + 16; tx += 4) {
            if (tx >= sx && tx < sx + 16) {
                Move(rp, tx, sy + 10);
                Draw(rp, tx, sy + 13);
            }
        }
    }

    /* Weapon level indicator (small dots on turret) */
    if (tank->weapon_level > 0) {
        SetAPen(rp, COL_BULLET);
        {
            WORD dots = tank->weapon_level;
            WORD dx;
            if (dots > 4) dots = 4;
            for (dx = 0; dx < dots; dx++) {
                WritePixel(rp, sx + 5 + dx * 2, sy + 1);
            }
        }
    }
}

void draw_bullets(struct RastPort *rp, Bullet *bullets, ScrollState *sc)
{
    WORD i;
    for (i = 0; i < MAX_BULLETS; i++) {
        Bullet *b = &bullets[i];
        WORD sx, sy;
        if (!b->active) continue;

        sx = W2S_X(b->x, sc);
        sy = W2S_Y(b->y);
        if (sx < 0 || sx >= SCREEN_W) continue;

        SetAPen(rp, COL_BULLET);
        switch (b->size) {
            case 0: /* small dot */
                WritePixel(rp, sx, sy);
                WritePixel(rp, sx + 1, sy);
                break;
            case 1: /* medium */
                RectFill(rp, sx, sy - 1, sx + 3, sy + 1);
                break;
            case 2: /* large */
                RectFill(rp, sx - 1, sy - 1, sx + 4, sy + 2);
                SetAPen(rp, COL_WHITE);
                RectFill(rp, sx, sy, sx + 2, sy);
                break;
        }
    }
}

void draw_enemies(struct RastPort *rp, Enemy *enemies, ScrollState *sc)
{
    WORD i;
    for (i = 0; i < MAX_ENEMIES; i++) {
        Enemy *e = &enemies[i];
        WORD sx, sy;
        if (!e->active) continue;

        sx = W2S_X(e->x, sc);
        sy = W2S_Y(e->y);
        if (sx < -16 || sx >= SCREEN_W + 16) continue;

        switch (e->type) {
            case ENEMY_WALKER:
                /* Simple robot shape */
                SetAPen(rp, COL_ENEMY);
                RectFill(rp, sx + 2, sy + 2, sx + 13, sy + 11);
                SetAPen(rp, COL_ENEMY2);
                RectFill(rp, sx + 4, sy, sx + 11, sy + 3);
                /* Eyes */
                SetAPen(rp, COL_WHITE);
                WritePixel(rp, sx + 5, sy + 4);
                WritePixel(rp, sx + 10, sy + 4);
                /* Legs */
                SetAPen(rp, COL_ENEMY);
                RectFill(rp, sx + 3, sy + 12, sx + 5, sy + 15);
                RectFill(rp, sx + 10, sy + 12, sx + 12, sy + 15);
                break;

            case ENEMY_FLYER:
                /* Winged creature */
                SetAPen(rp, COL_ENEMY2);
                RectFill(rp, sx + 4, sy + 4, sx + 11, sy + 11);
                /* Wings */
                SetAPen(rp, COL_ENEMY);
                if (e->anim_frame & 1) {
                    /* Wings up */
                    RectFill(rp, sx, sy, sx + 3, sy + 6);
                    RectFill(rp, sx + 12, sy, sx + 15, sy + 6);
                } else {
                    /* Wings down */
                    RectFill(rp, sx, sy + 6, sx + 3, sy + 12);
                    RectFill(rp, sx + 12, sy + 6, sx + 15, sy + 12);
                }
                /* Eye */
                SetAPen(rp, COL_WHITE);
                WritePixel(rp, sx + 6, sy + 6);
                WritePixel(rp, sx + 9, sy + 6);
                break;

            case ENEMY_TURRET:
                /* Turret on base */
                SetAPen(rp, COL_METAL);
                RectFill(rp, sx + 2, sy + 8, sx + 13, sy + 15);
                SetAPen(rp, COL_ENEMY);
                RectFill(rp, sx + 4, sy + 4, sx + 11, sy + 8);
                /* Barrel */
                if (e->facing == 0) {
                    RectFill(rp, sx + 11, sy + 5, sx + 15, sy + 7);
                } else {
                    RectFill(rp, sx, sy + 5, sx + 4, sy + 7);
                }
                break;

            case ENEMY_HOPPER:
                /* Frog-like hopper */
                SetAPen(rp, COL_ENEMY);
                RectFill(rp, sx + 3, sy + 4, sx + 12, sy + 12);
                SetAPen(rp, COL_ENEMY2);
                RectFill(rp, sx + 4, sy + 2, sx + 11, sy + 5);
                /* Eyes */
                SetAPen(rp, COL_WHITE);
                WritePixel(rp, sx + 5, sy + 3);
                WritePixel(rp, sx + 10, sy + 3);
                /* Legs */
                if (e->state == 1) {
                    /* Jumping - legs extended */
                    SetAPen(rp, COL_ENEMY);
                    RectFill(rp, sx + 1, sy + 13, sx + 4, sy + 15);
                    RectFill(rp, sx + 11, sy + 13, sx + 14, sy + 15);
                } else {
                    SetAPen(rp, COL_ENEMY);
                    RectFill(rp, sx + 2, sy + 12, sx + 5, sy + 15);
                    RectFill(rp, sx + 10, sy + 12, sx + 13, sy + 15);
                }
                break;

            case ENEMY_BOSS:
                /* Big boss - 32x24-ish presence */
                SetAPen(rp, COL_BOSS);
                RectFill(rp, sx - 4, sy - 4, sx + 19, sy + 19);
                SetAPen(rp, COL_ENEMY);
                RectFill(rp, sx - 2, sy - 2, sx + 17, sy + 17);
                /* Face */
                SetAPen(rp, COL_WHITE);
                RectFill(rp, sx + 2, sy + 2, sx + 5, sy + 5);
                RectFill(rp, sx + 10, sy + 2, sx + 13, sy + 5);
                /* Mouth */
                SetAPen(rp, COL_BOSS);
                RectFill(rp, sx + 3, sy + 9, sx + 12, sy + 12);
                /* Health bar above boss */
                {
                    WORD bar_w = 24;
                    WORD hp_w = (e->health * bar_w) / (30 + 10 * 2);
                    if (hp_w < 0) hp_w = 0;
                    SetAPen(rp, COL_HUD_LOW);
                    RectFill(rp, sx - 4, sy - 8, sx - 4 + bar_w, sy - 7);
                    SetAPen(rp, COL_HUD_BAR);
                    if (hp_w > 0)
                        RectFill(rp, sx - 4, sy - 8, sx - 4 + hp_w, sy - 7);
                }
                break;
        }
    }
}

void draw_enemy_bullets(struct RastPort *rp, EnemyBullet *ebullets, ScrollState *sc)
{
    WORD i;
    for (i = 0; i < MAX_ENEMY_BULLETS; i++) {
        EnemyBullet *b = &ebullets[i];
        WORD sx, sy;
        if (!b->active) continue;

        sx = W2S_X(b->x, sc);
        sy = W2S_Y(b->y);
        if (sx < 0 || sx >= SCREEN_W) continue;

        SetAPen(rp, COL_ENEMY);
        RectFill(rp, sx - 1, sy - 1, sx + 1, sy + 1);
    }
}

void draw_particles(struct RastPort *rp, Particle *particles, ScrollState *sc)
{
    WORD i;
    for (i = 0; i < MAX_PARTICLES; i++) {
        Particle *p = &particles[i];
        WORD sx, sy;
        if (!p->active) continue;

        sx = W2S_X(p->x, sc);
        sy = W2S_Y(p->y);
        if (sx < 0 || sx >= SCREEN_W || sy < 0 || sy >= SCREEN_H) continue;

        SetAPen(rp, p->color);
        WritePixel(rp, sx, sy);
    }
}

void draw_explosions(struct RastPort *rp, Explosion *explosions, ScrollState *sc)
{
    WORD i;
    for (i = 0; i < MAX_EXPLOSIONS; i++) {
        Explosion *e = &explosions[i];
        WORD sx, sy, r;
        if (!e->active) continue;

        sx = W2S_X(e->x, sc);
        sy = W2S_Y(e->y);
        r = e->radius * e->life / 8;

        if (sx + r < 0 || sx - r >= SCREEN_W) continue;

        /* Flash circle */
        SetAPen(rp, (e->life > 4) ? COL_WHITE : COL_BULLET);
        {
            WORD x1 = sx - r;
            WORD y1 = sy - r;
            WORD x2 = sx + r;
            WORD y2 = sy + r;
            if (x1 < 0) x1 = 0;
            if (y1 < 0) y1 = 0;
            if (x2 >= SCREEN_W) x2 = SCREEN_W - 1;
            if (y2 >= SCREEN_H) y2 = SCREEN_H - 1;
            if (x1 <= x2 && y1 <= y2)
                RectFill(rp, x1, y1, x2, y2);
        }
    }
}

void draw_powerups(struct RastPort *rp, Powerup *powerups, ScrollState *sc)
{
    WORD i;
    for (i = 0; i < MAX_POWERUPS; i++) {
        Powerup *p = &powerups[i];
        WORD sx, sy;
        if (!p->active) continue;

        sx = W2S_X(p->x, sc);
        sy = W2S_Y(p->y);
        if (sx < -16 || sx >= SCREEN_W) continue;

        /* Bobbing */
        sy += (p->anim_frame < 2) ? -1 : 1;

        if (p->type == POWERUP_WEAPON) {
            SetAPen(rp, COL_POWERUP_W);
            RectFill(rp, sx + 2, sy + 2, sx + 13, sy + 13);
            /* W letter */
            SetAPen(rp, COL_BG);
            draw_char(rp, sx + 4, sy + 4, 'W', 1);
        } else {
            SetAPen(rp, COL_POWERUP_H);
            RectFill(rp, sx + 2, sy + 2, sx + 13, sy + 13);
            /* + symbol */
            SetAPen(rp, COL_WHITE);
            Move(rp, sx + 5, sy + 7);
            Draw(rp, sx + 10, sy + 7);
            Move(rp, sx + 7, sy + 5);
            Draw(rp, sx + 7, sy + 10);
        }
    }
}

/* --- HUD --- */

void draw_hud(struct RastPort *rp, GameState *gs)
{
    char buf[32];
    WORD i;

    /* Health bar */
    SetAPen(rp, COL_HUD_TEXT);
    draw_string(rp, 4, 2, "HP", 1);

    for (i = 0; i < MAX_HEALTH; i++) {
        WORD bx = 18 + i * 6;
        if (i < gs->tank.health) {
            SetAPen(rp, (gs->tank.health <= 2) ? COL_HUD_LOW : COL_HUD_BAR);
        } else {
            SetAPen(rp, 13);  /* dark */
        }
        RectFill(rp, bx, 2, bx + 4, 7);
    }

    /* Score */
    {
        /* Manual score formatting to avoid sprintf %d issues */
        long s = (long)gs->score;
        char *p = buf;
        int started = 0;
        long div;

        for (div = 1000000L; div > 0; div /= 10) {
            int digit = (int)(s / div);
            s -= (long)digit * div;
            if (digit || started || div == 1) {
                *p++ = '0' + digit;
                started = 1;
            }
        }
        *p = '\0';
    }
    SetAPen(rp, COL_HUD_TEXT);
    draw_string(rp, SCREEN_W - string_width(buf, 1) - 4, 2, buf, 1);

    /* Lives */
    SetAPen(rp, COL_TANK_BODY);
    for (i = 0; i < gs->lives - 1 && i < 5; i++) {
        WORD lx = 4 + i * 12;
        /* Mini tank icon */
        RectFill(rp, lx, 12, lx + 8, 17);
    }

    /* Weapon level */
    SetAPen(rp, COL_HUD_TEXT);
    draw_string(rp, SCREEN_W / 2 - 18, 2, "GUN", 1);
    {
        char lvl[2];
        lvl[0] = '1' + gs->tank.weapon_level;
        lvl[1] = '\0';
        SetAPen(rp, COL_BULLET);
        draw_string(rp, SCREEN_W / 2 + 6, 2, lvl, 1);
    }

    /* Level indicator */
    SetAPen(rp, COL_HUD_TEXT);
    {
        char lv[6];
        lv[0] = 'L';
        lv[1] = 'V';
        lv[2] = '1' + gs->level;
        lv[3] = '\0';
        draw_string(rp, SCREEN_W / 2 - 10, 12, lv, 1);
    }
}

/* --- Title screen --- */

void draw_title(struct RastPort *rp, WORD frame)
{
    WORD cx = SCREEN_W / 2;
    WORD w;

    SetAPen(rp, COL_BG);
    RectFill(rp, 0, 0, SCREEN_W - 1, SCREEN_H - 1);

    /* Starfield background */
    SetAPen(rp, COL_WHITE);
    {
        ULONG seed = 42;
        WORD i;
        for (i = 0; i < 40; i++) {
            seed = seed * 1103515245UL + 12345UL;
            {
                WORD sx = (WORD)((seed >> 16) % SCREEN_W);
                WORD sy = (WORD)((seed >> 8) % SCREEN_H);
                WritePixel(rp, sx, sy);
            }
        }
    }

    /* Title: PEA SHOOTER BLAST */
    SetAPen(rp, COL_ENEMY);
    w = string_width("PEA SHOOTER", 3);
    draw_string(rp, cx - w / 2, 30, "PEA SHOOTER", 3);

    SetAPen(rp, COL_BULLET);
    w = string_width("BLAST", 4);
    draw_string(rp, cx - w / 2, 60, "BLAST", 4);

    /* Subtitle */
    SetAPen(rp, COL_PIPE);
    w = string_width("A BLASTER MASTER HOMAGE", 1);
    draw_string(rp, cx - w / 2, 100, "A BLASTER MASTER HOMAGE", 1);

    /* Tank sprite in title */
    SetAPen(rp, COL_TANK_BODY);
    RectFill(rp, cx - 20, 120, cx + 10, 134);
    SetAPen(rp, COL_TANK_TURRET);
    RectFill(rp, cx - 14, 116, cx + 2, 122);
    RectFill(rp, cx + 2, 118, cx + 18, 120);
    SetAPen(rp, COL_TANK_TREAD);
    RectFill(rp, cx - 22, 135, cx + 12, 140);

    /* Controls */
    SetAPen(rp, COL_WHITE);
    w = string_width("CONTROLS", 2);
    draw_string(rp, cx - w / 2, 155, "CONTROLS", 2);

    SetAPen(rp, COL_TANK_TURRET);
    draw_string(rp, 40, 175, "A/D OR JOYSTICK  MOVE", 1);
    draw_string(rp, 40, 185, "W OR JOY UP      JUMP", 1);
    draw_string(rp, 40, 195, "SPACE/ALT/FIRE   SHOOT", 1);
    draw_string(rp, 40, 205, "ESC              QUIT", 1);

    /* Press fire */
    if ((frame / 20) & 1) {
        SetAPen(rp, COL_BULLET);
        w = string_width("PRESS FIRE TO START", 2);
        draw_string(rp, cx - w / 2, 230, "PRESS FIRE TO START", 2);
    }
}

void draw_gameover(struct RastPort *rp, LONG score)
{
    WORD cx = SCREEN_W / 2;
    WORD w;
    char buf[32];

    SetAPen(rp, COL_ENEMY);
    w = string_width("GAME OVER", 3);
    draw_string(rp, cx - w / 2, SCREEN_H / 2 - 30, "GAME OVER", 3);

    /* Score */
    {
        long s = (long)score;
        char *p = buf;
        int started = 0;
        long div;
        for (div = 1000000L; div > 0; div /= 10) {
            int digit = (int)(s / div);
            s -= (long)digit * div;
            if (digit || started || div == 1) {
                *p++ = '0' + digit;
                started = 1;
            }
        }
        *p = '\0';
    }
    SetAPen(rp, COL_BULLET);
    w = string_width(buf, 2);
    draw_string(rp, cx - w / 2, SCREEN_H / 2 + 10, buf, 2);
}

void draw_levelclear(struct RastPort *rp, WORD level)
{
    WORD cx = SCREEN_W / 2;
    WORD w;

    SetAPen(rp, COL_HUD_BAR);
    w = string_width("LEVEL CLEAR!", 3);
    draw_string(rp, cx - w / 2, SCREEN_H / 2 - 20, "LEVEL CLEAR!", 3);

    SetAPen(rp, COL_WHITE);
    {
        char lv[12];
        lv[0] = 'L'; lv[1] = 'E'; lv[2] = 'V'; lv[3] = 'E'; lv[4] = 'L';
        lv[5] = ' ';
        lv[6] = '1' + level;
        lv[7] = '\0';
        w = string_width(lv, 2);
        draw_string(rp, cx - w / 2, SCREEN_H / 2 + 15, lv, 2);
    }
}
