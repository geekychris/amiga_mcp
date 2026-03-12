/*
 * Pea Shooter Blast - Game definitions
 * A Blaster Master homage for Amiga
 */
#ifndef GAME_H
#define GAME_H

#include <exec/types.h>

/* Screen dimensions */
#define SCREEN_W    320
#define SCREEN_H    256
#define BITMAP_W    352   /* oversized for smooth scrolling */
#define TILE_W      16
#define TILE_H      16
#define TILES_X     (BITMAP_W / TILE_W)  /* 22 tile columns in bitmap */
#define TILES_Y     (SCREEN_H / TILE_H)  /* 16 tile rows */
#define VIEW_TILES_X (SCREEN_W / TILE_W) /* 20 visible columns */

/* Map dimensions (in tiles) */
#define MAP_W       128   /* tiles wide per level */
#define MAP_H       16    /* tiles tall (one screen height) */

/* Fixed-point 16.16 */
typedef LONG Fixed;
#define FIX(x)       ((Fixed)((x) * 65536L))
#define FIXF(x)      ((Fixed)((x) * 65536.0f))
#define FIX_INT(x)   ((x) >> 16)
#define FIX_FRAC(x)  ((x) & 0xFFFF)
#define FIX_MUL(a,b) ((Fixed)(((LONG)(a) >> 8) * ((LONG)(b) >> 8)))

/* Tile types */
#define TILE_EMPTY      0
#define TILE_GROUND     1   /* solid ground */
#define TILE_BRICK      2   /* brick wall */
#define TILE_ROCK       3   /* rocky wall */
#define TILE_PLATFORM   4   /* one-way platform (land on top) */
#define TILE_LADDER     5   /* climbable */
#define TILE_SPIKES     6   /* damage */
#define TILE_PIPE_H     7   /* horizontal pipe (decoration) */
#define TILE_PIPE_V     8   /* vertical pipe */
#define TILE_DIRT       9   /* dirt fill */
#define TILE_METAL      10  /* metal panel */
#define TILE_GATE       11  /* level exit gate */
#define TILE_POWERUP    12  /* weapon powerup spawn point */
#define NUM_TILE_TYPES  13

/* Tile properties (bit flags) */
#define TPROP_SOLID     1   /* blocks movement */
#define TPROP_PLATFORM  2   /* solid from top only */
#define TPROP_DAMAGE    4   /* hurts player */
#define TPROP_LADDER    8   /* climbable */
#define TPROP_EXIT      16  /* level exit trigger */

extern UBYTE tile_props[NUM_TILE_TYPES];

/* Tank (player) */
#define TANK_W          16
#define TANK_H          14
#define TANK_SPEED      FIX(2)
#define TANK_JUMP_VEL   FIX(-4)
#define TANK_GRAVITY    (65536 / 4)  /* 0.25 in fixed */
#define TANK_MAX_FALL   FIX(5)
#define TANK_GUN_COOLDOWN 6  /* frames between shots */
#define MAX_HEALTH      8
#define INVULN_TIME     60   /* frames of invulnerability */

typedef struct {
    Fixed x, y;          /* position (top-left of sprite) */
    Fixed dx, dy;        /* velocity */
    WORD  on_ground;     /* standing on solid tile */
    WORD  facing;        /* 0=right, 1=left */
    WORD  alive;
    WORD  health;
    WORD  weapon_level;  /* 0..7 */
    WORD  gun_timer;     /* cooldown */
    WORD  invuln_timer;  /* invulnerability countdown */
    WORD  jump_held;     /* is jump button held? (for variable height) */
    WORD  anim_frame;
} Tank;

/* Bullet */
#define MAX_BULLETS     12
#define BULLET_BASE_SPEED FIX(5)
#define BULLET_LIFE     50  /* frames */

typedef struct {
    Fixed x, y;
    Fixed dx, dy;
    WORD  life;
    WORD  active;
    WORD  power;    /* damage (scales with weapon level) */
    WORD  size;     /* visual size: 0=small, 1=medium, 2=large */
} Bullet;

