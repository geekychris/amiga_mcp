/*
 * Frank the Frog - Game constants and shared types
 */
#ifndef GAME_H
#define GAME_H

#include <exec/types.h>

/* Screen dimensions */
#define SCREEN_W 320
#define SCREEN_H 256
#define SCREEN_DEPTH 4  /* 16 colors */

/* Tile grid */
#define TILE_W 16
#define TILE_H 16
#define GRID_COLS (SCREEN_W / TILE_W)   /* 20 columns */
#define GRID_ROWS 13

/* Row layout (top to bottom) */
#define ROW_HOME     0    /* Home bases */
#define ROW_RIVER_5  1    /* River lanes (top = fast) */
#define ROW_RIVER_4  2
#define ROW_RIVER_3  3
#define ROW_RIVER_2  4
#define ROW_RIVER_1  5
#define ROW_MEDIAN   6    /* Safe zone */
#define ROW_ROAD_5   7    /* Road lanes (top = fast) */
#define ROW_ROAD_4   8
#define ROW_ROAD_3   9
#define ROW_ROAD_2   10
#define ROW_ROAD_1   11
#define ROW_START    12   /* Start area */

/* HUD area: below the 13 tile rows */
#define HUD_Y (GRID_ROWS * TILE_H)  /* 208 */
#define HUD_H (SCREEN_H - HUD_Y)    /* 48 */

/* Palette indices */
#define COL_BG        0   /* black */
#define COL_GRASS     1   /* green */
#define COL_ROAD      2   /* dark gray */
#define COL_WATER     3   /* blue */
#define COL_FROG      4   /* bright green */
#define COL_FROG_DARK 5   /* dark green (outline) */
#define COL_LOG       6   /* brown */
#define COL_CAR_RED   7   /* red */
#define COL_CAR_YELLOW 8  /* yellow */
#define COL_CAR_BLUE  9   /* blue car */
#define COL_TRUCK     10  /* purple/dark */
#define COL_TURTLE    11  /* dark green */
#define COL_LILY      12  /* light green */
#define COL_WHITE     13
#define COL_HUD_BG    14  /* dark blue */
#define COL_ROAD_LINE 15  /* yellow dashes */

/* Game states */
#define STATE_TITLE    0
#define STATE_PLAYING  1
#define STATE_DYING    2
#define STATE_LEVEL    3
#define STATE_GAMEOVER 4

/* Max objects per lane */
#define MAX_LANE_OBJECTS 6

/* Directions */
#define DIR_LEFT  (-1)
#define DIR_RIGHT  1

#endif /* GAME_H */
