/*
 * Gold Rush - In-game level editor
 *
 * Allows creating and editing levels with cursor-based tile placement.
 * Uses joystick/arrows + keyboard for navigation and tile selection.
 *
 * amiga.lib sprintf returns char*, not int - use strlen() after.
 * All pen/coordinate args cast to (long) for Amiga varargs.
 */

#include <exec/types.h>
#include <graphics/rastport.h>
#include <proto/graphics.h>

#include <stdio.h>
#include <string.h>

#include "game.h"
#include "input.h"
#include "level.h"
#include "render.h"
#include "editor.h"

/* Raw keycodes for number keys (main keyboard, Amiga layout) */
#define KEY_0  0x0A
#define KEY_1  0x01
#define KEY_2  0x02
#define KEY_3  0x03
#define KEY_4  0x04
#define KEY_5  0x05
#define KEY_6  0x06
#define KEY_7  0x07
#define KEY_8  0x08
#define KEY_9  0x09

/* Editor state */
static int cursor_x = 0;
static int cursor_y = 0;
static int current_tile = TILE_BRICK;
static int editor_level = 0;
static char filename[64];
static char status_msg[64];
static int status_timer = 0;

/* Cursor movement repeat delay */
static int move_delay = 0;
#define MOVE_REPEAT_DELAY 6

/* Number of selectable tile types for cycling */
#define NUM_TILE_TYPES 10
static const int tile_cycle[NUM_TILE_TYPES] = {
    TILE_EMPTY, TILE_BRICK, TILE_SOLID, TILE_LADDER, TILE_BAR,
    TILE_GOLD, TILE_HIDDEN_LADDER, TILE_PLAYER, TILE_ENEMY, TILE_TRAP
};

/* Short tile names for palette display */
static const char *tile_names[NUM_TILE_TYPES] = {
    "EMPT", "BRCK", "SOLD", "LADR", "BAR ",
    "GOLD", "HLAD", "PLYR", "ENMY", "TRAP"
};

/* Map tile type to its index in tile_cycle */
static int tile_to_index(int tile)
{
    int i;
    for (i = 0; i < NUM_TILE_TYPES; i++) {
        if (tile_cycle[i] == tile) return i;
    }
    return 0;
}

/* ------------------------------------------------------------------ */
/*  editor_init                                                        */
/* ------------------------------------------------------------------ */
void editor_init(GameState *gs, int level_num)
{
    int x;

    editor_level = level_num;

    if (level_num > 0) {
        level_load(gs, level_num);
    } else {
        /* Blank level: clear tiles, solid bottom row */
        memset(gs->tiles, TILE_EMPTY, sizeof(gs->tiles));
        for (x = 0; x < GRID_COLS; x++) {
            gs->tiles[x][GRID_ROWS - 1] = TILE_SOLID;
        }
    }

    cursor_x = 0;
    cursor_y = 0;
    current_tile = TILE_BRICK;
    move_delay = 0;
    status_msg[0] = '\0';
    status_timer = 0;
}

