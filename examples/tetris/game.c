/*
 * StakAttack - Tetris game logic
 * Pure game state management, no rendering or input reading.
 */

#include "game.h"

/* ------------------------------------------------------------------ */
/* Piece shape data: 7 pieces x 4 rotations                          */
/* Cell values = piece type + 1, so each piece has a unique color 1-7 */
/* ------------------------------------------------------------------ */

static const PieceShape piece_shapes[NUM_PIECES][4] = {
    /* PIECE_I (color 1) */
    {
        /* Rotation 0 */
        {{{ 0,0,0,0 },
          { 1,1,1,1 },
          { 0,0,0,0 },
          { 0,0,0,0 }}},
        /* Rotation 1 */
        {{{ 0,0,1,0 },
          { 0,0,1,0 },
          { 0,0,1,0 },
          { 0,0,1,0 }}},
        /* Rotation 2 */
        {{{ 0,0,0,0 },
          { 0,0,0,0 },
          { 1,1,1,1 },
          { 0,0,0,0 }}},
        /* Rotation 3 */
        {{{ 0,1,0,0 },
          { 0,1,0,0 },
          { 0,1,0,0 },
          { 0,1,0,0 }}}
    },
    /* PIECE_O (color 2) */
    {
        /* All 4 rotations identical */
        {{{ 0,2,2,0 },
          { 0,2,2,0 },
          { 0,0,0,0 },
          { 0,0,0,0 }}},
        {{{ 0,2,2,0 },
          { 0,2,2,0 },
          { 0,0,0,0 },
          { 0,0,0,0 }}},
        {{{ 0,2,2,0 },
          { 0,2,2,0 },
          { 0,0,0,0 },
          { 0,0,0,0 }}},
        {{{ 0,2,2,0 },
          { 0,2,2,0 },
          { 0,0,0,0 },
          { 0,0,0,0 }}}
    },
    /* PIECE_T (color 3) */
    {
        /* Rotation 0: flat, point up */
        {{{ 0,3,0,0 },
          { 3,3,3,0 },
          { 0,0,0,0 },
          { 0,0,0,0 }}},
        /* Rotation 1: point right */
        {{{ 0,3,0,0 },
          { 0,3,3,0 },
          { 0,3,0,0 },
          { 0,0,0,0 }}},
        /* Rotation 2: point down */
        {{{ 0,0,0,0 },
          { 3,3,3,0 },
          { 0,3,0,0 },
          { 0,0,0,0 }}},
        /* Rotation 3: point left */
        {{{ 0,3,0,0 },
          { 3,3,0,0 },
          { 0,3,0,0 },
          { 0,0,0,0 }}}
    },
    /* PIECE_S (color 4) */
    {
        /* Rotation 0 */
        {{{ 0,4,4,0 },
          { 4,4,0,0 },
          { 0,0,0,0 },
          { 0,0,0,0 }}},
        /* Rotation 1 */
        {{{ 0,4,0,0 },
          { 0,4,4,0 },
          { 0,0,4,0 },
          { 0,0,0,0 }}},
        /* Rotation 2 */
        {{{ 0,0,0,0 },
          { 0,4,4,0 },
          { 4,4,0,0 },
          { 0,0,0,0 }}},
        /* Rotation 3 */
        {{{ 4,0,0,0 },
          { 4,4,0,0 },
          { 0,4,0,0 },
          { 0,0,0,0 }}}
    },
    /* PIECE_Z (color 5) */
    {
        /* Rotation 0 */
        {{{ 5,5,0,0 },
          { 0,5,5,0 },
          { 0,0,0,0 },
          { 0,0,0,0 }}},
        /* Rotation 1 */
        {{{ 0,0,5,0 },
          { 0,5,5,0 },
          { 0,5,0,0 },
          { 0,0,0,0 }}},
        /* Rotation 2 */
        {{{ 0,0,0,0 },
          { 5,5,0,0 },
          { 0,5,5,0 },
          { 0,0,0,0 }}},
        /* Rotation 3 */
        {{{ 0,5,0,0 },
          { 5,5,0,0 },
          { 5,0,0,0 },
          { 0,0,0,0 }}}
    },
    /* PIECE_L (color 6) */
    {
        /* Rotation 0 */
        {{{ 0,0,6,0 },
          { 6,6,6,0 },
          { 0,0,0,0 },
          { 0,0,0,0 }}},
        /* Rotation 1 */
        {{{ 0,6,0,0 },
          { 0,6,0,0 },
          { 0,6,6,0 },
          { 0,0,0,0 }}},
        /* Rotation 2 */
        {{{ 0,0,0,0 },
          { 6,6,6,0 },
          { 6,0,0,0 },
          { 0,0,0,0 }}},
        /* Rotation 3 */
        {{{ 6,6,0,0 },
          { 0,6,0,0 },
          { 0,6,0,0 },
          { 0,0,0,0 }}}
    },
    /* PIECE_J (color 7) */
    {
        /* Rotation 0 */
        {{{ 7,0,0,0 },
          { 7,7,7,0 },
          { 0,0,0,0 },
          { 0,0,0,0 }}},
        /* Rotation 1 */
        {{{ 0,7,7,0 },
          { 0,7,0,0 },
          { 0,7,0,0 },
          { 0,0,0,0 }}},
        /* Rotation 2 */
        {{{ 0,0,0,0 },
          { 7,7,7,0 },
          { 0,0,7,0 },
          { 0,0,0,0 }}},
        /* Rotation 3 */
        {{{ 0,7,0,0 },
          { 0,7,0,0 },
          { 7,7,0,0 },
          { 0,0,0,0 }}}
    }
};

