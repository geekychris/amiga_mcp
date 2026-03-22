/*
 * SKY KNIGHTS - Drawing functions
 * All rendering: players, enemies, platforms, eggs, HUD, title/gameover screens
 */
#include <proto/graphics.h>
#include <graphics/gfx.h>
#include <graphics/rastport.h>
#include <string.h>
#include <stdio.h>
#include "game.h"
#include "draw.h"

/* Color indices (matching palette in main.c) */
#define COL_BG          0
#define COL_WHITE       1
#define COL_PLATFORM    2
#define COL_PLAT_SHADOW 3
#define COL_P1_OSTRICH  4
#define COL_P1_RIDER    5
#define COL_P2_OSTRICH  6
#define COL_P2_RIDER    7
#define COL_BOUNDER     8
#define COL_HUNTER      9
#define COL_SHADOW      10
#define COL_EGG         11
#define COL_SCORE       12
#define COL_GROUND      13
#define COL_EGG_HATCH   14
#define COL_LTGRAY      15

/* 5x7 bitmap font (A-Z, 0-9, space, common punctuation) */
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
    {0x00,0x00,0x0E,0x11,0x10,0x11,0x0E}, /* c */
    {0x01,0x01,0x0F,0x11,0x11,0x11,0x0F}, /* d */
    {0x00,0x00,0x0E,0x11,0x1F,0x10,0x0E}, /* e */
    {0x06,0x08,0x1E,0x08,0x08,0x08,0x08}, /* f */
    {0x00,0x00,0x0F,0x11,0x0F,0x01,0x0E}, /* g */
    {0x10,0x10,0x1E,0x11,0x11,0x11,0x11}, /* h */
    {0x04,0x00,0x0C,0x04,0x04,0x04,0x0E}, /* i */
    {0x02,0x00,0x06,0x02,0x02,0x12,0x0C}, /* j */
    {0x10,0x10,0x12,0x14,0x18,0x14,0x12}, /* k */
    {0x0C,0x04,0x04,0x04,0x04,0x04,0x0E}, /* l */
    {0x00,0x00,0x1A,0x15,0x15,0x11,0x11}, /* m */
    {0x00,0x00,0x1E,0x11,0x11,0x11,0x11}, /* n */
    {0x00,0x00,0x0E,0x11,0x11,0x11,0x0E}, /* o */
    {0x00,0x00,0x1E,0x11,0x1E,0x10,0x10}, /* p */
    {0x00,0x00,0x0F,0x11,0x0F,0x01,0x01}, /* q */
    {0x00,0x00,0x16,0x19,0x10,0x10,0x10}, /* r */
    {0x00,0x00,0x0F,0x10,0x0E,0x01,0x1E}, /* s */
    {0x08,0x08,0x1E,0x08,0x08,0x09,0x06}, /* t */
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

void draw_text(struct RastPort *rp, WORD x, WORD y, const char *str, WORD color)
{
    WORD ox = x;
    (void)ox;

    SetAPen(rp, color);

    while (*str) {
        WORD idx = *str - 32;
        if (idx >= 0 && idx < 96) {
            WORD row, col;
            const UBYTE *glyph = font_5x7[idx];
            for (row = 0; row < 7; row++) {
                UBYTE bits = glyph[row];
                if (bits == 0) continue;
                for (col = 0; col < 5; col++) {
                    if (bits & (0x10 >> col)) {
                        /* Use RectFill for single pixel */
                        RectFill(rp, x + col, y + row, x + col, y + row);
                    }
                }
            }
        }
        x += 6;
        str++;
    }
}

/* Draw text centered horizontally */
static void draw_text_centered(struct RastPort *rp, WORD y, const char *str, WORD color)
{
    WORD len = 0;
    const char *s = str;
    while (*s++) len++;
    {
        WORD x = (SCREEN_W - len * 6) / 2;
        draw_text(rp, x, y, str, color);
    }
}

