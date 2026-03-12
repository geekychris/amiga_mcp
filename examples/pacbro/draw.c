/*
 * PACBRO - Rendering (dirty-rect approach)
 *
 * Static maze drawn once per buffer at level start.
 * Per-frame: only erase/redraw sprite areas (~20 RectFill/frame).
 */
#include <exec/types.h>
#include <graphics/rastport.h>
#include <graphics/gfxmacros.h>
#include <proto/graphics.h>
#include <string.h>
#include "draw.h"
#include "game.h"

/* Clipped RectFill */
static void safe_rect(struct RastPort *rp, WORD x1, WORD y1, WORD x2, WORD y2)
{
    if (x1 < 0) x1 = 0;
    if (y1 < 0) y1 = 0;
    if (x2 >= SCREEN_W) x2 = SCREEN_W - 1;
    if (y2 >= SCREEN_H) y2 = SCREEN_H - 1;
    if (x1 > x2 || y1 > y2) return;
    RectFill(rp, x1, y1, x2, y2);
}

/* Previous sprite positions for erasing */
static WORD prev_pac_x, prev_pac_y;
static WORD prev_ghost_x[NUM_GHOSTS], prev_ghost_y[NUM_GHOSTS];
static WORD prev_fruit_active;
static WORD prev_fruit_x, prev_fruit_y;
static LONG prev_score;
static WORD prev_lives;
static WORD maze_dirty;  /* flag to force full maze redraw */

/* 5x7 font */
static const UBYTE font_data[36][7] = {
    {0x0E,0x11,0x13,0x15,0x19,0x11,0x0E},
    {0x04,0x0C,0x04,0x04,0x04,0x04,0x0E},
    {0x0E,0x11,0x01,0x02,0x04,0x08,0x1F},
    {0x0E,0x11,0x01,0x06,0x01,0x11,0x0E},
    {0x02,0x06,0x0A,0x12,0x1F,0x02,0x02},
    {0x1F,0x10,0x1E,0x01,0x01,0x11,0x0E},
    {0x06,0x08,0x10,0x1E,0x11,0x11,0x0E},
    {0x1F,0x01,0x02,0x04,0x08,0x08,0x08},
    {0x0E,0x11,0x11,0x0E,0x11,0x11,0x0E},
    {0x0E,0x11,0x11,0x0F,0x01,0x02,0x0C},
    {0x0E,0x11,0x11,0x1F,0x11,0x11,0x11},
    {0x1E,0x11,0x11,0x1E,0x11,0x11,0x1E},
    {0x0E,0x11,0x10,0x10,0x10,0x11,0x0E},
    {0x1C,0x12,0x11,0x11,0x11,0x12,0x1C},
    {0x1F,0x10,0x10,0x1E,0x10,0x10,0x1F},
    {0x1F,0x10,0x10,0x1E,0x10,0x10,0x10},
    {0x0E,0x11,0x10,0x17,0x11,0x11,0x0F},
    {0x11,0x11,0x11,0x1F,0x11,0x11,0x11},
    {0x0E,0x04,0x04,0x04,0x04,0x04,0x0E},
    {0x07,0x02,0x02,0x02,0x02,0x12,0x0C},
    {0x11,0x12,0x14,0x18,0x14,0x12,0x11},
    {0x10,0x10,0x10,0x10,0x10,0x10,0x1F},
    {0x11,0x1B,0x15,0x15,0x11,0x11,0x11},
    {0x11,0x19,0x15,0x13,0x11,0x11,0x11},
    {0x0E,0x11,0x11,0x11,0x11,0x11,0x0E},
    {0x1E,0x11,0x11,0x1E,0x10,0x10,0x10},
    {0x0E,0x11,0x11,0x11,0x15,0x12,0x0D},
    {0x1E,0x11,0x11,0x1E,0x14,0x12,0x11},
    {0x0E,0x11,0x10,0x0E,0x01,0x11,0x0E},
    {0x1F,0x04,0x04,0x04,0x04,0x04,0x04},
    {0x11,0x11,0x11,0x11,0x11,0x11,0x0E},
    {0x11,0x11,0x11,0x11,0x0A,0x0A,0x04},
    {0x11,0x11,0x11,0x15,0x15,0x1B,0x11},
    {0x11,0x11,0x0A,0x04,0x0A,0x11,0x11},
    {0x11,0x11,0x0A,0x04,0x04,0x04,0x04},
    {0x1F,0x01,0x02,0x04,0x08,0x10,0x1F},
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
        col = 0;
        while (col < 5) {
            if (bits & (0x10 >> col)) {
                WORD start = col;
                while (col < 5 && (bits & (0x10 >> col))) col++;
                safe_rect(rp, x + start * scale, y + row * scale,
                          x + col * scale - 1, y + row * scale + scale - 1);
            } else {
                col++;
            }
        }
    }
}

