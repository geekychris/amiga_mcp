/*
 * Jump Quest - Game Header
 * All shared defines, types, and externs
 */
#ifndef GAME_H
#define GAME_H

#include <exec/types.h>
#include <exec/memory.h>
#include <intuition/intuition.h>
#include <intuition/screens.h>
#include <graphics/gfx.h>
#include <graphics/gfxbase.h>
#include <graphics/rastport.h>
#include <graphics/view.h>
#include <hardware/cia.h>
#include <hardware/custom.h>
#include <hardware/dmabits.h>

#include <proto/exec.h>
#include <proto/intuition.h>
#include <proto/graphics.h>
#include <proto/dos.h>

/* Screen */
#define SCREEN_W        320
#define SCREEN_H        256
#define SCREEN_DEPTH    4
#define SCREEN_COLORS   16
#define TILE_SIZE       16
#define TILES_X         20
#define TILES_Y         14
#define HUD_Y           224
#define HUD_H           32

/* Physics (fixed-point 10.6) */
#define FP_SHIFT        6
#define FP(x)           ((x) << FP_SHIFT)
#define FP_INT(x)       ((x) >> FP_SHIFT)
#define GRAVITY         6
#define JUMP_FORCE      (-78)
#define MOVE_SPEED      FP(2)
#define MAX_FALL        FP(6)

/* Player dimensions */
#define PLAYER_W        14
#define PLAYER_H        28
#define PLAYER_H_SHORT  24  /* Dale */

/* Max entities */
#define MAX_ENEMIES     16
#define MAX_ITEMS       24
#define MAX_PARTICLES   16

/* Tile types */
#define TILE_EMPTY      0
#define TILE_GROUND     1
#define TILE_GRASS      2
#define TILE_BRICK      3
#define TILE_QBLOCK     4
#define TILE_STONE      5
#define TILE_PIPE_TL    6
#define TILE_PIPE_TR    7
#define TILE_PIPE_BL    8
#define TILE_PIPE_BR    9
#define TILE_PLATFORM   10
#define TILE_CLOUD_L    11
#define TILE_CLOUD_R    12
#define TILE_QBLOCK_HIT 13
#define TILE_FLAG       14
#define TILE_BUSH       15

/* Entity types */
#define ENT_NONE        0
#define ENT_PLAYER_START 1
#define ENT_BEETLE      2
#define ENT_FLY         3
#define ENT_SPIDER      4
#define ENT_ANT         5
#define ENT_BOARD       10
#define ENT_DISKETTE    11
#define ENT_BALL        12
#define ENT_PILLOW_C    13
#define ENT_PLANT       14
#define ENT_BRIEFCASE   20
#define ENT_HEART       21
#define ENT_PILLOW_P    22
#define ENT_BEER        23
#define ENT_WINE        24
#define ENT_LEVEL_END   30

/* Game states */
#define STATE_TITLE     0
#define STATE_PLAYING   1
#define STATE_DYING     2
#define STATE_LEVELWIN  3
#define STATE_GAMEOVER  4
#define STATE_NEXTLEVEL 5

/* Character select */
#define CHAR_RJ         0
#define CHAR_DALE       1

/* Palette indices */
#define COL_SKY         0
#define COL_SKY2        1
#define COL_BROWN       2
#define COL_DKBROWN     3
#define COL_GREEN       4
#define COL_DKGREEN     5
#define COL_ORANGE      6
#define COL_DKORANGE    7
#define COL_SKIN        8
#define COL_GREY        9
#define COL_BLUE        10
#define COL_WHITE       11
#define COL_BLACK       12
#define COL_YELLOW      13
#define COL_RED         14
#define COL_LTGREEN     15

/* Input bits */
#define INP_LEFT        0x01
#define INP_RIGHT       0x02
#define INP_UP          0x04
#define INP_DOWN        0x08
#define INP_JUMP        0x10
#define INP_START       0x20

/* Direction */
#define DIR_RIGHT       1
#define DIR_LEFT        (-1)

/* Entity spawn definition */
typedef struct {
    UBYTE type;
    UWORD tx, ty;   /* tile coordinates */
} EntitySpawn;

/* Enemy state */
typedef struct {
    BYTE  type;
    WORD  x, y;      /* pixel position (world coords) */
    WORD  vx, vy;    /* velocity (fixed-point) */
    BYTE  dir;
    BYTE  alive;
    BYTE  frame;
    BYTE  timer;
} Enemy;

