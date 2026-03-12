/*
 * PACBRO - Pac-Man clone for Amiga
 * Game definitions and structures
 */
#ifndef GAME_H
#define GAME_H

#include <exec/types.h>

/* Screen dimensions */
#define SCREEN_W    320
#define SCREEN_H    256

/* Maze dimensions: 28x31 tiles of 8x8 pixels = 224x248 */
#define MAZE_W      28
#define MAZE_H      31
#define TILE_SIZE   8

/* Maze offset to center on screen */
#define MAZE_OX     48
#define MAZE_OY     4

/* Tile types in maze */
#define T_EMPTY     0
#define T_WALL      1
#define T_DOT       2
#define T_POWER     3
#define T_GHOST_DOOR 4

/* Directions */
#define DIR_NONE    -1
#define DIR_RIGHT   0
#define DIR_DOWN    1
#define DIR_LEFT    2
#define DIR_UP      3

/* Ghost states */
#define GHOST_SCATTER   0
#define GHOST_CHASE     1
#define GHOST_FRIGHT    2
#define GHOST_EATEN     3

/* Ghost IDs */
#define GHOST_BLINKY    0
#define GHOST_PINKY     1
#define GHOST_INKY      2
#define GHOST_CLYDE     3
#define NUM_GHOSTS      4

/* Game states */
#define STATE_TITLE      0
#define STATE_READY      1
#define STATE_PLAYING    2
#define STATE_DYING      3
#define STATE_GAMEOVER   4
#define STATE_LEVEL_DONE 5
#define STATE_EAT_GHOST  6

/* Speed: pixels per frame (fixed 8.8) */
#define PACMAN_SPEED     320   /* ~1.25 px/frame at 50fps */
#define GHOST_SPEED      288   /* slightly slower */
#define GHOST_FRIGHT_SPD 160   /* slow when frightened */
#define GHOST_EATEN_SPD  640   /* fast returning to ghost house */
#define GHOST_TUNNEL_SPD 160   /* slow in tunnel */

/* Timing (frames at 50fps) */
#define READY_TIME       100   /* 2 seconds */
#define DYING_TIME       60
#define EAT_GHOST_TIME   30
#define LEVEL_DONE_TIME  100
#define FRIGHT_TIME      350   /* 7 seconds */
#define FRIGHT_BLINK     200   /* start blinking at this remaining */

/* Scatter/Chase mode durations (in frames) */
#define SCATTER1_TIME    350   /* 7s */
#define CHASE1_TIME      1000  /* 20s */
#define SCATTER2_TIME    350
#define CHASE2_TIME      1000
#define SCATTER3_TIME    250   /* 5s */
#define CHASE3_TIME      1000
#define SCATTER4_TIME    250
/* After scatter4: permanent chase */

/* Scoring */
#define SCORE_DOT        10
#define SCORE_POWER      50
#define SCORE_GHOST1     200
#define SCORE_GHOST2     400
#define SCORE_GHOST3     800
#define SCORE_GHOST4     1600
#define SCORE_FRUIT_BASE 100
#define EXTRA_LIFE_SCORE 10000

/* Fruit types */
#define FRUIT_CHERRY     0
#define FRUIT_STRAWBERRY 1
#define FRUIT_ORANGE     2
#define FRUIT_APPLE      3
#define FRUIT_GRAPE      4
#define FRUIT_BELL       5
#define FRUIT_KEY        6
#define MAX_FRUIT_TYPE   7

/* Fruit point values */
static const LONG fruit_scores[MAX_FRUIT_TYPE] = {
    100, 300, 500, 700, 1000, 3000, 5000
};

/* Sound event flags */
#define EV_CHOMP     1
#define EV_EAT_GHOST 2
#define EV_DIE       4
#define EV_FRUIT     8
#define EV_EXTRA     16
#define EV_POWER     32