/* Enemy types */
#define ENEMY_WALKER    0   /* walks on platforms, turns at edges */
#define ENEMY_FLYER     1   /* flies in sine wave pattern */
#define ENEMY_TURRET    2   /* stationary, shoots at player */
#define ENEMY_HOPPER    3   /* hops toward player */
#define ENEMY_BOSS      4   /* level boss */

#define MAX_ENEMIES     16
#define MAX_ENEMY_BULLETS 8

typedef struct {
    Fixed x, y;
    Fixed dx, dy;
    WORD  type;
    WORD  health;
    WORD  active;
    WORD  facing;     /* 0=right, 1=left */
    WORD  state;      /* AI state */
    WORD  timer;      /* general-purpose timer */
    WORD  anim_frame;
    WORD  fire_timer;
} Enemy;

/* Enemy bullet */
typedef struct {
    Fixed x, y;
    Fixed dx, dy;
    WORD  life;
    WORD  active;
} EnemyBullet;

/* Particle (explosions) */
#define MAX_PARTICLES   32

typedef struct {
    Fixed x, y;
    Fixed dx, dy;
    WORD  life;
    WORD  color;
    WORD  active;
} Particle;

/* Powerup */
#define MAX_POWERUPS    4
#define POWERUP_WEAPON  0
#define POWERUP_HEALTH  1

typedef struct {
    Fixed x, y;
    WORD  type;
    WORD  active;
    WORD  anim_frame;
} Powerup;

/* Explosion flash */
#define MAX_EXPLOSIONS  8

typedef struct {
    Fixed x, y;
    WORD  radius;
    WORD  life;
    WORD  active;
} Explosion;

/* Game states */
#define STATE_TITLE     0
#define STATE_PLAYING   1
#define STATE_DEAD      2
#define STATE_GAMEOVER  3
#define STATE_LEVELCLEAR 4

/* Scroll state */
typedef struct {
    LONG  pixel_x;       /* current scroll position in pixels */
    LONG  target_x;      /* where we want to scroll to */
    WORD  tile_col;      /* leftmost visible tile column */
    WORD  fine_x;        /* pixel offset within tile (0..15) */
    WORD  last_col;      /* last column drawn for incremental update */
} ScrollState;

/* Game state */
typedef struct {
    Tank         tank;
    Bullet       bullets[MAX_BULLETS];
    Enemy        enemies[MAX_ENEMIES];
    EnemyBullet  enemy_bullets[MAX_ENEMY_BULLETS];
    Particle     particles[MAX_PARTICLES];
    Powerup      powerups[MAX_POWERUPS];
    Explosion    explosions[MAX_EXPLOSIONS];
    ScrollState  scroll;
    LONG         score;
    WORD         lives;
    WORD         level;     /* 0, 1, 2 */
    WORD         state;
    WORD         state_timer;
    WORD         frame;
} GameState;

/* Tunables (exposed via bridge) */
typedef struct {
    LONG  tank_speed;       /* * 100 */
    LONG  jump_power;       /* * 100 */
    LONG  gravity;          /* * 100 */
    LONG  bullet_speed;     /* * 100 */
    LONG  start_lives;
    LONG  enemy_speed;      /* * 100 */
} Tunables;

extern Tunables g_tune;

/* Level data */
extern UBYTE level_maps[3][MAP_H][MAP_W];

/* Core functions */
void game_init(GameState *gs);
void game_update(GameState *gs, WORD inp_left, WORD inp_right,
                 WORD inp_jump, WORD inp_fire);
void game_load_level(GameState *gs, WORD level_num);
void game_spawn_particles(GameState *gs, Fixed x, Fixed y,
                          WORD count, WORD color);
void game_spawn_explosion(GameState *gs, Fixed x, Fixed y, WORD radius);

/* SFX callbacks (defined in main.c) */
void sfx_shoot(void);
void sfx_hit(void);
void sfx_explode(void);
void sfx_powerup(void);
void sfx_jump(void);
void sfx_player_hit(void);

#endif
