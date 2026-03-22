/*
 * Gold Rush - Player movement and actions
 *
 * Handles input-driven movement, gravity, digging, gold collection,
 * and smooth pixel-level scrolling between grid positions.
 */
#include "player.h"
#include "level.h"
#include "game.h"

/*--------------------------------------------------------------------
 * Static helper: bounds-checked tile lookup
 *-------------------------------------------------------------------*/
static UBYTE tile_at(GameState *gs, int x, int y)
{
    if (x < 0 || x >= GRID_COLS || y < 0 || y >= GRID_ROWS)
        return TILE_SOLID;  /* treat out-of-bounds as solid */
    return gs->tiles[x][y];
}

/*--------------------------------------------------------------------
 * Static helper: is the tile a solid surface (blocks falling)?
 *-------------------------------------------------------------------*/
static int is_solid(UBYTE t)
{
    return (t == TILE_BRICK || t == TILE_SOLID || t == TILE_TRAP);
}

/*--------------------------------------------------------------------
 * Static helper: does the player have support at grid position?
 * Player has support if:
 *   - standing on solid/brick/trap tile below
 *   - on a ladder tile
 *   - at the bottom row of the grid
 *-------------------------------------------------------------------*/
static int has_support(GameState *gs, int gx, int gy)
{
    UBYTE here, below;

    /* Bottom row always has support */
    if (gy >= GRID_ROWS - 1)
        return 1;

    here = tile_at(gs, gx, gy);

    /* On a ladder = supported */
    if (here == TILE_LADDER)
        return 1;

    /* Standing on something solid below */
    below = tile_at(gs, gx, gy + 1);
    if (is_solid(below) || below == TILE_LADDER)
        return 1;

    return 0;
}

/*--------------------------------------------------------------------
 * Static helper: is the player on a bar?
 *-------------------------------------------------------------------*/
static int is_on_bar(GameState *gs, int gx, int gy)
{
    return tile_at(gs, gx, gy) == TILE_BAR;
}

/*--------------------------------------------------------------------
 * Static helper: is the player on a ladder?
 *-------------------------------------------------------------------*/
static int is_on_ladder(GameState *gs, int gx, int gy)
{
    return tile_at(gs, gx, gy) == TILE_LADDER;
}

/*--------------------------------------------------------------------
 * Static helper: can the player walk into a tile?
 * Player can enter tiles that are empty, gold, ladder, bar, trap,
 * or hidden ladder. Cannot enter solid or brick.
 *-------------------------------------------------------------------*/
static int can_enter(UBYTE t)
{
    return (t == TILE_EMPTY || t == TILE_GOLD || t == TILE_LADDER ||
            t == TILE_BAR || t == TILE_HIDDEN_LADDER || t == TILE_TRAP);
}

/*--------------------------------------------------------------------
 * Static helper: snap pixel position to grid alignment
 *-------------------------------------------------------------------*/
static int grid_to_pixel_x(int gx) { return gx * TILE_W; }
static int grid_to_pixel_y(int gy) { return gy * TILE_H; }

/*--------------------------------------------------------------------
 * Static helper: check if pixel position is grid-aligned
 *-------------------------------------------------------------------*/
static int is_aligned_x(int px) { return (px % TILE_W) == 0; }
static int is_aligned_y(int py) { return (py % TILE_H) == 0; }

/*--------------------------------------------------------------------
 * Static helper: move pixel value toward target by speed
 *-------------------------------------------------------------------*/
static int move_toward(int current, int target, int speed)
{
    if (current < target) {
        current += speed;
        if (current > target) current = target;
    } else if (current > target) {
        current -= speed;
        if (current < target) current = target;
    }
    return current;
}

/*--------------------------------------------------------------------
 * player_update - Process one frame of player movement
 *
 * dx: -1 = left, 0 = none, 1 = right
 * dy: -1 = up, 0 = none, 1 = down
 * dig_left:  1 = dig left button pressed
 * dig_right: 1 = dig right button pressed
 *-------------------------------------------------------------------*/
