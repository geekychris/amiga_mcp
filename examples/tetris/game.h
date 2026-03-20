#ifndef GAME_H
#define GAME_H

#include <exec/types.h>

#define FIELD_W 10
#define FIELD_H 20
#define NUM_PIECES 7

/* Piece types */
#define PIECE_I 0
#define PIECE_O 1
#define PIECE_T 2
#define PIECE_S 3
#define PIECE_Z 4
#define PIECE_L 5
#define PIECE_J 6

/* Cell values: 0=empty, 1-7=piece color (matches piece type+1) */

/* Game states */
#define STATE_TITLE   0
#define STATE_PLAYING 1
#define STATE_PAUSED  2
#define STATE_GAMEOVER 3

/* Piece data: 4x4 grid, 4 rotations per piece */
typedef struct {
    UBYTE cells[4][4];  /* [row][col], nonzero = filled */
} PieceShape;

typedef struct {
    int type;       /* PIECE_I..PIECE_J */
    int rotation;   /* 0-3 */
    int x, y;       /* position in field (top-left of 4x4 bounding box) */
} ActivePiece;

typedef struct GameState {
    UBYTE field[FIELD_H][FIELD_W];  /* The playfield grid */
    ActivePiece current;
    int next_type;        /* Next piece type */
    int state;
    ULONG score;
    int level;
    int lines;
    int drop_timer;       /* Counts down to auto-drop */
    int drop_speed;       /* Frames between auto-drops */
    int lock_timer;       /* Delay before locking piece */
    int clear_timer;      /* Animation timer for line clearing */
    int clear_lines[4];   /* Which lines are being cleared (-1 = none) */
    int clear_count;      /* Number of lines being cleared */
    int das_timer;        /* Delayed auto-shift timer */
    int das_dir;          /* -1=left, 0=none, 1=right */
    BOOL just_locked;     /* Flag for SFX trigger */
    BOOL just_cleared;    /* Flag for SFX trigger */
    BOOL just_dropped;    /* Flag for hard drop SFX */
    ULONG rng_state;      /* Random number state */
} GameState;

/* Get the shape data for a piece type and rotation */
const PieceShape *piece_get_shape(int type, int rotation);

void game_init(GameState *gs);
void game_start(GameState *gs);

/* Input flags for game_update */
#define GINPUT_LEFT   0x01
#define GINPUT_RIGHT  0x02
#define GINPUT_DOWN   0x04  /* Soft drop */
#define GINPUT_DROP   0x08  /* Hard drop */
#define GINPUT_ROTATE 0x10  /* Rotate clockwise */
#define GINPUT_START  0x20  /* Start/pause */

void game_update(GameState *gs, int input);

/* Get ghost piece Y position (where piece would land) */
int game_ghost_y(const GameState *gs);

#endif
