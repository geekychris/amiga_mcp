/*
 * Lode Runner - Enemy AI
 *
 * BFS-based pathfinding toward the player, gravity, trapping in
 * dug holes, gold stealing, and respawning.
 */
#include "enemy.h"
#include "level.h"
#include "game.h"

/* BFS queue and visited arrays - static to avoid stack overflow on 68k */
#define BFS_SIZE (GRID_COLS * GRID_ROWS)
static UBYTE bfs_visited[BFS_SIZE];
static int   bfs_queue_x[BFS_SIZE];
static int   bfs_queue_y[BFS_SIZE];
static int   bfs_parent_dir[BFS_SIZE]; /* direction from start to reach this cell */

/* Simple LCG pseudo-random for enemy behavior */
static ULONG enemy_rng_state = 12345;
static int enemy_rand(void)
{
    enemy_rng_state = enemy_rng_state * 1103515245 + 12345;
    return (int)((enemy_rng_state >> 16) & 0x7FFF);
}

/*--------------------------------------------------------------------
 * Static helper: bounds-checked tile lookup
 *-------------------------------------------------------------------*/
static UBYTE tile_at(GameState *gs, int x, int y)
{
    if (x < 0 || x >= GRID_COLS || y < 0 || y >= GRID_ROWS)
        return TILE_SOLID;
    return gs->tiles[x][y];
}

/*--------------------------------------------------------------------
 * Static helper: is a tile solid (blocks movement)?
 *-------------------------------------------------------------------*/
static int is_solid(UBYTE t)
{
    return (t == TILE_BRICK || t == TILE_SOLID || t == TILE_TRAP);
}

/*--------------------------------------------------------------------
 * Static helper: can an enemy enter this tile?
 *-------------------------------------------------------------------*/
static int can_enter(UBYTE t)
{
    return (t == TILE_EMPTY || t == TILE_GOLD || t == TILE_LADDER ||
            t == TILE_BAR || t == TILE_HIDDEN_LADDER);
}

/*--------------------------------------------------------------------
 * Static helper: does position have support (not falling)?
 *-------------------------------------------------------------------*/
static int has_support(GameState *gs, int gx, int gy)
{
    UBYTE here, below;

    if (gy >= GRID_ROWS - 1)
        return 1;

    here = tile_at(gs, gx, gy);
    if (here == TILE_LADDER)
        return 1;

    below = tile_at(gs, gx, gy + 1);
    if (is_solid(below) || below == TILE_LADDER)
        return 1;

    return 0;
}

/*--------------------------------------------------------------------
 * BFS pathfinding - find best first move toward player
 *
 * Returns direction via out_dx/out_dy. Returns 0 if no path found.
 *-------------------------------------------------------------------*/
