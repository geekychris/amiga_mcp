/*
 * Frank the Frog - Playfield drawing
 */

#include <exec/types.h>
#include <graphics/gfx.h>
#include <graphics/rastport.h>
#include <proto/graphics.h>

#include "game.h"
#include "playfield.h"

/* Draw a row-width filled rectangle */
static void fill_row(struct RastPort *rp, int row, UBYTE color)
{
    SetAPen(rp, (long)color);
    RectFill(rp, 0, row * TILE_H,
             SCREEN_W - 1, (row + 1) * TILE_H - 1);
}

/* Draw road lane divider dashes between two road rows */
static void draw_road_divider(struct RastPort *rp, int row)
{
    int col;
    int y_mid = row * TILE_H + TILE_H / 2;

    /* Dashed center line */
    SetAPen(rp, (long)COL_ROAD_LINE);
    for (col = 0; col < SCREEN_W; col += 24) {
        RectFill(rp, col, y_mid, col + 10, y_mid + 1);
    }
}

/* Draw home base slots (5 lily pads on water background) */
static void draw_home_row(struct RastPort *rp)
{
    int slot;
    int pad_w = 24;
    int pad_h = 12;

    /* Water background */
    fill_row(rp, ROW_HOME, COL_WATER);

    /* 5 home slots evenly spaced */
    SetAPen(rp, (long)COL_LILY);
    for (slot = 0; slot < 5; slot++) {
        int cx = slot * (SCREEN_W / 5) + (SCREEN_W / 10);
        int x0 = cx - pad_w / 2;
        int y0 = ROW_HOME * TILE_H + (TILE_H - pad_h) / 2;
        RectFill(rp, x0, y0, x0 + pad_w - 1, y0 + pad_h - 1);
    }
}

void playfield_draw(struct RastPort *rp)
{
    int row;

    /* Home bases */
    draw_home_row(rp);

    /* River lanes */
    for (row = ROW_RIVER_5; row <= ROW_RIVER_1; row++) {
        fill_row(rp, row, COL_WATER);
    }

    /* Median (safe zone) */
    fill_row(rp, ROW_MEDIAN, COL_GRASS);

    /* Road lanes */
    for (row = ROW_ROAD_5; row <= ROW_ROAD_1; row++) {
        fill_row(rp, row, COL_ROAD);
    }
    /* Dividers between road lanes (not on top/bottom edges) */
    for (row = ROW_ROAD_5; row < ROW_ROAD_1; row++) {
        draw_road_divider(rp, row);
    }

    /* Start area */
    fill_row(rp, ROW_START, COL_GRASS);

    /* HUD area */
    SetAPen(rp, (long)COL_HUD_BG);
    RectFill(rp, 0, HUD_Y, SCREEN_W - 1, SCREEN_H - 1);
}
