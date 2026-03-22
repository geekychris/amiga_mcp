/*
 * DEFENDER for Amiga - Game definitions
 * All structs, constants, function declarations
 */
#ifndef GAME_H
#define GAME_H

#include <exec/types.h>

/* Forward declarations for types used in function prototypes */
struct RastPort;

/* Amiga sprintf returns char* not int */
char *sprintf(char *buf, const char *fmt, ...);

/* Screen constants */
#define SCREEN_W        320
#define SCREEN_H        256
#define SCREEN_DEPTH    4       /* 16 colors */

/* World dimensions */
#define WORLD_W         1920    /* 6 screens wide */

/* Layout */
#define SCANNER_H       18
#define SCANNER_SEP     2
#define PLAY_TOP        (SCANNER_H + SCANNER_SEP)
#define PLAY_BOT        234
#define HUD_Y           238

/* Fixed point 8.8 */
#define FP_SHIFT        8
#define FP_ONE          (1 << FP_SHIFT)
#define TO_FP(x)        ((LONG)(x) << FP_SHIFT)
#define FROM_FP(x)      ((LONG)(x) >> FP_SHIFT)

/* Ship constants */
#define SHIP_W          16
#define SHIP_H          7
#define SHIP_THRUST     160     /* acceleration per frame (FP) */
#define SHIP_MAX_VX     (TO_FP(12))
#define SHIP_MAX_VY     (TO_FP(8))
#define SHIP_DRAG       4       /* velocity *= (256-DRAG)/256 each frame */
#define SHIP_VDRAG      8
#define SHIP_SCREEN_X_R 90      /* screen X when facing right */
#define SHIP_SCREEN_X_L 230     /* screen X when facing left */

/* Entity limits */
#define MAX_ENEMIES         16
#define MAX_HUMANS          10
#define MAX_PLAYER_BULLETS  8
#define MAX_ENEMY_BULLETS   12
#define MAX_PARTICLES       40
#define MAX_EXPLOSIONS      8
#define MAX_MINES           12

/* Game states */
#define STATE_TITLE         0
#define STATE_PLAYING       1
#define STATE_DYING         2
#define STATE_RESPAWNING    3
#define STATE_LEVEL_START   4
#define STATE_LEVEL_CLEAR   5
#define STATE_GAMEOVER      6
#define STATE_HISCORE_ENTRY 7
#define STATE_HISCORE_VIEW  8

/* Enemy types */
#define ENT_NONE        0
#define ENT_LANDER      1
#define ENT_MUTANT      2
#define ENT_BAITER      3
#define ENT_BOMBER      4
#define ENT_POD         5
#define ENT_SWARMER     6

/* Lander states */
#define LANDER_SEEKING      0
#define LANDER_DESCENDING   1
#define LANDER_GRABBING     2
#define LANDER_ASCENDING    3

/* Human states */
#define HUMAN_WALKING   0
#define HUMAN_GRABBED   1
#define HUMAN_FALLING   2
#define HUMAN_DEAD      3

/* Input bits */
#define INPUT_LEFT      0x0001
#define INPUT_RIGHT     0x0002
#define INPUT_UP        0x0004
#define INPUT_DOWN      0x0008
#define INPUT_FIRE      0x0010
#define INPUT_BOMB      0x0020
#define INPUT_HYPER     0x0040
#define INPUT_QUIT      0x0080
#define INPUT_START     0x0100

/* Scoring */
#define SCORE_LANDER    150
#define SCORE_MUTANT    150
#define SCORE_BAITER    200
#define SCORE_BOMBER    250
#define SCORE_POD       1000
#define SCORE_SWARMER   150
#define SCORE_MINE      50
#define SCORE_HUMAN_CATCH   500
#define SCORE_HUMAN_LAND    250
#define SCORE_WAVE_BONUS    1000

/* Colors (16 color palette, 4 bitplanes) */
#define COL_BLACK       0
#define COL_WHITE       1
#define COL_STAR_DIM    2
#define COL_STAR_BLUE   2   /* shared with dim star */
#define COL_TERRAIN_DK  3
#define COL_TERRAIN     4
#define COL_TERRAIN_LT  5
#define COL_SHIP_BODY   6
#define COL_SHIP_WING   6   /* shared */
#define COL_THRUST      7
#define COL_LANDER      8
#define COL_LANDER2     8   /* shared */
#define COL_MUTANT      9
#define COL_LASER       1   /* white */
#define COL_LASER2      1
#define COL_BOMBER      10
#define COL_POD         9   /* shared with mutant */
#define COL_SWARMER     7   /* shared with thrust */
#define COL_HUMAN       1   /* white */
#define COL_HUMAN2      5   /* shared with terrain_lt */
#define COL_EXPL_RED    10  /* shared with bomber */
#define COL_EXPL_ORG    7   /* shared with thrust */
#define COL_EXPL_YEL    6   /* shared with ship */
#define COL_SCANNER_BG  3   /* shared with terrain_dk */
#define COL_SCANNER_BRD 2   /* shared with star_dim */
#define COL_HUD         11
#define COL_HUD2        11  /* shared */
#define COL_MINE        10  /* shared with bomber */
#define COL_BAITER      11  /* shared with HUD */
#define COL_FLASH       1   /* white */
#define COL_HYPER       11  /* shared */