static int bfs_find_next_move(GameState *gs,
                              int from_x, int from_y,
                              int to_x, int to_y,
                              int *out_dx, int *out_dy)
{
    int head, tail, i;
    int cx, cy, nx, ny, idx, nidx;
    int dx_tab[4] = { -1, 1, 0, 0 };
    int dy_tab[4] = {  0, 0,-1, 1 };
    int dir_tab[4] = { DIR_LEFT, DIR_RIGHT, DIR_UP, DIR_DOWN };
    UBYTE target_tile, cur_tile;

    /* Clear visited */
    for (i = 0; i < BFS_SIZE; i++) {
        bfs_visited[i] = 0;
        bfs_parent_dir[i] = DIR_NONE;
    }

    /* Init queue with start position */
    head = 0;
    tail = 0;
    idx = from_y * GRID_COLS + from_x;
    bfs_visited[idx] = 1;
    bfs_parent_dir[idx] = DIR_NONE;
    bfs_queue_x[tail] = from_x;
    bfs_queue_y[tail] = from_y;
    tail++;

    while (head < tail) {
        cx = bfs_queue_x[head];
        cy = bfs_queue_y[head];
        head++;

        /* Found target? */
        if (cx == to_x && cy == to_y) {
            /* Trace back to find first move */
            int trace_x = to_x, trace_y = to_y;
            int prev_x, prev_y, last_dir;

            /* Walk parent chain back to start */
            for (;;) {
                nidx = trace_y * GRID_COLS + trace_x;
                last_dir = bfs_parent_dir[nidx];
                if (last_dir == DIR_NONE)
                    break;

                /* Compute previous position */
                switch (last_dir) {
                    case DIR_LEFT:  prev_x = trace_x + 1; prev_y = trace_y; break;
                    case DIR_RIGHT: prev_x = trace_x - 1; prev_y = trace_y; break;
                    case DIR_UP:    prev_x = trace_x; prev_y = trace_y + 1; break;
                    case DIR_DOWN:  prev_x = trace_x; prev_y = trace_y - 1; break;
                    default: prev_x = trace_x; prev_y = trace_y; break;
                }

                if (prev_x == from_x && prev_y == from_y) {
                    /* This is the first step */
                    switch (last_dir) {
                        case DIR_LEFT:  *out_dx = -1; *out_dy = 0; break;
                        case DIR_RIGHT: *out_dx = 1;  *out_dy = 0; break;
                        case DIR_UP:    *out_dx = 0;  *out_dy = -1; break;
                        case DIR_DOWN:  *out_dx = 0;  *out_dy = 1; break;
                    }
                    return 1;
                }

                trace_x = prev_x;
                trace_y = prev_y;
            }
            /* Shouldn't get here, but just in case */
            *out_dx = 0;
            *out_dy = 0;
            return 0;
        }

        /* Explore neighbors */
        idx = cy * GRID_COLS + cx;
        cur_tile = tile_at(gs, cx, cy);

        for (i = 0; i < 4; i++) {
            nx = cx + dx_tab[i];
            ny = cy + dy_tab[i];

            if (nx < 0 || nx >= GRID_COLS || ny < 0 || ny >= GRID_ROWS)
                continue;

            nidx = ny * GRID_COLS + nx;
            if (bfs_visited[nidx])
                continue;

            target_tile = tile_at(gs, nx, ny);
            if (!can_enter(target_tile))
                continue;

            /* Movement rules */
            if (dy_tab[i] == -1) {
                /* Moving up: must be on ladder */
                if (cur_tile != TILE_LADDER)
                    continue;
            } else if (dy_tab[i] == 1) {
                /* Moving down: always allowed (gravity/ladder) */
            } else {
                /* Horizontal: need support or bar */
                if (!has_support(gs, cx, cy) && cur_tile != TILE_BAR)
                    continue;
            }

            bfs_visited[nidx] = 1;
            bfs_parent_dir[nidx] = dir_tab[i];
            bfs_queue_x[tail] = nx;
            bfs_queue_y[tail] = ny;
            tail++;

            if (tail >= BFS_SIZE)
                goto bfs_done;
        }
    }

bfs_done:
    /* No path found */
    *out_dx = 0;
    *out_dy = 0;
    return 0;
}

/*--------------------------------------------------------------------
 * Static helper: respawn a dead enemy at the top row
 *-------------------------------------------------------------------*/
static void enemy_respawn(GameState *gs, Enemy *e)
{
    int x, attempts;

    /* Find an empty spot at the top */
    attempts = 0;
    do {
        x = enemy_rand() % GRID_COLS;
        attempts++;
    } while (tile_at(gs, x, 0) != TILE_EMPTY && attempts < 100);

    if (attempts >= 100) {
        /* Fallback: just pick first empty spot */
        for (x = 0; x < GRID_COLS; x++) {
            if (tile_at(gs, x, 0) == TILE_EMPTY)
                break;
        }
        if (x >= GRID_COLS) x = GRID_COLS / 2; /* desperate fallback */
    }

    e->gx = x;
    e->gy = 0;
    e->px = x * TILE_W;
    e->py = 0;
    e->state = ES_FALLING;
    e->dir = DIR_NONE;
    e->anim = 0;
    e->trap_timer = 0;
    e->has_gold = 0;
    e->active = 1;
    e->move_timer = 0;
}

