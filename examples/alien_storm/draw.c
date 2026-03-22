#include <exec/types.h>
#include <graphics/rastport.h>
#include <graphics/gfxmacros.h>
#include <proto/graphics.h>
#include <string.h>
#include "draw.h"
#include "game.h"

/* 5x7 bitmap font - each character is 7 bytes, bits 4-0 = columns L-R */
static const UBYTE font_data[36][7] = {
    /* 0 */ {0x0E,0x11,0x13,0x15,0x19,0x11,0x0E},
    /* 1 */ {0x04,0x0C,0x04,0x04,0x04,0x04,0x0E},
    /* 2 */ {0x0E,0x11,0x01,0x02,0x04,0x08,0x1F},
    /* 3 */ {0x0E,0x11,0x01,0x06,0x01,0x11,0x0E},
    /* 4 */ {0x02,0x06,0x0A,0x12,0x1F,0x02,0x02},
    /* 5 */ {0x1F,0x10,0x1E,0x01,0x01,0x11,0x0E},
    /* 6 */ {0x06,0x08,0x10,0x1E,0x11,0x11,0x0E},
    /* 7 */ {0x1F,0x01,0x02,0x04,0x08,0x08,0x08},
    /* 8 */ {0x0E,0x11,0x11,0x0E,0x11,0x11,0x0E},
    /* 9 */ {0x0E,0x11,0x11,0x0F,0x01,0x02,0x0C},
    /* A */ {0x0E,0x11,0x11,0x1F,0x11,0x11,0x11},
    /* B */ {0x1E,0x11,0x11,0x1E,0x11,0x11,0x1E},
    /* C */ {0x0E,0x11,0x10,0x10,0x10,0x11,0x0E},
    /* D */ {0x1C,0x12,0x11,0x11,0x11,0x12,0x1C},
    /* E */ {0x1F,0x10,0x10,0x1E,0x10,0x10,0x1F},
    /* F */ {0x1F,0x10,0x10,0x1E,0x10,0x10,0x10},
    /* G */ {0x0E,0x11,0x10,0x17,0x11,0x11,0x0F},
    /* H */ {0x11,0x11,0x11,0x1F,0x11,0x11,0x11},
    /* I */ {0x0E,0x04,0x04,0x04,0x04,0x04,0x0E},
    /* J */ {0x07,0x02,0x02,0x02,0x02,0x12,0x0C},
    /* K */ {0x11,0x12,0x14,0x18,0x14,0x12,0x11},
    /* L */ {0x10,0x10,0x10,0x10,0x10,0x10,0x1F},
    /* M */ {0x11,0x1B,0x15,0x15,0x11,0x11,0x11},
    /* N */ {0x11,0x19,0x15,0x13,0x11,0x11,0x11},
    /* O */ {0x0E,0x11,0x11,0x11,0x11,0x11,0x0E},
    /* P */ {0x1E,0x11,0x11,0x1E,0x10,0x10,0x10},
    /* Q */ {0x0E,0x11,0x11,0x11,0x15,0x12,0x0D},
    /* R */ {0x1E,0x11,0x11,0x1E,0x14,0x12,0x11},
    /* S */ {0x0E,0x11,0x10,0x0E,0x01,0x11,0x0E},
    /* T */ {0x1F,0x04,0x04,0x04,0x04,0x04,0x04},
    /* U */ {0x11,0x11,0x11,0x11,0x11,0x11,0x0E},
    /* V */ {0x11,0x11,0x11,0x11,0x0A,0x0A,0x04},
    /* W */ {0x11,0x11,0x11,0x15,0x15,0x1B,0x11},
    /* X */ {0x11,0x11,0x0A,0x04,0x0A,0x11,0x11},
    /* Y */ {0x11,0x11,0x0A,0x04,0x04,0x04,0x04},
    /* Z */ {0x1F,0x01,0x02,0x04,0x08,0x10,0x1F},
};

static void draw_char(struct RastPort *rp, WORD x, WORD y, UBYTE color,
                       char ch, WORD scale)
{
    int idx, row, col;
    if (ch >= '0' && ch <= '9') idx = ch - '0';
    else if (ch >= 'A' && ch <= 'Z') idx = ch - 'A' + 10;
    else if (ch >= 'a' && ch <= 'z') idx = ch - 'a' + 10;
    else return;

    SetAPen(rp, color);
    for (row = 0; row < 7; row++) {
        UBYTE bits = font_data[idx][row];
        for (col = 0; col < 5; col++) {
            if (bits & (0x10 >> col)) {
                RectFill(rp, x + col * scale, y + row * scale,
                         x + col * scale + scale - 1,
                         y + row * scale + scale - 1);
            }
        }
    }
}

