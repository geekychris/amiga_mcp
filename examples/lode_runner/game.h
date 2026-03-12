/*
 * Lode Runner - Game constants and shared types
 */
#ifndef GAME_H
#define GAME_H

#include <exec/types.h>

/* Screen dimensions */
#define SCREEN_W     320
#define SCREEN_H     256
#define SCREEN_DEPTH 4    /* 16 colors */

/* Tile grid */
#define TILE_W     10
#define TILE_H     14
#define GRID_COLS  28
#define GRID_ROWS  16

/* Playfield area */
#define PLAYFIELD_X  20
#define PLAYFIELD_Y  4
#define PLAYFIELD_W  280   /* GRID_COLS * TILE_W */
#define PLAYFIELD_H  224   /* GRID_ROWS * TILE_H */

/* Status bar */
#define STATUS_Y  232
#define STATUS_H  24

/* Palette indices */
#define COL_BG        0    /* black        #000 */
#define COL_BRICK     1    /* brown        #A52 */
#define COL_BRICK_HI  2    /* light brown  #C73 */
#define COL_SOLID     3    /* gray         #888 */
#define COL_SOLID_HI  4    /* light gray   #AAA */
#define COL_LADDER    5    /* cyan         #0AA */
#define COL_BAR       6    /* dark cyan    #088 */
#define COL_GOLD      7    /* yellow       #EE0 */
#define COL_GOLD_HI   8    /* bright yel   #FF8 */
#define COL_PLAYER    9    /* green        #0D0 */
#define COL_PLAYER_HI 10   /* light green  #0F4 */
#define COL_ENEMY     11   /* red          #D00 */
#define COL_ENEMY_HI  12   /* orange       #F80 */
#define COL_TEXT      13   /* white        #FFF */
#define COL_HUD_BG    14   /* dark blue    #113 */
#define COL_TRAP      15   /* same as brick #A52 */

/* Game states */
#define STATE_TITLE      0
#define STATE_PLAYING    1
#define STATE_DYING      2
#define STATE_LEVEL_DONE 3
#define STATE_GAMEOVER   4
#define STATE_EDITOR     5

/* Tile types */
#define TILE_EMPTY         0
#define TILE_BRICK         1
#define TILE_SOLID         2
#define TILE_LADDER        3
#define TILE_BAR           4
#define TILE_GOLD          5
#define TILE_HIDDEN_LADDER 6
#define TILE_PLAYER        7
#define TILE_ENEMY         8
#define TILE_TRAP          9

/* Limits */
#define MAX_ENEMIES  5
#define MAX_LEVELS   150

/* Timing */
#define BRICK_REGEN_FRAMES 150   /* 3 seconds at 50fps */
#define PLAYER_SPEED       2     /* pixels per frame */
#define ENEMY_SPEED        1

/* Directions */
#define DIR_NONE   0
#define DIR_LEFT   1
#define DIR_RIGHT  2
#define DIR_UP     3
#define DIR_DOWN   4

/* Player states */
#define PS_IDLE      0
#define PS_RUNNING   1
#define PS_CLIMBING  2
#define PS_HANGING   3
#define PS_FALLING   4
#define PS_DIGGING   5
#define PS_DEAD      6

/* Enemy states */
#define ES_RUNNING   0
#define ES_CLIMBING  1
#define ES_HANGING   2
#define ES_FALLING   3
#define ES_TRAPPED   4
#define ES_DEAD      5

/* Structs */
typedef struct {
    int gx, gy;        /* grid position */
    int px, py;         /* pixel position */
    int dir;            /* facing direction */
    int state;          /* PS_* */
    int anim;           /* animation frame counter */
    int dig_timer;      /* frames remaining in dig action */
    int fall_count;     /* consecutive frames falling */
} Player;

typedef struct {
    int gx, gy;         /* grid position */
    int px, py;          /* pixel position */
    int dir;             /* facing direction */
    int state;           /* ES_* */
    int anim;            /* animation frame counter */
    int trap_timer;      /* frames remaining in trap */
    int has_gold;        /* carrying stolen gold? */
    int active;          /* is this enemy slot in use? */
    int move_timer;      /* movement timing counter */
} Enemy;

typedef struct {
    int state;           /* STATE_* */
    int level_num;
    int score;
    int lives;
    int gold_total;
    int gold_collected;
    int frame;
    Player player;
    Enemy enemies[MAX_ENEMIES];
    int num_enemies;
    UBYTE tiles[GRID_COLS][GRID_ROWS];
    UWORD brick_timer[GRID_COLS][GRID_ROWS];
} GameState;

#endif /* GAME_H */