/* Draw scaled text (2x) */
static void draw_text_big(struct RastPort *rp, WORD x, WORD y, const char *str, WORD color)
{
    SetAPen(rp, color);

    while (*str) {
        WORD idx = *str - 32;
        if (idx >= 0 && idx < 96) {
            WORD row, col;
            const UBYTE *glyph = font_5x7[idx];
            for (row = 0; row < 7; row++) {
                UBYTE bits = glyph[row];
                if (bits == 0) continue;
                for (col = 0; col < 5; col++) {
                    if (bits & (0x10 >> col)) {
                        RectFill(rp, x + col * 2, y + row * 2,
                                 x + col * 2 + 1, y + row * 2 + 1);
                    }
                }
            }
        }
        x += 12;
        str++;
    }
}

static void draw_text_big_centered(struct RastPort *rp, WORD y, const char *str, WORD color)
{
    WORD len = 0;
    const char *s = str;
    while (*s++) len++;
    {
        WORD x = (SCREEN_W - len * 12) / 2;
        draw_text_big(rp, x, y, str, color);
    }
}

/* Draw a platform */
static void draw_platform(struct RastPort *rp, Platform *pl)
{
    /* Main surface */
    SetAPen(rp, COL_PLATFORM);
    RectFill(rp, pl->x, pl->y, pl->x + pl->w - 1, pl->y + 2);
    /* Shadow/underside */
    SetAPen(rp, COL_PLAT_SHADOW);
    RectFill(rp, pl->x, pl->y + 3, pl->x + pl->w - 1, pl->y + PLAT_THICK - 1);
}

/* Clipped RectFill - safe for out-of-bounds coords */
static void safe_rect(struct RastPort *rp, WORD x1, WORD y1, WORD x2, WORD y2)
{
    if (x1 < 0) x1 = 0;
    if (y1 < 0) y1 = 0;
    if (x2 >= SCREEN_W) x2 = SCREEN_W - 1;
    if (y2 >= SCREEN_H) y2 = SCREEN_H - 1;
    if (x1 > x2 || y1 > y2) return;
    RectFill(rp, x1, y1, x2, y2);
}

/* Draw ostrich rider (player or enemy) - 20x20 bounding box */
static void draw_rider(struct RastPort *rp, WORD x, WORD y, WORD facing,
                       WORD flapping, WORD anim_frame,
                       WORD body_color, WORD rider_color)
{
    WORD rx = x;
    if (rx < -PLAYER_W - 4 || rx >= SCREEN_W + 4) return;
    if (y < -4 || y >= SCREEN_H) return;

    /* Rider (upper body) - 7x7 block at top */
    SetAPen(rp, rider_color);
    safe_rect(rp, rx + 6, y, rx + 13, y + 6);

    /* Rider head */
    safe_rect(rp, rx + 7, y - 2, rx + 12, y - 1);

    /* Lance (in facing direction) */
    SetAPen(rp, COL_WHITE);
    if (facing == 0) {
        safe_rect(rp, rx + 14, y + 2, rx + 19, y + 4);
    } else {
        safe_rect(rp, rx, y + 2, rx + 5, y + 4);
    }

    /* Ostrich body - wider and taller */
    SetAPen(rp, body_color);
    safe_rect(rp, rx + 3, y + 7, rx + 16, y + 13);

    /* Ostrich neck */
    safe_rect(rp, rx + 4, y + 4, rx + 6, y + 7);

    /* Ostrich head/beak */
    if (facing == 0) {
        safe_rect(rp, rx + 2, y + 2, rx + 4, y + 5);
        SetAPen(rp, COL_SCORE);
        safe_rect(rp, rx, y + 3, rx + 2, y + 4);
    } else {
        safe_rect(rp, rx + 15, y + 2, rx + 17, y + 5);
        SetAPen(rp, COL_SCORE);
        safe_rect(rp, rx + 17, y + 3, rx + 19, y + 4);
    }

    /* Legs - longer */
    SetAPen(rp, body_color);
    if (anim_frame == 0) {
        safe_rect(rp, rx + 6, y + 14, rx + 8, y + 19);
        safe_rect(rp, rx + 12, y + 14, rx + 14, y + 17);
    } else {
        safe_rect(rp, rx + 6, y + 14, rx + 8, y + 17);
        safe_rect(rp, rx + 12, y + 14, rx + 14, y + 19);
    }

    /* Wings - larger */
    SetAPen(rp, body_color);
    if (flapping) {
        safe_rect(rp, rx, y + 5, rx + 3, y + 7);
        safe_rect(rp, rx + 16, y + 5, rx + 19, y + 7);
    } else {
        safe_rect(rp, rx, y + 9, rx + 3, y + 12);
        safe_rect(rp, rx + 16, y + 9, rx + 19, y + 12);
    }
}

