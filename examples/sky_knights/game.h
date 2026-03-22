/*
 * SKY KNIGHTS - Arcade clone for Amiga
 * Game definitions and structures
 */
#ifndef GAME_H
#define GAME_H

#include <exec/types.h>

/* Screen dimensions */
#define SCREEN_W    320
#define SCREEN_H    256

/* Physics constants (fixed point 16.16) */
typedef LONG Fixed;
#define FIX(x)       ((Fixed)((x) * 65536L))
#define FIX_INT(x)   ((WORD)((x) >> 16))
#define FIX_FRAC(x)  ((x) & 0xFFFF)

#define GRAVITY         19661   /* ~0.3 * 65536 */
#define FLAP_IMPULSE   (-196608) /* -3.0 * 65536 */
#define MAX_FALL_VEL    262144  /* 4.0 * 65536 */
#define MAX_RISE_VEL   (-327680) /* -5.0 * 65536 */
#define MOVE_SPEED      131072  /* 2.0 * 65536 */
#define ENEMY_SLOW      65536   /* 1.0 */
#define ENEMY_MED       98304   /* 1.5 */
#define ENEMY_FAST      131072  /* 2.0 */

/* Platform definitions */
#define MAX_PLATFORMS   8
#define GROUND_Y        236     /* ground level */
#define PLAT_THICK      8       /* platform thickness */

typedef struct {
    WORD x, y, w;   /* position and width in pixels */
} Platform;

/* Rider dimensions (player and enemy, same size) */
#define PLAYER_W    20
#define PLAYER_H    20
#define LANCE_H     4   /* height of lance hitbox from top */

/* Enemy types */
#define ETYPE_BOUNDER    0
#define ETYPE_HUNTER     1
#define ETYPE_SHADOW     2

#define MAX_ENEMIES     8
#define MAX_EGGS        8
#define MAX_PLAYERS     2

/* Egg */
typedef struct {
    Fixed x, y;
    Fixed dy;
    WORD  active;
    WORD  timer;      /* hatch countdown */
    WORD  on_ground;
    WORD  hatch_type; /* what enemy type it hatches into */
} Egg;

/* Enemy */
typedef struct {
    Fixed x, y;
    Fixed dx, dy;
    WORD  type;       /* ETYPE_* */
    WORD  active;
    WORD  facing;     /* 0=right, 1=left */
    WORD  anim_frame;
    WORD  anim_timer;
    WORD  flap_timer; /* frames until next flap */
    WORD  on_ground;
} Enemy;

/* Player */
typedef struct {
    Fixed x, y;
    Fixed dx, dy;
    WORD  active;
    WORD  alive;
    WORD  lives;
    WORD  facing;     /* 0=right, 1=left */
    WORD  anim_frame;
    WORD  anim_timer;
    WORD  on_ground;
    WORD  respawn_timer;
    WORD  flapping;   /* visual: wings up */
} Player;

/* Game states */
#define STATE_TITLE      0
#define STATE_PLAYING    1
#define STATE_WAVE_INTRO 2
#define STATE_DYING      3
#define STATE_GAMEOVER   4

/* Sound event flags (set by game, cleared by main after triggering) */
#define EV_FLAP      1
#define EV_KILL      2
#define EV_EGG       4
#define EV_DIE       8
#define EV_WAVE      16

/* Input bits */
#define INP_LEFT     1
#define INP_RIGHT    2
#define INP_FLAP     4
#define INP_ESC      8
#define INP_START1   16   /* F1 or 1 */
#define INP_START2   32   /* F2 or 2 */

/* Input state for both players */
typedef struct {
    UWORD p1;   /* keyboard: left/right/flap */
    UWORD p2;   /* joystick: left/right/flap */
    UWORD sys;  /* ESC, F1, F2 */
} InputState;

/* Game state */
typedef struct {
    Player    players[MAX_PLAYERS];
    Enemy     enemies[MAX_ENEMIES];
    Egg       eggs[MAX_EGGS];
    Platform  platforms[MAX_PLATFORMS];
    WORD      num_platforms;

    LONG      score[MAX_PLAYERS];
    WORD      wave;
    WORD      state;
    WORD      state_timer;
    WORD      frame;
    WORD      num_players;    /* 1 or 2 */
    WORD      enemies_alive;
    WORD      ev_flags;       /* sound event flags */

    WORD      title_selection; /* 0=1P, 1=2P */
} GameState;

/* Egg timing */
#define EGG_HATCH_TIME  300   /* frames (~5 sec at 50fps) */
#define EGG_WOBBLE_TIME 240   /* start wobbling at this point */

/* Scoring */
#define SCORE_BOUNDER   500
#define SCORE_HUNTER    750
#define SCORE_SHADOW    1000
#define SCORE_EGG       250
#define SCORE_WAVE      1000

/* Functions */
void game_init(GameState *gs);
void game_start(GameState *gs);
void game_update(GameState *gs, InputState *input);
void game_start_wave(GameState *gs);

#endif
