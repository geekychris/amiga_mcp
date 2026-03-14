/*
 * RJ's 70th Birthday Bash
 * A tribute to RJ Mical
 *
 * game.h - Shared types and constants
 */
#ifndef GAME_H
#define GAME_H

#include <exec/types.h>
#include <graphics/rastport.h>

/* Screen */
#define SCREEN_W    320
#define SCREEN_H    256
#define DEPTH       4
#define NUM_COLORS  16

/* World */
#define ROOM_W      320
#define ROOM_COUNT  6
#define WORLD_W     (ROOM_W * ROOM_COUNT)

/* Limits */
#define MAX_GUESTS  8
#define MAX_ITEMS   16
#define MAX_NAMES   16
#define NAME_LEN    16

/* Game tuning */
#define START_LIVES     3
#define PARTY_FRAMES    (50 * 150)  /* 2.5 minutes at 50fps */
#define FLOOR_Y         210
#define WALK_Y_MIN      160
#define WALK_Y_MAX      235
#define HUD_H           16
#define PLAYER_W        12
#define PLAYER_H        24
#define GUEST_W         10
#define GUEST_H         16
#define PLAYER_SPEED    3

/* Rooms */
#define ROOM_FOYER      0
#define ROOM_AMSTERDAM  1
#define ROOM_TOKYO      2
#define ROOM_SILICON    3
#define ROOM_BAYAREA    4
#define ROOM_LIVING     5

/* Game states */
#define GS_TITLE        0
#define GS_PLAYING      1
#define GS_GAMEOVER     2
#define GS_CREDITS      3
#define GS_WIN          4
#define GS_HISCORE      5   /* high score table display */
#define GS_ENTER_NAME   6   /* entering name for high score */
#define GS_ADD_GUEST    7   /* adding name to guest list */
#define GS_HELP         8   /* how to play */
#define GS_GUEST_EDIT   9   /* guest list editor */
#define GS_JAIL         10  /* busted by pot police! */

/* High score table */
#define MAX_HISCORES    5
#define HISCORE_NAMELEN 12

/* Guest states */
#define GUEST_NONE      0
#define GUEST_ENTERING  1
#define GUEST_IDLE      2
#define GUEST_WANT      3
#define GUEST_HAPPY     4
#define GUEST_ANGRY     5
#define GUEST_LEAVING   6
#define GUEST_GREETED   7   /* just greeted by RJ */

/* Bubble text max length */
#define BUBBLE_LEN      20

/* Item types */
#define ITEM_NONE       0
#define ITEM_GIFT       1
#define ITEM_BROWNIE    2
#define ITEM_SUSHI_R    3
#define ITEM_SUSHI_B    4
#define ITEM_SUSHI_G    5
#define ITEM_FAVOR      6
#define ITEM_DEMO       7
#define ITEM_PLANT      8

/* Pot Police */
#define MAX_COPS        3
#define COP_W           10
#define COP_H           18
#define COP_SPEED       2

/* Smoke puff */
#define MAX_PUFFS       6
#define PUFF_LIFE       20

/* Input bits */
#define INP_LEFT    0x01
#define INP_RIGHT   0x02
#define INP_UP      0x04
#define INP_DOWN    0x08
#define INP_FIRE    0x10
#define INP_ESC     0x20

/* Directions */
#define DIR_LEFT    0
#define DIR_RIGHT   1

/* Palette indices */
#define COL_BG          0
#define COL_WHITE       1
#define COL_BROWN       2
#define COL_DKBROWN     3
#define COL_RED         4
#define COL_GREEN       5
#define COL_BLUE        6
#define COL_LTBLUE      7
#define COL_YELLOW      8
#define COL_ORANGE      9
#define COL_PINK        10
#define COL_LTGREEN     11
#define COL_TAN         12
#define COL_GREY        13
#define COL_DKRED       14
#define COL_BTYELLOW    15

