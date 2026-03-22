/*
 * game.h - Game types and logic for Ace Pilot
 */
#ifndef GAME_H
#define GAME_H

#include <exec/types.h>
#include "engine3d.h"
#include "models.h"

/* Screen */
#define SCREEN_W  320
#define SCREEN_H  256

/* Limits */
#define MAX_ENEMIES     8
#define MAX_GROUND      16
#define MAX_BULLETS     16
#define MAX_PARTICLES   32

/* World bounds */
#define WORLD_SIZE      4000
#define WORLD_HALF      2000
#define GROUND_Y        0
#define MIN_ALT         50
#define MAX_ALT         800

/* Default player forward speed */
#define BASE_SPEED      8

/* AI states */
#define AI_PATROL       0
#define AI_ATTACK       1
#define AI_EVADE        2

/* Object types for color lookup */
#define OBJ_ENEMY       0
#define OBJ_BLIMP       1
#define OBJ_GROUND      2
#define OBJ_GRID        3
#define OBJ_BULLET_P    4
#define OBJ_BULLET_E    5
#define OBJ_HUD         6

/* Game states */
#define STATE_TITLE     0
#define STATE_PLAYING   1
#define STATE_DYING     2
#define STATE_GAMEOVER  3

/* Display modes */
#define DISPLAY_CLASSIC 0
#define DISPLAY_COLOR   1

/* Score values */
#define SCORE_PLANE     100
#define SCORE_BLIMP     500
#define SCORE_HANGAR    200
#define SCORE_FUEL      150
#define SCORE_PYRAMID   100
#define SCORE_TREE      50

/* Enemy plane */
typedef struct {
    Vec3 pos;
    WORD yaw;
    WORD pitch;
    WORD speed;
    WORD ai_state;
    WORD ai_timer;
    WORD fire_cooldown;
    WORD health;
    WORD model_id;     /* MODEL_BIPLANE or MODEL_BLIMP */
    WORD active;
} Enemy;

/* Ground target */
typedef struct {
    Vec3 pos;
    WORD yaw;
    WORD model_id;
    WORD score_value;
    WORD active;
} GroundTarget;

/* 3D bullet */
typedef struct {
    Vec3 pos;
    LONG vx, vy, vz;
    WORD life;
    WORD owner;    /* 0=player1, 1=player2, 2=enemy */
    WORD active;
} Bullet;

/* Explosion particle */
typedef struct {
    Vec3 pos;
    LONG vx, vy, vz;
    WORD life;
    WORD active;
} Particle;

/* Per-player state */
typedef struct {
    Camera cam;
    LONG score;
    WORD lives;
    WORD alive;
    WORD respawn_timer;
    WORD fire_cooldown;
    WORD gun_side;     /* alternates -1/1 for left/right wing gun */
    WORD speed;
    WORD invuln;
} PlayerState;

/* Tunables (exposed to bridge debug vars) */
typedef struct {
    LONG enemy_speed;
    LONG enemy_accuracy;
    LONG enemy_fire_rate;
    LONG spawn_rate;
    LONG difficulty;
    LONG start_lives;
    LONG extra_life_score;
    LONG player_speed;
    LONG display_mode;
    LONG god_mode;
    LONG invert_pitch;
    LONG num_players;
} Tunables;

extern Tunables g_tune;

/* Game world state */
typedef struct {
    PlayerState players[2];
    Enemy       enemies[MAX_ENEMIES];
    GroundTarget ground[MAX_GROUND];
    Bullet      bullets[MAX_BULLETS];
    Particle    particles[MAX_PARTICLES];
    WORD        wave;
    WORD        enemies_alive;
    WORD        enemies_to_spawn;
    WORD        spawn_timer;
    WORD        state;
    WORD        state_timer;
    WORD        frame;
} GameWorld;

/* Functions */
void game_init(GameWorld *w);
void game_update(GameWorld *w, UWORD inp1, UWORD inp2);
void game_spawn_wave(GameWorld *w);
void game_setup_ground(GameWorld *w);

/* Get render color for object type based on display mode */
WORD get_obj_color(WORD obj_type, LONG depth);

/* SFX callbacks (defined in main.c/sound.c) */
extern void sfx_gunfire(void);
extern void sfx_explosion(void);
extern void sfx_hit(void);
extern void sfx_die(void);

#endif