/*--------------------------------------------------------------------
 * Static helper: update a single enemy
 *-------------------------------------------------------------------*/
static void enemy_update_one(GameState *gs, Enemy *e)
{
    int move_dx, move_dy;
    int new_gx, new_gy;
    UBYTE here, below, target_tile;

    if (!e->active)
        return;

    /*--- Trapped state ---*/
    if (e->state == ES_TRAPPED) {
        e->trap_timer--;
        e->anim++;

        if (e->trap_timer <= 0) {
            /* Escape upward */
            if (e->gy > 0 && can_enter(tile_at(gs, e->gx, e->gy - 1))) {
                e->gy--;
                e->py = e->gy * TILE_H;
                e->state = ES_RUNNING;
            } else {
                /* Can't escape - brick reformed, die */
                e->state = ES_DEAD;
                if (e->has_gold) {
                    /* Drop gold above if possible */
                    if (e->gy > 0 && tile_at(gs, e->gx, e->gy - 1) == TILE_EMPTY) {
                        gs->tiles[e->gx][e->gy - 1] = TILE_GOLD;
                    }
                    e->has_gold = 0;
                }
            }
        }
        return;
    }

    /*--- Dead state: respawn ---*/
    if (e->state == ES_DEAD) {
        enemy_respawn(gs, e);
        return;
    }

    /*--- Falling state ---*/
    here = tile_at(gs, e->gx, e->gy);

    if (e->state == ES_FALLING ||
        (!has_support(gs, e->gx, e->gy) &&
         here != TILE_BAR &&
         here != TILE_LADDER)) {

        e->state = ES_FALLING;
        e->py += ENEMY_SPEED;

        /* Check grid crossing */
        new_gy = e->py / TILE_H;
        if (new_gy != e->gy && (e->py % TILE_H) == 0) {
            e->gy = new_gy;

            if (e->gy >= GRID_ROWS) {
                e->state = ES_DEAD;
                return;
            }

            if (has_support(gs, e->gx, e->gy)) {
                e->state = ES_RUNNING;
                e->py = e->gy * TILE_H;
            }
        }
        return;
    }

    /*--- Movement timing (enemies are slower) ---*/
    e->move_timer++;
    if (e->move_timer < (PLAYER_SPEED + 1)) {
        return;
    }
    e->move_timer = 0;

    /*--- AI pathfinding ---*/
    move_dx = 0;
    move_dy = 0;

    if (!bfs_find_next_move(gs, e->gx, e->gy,
                            gs->player.gx, gs->player.gy,
                            &move_dx, &move_dy)) {
        /* No path - random movement */
        int r = enemy_rand() % 4;
        switch (r) {
            case 0: move_dx = -1; break;
            case 1: move_dx = 1;  break;
            case 2: move_dy = -1; break;
            case 3: move_dy = 1;  break;
        }
    }

    /*--- Apply movement ---*/
    here = tile_at(gs, e->gx, e->gy);

    if (move_dx != 0) {
        /* Horizontal movement */
        new_gx = e->gx + move_dx;
        if (new_gx >= 0 && new_gx < GRID_COLS) {
            target_tile = tile_at(gs, new_gx, e->gy);
            if (can_enter(target_tile) &&
                (has_support(gs, e->gx, e->gy) || here == TILE_BAR)) {
                e->gx = new_gx;
                e->px = e->gx * TILE_W;
                e->dir = (move_dx < 0) ? DIR_LEFT : DIR_RIGHT;

                if (tile_at(gs, e->gx, e->gy) == TILE_BAR)
                    e->state = ES_HANGING;
                else
                    e->state = ES_RUNNING;
            }
        }
    } else if (move_dy != 0) {
        if (move_dy < 0) {
            /* Up - only on ladder */
            if (here == TILE_LADDER) {
                new_gy = e->gy - 1;
                if (new_gy >= 0 && can_enter(tile_at(gs, e->gx, new_gy))) {
                    e->gy = new_gy;
                    e->py = e->gy * TILE_H;
                    e->state = ES_CLIMBING;
                    e->dir = DIR_UP;
                }
            }
        } else {
            /* Down - ladder or fall */
            if (here == TILE_LADDER ||
                tile_at(gs, e->gx, e->gy + 1) == TILE_LADDER) {
                new_gy = e->gy + 1;
                if (new_gy < GRID_ROWS && can_enter(tile_at(gs, e->gx, new_gy))) {
                    e->gy = new_gy;
                    e->py = e->gy * TILE_H;
                    e->state = ES_CLIMBING;
                    e->dir = DIR_DOWN;
                }
            } else if (here == TILE_BAR) {
                /* Release from bar */
                e->state = ES_FALLING;
            }
        }
    }

    e->anim++;

    /*--- Gold stealing ---*/
    if (!e->has_gold && tile_at(gs, e->gx, e->gy) == TILE_GOLD) {
        /* 30% chance to pick up gold */
        if ((enemy_rand() % 100) < 30) {
            e->has_gold = 1;
            gs->tiles[e->gx][e->gy] = TILE_EMPTY;
        }
    }

    /*--- Drop gold when falling into hole ---*/
    if (e->has_gold && e->state == ES_TRAPPED) {
        /* Drop gold one tile above */
        if (e->gy > 0 && tile_at(gs, e->gx, e->gy - 1) == TILE_EMPTY) {
            gs->tiles[e->gx][e->gy - 1] = TILE_GOLD;
        }
        e->has_gold = 0;
    }
}