static void draw_text(struct RastPort *rp, WORD x, WORD y, UBYTE color,
                      const char *text, WORD scale)
{
    while (*text) {
        if (*text == ' ') x += 6 * scale;
        else { draw_char(rp, x, y, color, *text, scale); x += 6 * scale; }
        text++;
    }
}

static void draw_text_centered(struct RastPort *rp, WORD y, UBYTE color,
                                const char *text, WORD scale)
{
    int len = 0;
    const char *p = text;
    WORD w;
    while (*p++) len++;
    w = len * 6 * scale;
    draw_text(rp, (SCREEN_W - w) / 2, y, color, text, scale);
}

static void format_number(char *buf, LONG val, int field_width)
{
    char tmp[12];
    int i = 0, j;
    LONG v = val;
    if (v == 0) { tmp[i++] = '0'; }
    else { while (v > 0) { tmp[i++] = '0' + (char)(v % 10); v /= 10; } }
    for (j = 0; j < field_width - i; j++) buf[j] = ' ';
    for (j = 0; j < i; j++) buf[field_width - 1 - j] = tmp[j];
    buf[field_width] = '\0';
}

void draw_set_dirty(void)
{
    maze_dirty = 2; /* both buffers need redraw */
}

/* --- Full maze draw (called once per buffer at level start) --- */

void draw_maze_full(struct RastPort *rp, GameState *gs)
{
    WORD ty, tx;

    SetRast(rp, COL_BLACK);

    for (ty = 0; ty < MAZE_H; ty++) {
        WORD sy = MAZE_OY + ty * TILE_SIZE;

        /* Wall runs */
        tx = 0;
        while (tx < MAZE_W) {
            if (gs->maze[ty][tx] == T_WALL) {
                WORD start = tx;
                WORD x1, x2;
                while (tx < MAZE_W && gs->maze[ty][tx] == T_WALL) tx++;
                x1 = MAZE_OX + start * TILE_SIZE;
                x2 = MAZE_OX + tx * TILE_SIZE - 1;
                SetAPen(rp, COL_BLUE);
                safe_rect(rp, x1 + 1, sy + 1, x2 - 1, sy + TILE_SIZE - 2);
                SetAPen(rp, COL_DKBLUE);
                safe_rect(rp, x1, sy, x2, sy);
                safe_rect(rp, x1, sy + TILE_SIZE - 1, x2, sy + TILE_SIZE - 1);
                safe_rect(rp, x1, sy, x1, sy + TILE_SIZE - 1);
                safe_rect(rp, x2, sy, x2, sy + TILE_SIZE - 1);
            } else {
                tx++;
            }
        }

        /* Dots, power pellets, ghost door */
        for (tx = 0; tx < MAZE_W; tx++) {
            WORD sx = MAZE_OX + tx * TILE_SIZE;
            UBYTE tile = gs->maze[ty][tx];
            if (tile == T_DOT) {
                SetAPen(rp, COL_CREAM);
                safe_rect(rp, sx + 3, sy + 3, sx + 4, sy + 4);
            } else if (tile == T_POWER) {
                SetAPen(rp, COL_CREAM);
                safe_rect(rp, sx + 1, sy + 1, sx + 6, sy + 6);
            } else if (tile == T_GHOST_DOOR) {
                SetAPen(rp, COL_PINK);
                safe_rect(rp, sx, sy + 3, sx + 7, sy + 4);
            }
        }
    }

    /* HUD labels (static parts) */
    draw_text(rp, 4, 0, COL_WHITE, "1UP", 1);
    draw_text(rp, MAZE_OX + 80, 0, COL_WHITE, "HIGH SCORE", 1);
}

/* --- Restore tiles under a sprite area --- */