/* Item state */
typedef struct {
    BYTE  type;
    WORD  x, y;      /* pixel position */
    BYTE  active;
    BYTE  frame;
    BYTE  collected;
    BYTE  float_off;  /* bobbing offset */
} Item;

/* Particle (for brick break, item collect effects) */
typedef struct {
    WORD  x, y;
    WORD  vx, vy;
    BYTE  life;
    BYTE  color;
} Particle;

/* Level definition */
typedef struct {
    UWORD width;     /* in tiles */
    UWORD height;    /* in tiles (always 14) */
    const UBYTE *tiles;
    const EntitySpawn *entities;
    UWORD bg_color1;  /* sky gradient top */
    UWORD bg_color2;  /* sky gradient bottom */
    const char *name;
} LevelDef;

/* Player state */
typedef struct {
    WORD  x, y;       /* pixel position (world coords) */
    WORD  vx, vy;     /* velocity (fixed-point) */
    BYTE  dir;        /* facing direction */
    BYTE  on_ground;
    BYTE  character;  /* CHAR_RJ or CHAR_DALE */
    BYTE  health;     /* current hearts */
    BYTE  max_health;
    BYTE  lives;
    LONG  score;
    BYTE  state;      /* 0=idle, 1=walk, 2=jump, 3=fall */
    BYTE  frame;      /* animation frame */
    BYTE  frame_timer;
    BYTE  invuln;     /* invulnerability timer */
    BYTE  speed_boost; /* speed boost timer */
    BYTE  walk_frame;
} Player;

/* Game state */
typedef struct {
    BYTE  state;
    BYTE  current_level;
    BYTE  num_players;  /* 1 or 2 */
    BYTE  active_player; /* 0 or 1 in 2P mode */
    WORD  cam_x;       /* camera x in world pixels */
    UWORD transition;  /* state transition timer */
    Player player[2];  /* RJ and Dale */
    Enemy enemies[MAX_ENEMIES];
    Item  items[MAX_ITEMS];
    Particle particles[MAX_PARTICLES];
} Game;

/* Globals */
extern Game game;
extern struct GfxBase *GfxBase;
extern struct IntuitionBase *IntuitionBase;
extern struct Screen *gameScreen;

/* gfx.c */
int  gfx_init(void);
void gfx_cleanup(void);
void gfx_swap(void);
struct RastPort *gfx_backbuffer(void);
void gfx_draw_tile(struct RastPort *rp, int tile_id, int sx, int sy);
void gfx_clear_area(struct RastPort *rp, int x, int y, int w, int h, int color);

/* input.c */
void input_init(void);
UWORD input_read(void);
BOOL input_check_esc(void);

/* player.c */
void player_init(Player *p, int character, int start_x, int start_y);
void player_update(Player *p, UWORD input);
void player_draw(struct RastPort *rp, Player *p, int cam_x);

/* level.c */
void level_load(int level_num);
void level_draw(struct RastPort *rp, int cam_x);
UBYTE level_get_tile(int tx, int ty);
void level_set_tile(int tx, int ty, UBYTE tile);
BOOL level_is_solid(int tx, int ty);
BOOL level_is_platform(int tx, int ty);
int  level_width_pixels(void);
const LevelDef *level_current(void);

/* enemy.c */
void enemies_init(void);
void enemies_spawn(const EntitySpawn *spawns);
void enemies_update(void);
void enemies_draw(struct RastPort *rp, int cam_x);
int  enemies_check_stomp(int px, int py, int pw, int ph);
int  enemies_check_touch(int px, int py, int pw, int ph);

/* items.c */
void items_init(void);
void items_spawn(const EntitySpawn *spawns);
void items_update(void);
void items_draw(struct RastPort *rp, int cam_x);
int  items_check_collect(int px, int py, int pw, int ph);

/* hud.c */
void hud_draw(struct RastPort *rp, Player *p, int level);

/* title.c */
int  title_screen(struct RastPort *rp);

/* sound.c */
int  sound_init(void);
void sound_cleanup(void);
void sound_music_start(void);
void sound_music_stop(void);
void sound_music_tick(void);
void sound_jump(void);
void sound_stomp(void);
void sound_collect(void);
void sound_powerup(void);
void sound_hurt(void);
void sound_die(void);
void sound_levelwin(void);

/* particles */
void particles_init(void);
void particles_spawn(int x, int y, int count, int color);
void particles_update(void);
void particles_draw(struct RastPort *rp, int cam_x);

#endif /* GAME_H */
