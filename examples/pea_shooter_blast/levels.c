/*
 * Pea Shooter Blast - Level data
 *
 * Each level is MAP_H (16) x MAP_W (128) tiles.
 * Tile types defined in game.h.
 *
 * Legend:
 *  . = empty    # = ground    B = brick    R = rock
 *  - = platform L = ladder    ^ = spikes   P = pipe_h
 *  | = pipe_v   D = dirt      M = metal    G = gate
 *  W = powerup
 */
#include "game.h"

/* Tile property table */
UBYTE tile_props[NUM_TILE_TYPES] = {
    0,                      /* TILE_EMPTY */
    TPROP_SOLID,            /* TILE_GROUND */
    TPROP_SOLID,            /* TILE_BRICK */
    TPROP_SOLID,            /* TILE_ROCK */
    TPROP_PLATFORM,         /* TILE_PLATFORM */
    TPROP_LADDER,           /* TILE_LADDER */
    TPROP_DAMAGE,           /* TILE_SPIKES */
    0,                      /* TILE_PIPE_H (decoration) */
    0,                      /* TILE_PIPE_V (decoration) */
    TPROP_SOLID,            /* TILE_DIRT */
    TPROP_SOLID,            /* TILE_METAL */
    TPROP_EXIT,             /* TILE_GATE */
    0,                      /* TILE_POWERUP */
};

/*
 * Helper: we'll generate level data procedurally at init time
 * to avoid giant arrays in the source. Each level has a theme.
 */

/* Level 1: Underground tunnel - gentle introduction */
static void gen_level_1(void)
{
    WORD x, y;

    /* Fill with empty */
    for (y = 0; y < MAP_H; y++)
        for (x = 0; x < MAP_W; x++)
            level_maps[0][y][x] = TILE_EMPTY;

    /* Ground floor (row 15 = bottom) */
    for (x = 0; x < MAP_W; x++) {
        level_maps[0][15][x] = TILE_GROUND;
        level_maps[0][14][x] = TILE_DIRT;
    }

    /* Ceiling (row 0) */
    for (x = 0; x < MAP_W; x++) {
        level_maps[0][0][x] = TILE_ROCK;
    }

    /* Some gaps in the floor */
    for (x = 20; x < 23; x++) {
        level_maps[0][15][x] = TILE_EMPTY;
        level_maps[0][14][x] = TILE_EMPTY;
    }
    for (x = 45; x < 48; x++) {
        level_maps[0][15][x] = TILE_EMPTY;
        level_maps[0][14][x] = TILE_EMPTY;
    }

    /* Spikes in gaps */
    for (x = 20; x < 23; x++)
        level_maps[0][15][x] = TILE_SPIKES;
    for (x = 45; x < 48; x++)
        level_maps[0][15][x] = TILE_SPIKES;

    /* Platforms */
    for (x = 18; x < 25; x++)
        level_maps[0][11][x] = TILE_PLATFORM;
    for (x = 30; x < 36; x++)
        level_maps[0][10][x] = TILE_PLATFORM;
    for (x = 50; x < 56; x++)
        level_maps[0][11][x] = TILE_PLATFORM;

    /* Brick walls / obstacles */
    for (y = 10; y < 14; y++) {
        level_maps[0][y][35] = TILE_BRICK;
        level_maps[0][y][36] = TILE_BRICK;
    }
    for (y = 8; y < 14; y++) {
        level_maps[0][y][60] = TILE_BRICK;
    }

    /* Upper platforms with ladders */
    for (x = 55; x < 65; x++)
        level_maps[0][8][x] = TILE_PLATFORM;
    for (y = 9; y < 14; y++)
        level_maps[0][y][58] = TILE_LADDER;

    /* More terrain variety */
    for (y = 12; y < 14; y++)
        for (x = 70; x < 75; x++)
            level_maps[0][y][x] = TILE_ROCK;

    /* Pipe decoration */
    for (x = 80; x < 90; x++)
        level_maps[0][2][x] = TILE_PIPE_H;
    for (y = 2; y < 10; y++)
        level_maps[0][y][80] = TILE_PIPE_V;

    /* Metal section near end */
    for (y = 10; y < 14; y++)
        for (x = 95; x < 100; x++)
            level_maps[0][y][x] = TILE_METAL;
    for (x = 95; x < 105; x++)
        level_maps[0][9][x] = TILE_PLATFORM;

    /* Boss arena (open area) */
    for (x = 110; x < MAP_W; x++) {
        level_maps[0][0][x] = TILE_METAL;
        level_maps[0][15][x] = TILE_METAL;
        level_maps[0][14][x] = TILE_METAL;
    }
    for (y = 0; y < MAP_H; y++) {
        level_maps[0][y][110] = TILE_METAL;
    }

    /* Powerups */
    level_maps[0][10][32] = TILE_POWERUP;
    level_maps[0][7][60] = TILE_POWERUP;
    level_maps[0][12][85] = TILE_POWERUP;

    /* Gate at end */
    level_maps[0][12][MAP_W - 3] = TILE_GATE;
    level_maps[0][13][MAP_W - 3] = TILE_GATE;
}