static void restore_area(struct RastPort *rp, GameState *gs, WORD px, WORD py)
{
    /* px, py are pixel coords in maze space. Restore the 2x2 tile area */
    WORD tx_start, ty_start, tx_end, ty_end, tx, ty;

    /* Expand area slightly for sprite overhang */
    tx_start = (px - 1) / TILE_SIZE;
    ty_start = (py - 1) / TILE_SIZE;
    tx_end = (px + TILE_SIZE + 1) / TILE_SIZE;
    ty_end = (py + TILE_SIZE + 1) / TILE_SIZE;

    if (tx_start < 0) tx_start = 0;
    if (ty_start < 0) ty_start = 0;
    if (tx_end >= MAZE_W) tx_end = MAZE_W - 1;
    if (ty_end >= MAZE_H) ty_end = MAZE_H - 1;

    for (ty = ty_start; ty <= ty_end; ty++) {
        WORD sy = MAZE_OY + ty * TILE_SIZE;
        for (tx = tx_start; tx <= tx_end; tx++) {
            WORD sx = MAZE_OX + tx * TILE_SIZE;
            UBYTE tile = gs->maze[ty][tx];

            if (tile == T_WALL) {
                /* Redraw wall tile (simple solid fill) */
                SetAPen(rp, COL_BLUE);
                safe_rect(rp, sx + 1, sy + 1, sx + TILE_SIZE - 2, sy + TILE_SIZE - 2);
                SetAPen(rp, COL_DKBLUE);
                safe_rect(rp, sx, sy, sx + TILE_SIZE - 1, sy);
                safe_rect(rp, sx, sy + TILE_SIZE - 1, sx + TILE_SIZE - 1, sy + TILE_SIZE - 1);
                safe_rect(rp, sx, sy, sx, sy + TILE_SIZE - 1);
                safe_rect(rp, sx + TILE_SIZE - 1, sy, sx + TILE_SIZE - 1, sy + TILE_SIZE - 1);
            } else if (tile == T_DOT) {
                SetAPen(rp, COL_BLACK);
                safe_rect(rp, sx, sy, sx + TILE_SIZE - 1, sy + TILE_SIZE - 1);
                SetAPen(rp, COL_CREAM);
                safe_rect(rp, sx + 3, sy + 3, sx + 4, sy + 4);
            } else if (tile == T_POWER) {
                SetAPen(rp, COL_BLACK);
                safe_rect(rp, sx, sy, sx + TILE_SIZE - 1, sy + TILE_SIZE - 1);
                /* Power pellet blink handled in main draw */
                SetAPen(rp, COL_CREAM);
                safe_rect(rp, sx + 1, sy + 1, sx + 6, sy + 6);
            } else if (tile == T_GHOST_DOOR) {
                SetAPen(rp, COL_BLACK);
                safe_rect(rp, sx, sy, sx + TILE_SIZE - 1, sy + TILE_SIZE - 1);
                SetAPen(rp, COL_PINK);
                safe_rect(rp, sx, sy + 3, sx + 7, sy + 4);
            } else {
                /* Empty tile - just black */
                SetAPen(rp, COL_BLACK);
                safe_rect(rp, sx, sy, sx + TILE_SIZE - 1, sy + TILE_SIZE - 1);
            }
        }
    }
}

/* --- Pac-Man sprite --- */

static void draw_pacman(struct RastPort *rp, GameState *gs)
{
    WORD px = MAZE_OX + gs->pac.pixel_x;
    WORD py = MAZE_OY + gs->pac.pixel_y;
    WORD cx = px + 3;
    WORD cy = py + 3;

    SetAPen(rp, COL_YELLOW);

    if (!gs->pac.mouth_open) {
        safe_rect(rp, cx - 3, cy - 2, cx + 3, cy + 2);
        safe_rect(rp, cx - 2, cy - 3, cx + 2, cy + 3);
    } else {
        switch (gs->pac.dir) {
        case DIR_RIGHT:
            safe_rect(rp, cx - 3, cy - 2, cx + 1, cy + 2);
            safe_rect(rp, cx - 2, cy - 3, cx + 0, cy + 3);
            safe_rect(rp, cx - 3, cy - 1, cx + 3, cy - 1);
            safe_rect(rp, cx - 3, cy + 1, cx + 3, cy + 1);
            break;
        case DIR_LEFT:
            safe_rect(rp, cx - 1, cy - 2, cx + 3, cy + 2);
            safe_rect(rp, cx + 0, cy - 3, cx + 2, cy + 3);
            safe_rect(rp, cx - 3, cy - 1, cx + 3, cy - 1);
            safe_rect(rp, cx - 3, cy + 1, cx + 3, cy + 1);
            break;
        case DIR_UP:
            safe_rect(rp, cx - 2, cy - 1, cx + 2, cy + 3);
            safe_rect(rp, cx - 3, cy + 0, cx + 3, cy + 2);
            safe_rect(rp, cx - 1, cy - 3, cx - 1, cy + 3);
            safe_rect(rp, cx + 1, cy - 3, cx + 1, cy + 3);
            break;
        case DIR_DOWN:
            safe_rect(rp, cx - 2, cy - 3, cx + 2, cy + 1);
            safe_rect(rp, cx - 3, cy - 2, cx + 3, cy + 0);
            safe_rect(rp, cx - 1, cy - 3, cx - 1, cy + 3);
            safe_rect(rp, cx + 1, cy - 3, cx + 1, cy + 3);
            break;
        default:
            safe_rect(rp, cx - 3, cy - 2, cx + 3, cy + 2);
            safe_rect(rp, cx - 2, cy - 3, cx + 2, cy + 3);
            break;
        }
    }
}