void player_update(GameState *gs, int dx, int dy, int dig_left, int dig_right)
{
    Player *p = &gs->player;
    int target_px, target_py;
    UBYTE here, below, target_tile;
    int new_gx, new_gy;

    /* Dead players don't move */
    if (p->state == PS_DEAD)
        return;

    /* Digging animation in progress */
    if (p->state == PS_DIGGING) {
        p->dig_timer--;
        if (p->dig_timer <= 0) {
            p->state = PS_IDLE;
            p->dig_timer = 0;
        }
        /* Animate but don't allow movement while digging */
        p->anim++;
        return;
    }

    /* Get current tile info */
    here = tile_at(gs, p->gx, p->gy);
    below = tile_at(gs, p->gx, p->gy + 1);

    /*--- Gravity check ---*/
    /* Fall if: not on ladder, not on bar, no solid below, and grid-aligned */
    if (!is_on_ladder(gs, p->gx, p->gy) &&
        !is_on_bar(gs, p->gx, p->gy) &&
        !has_support(gs, p->gx, p->gy)) {

        /* Apply gravity */
        p->state = PS_FALLING;
        p->py += PLAYER_SPEED;
        p->fall_count++;

        /* Check if we've reached next grid row */
        new_gy = p->py / TILE_H;
        if (new_gy != p->gy && is_aligned_y(p->py)) {
            p->gy = new_gy;

            /* Fell off bottom of screen = death */
            if (p->gy >= GRID_ROWS) {
                p->state = PS_DEAD;
                return;
            }

            /* Check if we landed */
            if (has_support(gs, p->gx, p->gy)) {
                p->state = PS_IDLE;
                p->fall_count = 0;
                p->py = grid_to_pixel_y(p->gy);
            }
        }
        /* Pick up gold while falling through */
        if (tile_at(gs, p->gx, p->gy) == TILE_GOLD) {
            gs->tiles[p->gx][p->gy] = TILE_EMPTY;
            gs->gold_collected++;
            gs->score += 100;
        }
        return;
    }

    /* Not falling - reset fall count */
    p->fall_count = 0;

    /*--- Process digging ---*/
    if (dig_left && is_aligned_x(p->px) && is_aligned_y(p->py)) {
        int dig_x = p->gx - 1;
        int dig_y = p->gy + 1;
        /* Can dig left: brick below-left, and empty to the left */
        if (dig_x >= 0 && dig_y < GRID_ROWS &&
            tile_at(gs, dig_x, dig_y) == TILE_BRICK &&
            can_enter(tile_at(gs, dig_x, p->gy))) {
            if (level_dig_brick(gs, dig_x, dig_y)) {
                p->state = PS_DIGGING;
                p->dig_timer = 10;
                p->dir = DIR_LEFT;
                return;
            }
        }
    }

    if (dig_right && is_aligned_x(p->px) && is_aligned_y(p->py)) {
        int dig_x = p->gx + 1;
        int dig_y = p->gy + 1;
        /* Can dig right: brick below-right, and empty to the right */
        if (dig_x < GRID_COLS && dig_y < GRID_ROWS &&
            tile_at(gs, dig_x, dig_y) == TILE_BRICK &&
            can_enter(tile_at(gs, dig_x, p->gy))) {
            if (level_dig_brick(gs, dig_x, dig_y)) {
                p->state = PS_DIGGING;
                p->dig_timer = 10;
                p->dir = DIR_RIGHT;
                return;
            }
        }
    }

    /*--- Process horizontal movement ---*/
    if (dx != 0 && is_aligned_y(p->py)) {
        new_gx = p->gx + dx;
        if (new_gx >= 0 && new_gx < GRID_COLS) {
            target_tile = tile_at(gs, new_gx, p->gy);
            if (can_enter(target_tile)) {
                /* Must have support or be on bar/ladder to move horizontally */
                if (has_support(gs, p->gx, p->gy) ||
                    is_on_bar(gs, p->gx, p->gy) ||
                    is_on_ladder(gs, p->gx, p->gy)) {

                    target_px = grid_to_pixel_x(new_gx);
                    p->px = move_toward(p->px, target_px, PLAYER_SPEED);

                    /* Update grid position when aligned */
                    if (is_aligned_x(p->px)) {
                        p->gx = p->px / TILE_W;
                    }

                    p->dir = (dx < 0) ? DIR_LEFT : DIR_RIGHT;

                    /* Update state based on tile we're on */
                    if (is_on_bar(gs, p->gx, p->gy))
                        p->state = PS_HANGING;
                    else
                        p->state = PS_RUNNING;

                    p->anim++;
                    /* Pick up gold */
                    if (tile_at(gs, p->gx, p->gy) == TILE_GOLD) {
                        gs->tiles[p->gx][p->gy] = TILE_EMPTY;
                        gs->gold_collected++;
                        gs->score += 100;
                    }
                    return;
                }
            }
        }
    }

    /*--- Process vertical movement ---*/
    if (dy != 0 && is_aligned_x(p->px)) {
        if (dy < 0) {
            /* Moving up - only on ladders */
            if (is_on_ladder(gs, p->gx, p->gy)) {
                new_gy = p->gy - 1;
                if (new_gy >= 0 && can_enter(tile_at(gs, p->gx, new_gy))) {
                    target_py = grid_to_pixel_y(new_gy);
                    p->py = move_toward(p->py, target_py, PLAYER_SPEED);

                    if (is_aligned_y(p->py)) {
                        p->gy = p->py / TILE_H;
                    }

                    p->state = PS_CLIMBING;
                    p->dir = DIR_UP;
                    p->anim++;

                    /* Pick up gold */
                    if (tile_at(gs, p->gx, p->gy) == TILE_GOLD) {
                        gs->tiles[p->gx][p->gy] = TILE_EMPTY;
                        gs->gold_collected++;
                        gs->score += 100;
                    }
                    return;
                }
            }
        } else {
            /* Moving down - on ladder, or release from bar */
            if (is_on_ladder(gs, p->gx, p->gy) ||
                is_on_ladder(gs, p->gx, p->gy + 1)) {
                new_gy = p->gy + 1;
                if (new_gy < GRID_ROWS && can_enter(tile_at(gs, p->gx, new_gy))) {
                    target_py = grid_to_pixel_y(new_gy);
                    p->py = move_toward(p->py, target_py, PLAYER_SPEED);

                    if (is_aligned_y(p->py)) {
                        p->gy = p->py / TILE_H;
                    }

                    p->state = PS_CLIMBING;
                    p->dir = DIR_DOWN;
                    p->anim++;

                    /* Pick up gold */
                    if (tile_at(gs, p->gx, p->gy) == TILE_GOLD) {
                        gs->tiles[p->gx][p->gy] = TILE_EMPTY;
                        gs->gold_collected++;
                        gs->score += 100;
                    }
                    return;
                }
            }
            /* Release from bar - start falling */
            if (is_on_bar(gs, p->gx, p->gy)) {
                p->state = PS_FALLING;
                return;
            }
        }
    }

    /*--- No input or can't move: update idle state ---*/
    if (is_aligned_x(p->px) && is_aligned_y(p->py)) {
        /* Snap to grid */
        p->gx = p->px / TILE_W;
        p->gy = p->py / TILE_H;

        if (is_on_bar(gs, p->gx, p->gy) && !has_support(gs, p->gx, p->gy))
            p->state = PS_HANGING;
        else if (is_on_ladder(gs, p->gx, p->gy))
            p->state = PS_CLIMBING;
        else
            p->state = PS_IDLE;
    } else {
        /* Continue sliding to grid alignment */
        target_px = grid_to_pixel_x(p->gx);
        target_py = grid_to_pixel_y(p->gy);
        p->px = move_toward(p->px, target_px, PLAYER_SPEED);
        p->py = move_toward(p->py, target_py, PLAYER_SPEED);
    }
}