/* Level 2: Cave system - more vertical, tighter passages */
static void gen_level_2(void)
{
    WORD x, y;

    for (y = 0; y < MAP_H; y++)
        for (x = 0; x < MAP_W; x++)
            level_maps[1][y][x] = TILE_EMPTY;

    /* Floor and ceiling */
    for (x = 0; x < MAP_W; x++) {
        level_maps[1][15][x] = TILE_ROCK;
        level_maps[1][14][x] = TILE_ROCK;
        level_maps[1][0][x] = TILE_ROCK;
        level_maps[1][1][x] = TILE_ROCK;
    }

    /* Varied terrain with gaps and platforms */
    for (x = 15; x < 20; x++) {
        level_maps[1][14][x] = TILE_EMPTY;
        level_maps[1][15][x] = TILE_SPIKES;
    }

    /* Stepping platforms */
    level_maps[1][12][17] = TILE_PLATFORM;
    level_maps[1][12][18] = TILE_PLATFORM;
    level_maps[1][10][22] = TILE_PLATFORM;
    level_maps[1][10][23] = TILE_PLATFORM;

    /* Cave pillars */
    for (y = 5; y < 14; y++) {
        level_maps[1][y][30] = TILE_ROCK;
        level_maps[1][y][50] = TILE_ROCK;
        level_maps[1][y][70] = TILE_ROCK;
    }

    /* Mid-height platforms */
    for (x = 25; x < 35; x++)
        level_maps[1][8][x] = TILE_PLATFORM;
    for (x = 40; x < 50; x++)
        level_maps[1][9][x] = TILE_PLATFORM;
    for (x = 55; x < 65; x++)
        level_maps[1][7][x] = TILE_PLATFORM;

    /* Ladders between sections */
    for (y = 9; y < 14; y++) {
        level_maps[1][y][28] = TILE_LADDER;
        level_maps[1][y][48] = TILE_LADDER;
        level_maps[1][y][63] = TILE_LADDER;
    }

    /* More brick structures */
    for (y = 4; y < 8; y++) {
        for (x = 75; x < 80; x++)
            level_maps[1][y][x] = TILE_BRICK;
    }
    for (x = 75; x < 85; x++)
        level_maps[1][11][x] = TILE_PLATFORM;

    /* Tight passage */
    for (x = 85; x < 100; x++) {
        level_maps[1][4][x] = TILE_ROCK;
        level_maps[1][11][x] = TILE_ROCK;
    }

    /* Powerups */
    level_maps[1][7][27] = TILE_POWERUP;
    level_maps[1][6][60] = TILE_POWERUP;
    level_maps[1][10][82] = TILE_POWERUP;

    /* Boss arena */
    for (x = 110; x < MAP_W; x++) {
        level_maps[1][0][x] = TILE_METAL;
        level_maps[1][1][x] = TILE_METAL;
        level_maps[1][14][x] = TILE_METAL;
        level_maps[1][15][x] = TILE_METAL;
    }
    for (y = 0; y < MAP_H; y++)
        level_maps[1][y][110] = TILE_METAL;

    level_maps[1][12][MAP_W - 3] = TILE_GATE;
    level_maps[1][13][MAP_W - 3] = TILE_GATE;
}