void draw_text_scaled(struct RastPort *rp, WORD x, WORD y, UBYTE color,
                      const char *text, WORD scale)
{
    while (*text) {
        if (*text == ' ')
            x += 6 * scale;
        else {
            draw_char(rp, x, y, color, *text, scale);
            x += 6 * scale;
        }
        text++;
    }
}

/* Center text at y position */
static void draw_text_centered(struct RastPort *rp, WORD y, UBYTE color,
                                const char *text, WORD scale)
{
    int len = 0;
    const char *p = text;
    while (*p++) len++;
    WORD w = len * 6 * scale;
    draw_text_scaled(rp, (SCREEN_W - w) / 2, y, color, text, scale);
}

/* Format a number into a string buffer, right-aligned in field_width chars */
static void format_number(char *buf, LONG val, int field_width)
{
    char tmp[12];
    int i = 0, j;
    LONG v = val;

    if (v == 0) {
        tmp[i++] = '0';
    } else {
        while (v > 0) {
            tmp[i++] = '0' + (char)(v % 10);
            v /= 10;
        }
    }

    /* Pad with spaces */
    for (j = 0; j < field_width - i; j++) buf[j] = ' ';
    /* Reverse digits */
    for (j = 0; j < i; j++) buf[field_width - 1 - j] = tmp[j];
    buf[field_width] = '\0';
}

/* Alien sprite data: 8 columns x 8 rows, each byte = one row's bits */
/* Type C (top, 30pts) - tentacle creature */
static const UBYTE sprite_c[2][8] = {
    {0x18, 0x3C, 0x7E, 0xDB, 0xFF, 0x24, 0x5A, 0xA5},
    {0x18, 0x3C, 0x7E, 0xDB, 0xFF, 0x24, 0x42, 0x24}
};
/* Type B (middle, 20pts) - crab */
static const UBYTE sprite_b[2][8] = {
    {0x24, 0x18, 0x7E, 0xDB, 0xFF, 0xFF, 0xA5, 0x24},
    {0x24, 0x18, 0x7E, 0xDB, 0xFF, 0xFF, 0x24, 0x42}
};
/* Type A (bottom, 10pts) - squid */
static const UBYTE sprite_a[2][8] = {
    {0x3C, 0x7E, 0xFF, 0xDB, 0xFF, 0x5A, 0x81, 0x42},
    {0x3C, 0x7E, 0xFF, 0xDB, 0xFF, 0x5A, 0x24, 0x5A}
};

/* Color per alien type */
static const UBYTE alien_colors[] = {
    COL_RED,     /* Type C (top) */
    COL_ORANGE,  /* Type B (middle) */
    COL_CYAN     /* Type A (bottom) */
};

static void draw_alien(struct RastPort *rp, WORD x, WORD y, WORD type,
                        WORD frame)
{
    const UBYTE *spr;
    int row, col;
    UBYTE color;

    if (type == ALIEN_TYPE_C) spr = sprite_c[frame];
    else if (type == ALIEN_TYPE_B) spr = sprite_b[frame];
    else spr = sprite_a[frame];

    color = alien_colors[type];
    SetAPen(rp, color);

    for (row = 0; row < 8; row++) {
        UBYTE bits = spr[row];
        /* Find runs of set bits for efficient RectFill */
        col = 0;
        while (col < 8) {
            if (bits & (0x80 >> col)) {
                WORD start = col;
                while (col < 8 && (bits & (0x80 >> col))) col++;
                /* Each sprite pixel = 2x1 screen pixels for width */
                RectFill(rp, x + start * 2, y + row,
                         x + col * 2 - 1, y + row);
            } else {
                col++;
            }
        }
    }
}

static void draw_player(struct RastPort *rp, WORD x, WORD y)
{
    /* Cannon shape - blue body with light blue highlights */
    SetAPen(rp, COL_BLUE);
    /* Base */
    RectFill(rp, x, y + 5, x + PLAYER_W - 1, y + PLAYER_H - 1);
    /* Body */
    RectFill(rp, x + 2, y + 3, x + PLAYER_W - 3, y + 5);
    /* Barrel */
    RectFill(rp, x + 6, y, x + 8, y + 3);

    /* Highlights */
    SetAPen(rp, COL_LTBLUE);
    RectFill(rp, x + 3, y + 4, x + PLAYER_W - 4, y + 4);
    WritePixel(rp, x + 7, y);
}