/* Structures */

typedef struct {
    LONG wx, wy;            /* world position (FP 8.8) */
    LONG vx, vy;            /* velocity (FP 8.8) */
    WORD facing;            /* -1=left, +1=right */
    WORD alive;
    WORD invuln_timer;      /* invulnerability frames */
    WORD smart_bombs;
    WORD carried_human;     /* index into humans[] or -1 */
    WORD fire_cooldown;
    WORD thrust_on;         /* for flame animation */
} Ship;

typedef struct {
    LONG wx, wy;            /* world position (FP 8.8) */
    LONG vx, vy;            /* velocity (FP 8.8) */
    WORD active;
    WORD type;              /* ENT_* */
    WORD hp;
    WORD state;             /* type-specific state */
    WORD timer;             /* general purpose timer */
    WORD target_human;      /* for landers */
    WORD anim_frame;
} Enemy;

typedef struct {
    LONG wx, wy;            /* world position (FP 8.8) */
    WORD active;
    WORD state;             /* HUMAN_* */
    WORD grabbed_by;        /* enemy index or -1 */
    WORD walk_dir;          /* -1 or +1 */
    WORD walk_timer;
} Human;

typedef struct {
    LONG wx, wy;
    LONG vx;
    WORD active;
    WORD facing;            /* -1=left, +1=right */
} Bullet;

typedef struct {
    LONG wx, wy;
    WORD active;
} Mine;

typedef struct {
    WORD x, y;              /* screen coordinates */
    WORD vx, vy;
    WORD life;
    WORD color;
} Particle;

typedef struct {
    LONG wx, wy;
    WORD radius;
    WORD life;
    WORD active;
} Explosion;

typedef struct {
    Ship    ship;
    Enemy   enemies[MAX_ENEMIES];
    Human   humans[MAX_HUMANS];
    Bullet  player_bullets[MAX_PLAYER_BULLETS];
    Bullet  enemy_bullets[MAX_ENEMY_BULLETS];
    Mine    mines[MAX_MINES];
    Particle particles[MAX_PARTICLES];
    Explosion explosions[MAX_EXPLOSIONS];

    WORD    terrain_h[WORLD_W]; /* terrain Y at each world X pixel */

    LONG    viewport_x;        /* world X of left edge of screen (FP 8.8) */
    LONG    score;
    LONG    hiscore;
    WORD    lives;
    WORD    level;
    WORD    state;
    WORD    state_timer;
    WORD    frame;
    WORD    enemies_alive;
    WORD    humans_alive;
    WORD    planet_destroyed;   /* all humans dead = planet gone */
    WORD    baiter_timer;       /* counts down, spawns baiter at 0 */
    WORD    wave_enemies_total; /* total enemies to spawn this wave */

    /* Sound event flags */
    WORD    ev_laser;
    WORD    ev_explode;
    WORD    ev_explode_big;
    WORD    ev_pickup;
    WORD    ev_human_die;
    WORD    ev_bomb;
    WORD    ev_hyper;
    WORD    ev_die;
    WORD    ev_level;
} GameState;

/* Input state */
typedef struct {
    WORD keys_held[128];
} InputData;

/* High score table */
#define MAX_HISCORES    10
#define HISCORE_NAMELEN 8
#define HISCORE_FILE    "PROGDIR:defender.scores"

typedef struct {
    char name[HISCORE_NAMELEN];
    LONG score;
} HiScoreEntry;

typedef struct {
    HiScoreEntry entries[MAX_HISCORES];
    WORD count;
} ScoreTable;

/* --- Function declarations --- */

/* game.c */
void game_init(GameState *gs, WORD level);
void game_update(GameState *gs, WORD input);
void game_spawn_wave(GameState *gs);
void game_generate_terrain(GameState *gs, WORD seed);
LONG world_dist(LONG a, LONG b);
WORD world_to_screen_x(LONG wx, LONG viewport_x);

/* draw.c */
void draw_all(struct RastPort *rp, GameState *gs);
void draw_title(struct RastPort *rp, GameState *gs);
void draw_gameover(struct RastPort *rp, GameState *gs);
void draw_hiscore_table(struct RastPort *rp, ScoreTable *st);
void draw_hiscore_entry(struct RastPort *rp, GameState *gs, char *name, WORD cursor);
void draw_level_message(struct RastPort *rp, WORD level, const char *msg);

/* input.c */
void input_init(InputData *id);
void input_key_event(InputData *id, UWORD code);
WORD input_read(InputData *id);
void input_cleanup(void);

/* sound.c */
void sound_init(void);
void sound_cleanup(void);
WORD sound_load_mod(const char *filename);
void sound_start_music(void);
void sound_stop_music(void);
void sound_update(GameState *gs);
void sfx_laser(void);
void sfx_explode(void);
void sfx_explode_big(void);
void sfx_pickup(void);
void sfx_human_die(void);
void sfx_bomb(void);
void sfx_hyper(void);
void sfx_die(void);
void sfx_level(void);

/* score.c */
void score_init(ScoreTable *st);
void score_load(ScoreTable *st);
void score_save(ScoreTable *st);
WORD score_qualifies(ScoreTable *st, LONG score);
void score_insert(ScoreTable *st, WORD rank, const char *name, LONG score);

#endif /* GAME_H */