static void draw_egg(struct RastPort *rp, Egg *eg)
{
    WORD x = FIX_INT(eg->x);
    WORD y = FIX_INT(eg->y);
    WORD color;

    if (x < -8 || x >= SCREEN_W || y < 0 || y >= SCREEN_H) return;

    /* Wobble effect when about to hatch */
    if (eg->timer < EGG_WOBBLE_TIME) {
        color = (eg->timer & 4) ? COL_EGG_HATCH : COL_EGG;
        if (eg->timer < 60) {
            /* Shake */
            x += (eg->timer & 2) ? 1 : -1;
        }
    } else {
        color = COL_EGG;
    }

    SetAPen(rp, color);
    RectFill(rp, x + 1, y, x + 6, y + 1);
    RectFill(rp, x, y + 2, x + 7, y + 5);
    RectFill(rp, x + 1, y + 6, x + 6, y + 7);
}

/* Draw HUD */
static void draw_hud(struct RastPort *rp, GameState *gs)
{
    char buf[32];
    WORD i;

    /* Player 1 score */
    sprintf(buf, "P1 %07ld", (long)gs->score[0]);
    draw_text(rp, 4, 2, buf, COL_SCORE);

    /* Player 1 lives */
    for (i = 0; i < gs->players[0].lives && i < 5; i++) {
        SetAPen(rp, COL_P1_OSTRICH);
        RectFill(rp, 4 + i * 8, 11, 4 + i * 8 + 5, 15);
    }

    /* Wave */
    sprintf(buf, "WAVE %ld", (long)gs->wave);
    draw_text_centered(rp, 2, buf, COL_WHITE);

    /* Player 2 score (if active) */
    if (gs->num_players > 1) {
        sprintf(buf, "P2 %07ld", (long)gs->score[1]);
        draw_text(rp, SCREEN_W - 66, 2, buf, COL_SCORE);

        for (i = 0; i < gs->players[1].lives && i < 5; i++) {
            SetAPen(rp, COL_P2_OSTRICH);
            RectFill(rp, SCREEN_W - 44 + i * 8, 11,
                     SCREEN_W - 44 + i * 8 + 5, 15);
        }
    }
}

void draw_title(struct RastPort *rp, GameState *gs)
{
    WORD y;

    SetRast(rp, COL_BG);

    /* Title */
    draw_text_big_centered(rp, 40, "SKY KNIGHTS", COL_SCORE);

    /* Subtitle */
    draw_text_centered(rp, 70, "AN ARCADE CLASSIC", COL_WHITE);

    /* Draw a decorative ostrich */
    draw_rider(rp, 144, 100, 0, (gs->frame >> 3) & 1, (gs->frame >> 4) & 1,
               COL_P1_OSTRICH, COL_P1_RIDER);

    /* Menu */
    y = 150;
    draw_text_centered(rp, y, "1 PLAYER  - PRESS F1",
                       gs->title_selection == 0 ? COL_SCORE : COL_WHITE);
    draw_text_centered(rp, y + 16, "2 PLAYERS - PRESS F2",
                       gs->title_selection == 1 ? COL_SCORE : COL_WHITE);

    /* Controls info */
    draw_text_centered(rp, 200, "P1: ARROWS + SPACE", COL_LTGRAY);
    draw_text_centered(rp, 212, "P2: JOYSTICK PORT 2", COL_LTGRAY);
    draw_text_centered(rp, 230, "ESC TO QUIT", COL_LTGRAY);

    /* Draw some platforms for decoration */
    {
        WORD i;
        for (i = 0; i < gs->num_platforms; i++) {
            draw_platform(rp, &gs->platforms[i]);
        }
    }
}