/* Level 3: Underground factory - metal and machinery */
static void gen_level_3(void)
{
    WORD x, y;

    for (y = 0; y < MAP_H; y++)
        for (x = 0; x < MAP_W; x++)
            level_maps[2][y][x] = TILE_EMPTY;

    /* Metal floor and ceiling */
    for (x = 0; x < MAP_W; x++) {
        level_maps[2][15][x] = TILE_METAL;
        level_maps[2][14][x] = TILE_METAL;
        level_maps[2][0][x] = TILE_METAL;
    }

    /* Conveyor-like platforms at different heights */
    for (x = 10; x < 20; x++)
        level_maps[2][11][x] = TILE_METAL;
    for (x = 25; x < 35; x++)
        level_maps[2][9][x] = TILE_METAL;
    for (x = 40; x < 50; x++)
        level_maps[2][7][x] = TILE_METAL;
    for (x = 55; x < 65; x++)
        level_maps[2][10][x] = TILE_METAL;

    /* Spike pits */
    for (x = 20; x < 25; x++) {
        level_maps[2][14][x] = TILE_EMPTY;
        level_maps[2][15][x] = TILE_SPIKES;
    }
    for (x = 50; x < 55; x++) {
        level_maps[2][14][x] = TILE_EMPTY;
        level_maps[2][15][x] = TILE_SPIKES;
    }

    /* Pipe decorations */
    for (x = 0; x < 60; x++)
        level_maps[2][1][x] = TILE_PIPE_H;
    for (y = 1; y < 8; y++) {
        level_maps[2][y][15] = TILE_PIPE_V;
        level_maps[2][y][45] = TILE_PIPE_V;
    }

    /* Ladders */
    for (y = 8; y < 14; y++) {
        level_maps[2][y][18] = TILE_LADDER;
        level_maps[2][y][42] = TILE_LADDER;
        level_maps[2][y][62] = TILE_LADDER;
    }

    /* Brick walls forming maze-like sections */
    for (y = 3; y < 9; y++) {
        level_maps[2][y][35] = TILE_BRICK;
        level_maps[2][y][36] = TILE_BRICK;
    }
    for (y = 5; y < 14; y++) {
        level_maps[2][y][75] = TILE_BRICK;
    }

    /* Upper metal walkways */
    for (x = 70; x < 85; x++)
        level_maps[2][5][x] = TILE_METAL;
    for (x = 85; x < 100; x++)
        level_maps[2][8][x] = TILE_METAL;

    /* Powerups */
    level_maps[2][6][30] = TILE_POWERUP;
    level_maps[2][4][78] = TILE_POWERUP;
    level_maps[2][7][90] = TILE_POWERUP;
    level_maps[2][12][60] = TILE_POWERUP;

    /* Boss arena - larger for final boss */
    for (x = 105; x < MAP_W; x++) {
        level_maps[2][0][x] = TILE_METAL;
        level_maps[2][14][x] = TILE_METAL;
        level_maps[2][15][x] = TILE_METAL;
    }
    for (y = 0; y < MAP_H; y++)
        level_maps[2][y][105] = TILE_METAL;

    /* Some platforms in boss arena */
    for (x = 112; x < 118; x++)
        level_maps[2][10][x] = TILE_PLATFORM;
    for (x = 120; x < 126; x++)
        level_maps[2][7][x] = TILE_PLATFORM;

    level_maps[2][12][MAP_W - 3] = TILE_GATE;
    level_maps[2][13][MAP_W - 3] = TILE_GATE;
}

/* Level map storage */
UBYTE level_maps[3][MAP_H][MAP_W];

void levels_generate(void)
{
    gen_level_1();
    gen_level_2();
    gen_level_3();
}
