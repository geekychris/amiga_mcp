/*
 * Uranus Lander - Game definitions
 * All structs, constants, and function declarations
 */
#ifndef GAME_H
#define GAME_H

#include <exec/types.h>

/* Screen constants */
#define SCREEN_W    320
#define SCREEN_H    256

/* Fixed-point 16.16 */
typedef LONG Fixed;
#define FIX(x)      ((Fixed)(x) << 16)
#define FIXF(x)     ((Fixed)((x) * 65536.0))
#define FIX_INT(x)  ((WORD)(((x) + 0x8000) >> 16))  /* round instead of truncate */
#define FIX_MUL(a,b) ((Fixed)(((LONG)(a) >> 8) * ((LONG)(b) >> 8)))

/* Angle system: 256 steps, 0 = up */
#define ANGLE_COUNT 256

/* Game states */
#define STATE_TITLE      0
#define STATE_PLAYING    1
#define STATE_LANDED     2
#define STATE_CRASHING   3
#define STATE_GAMEOVER   4
#define STATE_ENTER_NAME 5

/* Colors */
#define COL_BG       0
#define COL_WHITE    1
#define COL_DKGRAY   2
#define COL_GRAY     3
#define COL_LTGRAY   4
#define COL_DKBLUE   5
#define COL_YELLOW   6
#define COL_GREEN    7
#define COL_RED      8
#define COL_ORANGE   9
#define COL_CYAN     10
#define COL_DKCYAN   11
#define COL_MAGENTA  12
#define COL_BRYELLOW 13
#define COL_SLATE    14
#define COL_BRWHITE  15

/* Physics defaults (tunable) */
#define GRAVITY_DEFAULT      1000
#define THRUST_DEFAULT       3200
#define FUEL_DEFAULT         600
#define SAFE_VY_DEFAULT      55000
#define SAFE_VX_DEFAULT      35000
#define SAFE_ANGLE_DEFAULT   14
#define TURN_RATE_DEFAULT    2
#define START_LIVES_DEFAULT  3

/* Terrain */
#define TERRAIN_W    320
#define TERRAIN_MIN  130
#define TERRAIN_MAX  238

/* Landing pads */
#define MAX_PADS     5

/* Particles */
#define MAX_PARTICLES 32

/* Stars */
#define MAX_STARS    60

/* High scores */
#define MAX_HISCORES 10
#define NAME_LEN     4

/* Ship structure */
typedef struct {
    Fixed x, y;
    Fixed vx, vy;
    WORD angle;        /* 0-255, 0 = up */
    WORD fuel;
    WORD alive;
    WORD thrusting;
} Ship;

/* Landing pad */
typedef struct {
    WORD x;
    WORD width;
    WORD y;
    WORD multiplier;   /* 1, 2, 3, or 5 */
} LandingPad;

/* Particle */
typedef struct {
    Fixed x, y;
    Fixed vx, vy;
    WORD life;
    WORD color;
    WORD active;
} Particle;

/* Star */
typedef struct {
    WORD x, y;
    WORD brightness;   /* color index */
    WORD speed;        /* sub-pixel speed (1-3) */
    WORD accum;        /* sub-pixel accumulator */
} Star;

/* High score entry */
typedef struct {
    char name[NAME_LEN];
    LONG score;
} HiScoreEntry;

/* Tunables (exposed via bridge) */
typedef struct {
    LONG gravity;
    LONG thrust;
    LONG fuel_max;
    LONG safe_vy;
    LONG safe_vx;
    LONG safe_angle;
    LONG turn_rate;
    LONG start_lives;
} Tunables;

/* Input state */
typedef struct {
    WORD left;
    WORD right;
    WORD thrust;
    WORD up;
    WORD down;
    WORD quit;
} InputState;

/* Main game state */
typedef struct {
    Ship ship;
    LandingPad pads[MAX_PADS];
    WORD num_pads;
    WORD terrain_y[TERRAIN_W];
    Particle particles[MAX_PARTICLES];
    Star stars[MAX_STARS];

    LONG score;
    WORD lives;
    WORD level;
    WORD state;
    WORD state_timer;
    WORD frame;

    /* Landing result */
    WORD landed_pad;
    LONG land_bonus;

    /* High scores */
    HiScoreEntry hiscores[MAX_HISCORES];

    /* Name entry */
    WORD name_pos;
    char name_buf[NAME_LEN];
    WORD name_letter;

    /* Sound event flags */
    WORD ev_thrust;
    WORD ev_crash;
    WORD ev_land;
    WORD ev_low_fuel;
} GameState;

/* Trig tables */
extern Fixed sin_tab[ANGLE_COUNT];
extern Fixed cos_tab[ANGLE_COUNT];

/* Tunables */
extern Tunables g_tune;

/* Game functions */
void game_init_tables(void);
void game_init(GameState *gs);
void game_new_level(GameState *gs);
void game_update(GameState *gs, InputState *inp);
void stars_update(GameState *gs);
void hiscore_load(GameState *gs);
void hiscore_save(GameState *gs);
WORD hiscore_qualifies(GameState *gs);

/* Sound callbacks (implemented in main.c) */
extern void sfx_thrust_play(void);
extern void sfx_crash_play(void);
extern void sfx_land_play(void);
extern void sfx_beep_play(void);

#endif /* GAME_H */