/* ------------------------------------------------------------------ */
/*  editor_update                                                      */
/* ------------------------------------------------------------------ */
void editor_update(GameState *gs)
{
    int dx, dy;
    int idx;

    /* --- Cursor movement with repeat delay --- */
    dx = input_dx();
    dy = input_dy();

    if (dx == 0 && dy == 0) {
        move_delay = 0;
    } else if (move_delay <= 0) {
        /* Move cursor */
        cursor_x += dx;
        cursor_y += dy;

        /* Clamp to grid bounds */
        if (cursor_x < 0) cursor_x = 0;
        if (cursor_x >= GRID_COLS) cursor_x = GRID_COLS - 1;
        if (cursor_y < 0) cursor_y = 0;
        if (cursor_y >= GRID_ROWS) cursor_y = GRID_ROWS - 1;

        move_delay = MOVE_REPEAT_DELAY;
    } else {
        move_delay--;
    }

    /* --- Place tile with fire button --- */
    if (input_fire() || input_fire_held()) {
        gs->tiles[cursor_x][cursor_y] = (UBYTE)current_tile;
    }

    /* --- Cycle tile type with SPACE --- */
    if (input_key(KEY_SPACE)) {
        idx = tile_to_index(current_tile);
        idx = (idx + 1) % NUM_TILE_TYPES;
        current_tile = tile_cycle[idx];
    }

    /* --- Number keys select tile type directly --- */
    if (input_key(KEY_1)) current_tile = TILE_BRICK;
    if (input_key(KEY_2)) current_tile = TILE_SOLID;
    if (input_key(KEY_3)) current_tile = TILE_LADDER;
    if (input_key(KEY_4)) current_tile = TILE_BAR;
    if (input_key(KEY_5)) current_tile = TILE_GOLD;
    if (input_key(KEY_6)) current_tile = TILE_HIDDEN_LADDER;
    if (input_key(KEY_7)) current_tile = TILE_PLAYER;
    if (input_key(KEY_8)) current_tile = TILE_ENEMY;
    if (input_key(KEY_9)) current_tile = TILE_TRAP;
    if (input_key(KEY_0)) current_tile = TILE_EMPTY;

    /* --- F1: Save level --- */
    if (input_key(KEY_F1)) {
        sprintf(filename, "DH2:Dev/levels/level_%03ld.txt",
                (long)editor_level);
        if (level_save_text(filename) == 0) {
            sprintf(status_msg, "Saved: level_%03ld", (long)editor_level);
        } else {
            sprintf(status_msg, "Save FAILED!");
        }
        status_timer = 100; /* ~2 seconds at 50fps */
    }

    /* --- F2: Load level --- */
    if (input_key(KEY_F2)) {
        sprintf(filename, "DH2:Dev/levels/level_%03ld.txt",
                (long)editor_level);
        if (level_load_text(filename) == 0) {
            sprintf(status_msg, "Loaded: level_%03ld", (long)editor_level);
        } else {
            sprintf(status_msg, "Load FAILED!");
        }
        status_timer = 100;
    }

    /* --- F3: Test play --- */
    if (input_key(KEY_F3)) {
        gs->state = STATE_PLAYING;
        gs->gold_total = level_count_gold();
        gs->gold_collected = 0;
    }

    /* --- ESC: exit editor (handled in main.c, but also set state) --- */
    /* Note: main.c checks input_key(KEY_ESC) before calling editor_update,
       so ESC exit is handled there. We set state as a fallback. */

    /* Decrement status timer */
    if (status_timer > 0) {
        status_timer--;
    }
}

/* ------------------------------------------------------------------ */
/*  editor_draw                                                        */
/* ------------------------------------------------------------------ */

/* Draw a color swatch for a tile type in the palette bar */
static void draw_palette_swatch(struct RastPort *rp, int idx,
                                int px, int py, int selected)
{
    int tile;
    int pen;
    int sw = 14; /* swatch width */
    int sh = 10; /* swatch height */

    tile = tile_cycle[idx];

    /* Pick representative color for each tile */
    switch (tile) {
    case TILE_EMPTY:         pen = COL_BG; break;
    case TILE_BRICK:         pen = COL_BRICK; break;
    case TILE_SOLID:         pen = COL_SOLID; break;
    case TILE_LADDER:        pen = COL_LADDER; break;
    case TILE_BAR:           pen = COL_BAR; break;
    case TILE_GOLD:          pen = COL_GOLD; break;
    case TILE_HIDDEN_LADDER: pen = COL_LADDER; break;
    case TILE_PLAYER:        pen = COL_PLAYER; break;
    case TILE_ENEMY:         pen = COL_ENEMY; break;
    case TILE_TRAP:          pen = COL_TRAP; break;
    default:                 pen = COL_BG; break;
    }

    /* Swatch fill */
    SetAPen(rp, (long)pen);
    RectFill(rp, (long)px, (long)py,
             (long)(px + sw - 1), (long)(py + sh - 1));

    /* For empty tile, draw a dot so it's visible */
    if (tile == TILE_EMPTY) {
        SetAPen(rp, (long)COL_SOLID);
        WritePixel(rp, (long)(px + sw / 2), (long)(py + sh / 2));
    }

    /* Hidden ladder gets a dashed look */
    if (tile == TILE_HIDDEN_LADDER) {
        SetAPen(rp, (long)COL_BG);
        Move(rp, (long)(px + 2), (long)(py + 3));
        Draw(rp, (long)(px + 4), (long)(py + 3));
        Move(rp, (long)(px + 8), (long)(py + 7));
        Draw(rp, (long)(px + 10), (long)(py + 7));
    }

    /* Selection highlight */
    if (selected) {
        SetAPen(rp, (long)COL_TEXT);
        Move(rp, (long)px, (long)py);
        Draw(rp, (long)(px + sw - 1), (long)py);
        Draw(rp, (long)(px + sw - 1), (long)(py + sh - 1));
        Draw(rp, (long)px, (long)(py + sh - 1));
        Draw(rp, (long)px, (long)py);
    }
}