/* --- Ghost sprites --- */

static const UBYTE ghost_colors[NUM_GHOSTS] = {
    COL_RED, COL_PINK, COL_CYAN, COL_ORANGE
};

static void draw_ghost(struct RastPort *rp, Ghost *g, WORD id, GameState *gs)
{
    WORD px = MAZE_OX + g->pixel_x;
    WORD py = MAZE_OY + g->pixel_y;
    WORD cx = px + 3;
    WORD cy = py + 3;
    UBYTE body_col, eye_col;

    if (g->state == GHOST_EATEN) {
        SetAPen(rp, COL_WHITE);
        safe_rect(rp, cx - 2, cy - 1, cx - 1, cy);
        safe_rect(rp, cx + 1, cy - 1, cx + 2, cy);
        SetAPen(rp, COL_BLUE);
        switch (g->dir) {
        case DIR_RIGHT: safe_rect(rp, cx-1, cy, cx-1, cy); safe_rect(rp, cx+2, cy, cx+2, cy); break;
        case DIR_LEFT:  safe_rect(rp, cx-2, cy, cx-2, cy); safe_rect(rp, cx+1, cy, cx+1, cy); break;
        case DIR_UP:    safe_rect(rp, cx-2, cy-1, cx-1, cy-1); safe_rect(rp, cx+1, cy-1, cx+2, cy-1); break;
        default:        safe_rect(rp, cx-2, cy, cx-1, cy); safe_rect(rp, cx+1, cy, cx+2, cy); break;
        }
        return;
    }

    if (g->state == GHOST_FRIGHT) {
        if (g->fright_timer < FRIGHT_BLINK && (g->fright_timer >> 3) & 1) {
            body_col = COL_WHITE; eye_col = COL_RED;
        } else {
            body_col = COL_DKBLUE; eye_col = COL_CREAM;
        }
    } else {
        body_col = ghost_colors[id]; eye_col = COL_WHITE;
    }

    SetAPen(rp, body_col);
    safe_rect(rp, cx - 3, cy - 2, cx + 3, cy + 2);
    safe_rect(rp, cx - 2, cy - 3, cx + 2, cy - 3);
    if ((gs->frame >> 2) & 1) {
        safe_rect(rp, cx-3, cy+3, cx-2, cy+3);
        safe_rect(rp, cx-1, cy+3, cx+0, cy+3);
        safe_rect(rp, cx+2, cy+3, cx+3, cy+3);
    } else {
        safe_rect(rp, cx-3, cy+3, cx-3, cy+3);
        safe_rect(rp, cx-1, cy+3, cx-1, cy+3);
        safe_rect(rp, cx+1, cy+3, cx+1, cy+3);
        safe_rect(rp, cx+3, cy+3, cx+3, cy+3);
    }

    if (g->state != GHOST_FRIGHT) {
        SetAPen(rp, eye_col);
        safe_rect(rp, cx-2, cy-1, cx-1, cy);
        safe_rect(rp, cx+1, cy-1, cx+2, cy);
        SetAPen(rp, COL_BLUE);
        switch (g->dir) {
        case DIR_RIGHT: safe_rect(rp, cx-1, cy, cx-1, cy); safe_rect(rp, cx+2, cy, cx+2, cy); break;
        case DIR_LEFT:  safe_rect(rp, cx-2, cy, cx-2, cy); safe_rect(rp, cx+1, cy, cx+1, cy); break;
        case DIR_UP:    safe_rect(rp, cx-2, cy-1, cx-1, cy-1); safe_rect(rp, cx+1, cy-1, cx+2, cy-1); break;
        default:        safe_rect(rp, cx-2, cy, cx-1, cy); safe_rect(rp, cx+1, cy, cx+2, cy); break;
        }
    } else {
        SetAPen(rp, eye_col);
        safe_rect(rp, cx-2, cy-1, cx-2, cy-1);
        safe_rect(rp, cx+2, cy-1, cx+2, cy-1);
        safe_rect(rp, cx-2, cy+1, cx-2, cy+1);
        safe_rect(rp, cx, cy+1, cx, cy+1);
        safe_rect(rp, cx+2, cy+1, cx+2, cy+1);
    }
}