/* ------------------------------------------------------------------ */
/* Public accessor for piece shape data                               */
/* ------------------------------------------------------------------ */

const PieceShape *piece_get_shape(int type, int rotation)
{
    return &piece_shapes[type][rotation & 3];
}

/* ------------------------------------------------------------------ */
/* Random number generator (LCG)                                      */
/* ------------------------------------------------------------------ */

static int game_rand(GameState *gs)
{
    gs->rng_state = gs->rng_state * 1103515245UL + 12345UL;
    return (int)((gs->rng_state >> 16) & 0x7FFF);
}

static int random_piece(GameState *gs)
{
    return game_rand(gs) % NUM_PIECES;
}

/* ------------------------------------------------------------------ */
/* Collision detection                                                 */
/* ------------------------------------------------------------------ */

static BOOL piece_fits(GameState *gs, int type, int rotation, int x, int y)
{
    const PieceShape *shape = piece_get_shape(type, rotation);
    int r, c;
    int fx, fy;

    for (r = 0; r < 4; r++) {
        for (c = 0; c < 4; c++) {
            if (shape->cells[r][c] == 0) continue;

            fx = x + c;
            fy = y + r;

            /* Out of bounds? */
            if (fx < 0 || fx >= FIELD_W) return FALSE;
            if (fy < 0 || fy >= FIELD_H) return FALSE;

            /* Overlaps existing cell? */
            if (gs->field[fy][fx] != 0) return FALSE;
        }
    }

    return TRUE;
}

/* ------------------------------------------------------------------ */
/* Piece spawning                                                     */
/* ------------------------------------------------------------------ */

static void spawn_piece(GameState *gs)
{
    gs->current.type = gs->next_type;
    gs->current.rotation = 0;
    gs->current.x = 3;
    gs->current.y = 0;

    gs->next_type = random_piece(gs);

    gs->drop_timer = gs->drop_speed;
    gs->lock_timer = 30;

    /* Check if the new piece fits */
    if (!piece_fits(gs, gs->current.type, gs->current.rotation,
                    gs->current.x, gs->current.y)) {
        gs->state = STATE_GAMEOVER;
    }
}

/* ------------------------------------------------------------------ */
/* Line clearing                                                      */
/* ------------------------------------------------------------------ */

static BOOL line_full(GameState *gs, int row)
{
    int c;
    for (c = 0; c < FIELD_W; c++) {
        if (gs->field[row][c] == 0) return FALSE;
    }
    return TRUE;
}

static void check_lines(GameState *gs)
{
    int r, i;

    gs->clear_count = 0;
    for (i = 0; i < 4; i++) {
        gs->clear_lines[i] = -1;
    }

    for (r = 0; r < FIELD_H; r++) {
        if (line_full(gs, r)) {
            gs->clear_lines[gs->clear_count] = r;
            gs->clear_count++;
            if (gs->clear_count >= 4) break;
        }
    }

    if (gs->clear_count > 0) {
        gs->clear_timer = 15;
        gs->just_cleared = TRUE;
    }
}