static void draw_ufo(struct RastPort *rp, WORD x, WORD y)
{
    SetAPen(rp, COL_RED);
    /* Body */
    RectFill(rp, x + 3, y, x + 12, y + 2);
    RectFill(rp, x + 1, y + 2, x + 14, y + 4);
    RectFill(rp, x, y + 4, x + 15, y + 5);
    /* Windows */
    SetAPen(rp, COL_YELLOW);
    WritePixel(rp, x + 4, y + 3);
    WritePixel(rp, x + 7, y + 3);
    WritePixel(rp, x + 10, y + 3);
    /* Dome */
    SetAPen(rp, COL_LTRED);
    RectFill(rp, x + 5, y, x + 10, y + 1);
}

static void draw_shields(struct RastPort *rp, GameState *gs)
{
    int s, y, x;
    for (s = 0; s < SHIELD_COUNT; s++) {
        Shield *sh = &gs->shields[s];
        SetAPen(rp, COL_GREEN);
        for (y = 0; y < SHIELD_H; y++) {
            x = 0;
            while (x < SHIELD_W) {
                /* Skip empty pixels */
                while (x < SHIELD_W && !sh->pixels[y][x]) x++;
                if (x >= SHIELD_W) break;
                /* Find run of set pixels */
                {
                    WORD start = x;
                    while (x < SHIELD_W && sh->pixels[y][x]) x++;
                    RectFill(rp, sh->x + start, sh->y + y,
                             sh->x + x - 1, sh->y + y);
                }
            }
        }
    }
}

static void draw_explosions(struct RastPort *rp, GameState *gs)
{
    int i;
    for (i = 0; i < MAX_EXPLOSIONS; i++) {
        Explosion *ex = &gs->explosions[i];
        if (!ex->active) continue;

        WORD size = 10 - ex->timer;
        SetAPen(rp, ex->color);

        /* Expanding cross pattern */
        WORD cx = ex->x, cy = ex->y;
        if (size > 0) {
            RectFill(rp, cx - size, cy - 1, cx + size, cy + 1);
            RectFill(rp, cx - 1, cy - size, cx + 1, cy + size);
        }
        /* Dots at diagonals */
        if (size > 2) {
            WORD d = size - 1;
            WritePixel(rp, cx - d, cy - d);
            WritePixel(rp, cx + d, cy - d);
            WritePixel(rp, cx - d, cy + d);
            WritePixel(rp, cx + d, cy + d);
        }
    }
}

static void draw_hud(struct RastPort *rp, GameState *gs)
{
    char buf[20];
    int i;

    /* Score */
    draw_text_scaled(rp, 8, 4, COL_WHITE, "SCORE", 1);
    format_number(buf, gs->score, 6);
    draw_text_scaled(rp, 8, 14, COL_YELLOW, buf, 1);

    /* Hi-Score */
    draw_text_scaled(rp, 120, 4, COL_WHITE, "HI SCORE", 1);
    format_number(buf, gs->hiscore, 6);
    draw_text_scaled(rp, 132, 14, COL_YELLOW, buf, 1);

    /* Lives */
    draw_text_scaled(rp, 230, 4, COL_WHITE, "LIVES", 1);
    for (i = 0; i < gs->lives && i < 5; i++) {
        WORD lx = 230 + i * 18;
        SetAPen(rp, COL_BLUE);
        RectFill(rp, lx, 15, lx + 10, 20);
        RectFill(rp, lx + 4, 13, lx + 6, 15);
    }

    /* Wave number */
    draw_text_scaled(rp, 270, 245, COL_GRAY, "WAVE", 1);
    format_number(buf, (LONG)gs->wave, 2);
    draw_text_scaled(rp, 296, 245, COL_WHITE, buf, 1);

    /* Ground line */
    SetAPen(rp, COL_GREEN);
    RectFill(rp, 0, 242, SCREEN_W - 1, 242);
}