/* --- Fruit --- */
static const UBYTE fruit_colors[MAX_FRUIT_TYPE] = {
    COL_RED, COL_RED, COL_ORANGE, COL_RED, COL_PURPLE, COL_YELLOW, COL_CYAN
};

static void draw_fruit(struct RastPort *rp, GameState *gs)
{
    WORD px = MAZE_OX + gs->fruit_x;
    WORD py = MAZE_OY + gs->fruit_y;
    SetAPen(rp, fruit_colors[gs->fruit_type]);
    safe_rect(rp, px + 1, py + 2, px + 6, py + 6);
    safe_rect(rp, px + 2, py + 1, px + 5, py + 7);
    SetAPen(rp, COL_GREEN);
    safe_rect(rp, px + 3, py, px + 4, py + 1);
}

/* --- Death animation --- */
static void draw_dying(struct RastPort *rp, GameState *gs)
{
    WORD px = MAZE_OX + gs->pac.pixel_x;
    WORD py = MAZE_OY + gs->pac.pixel_y;
    WORD cx = px + 3, cy = py + 3;
    WORD phase = (DYING_TIME - gs->state_timer) / 8;

    SetAPen(rp, COL_YELLOW);
    if (phase < 4) {
        WORD gap = phase + 1;
        safe_rect(rp, cx - 3, cy - 3, cx + 3, cy - gap);
        safe_rect(rp, cx - 3, cy + gap, cx + 3, cy + 3);
    } else if (phase < 7) {
        WORD s = 7 - phase;
        safe_rect(rp, cx - s, cy - s, cx + s, cy + s);
    }
}

/* --- HUD (partial update) --- */
static void draw_hud_score(struct RastPort *rp, GameState *gs)
{
    char buf[12];
    /* Clear score area */
    SetAPen(rp, COL_BLACK);
    safe_rect(rp, 0, 8, 46, 16);
    format_number(buf, gs->score, 7);
    draw_text(rp, 0, 8, COL_WHITE, buf, 1);

    /* High score */
    SetAPen(rp, COL_BLACK);
    safe_rect(rp, MAZE_OX + 92, 8, MAZE_OX + 140, 16);
    format_number(buf, gs->hiscore, 7);
    draw_text(rp, MAZE_OX + 92, 8, COL_WHITE, buf, 1);
}

static void draw_hud_lives(struct RastPort *rp, GameState *gs)
{
    WORD i;
    /* Clear lives area */
    SetAPen(rp, COL_BLACK);
    safe_rect(rp, 0, SCREEN_H - 12, 70, SCREEN_H - 1);

    for (i = 0; i < gs->lives - 1 && i < 5; i++) {
        WORD lx = 4 + i * 12;
        WORD ly = SCREEN_H - 10;
        SetAPen(rp, COL_YELLOW);
        safe_rect(rp, lx, ly + 1, lx + 6, ly + 5);
        safe_rect(rp, lx + 1, ly, lx + 5, ly + 6);
        SetAPen(rp, COL_BLACK);
        safe_rect(rp, lx + 4, ly + 2, lx + 6, ly + 2);
        safe_rect(rp, lx + 5, ly + 3, lx + 6, ly + 3);
        safe_rect(rp, lx + 4, ly + 4, lx + 6, ly + 4);
    }

    {
        WORD ft = (gs->level - 1) % MAX_FRUIT_TYPE;
        SetAPen(rp, fruit_colors[ft]);
        safe_rect(rp, SCREEN_W - 14, SCREEN_H - 10, SCREEN_W - 7, SCREEN_H - 3);
    }
}