static void remove_lines(GameState *gs)
{
    static const ULONG line_scores[5] = { 0, 100, 300, 500, 800 };
    int i, r, c;

    /* Remove completed lines from bottom up */
    for (i = gs->clear_count - 1; i >= 0; i--) {
        int line = gs->clear_lines[i];

        /* Shift everything above down by one */
        for (r = line; r > 0; r--) {
            for (c = 0; c < FIELD_W; c++) {
                gs->field[r][c] = gs->field[r - 1][c];
            }
        }

        /* Clear top row */
        for (c = 0; c < FIELD_W; c++) {
            gs->field[0][c] = 0;
        }

        /* Adjust remaining clear_lines indices that are above this line */
        {
            int j;
            for (j = 0; j < i; j++) {
                if (gs->clear_lines[j] < line) {
                    gs->clear_lines[j]++;
                }
            }
        }
    }

    /* Update score */
    if (gs->clear_count > 0 && gs->clear_count <= 4) {
        gs->score += line_scores[gs->clear_count] * (ULONG)gs->level;
    }

    /* Update line count and level */
    gs->lines += gs->clear_count;
    gs->level = 1 + gs->lines / 10;
    if (gs->level > 20) gs->level = 20;

    /* Recalculate drop speed */
    gs->drop_speed = 30 - gs->level * 2;
    if (gs->drop_speed < 2) gs->drop_speed = 2;

    gs->clear_count = 0;
}

/* ------------------------------------------------------------------ */
/* Lock piece into field                                              */
/* ------------------------------------------------------------------ */

static void lock_piece(GameState *gs)
{
    const PieceShape *shape = piece_get_shape(gs->current.type,
                                              gs->current.rotation);
    int r, c;

    for (r = 0; r < 4; r++) {
        for (c = 0; c < 4; c++) {
            if (shape->cells[r][c] != 0) {
                int fx = gs->current.x + c;
                int fy = gs->current.y + r;
                if (fx >= 0 && fx < FIELD_W && fy >= 0 && fy < FIELD_H) {
                    gs->field[fy][fx] = shape->cells[r][c];
                }
            }
        }
    }

    gs->just_locked = TRUE;
    check_lines(gs);

    if (gs->clear_count == 0) {
        spawn_piece(gs);
    }
}

/* ------------------------------------------------------------------ */
/* Ghost piece (drop preview)                                         */
/* ------------------------------------------------------------------ */

int game_ghost_y(const GameState *gs)
{
    /* Need non-const to call piece_fits, but we won't modify state */
    GameState *mgs = (GameState *)gs;
    int gy = gs->current.y;

    while (piece_fits(mgs, gs->current.type, gs->current.rotation,
                      gs->current.x, gy + 1)) {
        gy++;
    }

    return gy;
}

/* ------------------------------------------------------------------ */
/* Initialization                                                     */
/* ------------------------------------------------------------------ */

void game_init(GameState *gs)
{
    int r, c, i;

    for (r = 0; r < FIELD_H; r++) {
        for (c = 0; c < FIELD_W; c++) {
            gs->field[r][c] = 0;
        }
    }

    gs->current.type = 0;
    gs->current.rotation = 0;
    gs->current.x = 0;
    gs->current.y = 0;

    gs->next_type = 0;
    gs->state = STATE_TITLE;
    gs->score = 0;
    gs->level = 1;
    gs->lines = 0;
    gs->drop_timer = 0;
    gs->drop_speed = 28;
    gs->lock_timer = 30;
    gs->clear_timer = 0;
    gs->clear_count = 0;

    for (i = 0; i < 4; i++) {
        gs->clear_lines[i] = -1;
    }

    gs->das_timer = 0;
    gs->das_dir = 0;
    gs->just_locked = FALSE;
    gs->just_cleared = FALSE;
    gs->just_dropped = FALSE;
    gs->rng_state = 123456789UL;
}

void game_start(GameState *gs)
{
    int r, c, i;

    /* Clear the field */
    for (r = 0; r < FIELD_H; r++) {
        for (c = 0; c < FIELD_W; c++) {
            gs->field[r][c] = 0;
        }
    }

    gs->score = 0;
    gs->level = 1;
    gs->lines = 0;
    gs->drop_speed = 28;
    gs->clear_timer = 0;
    gs->clear_count = 0;

    for (i = 0; i < 4; i++) {
        gs->clear_lines[i] = -1;
    }

    gs->das_timer = 0;
    gs->das_dir = 0;
    gs->just_locked = FALSE;
    gs->just_cleared = FALSE;
    gs->just_dropped = FALSE;

    /* Pick first and next pieces */
    gs->next_type = random_piece(gs);
    spawn_piece(gs);

    gs->state = STATE_PLAYING;
}

/* ------------------------------------------------------------------ */
/* Main update                                                        */
/* ------------------------------------------------------------------ */