void draw_game(struct RastPort *rp, GameState *gs)
{
    int r, c, i;

    /* Clear screen */
    SetRast(rp, 0);

    /* Draw HUD */
    draw_hud(rp, gs);

    /* Draw shields */
    draw_shields(rp, gs);

    /* Draw aliens */
    for (r = 0; r < ALIEN_ROWS; r++) {
        WORD type;
        if (r == 0) type = ALIEN_TYPE_C;
        else if (r <= 2) type = ALIEN_TYPE_B;
        else type = ALIEN_TYPE_A;

        for (c = 0; c < ALIEN_COLS; c++) {
            if (!gs->swarm.alive[r][c]) continue;
            WORD ax = gs->swarm.grid_x + c * ALIEN_CELL_W;
            WORD ay = gs->swarm.grid_y + r * ALIEN_CELL_H;
            draw_alien(rp, ax, ay, type, gs->swarm.anim_frame);
        }
    }

    /* Draw UFO */
    if (gs->ufo.active) {
        draw_ufo(rp, gs->ufo.x, UFO_Y);
    }

    /* Draw player (unless dying and blinking) */
    if (gs->state != STATE_DYING || (gs->state_timer & 4)) {
        draw_player(rp, gs->player_x, PLAYER_Y);
    }

    /* Draw player bullet */
    if (gs->player_bullet.active) {
        SetAPen(rp, COL_WHITE);
        RectFill(rp, gs->player_bullet.x - 1, gs->player_bullet.y,
                 gs->player_bullet.x, gs->player_bullet.y + 3);
    }

    /* Draw alien bullets */
    SetAPen(rp, COL_MAGENTA);
    for (i = 0; i < MAX_ALIEN_BULLETS; i++) {
        if (!gs->alien_bullets[i].active) continue;
        WORD bx = gs->alien_bullets[i].x;
        WORD by = gs->alien_bullets[i].y;
        /* Zigzag pattern */
        if (by & 2) {
            RectFill(rp, bx - 1, by, bx, by + 3);
        } else {
            RectFill(rp, bx, by, bx + 1, by + 3);
        }
    }

    /* Draw explosions */
    draw_explosions(rp, gs);

    /* Wave clear message */
    if (gs->state == STATE_WAVE_CLEAR) {
        draw_text_centered(rp, 120, COL_YELLOW, "WAVE CLEAR", 2);
    }
}

void draw_title(struct RastPort *rp, GameState *gs)
{
    SetRast(rp, 0);

    /* Title */
    draw_text_centered(rp, 20, COL_GREEN, "ALIEN", 3);
    draw_text_centered(rp, 50, COL_CYAN, "STORM", 3);

    /* Show alien types and point values */
    draw_alien(rp, 100, 100, ALIEN_TYPE_C, (gs->title_blink >> 4) & 1);
    draw_text_scaled(rp, 130, 100, COL_WHITE, "30 PTS", 1);

    draw_alien(rp, 100, 118, ALIEN_TYPE_B, (gs->title_blink >> 4) & 1);
    draw_text_scaled(rp, 130, 118, COL_WHITE, "20 PTS", 1);

    draw_alien(rp, 100, 136, ALIEN_TYPE_A, (gs->title_blink >> 4) & 1);
    draw_text_scaled(rp, 130, 136, COL_WHITE, "10 PTS", 1);

    draw_ufo(rp, 97, 156);
    draw_text_scaled(rp, 130, 156, COL_WHITE, "MYSTERY", 1);

    /* Hi-score */
    if (gs->hiscore > 0) {
        char buf[20];
        draw_text_centered(rp, 182, COL_GRAY, "HI SCORE", 1);
        format_number(buf, gs->hiscore, 6);
        draw_text_centered(rp, 192, COL_YELLOW, buf, 1);
    }

    /* Blinking "PRESS FIRE" */
    if ((gs->title_blink >> 3) & 1) {
        draw_text_centered(rp, 215, COL_WHITE, "PRESS FIRE TO START", 1);
    }

    /* Credits */
    draw_text_centered(rp, 240, COL_GRAY, "MOUSE AND KEYBOARD", 1);
}

void draw_gameover(struct RastPort *rp, GameState *gs)
{
    char buf[20];

    SetRast(rp, 0);

    draw_text_centered(rp, 80, COL_RED, "GAME OVER", 3);

    draw_text_centered(rp, 130, COL_WHITE, "SCORE", 2);
    format_number(buf, gs->score, 6);
    draw_text_centered(rp, 155, COL_YELLOW, buf, 2);

    draw_text_centered(rp, 185, COL_GRAY, "WAVE", 1);
    format_number(buf, (LONG)gs->wave, 2);
    draw_text_centered(rp, 195, COL_WHITE, buf, 1);

    if ((gs->state_timer & 8)) {
        draw_text_centered(rp, 225, COL_WHITE, "PRESS FIRE", 1);
    }
}