/* Tapper lanes */
#define TAPPER_LANES    3
#define TAPPER_LANE_Y0  170
#define TAPPER_LANE_H   22

/* Sushi conveyor */
#define SUSHI_LANES     3
#define SUSHI_LANE_Y0   165
#define SUSHI_LANE_H    24

/* Smoke puff particle */
typedef struct {
    WORD x, y;          /* world coords */
    WORD vx, vy;
    WORD life;          /* frames remaining */
    WORD size;          /* radius */
} SmokePuff;

/* Pot Police officer */
typedef struct {
    WORD active;
    WORD x, y;          /* world coords */
    WORD dir;           /* DIR_LEFT or DIR_RIGHT */
    WORD frame;
    WORD anim_tick;
    WORD patrol_min;    /* left patrol bound (world x) */
    WORD patrol_max;    /* right patrol bound (world x) */
    WORD chase;         /* 1 = chasing RJ */
    WORD speed;
    WORD cooldown;      /* invulnerability after hit */
} PotCop;

/* Easter egg strings */
#define EGG_INTUITION  "I made Intuition!"
#define EGG_CRUNCH     "Crunch neck!"
#define EGG_JOEPILLOW  "Joe Pillow on the plane!"

typedef struct {
    WORD x, y;          /* world coords */
    WORD dir;
    WORD frame;
    WORD anim_tick;
    WORD carrying;      /* item type */
    WORD speed;
    WORD lane;          /* for Tapper: 0-2 */
} Player;

typedef struct {
    WORD active;
    WORD state;
    WORD x, y;          /* world coords */
    WORD room;
    WORD name_idx;
    WORD want;          /* item type wanted */
    WORD patience;      /* frames until angry */
    WORD patience_max;
    WORD color;         /* palette index */
    WORD lane;          /* for lane-based rooms */
    WORD timer;
    WORD target_x;
    WORD bubble_timer;  /* speech bubble countdown */
    WORD greeted;       /* has RJ greeted this guest? */
    char bubble_text[BUBBLE_LEN]; /* text to show in speech bubble */
} Guest;

typedef struct {
    WORD active;
    WORD type;
    WORD x, y;          /* world coords */
    WORD vx, vy;
    WORD room;
    WORD timer;
} Item;

typedef struct {
    Player player;
    Guest guests[MAX_GUESTS];
    Item items[MAX_ITEMS];

    LONG score;
    WORD lives;
    WORD party_clock;   /* frames elapsed */
    WORD state;         /* GS_* */

    WORD camera_x;
    WORD camera_target;
    WORD current_room;

    WORD guest_timer;   /* spawn timer */
    WORD wave;          /* difficulty */

    char names[MAX_NAMES][NAME_LEN];
    WORD name_count;
    WORD next_name;     /* round-robin */

    WORD happiness;     /* 0-100 */
    WORD flash_timer;
    WORD msg_timer;
    char msg[40];

    WORD tapper_active;
    WORD finale;
    WORD egg_timer;     /* easter egg display timer */
    char egg_text[40];  /* current easter egg text */

    WORD rj_bubble_timer; /* RJ speech bubble timer */
    char rj_bubble[BUBBLE_LEN]; /* RJ speech bubble text */
    WORD arcade_timer;  /* arcade play effect timer */
    WORD arcade_which;  /* 0=sinistar, 1=red baron */

    SmokePuff puffs[MAX_PUFFS];  /* smoke puff particles */
    PotCop cops[MAX_COPS];       /* pot police officers */
    WORD cop_spawn_timer;        /* time until next cop spawns */
    WORD cop_hit_cooldown;       /* player invulnerability after cop hit */
    WORD plants_collected;       /* total plants picked up (attracts cops) */
    WORD jail_timer;             /* frames left in jail */

    WORD title_blink;   /* title screen blink timer */
    WORD credits_scroll;/* credits Y offset */

    /* High score table */
    char hi_names[MAX_HISCORES][HISCORE_NAMELEN];
    LONG hi_scores[MAX_HISCORES];
    WORD hi_count;

    /* Name entry */
    char entry_name[HISCORE_NAMELEN];
    WORD entry_pos;     /* cursor position */
    WORD entry_char;    /* current character index */
    WORD entry_mode;    /* 0=hiscore entry, 1=add guest */
    WORD entry_blink;   /* cursor blink timer */

    /* Guest list editor */
    WORD edit_cursor;   /* selected name index */
    WORD edit_scroll;   /* scroll offset for long lists */
    WORD edit_return_state; /* state to return to on ESC */
    WORD help_scroll;   /* help page scroll */
} GameState;