void draw_wave_intro(struct RastPort *rp, GameState *gs)
{
    char buf[32];

    SetRast(rp, COL_BG);

    /* Draw platforms */
    {
        WORD i;
        for (i = 0; i < gs->num_platforms; i++) {
            draw_platform(rp, &gs->platforms[i]);
        }
    }

    /* Wave announcement */
    sprintf(buf, "WAVE %ld", (long)gs->wave);
    draw_text_big_centered(rp, 100, buf, COL_SCORE);

    draw_text_centered(rp, 130, "GET READY!", COL_WHITE);

    draw_hud(rp, gs);
}

void draw_playing(struct RastPort *rp, GameState *gs)
{
    WORD i;

    SetRast(rp, COL_BG);

    /* Draw ground */
    SetAPen(rp, COL_GROUND);
    RectFill(rp, 0, GROUND_Y, SCREEN_W - 1, SCREEN_H - 1);

    /* Ground detail */
    SetAPen(rp, COL_PLAT_SHADOW);
    {
        WORD gx;
        for (gx = 0; gx < SCREEN_W; gx += 12) {
            RectFill(rp, gx, GROUND_Y, gx + 1, GROUND_Y + 2);
        }
    }

    /* Draw platforms (skip ground, index 0) */
    for (i = 1; i < gs->num_platforms; i++) {
        draw_platform(rp, &gs->platforms[i]);
    }

    /* Draw eggs */
    for (i = 0; i < MAX_EGGS; i++) {
        if (gs->eggs[i].active) {
            draw_egg(rp, &gs->eggs[i]);
        }
    }

    /* Draw enemies */
    for (i = 0; i < MAX_ENEMIES; i++) {
        Enemy *e = &gs->enemies[i];
        if (!e->active) continue;

        {
            WORD body_col, rider_col;
            switch (e->type) {
            case ETYPE_BOUNDER:
                body_col = COL_BOUNDER;
                rider_col = COL_P2_RIDER; /* red */
                break;
            case ETYPE_HUNTER:
                body_col = COL_HUNTER;
                rider_col = COL_LTGRAY;
                break;
            case ETYPE_SHADOW:
                body_col = COL_SHADOW;
                rider_col = COL_P1_RIDER; /* blue */
                break;
            default:
                body_col = COL_BOUNDER;
                rider_col = COL_P2_RIDER;
                break;
            }

            draw_rider(rp, FIX_INT(e->x), FIX_INT(e->y),
                       e->facing, e->anim_frame, e->anim_frame,
                       body_col, rider_col);
        }
    }

    /* Draw players */
    for (i = 0; i < gs->num_players; i++) {
        Player *p = &gs->players[i];
        if (!p->alive) continue;

        {
            WORD body_col = (i == 0) ? COL_P1_OSTRICH : COL_P2_OSTRICH;
            WORD rider_col = (i == 0) ? COL_P1_RIDER : COL_P2_RIDER;

            draw_rider(rp, FIX_INT(p->x), FIX_INT(p->y),
                       p->facing, p->flapping, p->anim_frame,
                       body_col, rider_col);
        }
    }

    /* HUD */
    draw_hud(rp, gs);
}

void draw_gameover(struct RastPort *rp, GameState *gs)
{
    char buf[32];

    SetRast(rp, COL_BG);

    draw_text_big_centered(rp, 80, "GAME OVER", COL_P2_RIDER);

    sprintf(buf, "P1 SCORE: %ld", (long)gs->score[0]);
    draw_text_centered(rp, 120, buf, COL_SCORE);

    if (gs->num_players > 1) {
        sprintf(buf, "P2 SCORE: %ld", (long)gs->score[1]);
        draw_text_centered(rp, 136, buf, COL_SCORE);
    }

    sprintf(buf, "WAVE REACHED: %ld", (long)gs->wave);
    draw_text_centered(rp, 160, buf, COL_WHITE);

    draw_text_centered(rp, 200, "RETURNING TO TITLE...", COL_LTGRAY);
}
