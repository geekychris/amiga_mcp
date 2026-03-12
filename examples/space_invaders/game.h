#ifndef GAME_H
#define GAME_H

#include <exec/types.h>

/* Screen */
#define SCREEN_W 320
#define SCREEN_H 256
#define SCREEN_DEPTH 4

/* Alien grid */
#define ALIEN_COLS 11
#define ALIEN_ROWS 5
#define ALIEN_W 12
#define ALIEN_H 8
#define ALIEN_CELL_W 24
#define ALIEN_CELL_H 16
#define ALIEN_GRID_W (ALIEN_COLS * ALIEN_CELL_W)
#define ALIEN_START_X ((SCREEN_W - ALIEN_GRID_W) / 2)
#define ALIEN_START_Y 52

/* Alien types by row (top to bottom) */
#define ALIEN_TYPE_C 0  /* Top row - 30 pts */
#define ALIEN_TYPE_B 1  /* Middle 2 rows - 20 pts */
#define ALIEN_TYPE_A 2  /* Bottom 2 rows - 10 pts */

/* Shields */
#define SHIELD_COUNT 4
#define SHIELD_W 22
#define SHIELD_H 16
#define SHIELD_Y 200

/* Player */
#define PLAYER_W 15
#define PLAYER_H 8
#define PLAYER_Y 232
#define PLAYER_SPEED 3
#define PLAYER_MIN_X 8
#define PLAYER_MAX_X (SCREEN_W - 8 - PLAYER_W)

/* Bullets */
#define MAX_ALIEN_BULLETS 3
#define PLAYER_BULLET_SPEED 6
#define ALIEN_BULLET_SPEED 3

/* UFO */
#define UFO_Y 36
#define UFO_W 16
#define UFO_H 7
#define UFO_SPEED 1

/* Game states */
#define STATE_TITLE    0
#define STATE_PLAYING  1
#define STATE_DYING    2
#define STATE_GAMEOVER 3
#define STATE_WAVE_CLEAR 4

/* Explosions */
#define MAX_EXPLOSIONS 8

/* Colors */
#define COL_BG       0
#define COL_WHITE    1
#define COL_GREEN    2
#define COL_DKGREEN  3
#define COL_RED      4
#define COL_ORANGE   5
#define COL_CYAN     6
#define COL_MAGENTA  7
#define COL_YELLOW   8
#define COL_BLUE     9
#define COL_LTBLUE   10
#define COL_LTRED    11
#define COL_BRGREEN  12
#define COL_GRAY     13
#define COL_DKORANGE 14
#define COL_LTGRAY   15

typedef struct {
    WORD x, y;
    BOOL active;
} Bullet;

typedef struct {
    BOOL alive[ALIEN_ROWS][ALIEN_COLS];
    WORD grid_x, grid_y;
    WORD dir;               /* 1=right, -1=left */
    WORD move_timer;
    WORD move_delay;        /* frames between steps */
    WORD anim_frame;        /* 0 or 1 */
    WORD alive_count;
    WORD fire_timer;
    WORD fire_delay;
} AlienSwarm;

typedef struct {
    WORD x;
    WORD dir;
    BOOL active;
    WORD timer;
    WORD score_val;
} UFO;

typedef struct {
    UBYTE pixels[SHIELD_H][SHIELD_W];
    WORD x, y;
} Shield;

typedef struct {
    WORD x, y;
    WORD timer;
    BOOL active;
    UBYTE color;
} Explosion;

typedef struct {
    /* Player */
    WORD player_x;
    WORD lives;
    LONG score;
    LONG hiscore;
    WORD wave;

    /* State */
    WORD state;
    WORD state_timer;
    WORD title_blink;

    /* Aliens */
    AlienSwarm swarm;

    /* Bullets */
    Bullet player_bullet;
    Bullet alien_bullets[MAX_ALIEN_BULLETS];

    /* UFO */
    UFO ufo;

    /* Shields */
    Shield shields[SHIELD_COUNT];

    /* Explosions */
    Explosion explosions[MAX_EXPLOSIONS];

    /* Sound events (checked by main loop) */
    BOOL ev_shoot;
    BOOL ev_alien_hit;
    BOOL ev_player_hit;
    BOOL ev_ufo_hit;
    BOOL ev_march;

    /* March note index */
    WORD march_note;
} GameState;

typedef struct {
    BOOL left, right, fire;
    BOOL fire_pressed;  /* edge-triggered */
    BOOL quit;
    WORD mouse_dx;
} InputState;

/* Game functions */
void game_init(GameState *gs);
void game_init_wave(GameState *gs);
void game_update(GameState *gs, InputState *input);

/* RNG */
UWORD game_rand(void);
void game_srand(ULONG seed);

#endif