typedef struct {
    UWORD bits;
    UWORD fire_edge;    /* fire pressed this frame */
    UWORD prev_fire;
    UBYTE last_char;    /* last typed character (for name entry) */
    UBYTE key_up;       /* up arrow edge */
    UBYTE key_down;     /* down arrow edge */
    UBYTE key_return;   /* return key edge */
    UBYTE key_backspace;/* backspace edge */
    UBYTE key_delete;   /* delete edge */
} InputState;

/* Sine table for effects (256 entries, amplitude 127) */
extern BYTE sin_table[256];

/* Functions - game.c */
void game_init(GameState *gs);
void game_update(GameState *gs, InputState *inp);
void game_load_names(GameState *gs, const char *filename);
void game_save_names(GameState *gs, const char *filename);
void game_load_hiscores(GameState *gs, const char *filename);
void game_save_hiscores(GameState *gs, const char *filename);
WORD game_check_hiscore(GameState *gs, LONG score);
void game_insert_hiscore(GameState *gs, WORD slot, const char *name, LONG score);
void game_set_message(GameState *gs, const char *text, WORD duration);
void game_init_tables(void);

/* Functions - rooms.c */
void rooms_draw_bg(struct RastPort *rp, GameState *gs);
void rooms_draw_details(struct RastPort *rp, GameState *gs);

/* Functions - draw.c */
void draw_clear(struct RastPort *rp);
void draw_player(struct RastPort *rp, GameState *gs);
void draw_guests(struct RastPort *rp, GameState *gs);
void draw_items(struct RastPort *rp, GameState *gs);
void draw_hud(struct RastPort *rp, GameState *gs);
void draw_title(struct RastPort *rp, GameState *gs);
void draw_credits(struct RastPort *rp, GameState *gs);
void draw_gameover(struct RastPort *rp, GameState *gs);
void draw_win(struct RastPort *rp, GameState *gs);
void draw_hiscore(struct RastPort *rp, GameState *gs);
void draw_enter_name(struct RastPort *rp, GameState *gs);
void draw_message(struct RastPort *rp, GameState *gs);
void draw_puffs(struct RastPort *rp, GameState *gs);
void draw_cops(struct RastPort *rp, GameState *gs);
void draw_help(struct RastPort *rp, GameState *gs);
void draw_guest_edit(struct RastPort *rp, GameState *gs);
void draw_jail(struct RastPort *rp, GameState *gs);
void draw_number(struct RastPort *rp, WORD x, WORD y, LONG num);
void draw_text(struct RastPort *rp, WORD x, WORD y, const char *str);

/* Functions - input.c */
void input_init(void);
void input_read(InputState *inp, struct Window *win);

/* Functions - sound.c */
void sound_init(void);
void sound_cleanup(void);
void sfx_doorbell(void);
void sfx_slide(void);
void sfx_chomp(void);
void sfx_ding(void);
void sfx_buzzer(void);
void sfx_cheer(void);
void sfx_pickup(void);
void sfx_crash(void);
void sfx_party(void);
void sfx_sinistar(void);
void sinistar_load_voices(void);
void sinistar_cleanup_voices(void);
void sinistar_play_random(void);  /* pause music, play random voice clip */
WORD sinistar_is_playing(void);   /* returns 1 if voice clip still playing */
void sinistar_check_done(void);   /* call each frame to resume music when done */

#endif