/* Colors */
#define COL_BLACK    0
#define COL_YELLOW   1
#define COL_RED      2
#define COL_PINK     3
#define COL_CYAN     4
#define COL_ORANGE   5
#define COL_BLUE     6
#define COL_WHITE    7
#define COL_DKBLUE   8
#define COL_PEACH    9
#define COL_DKPINK   10
#define COL_DKRED    11
#define COL_GREEN    12
#define COL_PURPLE   13
#define COL_GRAY     14
#define COL_CREAM    15

/* Ghost structure */
typedef struct {
    WORD  tile_x, tile_y;    /* current tile position */
    WORD  pixel_x, pixel_y;  /* pixel position (top-left of sprite) */
    WORD  sub_x, sub_y;      /* fractional sub-pixel (8.8 fixed) */
    WORD  dir;               /* current direction */
    WORD  next_dir;          /* next direction at next intersection */
    WORD  state;             /* GHOST_SCATTER/CHASE/FRIGHT/EATEN */
    WORD  target_x, target_y; /* target tile for AI */
    WORD  home;              /* 1 if still in ghost house */
    WORD  dot_counter;       /* dots needed to leave house */
    WORD  fright_timer;      /* remaining fright frames */
} Ghost;

/* Pac-Man structure */
typedef struct {
    WORD  tile_x, tile_y;
    WORD  pixel_x, pixel_y;
    WORD  sub_x, sub_y;
    WORD  dir;
    WORD  next_dir;          /* buffered direction input */
    WORD  anim_frame;
    WORD  anim_timer;
    WORD  mouth_open;        /* for chomp animation */
} PacMan;

/* Input state */
typedef struct {
    WORD  dir;               /* direction input: DIR_* or DIR_NONE */
    WORD  start;             /* start button pressed */
    WORD  quit;              /* ESC pressed */
} InputState;

/* Game state */
typedef struct {
    UBYTE maze[MAZE_H][MAZE_W]; /* current maze state */
    PacMan pac;
    Ghost  ghosts[NUM_GHOSTS];

    LONG   score;
    LONG   hiscore;
    LONG   score_p2;         /* player 2 score */
    WORD   lives;
    WORD   level;
    WORD   state;
    WORD   state_timer;
    WORD   frame;

    /* Dot tracking */
    WORD   dots_total;
    WORD   dots_eaten;
    WORD   dots_eaten_global; /* for ghost house dot counter */

    /* Scatter/Chase mode */
    WORD   mode;             /* 0=scatter, 1=chase */
    LONG   mode_timer;
    WORD   mode_phase;       /* which scatter/chase phase we're in */

    /* Fright mode */
    WORD   fright_active;
    WORD   fright_timer;
    WORD   ghosts_eaten;     /* count during current fright for scoring */

    /* Fruit */
    WORD   fruit_active;
    WORD   fruit_timer;
    WORD   fruit_type;
    WORD   fruit_x, fruit_y; /* pixel position */

    /* Score popup */
    WORD   popup_active;
    WORD   popup_timer;
    WORD   popup_x, popup_y;
    LONG   popup_score;

    /* Multiplayer */
    WORD   num_players;      /* 1 or 2 */
    WORD   current_player;   /* 0 or 1 */

    /* Extra life tracking */
    WORD   extra_life_given;

    /* Sound events */
    WORD   ev_flags;

    /* Siren speed (increases as dots eaten) */
    WORD   siren_speed;

    /* Title screen */
    WORD   title_blink;
} GameState;

/* Functions */
void game_init(GameState *gs);
void game_start_level(GameState *gs);
void game_start_life(GameState *gs);
void game_update(GameState *gs, InputState *input);

/* Maze data (defined in game.c) */
extern const UBYTE maze_template[MAZE_H][MAZE_W];

/* Scatter corner targets for each ghost */
static const WORD scatter_target[NUM_GHOSTS][2] = {
    { 25, -3 },  /* Blinky: top-right */
    {  2, -3 },  /* Pinky: top-left */
    { 27, 34 },  /* Inky: bottom-right */
    {  0, 34 },  /* Clyde: bottom-left */
};

#endif