/* --- Main per-frame draw --- */

void draw_game(struct RastPort *rp, GameState *gs)
{
    WORD i;

    /* If maze is dirty, do full redraw */
    if (maze_dirty > 0) {
        draw_maze_full(rp, gs);
        draw_hud_score(rp, gs);
        draw_hud_lives(rp, gs);
        maze_dirty--;

        /* Init previous positions */
        prev_pac_x = gs->pac.pixel_x;
        prev_pac_y = gs->pac.pixel_y;
        for (i = 0; i < NUM_GHOSTS; i++) {
            prev_ghost_x[i] = gs->ghosts[i].pixel_x;
            prev_ghost_y[i] = gs->ghosts[i].pixel_y;
        }
        prev_score = gs->score;
        prev_lives = gs->lives;
        prev_fruit_active = gs->fruit_active;
    }

    /* Step 1: Erase previous sprite positions by restoring tiles */
    restore_area(rp, gs, prev_pac_x, prev_pac_y);
    for (i = 0; i < NUM_GHOSTS; i++) {
        restore_area(rp, gs, prev_ghost_x[i], prev_ghost_y[i]);
    }
    if (prev_fruit_active && !gs->fruit_active) {
        restore_area(rp, gs, prev_fruit_x, prev_fruit_y);
    }

    /* Step 2: Handle power pellet blink (only 4 tiles max) */
    {
        WORD ty, tx;
        for (ty = 0; ty < MAZE_H; ty++) {
            for (tx = 0; tx < MAZE_W; tx++) {
                if (gs->maze[ty][tx] == T_POWER) {
                    WORD sx = MAZE_OX + tx * TILE_SIZE;
                    WORD sy = MAZE_OY + ty * TILE_SIZE;
                    SetAPen(rp, COL_BLACK);
                    safe_rect(rp, sx + 1, sy + 1, sx + 6, sy + 6);
                    if ((gs->frame >> 3) & 1) {
                        SetAPen(rp, COL_CREAM);
                        safe_rect(rp, sx + 1, sy + 1, sx + 6, sy + 6);
                    }
                }
            }
        }
    }

    /* Step 3: Draw sprites at new positions */
    if (gs->fruit_active) draw_fruit(rp, gs);

    if (gs->state == STATE_DYING) {
        draw_dying(rp, gs);
    } else {
        draw_pacman(rp, gs);
    }

    for (i = 0; i < NUM_GHOSTS; i++) {
        draw_ghost(rp, &gs->ghosts[i], i, gs);
    }

    /* Step 4: Update HUD only when changed */
    if (gs->score != prev_score) {
        draw_hud_score(rp, gs);
    }
    if (gs->lives != prev_lives) {
        draw_hud_lives(rp, gs);
    }

    /* Popup text */
    if (gs->popup_active) {
        char buf[8];
        format_number(buf, gs->popup_score, 4);
        draw_text(rp, MAZE_OX + gs->popup_x - 8, MAZE_OY + gs->popup_y, COL_CYAN, buf, 1);
    }

    /* READY text */
    if (gs->state == STATE_READY) {
        draw_text_centered(rp, MAZE_OY + 17 * TILE_SIZE, COL_YELLOW, "READY", 1);
    }

    if (gs->state == STATE_LEVEL_DONE) {
        if ((gs->state_timer >> 3) & 1) {
            draw_text_centered(rp, MAZE_OY + 17 * TILE_SIZE, COL_WHITE, "LEVEL CLEAR", 1);
        }
    }

    /* Save current positions for next frame */
    prev_pac_x = gs->pac.pixel_x;
    prev_pac_y = gs->pac.pixel_y;
    for (i = 0; i < NUM_GHOSTS; i++) {
        prev_ghost_x[i] = gs->ghosts[i].pixel_x;
        prev_ghost_y[i] = gs->ghosts[i].pixel_y;
    }
    prev_score = gs->score;
    prev_lives = gs->lives;
    prev_fruit_active = gs->fruit_active;
    if (gs->fruit_active) {
        prev_fruit_x = gs->fruit_x;
        prev_fruit_y = gs->fruit_y;
    }
}

