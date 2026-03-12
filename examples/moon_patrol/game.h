/*
 * Moon Patrol - Game definitions
 * All structs, constants, function declarations
 */
#ifndef GAME_H
#define GAME_H

#include <exec/types.h>

/* Screen constants */
#define SCREEN_W    320
#define SCREEN_H    256

/* Game states */
#define STATE_TITLE      0
#define STATE_PLAYING    1
#define STATE_DYING      2
#define STATE_GAMEOVER   3
#define STATE_CHECKPOINT 4

/* Terrain constants */
#define TERRAIN_LEN     640     /* terrain buffer length (2 screens ahead) */
#define GROUND_Y        200     /* ground baseline Y */
#define TERRAIN_FLAT    0
#define TERRAIN_CRATER_SM 1     /* small crater */
#define TERRAIN_CRATER_LG 2     /* large crater */
#define TERRAIN_ROCK    3       /* boulder obstacle */
#define TERRAIN_HILL    4       /* cosmetic bump */
#define TERRAIN_MINE    5       /* ground mine */

/* Buggy constants */
#define BUGGY_W         24
#define BUGGY_H         12
#define BUGGY_X         80      /* fixed screen X position */
#define BUGGY_GROUND_Y  (GROUND_Y - BUGGY_H)
#define JUMP_VEL        (-600)  /* initial jump velocity (fixed point /100) */
#define GRAVITY         40      /* gravity per frame (/100) */

/* Scroll speeds (fixed point /100) */
#define SPEED_NORMAL    150
#define SPEED_FAST      300
#define SPEED_SLOW      50

/* Bullet limits */
#define MAX_FWD_BULLETS 2
#define MAX_UP_BULLETS  2
#define MAX_BOMBS       8
#define MAX_ENEMIES     8
#define MAX_EXPLOSIONS  6

/* Enemy types */
#define ENEMY_NONE      0
#define ENEMY_UFO_SM    1
#define ENEMY_UFO_LG    2
#define ENEMY_METEOR    3

/* Checkpoint distance */
#define CHECKPOINT_DIST 500

/* Colors */
#define COL_BLACK       0
#define COL_WHITE       1
#define COL_DKPURPLE    2
#define COL_PURPLE      3
#define COL_BROWN       4
#define COL_DKBROWN     5
#define COL_YELLOW      6
#define COL_GRAY        7
#define COL_RED         8
#define COL_ORANGE      9
#define COL_CYAN        10
#define COL_GREEN       11
#define COL_BLUE        12
#define COL_BRYELLOW    13
#define COL_DKGRAY      14
#define COL_PINK        15

/* Structures */

typedef struct {
    WORD x, y;          /* screen position */
    WORD vy;            /* vertical velocity (/100) */
    WORD on_ground;     /* flag */
    WORD alive;
    WORD wheel_frame;   /* 0 or 1 */
} Buggy;

typedef struct {
    WORD x, y;          /* screen position */
    WORD dx, dy;        /* velocity */
    WORD active;
} Bullet;

typedef struct {
    WORD x, y;
    WORD dx, dy;
    WORD type;          /* ENEMY_UFO_SM, ENEMY_UFO_LG, ENEMY_METEOR */
    WORD active;
    WORD hp;
    WORD bomb_timer;
    WORD flash;         /* animation counter */
} Enemy;

typedef struct {
    WORD x, y;
    WORD dy;
    WORD active;
} Bomb;

typedef struct {
    WORD x, y;
    WORD radius;
    WORD life;
    WORD active;
} Explosion;

typedef struct {
    LONG scroll_x;              /* total distance scrolled (world coords) */
    LONG scroll_speed;          /* current speed (/100) */
    WORD terrain[TERRAIN_LEN];  /* terrain type at each X pixel offset */
    WORD terrain_h[TERRAIN_LEN];/* terrain height at each X pixel offset */
    LONG terrain_gen_x;         /* next X to generate terrain at */

    Buggy buggy;

    Bullet fwd_bullets[MAX_FWD_BULLETS];
    Bullet up_bullets[MAX_UP_BULLETS];

    Enemy enemies[MAX_ENEMIES];
    Bomb bombs[MAX_BOMBS];
    Explosion explosions[MAX_EXPLOSIONS];

    LONG score;
    WORD lives;
    WORD state;
    WORD state_timer;
    WORD frame;

    WORD checkpoint_cur;        /* current checkpoint letter (0=A) */
    LONG checkpoint_dist;       /* distance to next checkpoint */

    WORD difficulty;            /* increases each loop */

    /* Sound event flags - set by game, consumed by main loop */
    WORD ev_shoot;
    WORD ev_explode;
    WORD ev_jump;
    WORD ev_checkpoint;
    WORD ev_death;
} GameState;

/* Input state */
typedef struct {
    WORD left;
    WORD right;
    WORD jump;
    WORD fire;
    WORD quit;
} InputState;

/* Game functions */
void game_init(GameState *gs);
void game_update(GameState *gs, InputState *inp);

/* Draw functions (draw.h) */
/* Input functions (input.h) */
/* Sound functions (sound.h) */

#endif /* GAME_H */