void game_update(GameState *gs, int input)
{
    switch (gs->state) {

    case STATE_TITLE:
        if (input & GINPUT_START) {
            game_start(gs);
        }
        break;

    case STATE_GAMEOVER:
        if (input & GINPUT_START) {
            gs->state = STATE_TITLE;
        }
        break;

    case STATE_PAUSED:
        if (input & GINPUT_START) {
            gs->state = STATE_PLAYING;
        }
        break;

    case STATE_PLAYING:
        /* Clear SFX flags */
        gs->just_locked = FALSE;
        gs->just_cleared = FALSE;
        gs->just_dropped = FALSE;

        /* Pause */
        if (input & GINPUT_START) {
            gs->state = STATE_PAUSED;
            break;
        }

        /* If line clear animation is running, just tick it down */
        if (gs->clear_timer > 0) {
            gs->clear_timer--;
            if (gs->clear_timer == 0) {
                remove_lines(gs);
                spawn_piece(gs);
            }
            break;
        }

        /* Rotation with wall kicks */
        if (input & GINPUT_ROTATE) {
            int new_rot = (gs->current.rotation + 1) & 3;
            if (piece_fits(gs, gs->current.type, new_rot,
                           gs->current.x, gs->current.y)) {
                gs->current.rotation = new_rot;
            } else if (piece_fits(gs, gs->current.type, new_rot,
                                  gs->current.x - 1, gs->current.y)) {
                gs->current.rotation = new_rot;
                gs->current.x -= 1;
            } else if (piece_fits(gs, gs->current.type, new_rot,
                                  gs->current.x + 1, gs->current.y)) {
                gs->current.rotation = new_rot;
                gs->current.x += 1;
            } else if (piece_fits(gs, gs->current.type, new_rot,
                                  gs->current.x - 2, gs->current.y)) {
                /* Extra kick for I piece */
                gs->current.rotation = new_rot;
                gs->current.x -= 2;
            } else if (piece_fits(gs, gs->current.type, new_rot,
                                  gs->current.x + 2, gs->current.y)) {
                gs->current.rotation = new_rot;
                gs->current.x += 2;
            }
        }

        /* Horizontal movement with DAS */
        {
            int h_dir = 0;
            if (input & GINPUT_LEFT)  h_dir = -1;
            if (input & GINPUT_RIGHT) h_dir = 1;

            if (h_dir != 0) {
                if (h_dir != gs->das_dir) {
                    /* New direction: move immediately, reset DAS */
                    gs->das_dir = h_dir;
                    gs->das_timer = 0;
                    if (piece_fits(gs, gs->current.type, gs->current.rotation,
                                   gs->current.x + h_dir, gs->current.y)) {
                        gs->current.x += h_dir;
                        gs->lock_timer = 30; /* Reset lock on horizontal move */
                    }
                } else {
                    /* Same direction held: DAS */
                    gs->das_timer++;
                    if (gs->das_timer >= 10) {
                        /* Auto-repeat phase: move every 3 frames */
                        if ((gs->das_timer - 10) % 3 == 0) {
                            if (piece_fits(gs, gs->current.type,
                                           gs->current.rotation,
                                           gs->current.x + h_dir,
                                           gs->current.y)) {
                                gs->current.x += h_dir;
                                gs->lock_timer = 30;
                            }
                        }
                    }
                }
            } else {
                gs->das_dir = 0;
                gs->das_timer = 0;
            }
        }

        /* Hard drop */
        if (input & GINPUT_DROP) {
            int ghost_y = game_ghost_y(gs);
            int rows = ghost_y - gs->current.y;
            gs->score += (ULONG)(rows * 2);
            gs->current.y = ghost_y;
            gs->just_dropped = TRUE;
            lock_piece(gs);
            break;
        }

        /* Soft drop: accelerate */
        if (input & GINPUT_DOWN) {
            if (piece_fits(gs, gs->current.type, gs->current.rotation,
                           gs->current.x, gs->current.y + 1)) {
                gs->current.y++;
                gs->score += 1;
                gs->drop_timer = gs->drop_speed;
                gs->lock_timer = 30;
            }
            /* Even if we can't move down, reset timer to keep it fast */
            gs->drop_timer = 0;
        }

        /* Auto-drop */
        gs->drop_timer--;
        if (gs->drop_timer <= 0) {
            gs->drop_timer = gs->drop_speed;

            if (piece_fits(gs, gs->current.type, gs->current.rotation,
                           gs->current.x, gs->current.y + 1)) {
                gs->current.y++;
                gs->lock_timer = 30;
            } else {
                /* Piece can't move down: tick lock timer */
                gs->lock_timer--;
                if (gs->lock_timer <= 0) {
                    lock_piece(gs);
                }
            }
        } else {
            /* Even when not auto-dropping, check if grounded and tick lock */
            if (!piece_fits(gs, gs->current.type, gs->current.rotation,
                            gs->current.x, gs->current.y + 1)) {
                gs->lock_timer--;
                if (gs->lock_timer <= 0) {
                    lock_piece(gs);
                }
            }
        }
        break;
    }
}