void draw_title(struct RastPort *rp, GameState *gs)
{
    WORD i;

    SetRast(rp, COL_BLACK);
    draw_text_centered(rp, 20, COL_YELLOW, "PACBRO", 3);

    {
        WORD anim_x = (gs->title_blink * 2) % (SCREEN_W + 40) - 20;
        SetAPen(rp, COL_YELLOW);
        safe_rect(rp, anim_x, 63, anim_x + 6, 69);
        safe_rect(rp, anim_x + 1, 62, anim_x + 5, 70);
        if ((gs->title_blink >> 2) & 1) {
            SetAPen(rp, COL_BLACK);
            safe_rect(rp, anim_x + 4, 64, anim_x + 6, 64);
            safe_rect(rp, anim_x + 5, 65, anim_x + 6, 65);
            safe_rect(rp, anim_x + 4, 66, anim_x + 6, 66);
        }
        SetAPen(rp, COL_CREAM);
        for (i = 0; i < 8; i++) {
            WORD dx = anim_x + 20 + i * 16;
            if (dx > 0 && dx < SCREEN_W) safe_rect(rp, dx, 65, dx + 1, 66);
        }
    }

    draw_text(rp, 60, 88, COL_WHITE, "CHARACTER", 1);
    draw_text(rp, 180, 88, COL_WHITE, "NICKNAME", 1);

    {
        Ghost tmp;
        memset(&tmp, 0, sizeof(tmp));
        tmp.pixel_x = 50 - MAZE_OX;
        tmp.dir = DIR_RIGHT;

        tmp.pixel_y = 100 - MAZE_OY; tmp.state = GHOST_CHASE;
        draw_ghost(rp, &tmp, GHOST_BLINKY, gs);
        draw_text(rp, 70, 102, COL_RED, "SHADOW", 1);
        draw_text(rp, 180, 102, COL_RED, "BLINKY", 1);

        tmp.pixel_y = 118 - MAZE_OY;
        draw_ghost(rp, &tmp, GHOST_PINKY, gs);
        draw_text(rp, 70, 120, COL_PINK, "SPEEDY", 1);
        draw_text(rp, 180, 120, COL_PINK, "PINKY", 1);

        tmp.pixel_y = 136 - MAZE_OY;
        draw_ghost(rp, &tmp, GHOST_INKY, gs);
        draw_text(rp, 70, 138, COL_CYAN, "BASHFUL", 1);
        draw_text(rp, 180, 138, COL_CYAN, "INKY", 1);

        tmp.pixel_y = 154 - MAZE_OY;
        draw_ghost(rp, &tmp, GHOST_CLYDE, gs);
        draw_text(rp, 70, 156, COL_ORANGE, "POKEY", 1);
        draw_text(rp, 180, 156, COL_ORANGE, "CLYDE", 1);
    }

    SetAPen(rp, COL_CREAM);
    safe_rect(rp, 100, 178, 101, 179);
    draw_text(rp, 110, 176, COL_WHITE, "10 PTS", 1);
    SetAPen(rp, COL_CREAM);
    safe_rect(rp, 98, 192, 103, 197);
    draw_text(rp, 110, 192, COL_WHITE, "50 PTS", 1);

    draw_text_centered(rp, 212, COL_GRAY, "ARROWS OR JOYSTICK", 1);
    if ((gs->title_blink >> 3) & 1) {
        draw_text_centered(rp, 230, COL_WHITE, "PRESS FIRE TO START", 1);
    }
    draw_text_centered(rp, SCREEN_H - 10, COL_GRAY, "ESC TO QUIT", 1);
}

void draw_gameover(struct RastPort *rp, GameState *gs)
{
    char buf[12];
    SetRast(rp, COL_BLACK);
    draw_text_centered(rp, 60, COL_RED, "GAME OVER", 3);
    draw_text_centered(rp, 110, COL_WHITE, "SCORE", 2);
    format_number(buf, gs->score, 7);
    draw_text_centered(rp, 135, COL_YELLOW, buf, 2);
    draw_text_centered(rp, 170, COL_GRAY, "LEVEL", 1);
    format_number(buf, (LONG)gs->level, 3);
    draw_text_centered(rp, 182, COL_WHITE, buf, 1);
    if (gs->score >= gs->hiscore) draw_text_centered(rp, 200, COL_CYAN, "NEW HIGH SCORE", 1);
    if ((gs->title_blink >> 3) & 1) draw_text_centered(rp, 225, COL_WHITE, "PRESS FIRE", 1);
}