void editor_draw(struct RastPort *rp, GameState *gs)
{
    int cx1, cy1, cx2, cy2;
    int i, px;
    char buf[64];
    int cur_idx;

    /* Draw the playfield tiles */
    render_playfield(rp, gs);

    /* --- Blinking cursor outline --- */
    if ((gs->frame / 8) % 2) {
        cx1 = PLAYFIELD_X + cursor_x * TILE_W;
        cy1 = PLAYFIELD_Y + cursor_y * TILE_H;
        cx2 = cx1 + TILE_W - 1;
        cy2 = cy1 + TILE_H - 1;

        SetAPen(rp, (long)COL_TEXT);
        Move(rp, (long)cx1, (long)cy1);
        Draw(rp, (long)cx2, (long)cy1);
        Draw(rp, (long)cx2, (long)cy2);
        Draw(rp, (long)cx1, (long)cy2);
        Draw(rp, (long)cx1, (long)cy1);
    }

    /* --- Status area / tile palette --- */
    SetAPen(rp, (long)COL_HUD_BG);
    RectFill(rp, 0L, (long)STATUS_Y,
             (long)(SCREEN_W - 1), (long)(SCREEN_H - 1));

    /* Top border */
    SetAPen(rp, (long)COL_LADDER);
    Move(rp, 0L, (long)STATUS_Y);
    Draw(rp, (long)(SCREEN_W - 1), (long)STATUS_Y);

    SetBPen(rp, (long)COL_HUD_BG);

    /* Draw tile palette swatches */
    cur_idx = tile_to_index(current_tile);
    px = 4;
    for (i = 0; i < NUM_TILE_TYPES; i++) {
        draw_palette_swatch(rp, i, px, STATUS_Y + 3, (i == cur_idx));

        /* Tile number label below swatch */
        SetAPen(rp, (long)COL_TEXT);
        sprintf(buf, "%ld", (long)((i + 1) % 10));
        Move(rp, (long)(px + 4), (long)(STATUS_Y + 21));
        Text(rp, (CONST_STRPTR)buf, (long)strlen(buf));

        px += 16;
    }

    /* Current tile name */
    SetAPen(rp, (long)COL_GOLD);
    Move(rp, 170L, (long)(STATUS_Y + 10));
    Text(rp, (CONST_STRPTR)tile_names[cur_idx], 4L);

    /* Editor info line */
    SetAPen(rp, (long)COL_TEXT);
    sprintf(buf, "EDITOR Lv:%ld (%ld,%ld)",
            (long)editor_level, (long)cursor_x, (long)cursor_y);
    Move(rp, 170L, (long)(STATUS_Y + 21));
    Text(rp, (CONST_STRPTR)buf, (long)strlen(buf));

    /* Help text in side borders */
    SetAPen(rp, (long)COL_LADDER);
    SetBPen(rp, (long)COL_HUD_BG);

    /* Use the left border area for minimal help (it's only 20px wide,
       so we put help in the playfield top area instead) */

    /* Status message display */
    if (status_timer > 0) {
        SetAPen(rp, (long)COL_GOLD_HI);
        SetBPen(rp, (long)COL_BG);
        Move(rp, (long)((SCREEN_W - (int)strlen(status_msg) * 8) / 2),
             (long)(PLAYFIELD_Y - 1));
        Text(rp, (CONST_STRPTR)status_msg, (long)strlen(status_msg));
    }
}