/*--------------------------------------------------------------------
 * enemy_update_all - Update all active enemies
 *-------------------------------------------------------------------*/
void enemy_update_all(GameState *gs)
{
    int i;
    for (i = 0; i < gs->num_enemies; i++) {
        enemy_update_one(gs, &gs->enemies[i]);
    }
}

/*--------------------------------------------------------------------
 * enemy_check_collision - Check if any enemy overlaps the player
 * Returns 1 if player is caught, 0 otherwise
 *-------------------------------------------------------------------*/
int enemy_check_collision(GameState *gs)
{
    int i, dx_abs, dy_abs;
    Player *p = &gs->player;

    for (i = 0; i < gs->num_enemies; i++) {
        if (!gs->enemies[i].active)
            continue;
        if (gs->enemies[i].state == ES_TRAPPED ||
            gs->enemies[i].state == ES_DEAD)
            continue;

        dx_abs = gs->enemies[i].px - p->px;
        if (dx_abs < 0) dx_abs = -dx_abs;
        dy_abs = gs->enemies[i].py - p->py;
        if (dy_abs < 0) dy_abs = -dy_abs;

        if (dx_abs < (TILE_W - 2) && dy_abs < (TILE_H - 2))
            return 1;
    }
    return 0;
}

/*--------------------------------------------------------------------
 * enemy_trap_check - Check if enemy at grid pos should be trapped
 * Called when a brick is dug at (x, y)
 *-------------------------------------------------------------------*/
void enemy_trap_check(GameState *gs, int x, int y)
{
    int i;
    for (i = 0; i < gs->num_enemies; i++) {
        if (!gs->enemies[i].active)
            continue;
        if (gs->enemies[i].state == ES_TRAPPED ||
            gs->enemies[i].state == ES_DEAD)
            continue;

        if (gs->enemies[i].gx == x && gs->enemies[i].gy == y) {
            gs->enemies[i].state = ES_TRAPPED;
            gs->enemies[i].trap_timer = BRICK_REGEN_FRAMES - 10;
            gs->enemies[i].px = x * TILE_W;
            gs->enemies[i].py = y * TILE_H;

            /* Drop gold above the hole */
            if (gs->enemies[i].has_gold) {
                if (y > 0 && tile_at(gs, x, y - 1) == TILE_EMPTY) {
                    gs->tiles[x][y - 1] = TILE_GOLD;
                }
                gs->enemies[i].has_gold = 0;
            }
        }
    }
}
