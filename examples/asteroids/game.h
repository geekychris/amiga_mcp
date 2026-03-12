/*
 * Asteroids - Game definitions
 */
#ifndef GAME_H
#define GAME_H

#include <exec/types.h>

/* Screen dimensions */
#define SCREEN_W  320
#define SCREEN_H  256

/* Fixed-point 16.16 */
typedef LONG Fixed;
#define FIX(x)       ((Fixed)((x) * 65536L))
#define FIXF(x)      ((Fixed)((x) * 65536.0f))
#define FIX_INT(x)   ((x) >> 16)
#define FIX_MUL(a,b) ((Fixed)(((LONG)(a) >> 8) * ((LONG)(b) >> 8)))

/* Angle: 0..255 maps to 0..360 degrees */
#define ANGLE_COUNT 256

/* Trig tables (16.16 fixed) - defined in game.c */
extern Fixed sin_tab[ANGLE_COUNT];
extern Fixed cos_tab[ANGLE_COUNT];

/* Ship */
#define SHIP_THRUST     FIX(0)  /* set at runtime via tunable */
#define SHIP_MAX_SPEED  FIX(3)
#define SHIP_DRAG       65200   /* 0.995 in 16.16 approx */
#define SHIP_TURN_SPEED 3       /* angle units per frame */
#define SHIP_RADIUS     8
#define SHIP_INVULN_TIME 100    /* frames of invulnerability after respawn */

typedef struct {
    Fixed x, y;
    Fixed dx, dy;
    WORD  angle;         /* 0..255 */
    WORD  alive;
    WORD  invuln_timer;  /* countdown after respawn */
    WORD  thrust_on;
} Ship;

/* Bullet */
#define MAX_BULLETS     8
#define BULLET_SPEED    FIX(5)
#define BULLET_LIFE     40      /* frames */

typedef struct {
    Fixed x, y;
    Fixed dx, dy;
    WORD  life;
    WORD  active;
} Bullet;

/* Rock sizes */
#define ROCK_LARGE   0
#define ROCK_MEDIUM  1
#define ROCK_SMALL   2

#define MAX_ROCKS    24

typedef struct {
    Fixed x, y;
    Fixed dx, dy;
    WORD  size;       /* ROCK_LARGE/MEDIUM/SMALL */
    WORD  shape;      /* 0..3 shape variant */
    WORD  angle;      /* visual rotation */
    WORD  rot_speed;  /* rotation speed */
    WORD  active;
} Rock;

/* Particle (for explosions) */
#define MAX_PARTICLES 64

typedef struct {
    Fixed x, y;
    Fixed dx, dy;
    WORD  life;
    WORD  color;
    WORD  active;
} Particle;

/* Game state */
#define STATE_TITLE   0
#define STATE_PLAYING 1
#define STATE_DEAD    2
#define STATE_GAMEOVER 3

typedef struct {
    Ship       ship;
    Bullet     bullets[MAX_BULLETS];
    Rock       rocks[MAX_ROCKS];
    Particle   particles[MAX_PARTICLES];
    LONG       score;
    WORD       lives;
    WORD       level;
    WORD       state;
    WORD       state_timer;
    WORD       rock_count;     /* active rocks */
    WORD       frame;
} GameState;

/* Tunables (exposed to bridge) */
typedef struct {
    LONG  rock_speed;       /* base rock speed * 100 */
    LONG  ship_thrust;      /* thrust acceleration * 100 */
    LONG  bullet_speed;     /* bullet speed * 100 */
    LONG  start_lives;
    LONG  start_rocks;      /* rocks per level at start */
    LONG  rocks_per_level;  /* additional rocks per level */
} Tunables;

extern Tunables g_tune;

/* Functions */
void game_init_tables(void);
void game_init(GameState *gs);
void game_spawn_rocks(GameState *gs, WORD count);
void game_update(GameState *gs, WORD joy_left, WORD joy_right,
                 WORD joy_up, WORD joy_fire);
void game_spawn_particles(GameState *gs, Fixed x, Fixed y,
                          WORD count, WORD color);

/* Rock radius by size */
WORD rock_radius(WORD size);

/* Score values */
#define SCORE_LARGE  20
#define SCORE_MEDIUM 50
#define SCORE_SMALL  100

#endif
