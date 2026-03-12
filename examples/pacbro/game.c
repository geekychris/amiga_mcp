/*
 * PACBRO - Game logic
 * Ghost AI, movement, collision, scoring
 */
#include <string.h>
#include "game.h"

/*
 * Classic Pac-Man maze template: 28x31
 * 1=wall, 2=dot, 3=power pellet, 4=ghost door, 0=empty
 */
const UBYTE maze_template[MAZE_H][MAZE_W] = {
    {1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1},
    {1,2,2,2,2,2,2,2,2,2,2,2,2,1,1,2,2,2,2,2,2,2,2,2,2,2,2,1},
    {1,2,1,1,1,1,2,1,1,1,1,1,2,1,1,2,1,1,1,1,1,2,1,1,1,1,2,1},
    {1,3,1,1,1,1,2,1,1,1,1,1,2,1,1,2,1,1,1,1,1,2,1,1,1,1,3,1},
    {1,2,1,1,1,1,2,1,1,1,1,1,2,1,1,2,1,1,1,1,1,2,1,1,1,1,2,1},
    {1,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,1},
    {1,2,1,1,1,1,2,1,1,2,1,1,1,1,1,1,1,1,2,1,1,2,1,1,1,1,2,1},
    {1,2,1,1,1,1,2,1,1,2,1,1,1,1,1,1,1,1,2,1,1,2,1,1,1,1,2,1},
    {1,2,2,2,2,2,2,1,1,2,2,2,2,1,1,2,2,2,2,1,1,2,2,2,2,2,2,1},
    {1,1,1,1,1,1,2,1,1,1,1,1,0,1,1,0,1,1,1,1,1,2,1,1,1,1,1,1},
    {0,0,0,0,0,1,2,1,1,1,1,1,0,1,1,0,1,1,1,1,1,2,1,0,0,0,0,0},
    {0,0,0,0,0,1,2,1,1,0,0,0,0,0,0,0,0,0,0,1,1,2,1,0,0,0,0,0},
    {0,0,0,0,0,1,2,1,1,0,1,1,1,4,4,1,1,1,0,1,1,2,1,0,0,0,0,0},
    {1,1,1,1,1,1,2,1,1,0,1,0,0,0,0,0,0,1,0,1,1,2,1,1,1,1,1,1},
    {0,0,0,0,0,0,2,0,0,0,1,0,0,0,0,0,0,1,0,0,0,2,0,0,0,0,0,0},
    {1,1,1,1,1,1,2,1,1,0,1,0,0,0,0,0,0,1,0,1,1,2,1,1,1,1,1,1},
    {0,0,0,0,0,1,2,1,1,0,1,1,1,1,1,1,1,1,0,1,1,2,1,0,0,0,0,0},
    {0,0,0,0,0,1,2,1,1,0,0,0,0,0,0,0,0,0,0,1,1,2,1,0,0,0,0,0},
    {0,0,0,0,0,1,2,1,1,0,1,1,1,1,1,1,1,1,0,1,1,2,1,0,0,0,0,0},
    {1,1,1,1,1,1,2,1,1,0,1,1,1,1,1,1,1,1,0,1,1,2,1,1,1,1,1,1},
    {1,2,2,2,2,2,2,2,2,2,2,2,2,1,1,2,2,2,2,2,2,2,2,2,2,2,2,1},
    {1,2,1,1,1,1,2,1,1,1,1,1,2,1,1,2,1,1,1,1,1,2,1,1,1,1,2,1},
    {1,2,1,1,1,1,2,1,1,1,1,1,2,1,1,2,1,1,1,1,1,2,1,1,1,1,2,1},
    {1,3,2,2,1,1,2,2,2,2,2,2,2,0,0,2,2,2,2,2,2,2,1,1,2,2,3,1},
    {1,1,1,2,1,1,2,1,1,2,1,1,1,1,1,1,1,1,2,1,1,2,1,1,2,1,1,1},
    {1,1,1,2,1,1,2,1,1,2,1,1,1,1,1,1,1,1,2,1,1,2,1,1,2,1,1,1},
    {1,2,2,2,2,2,2,1,1,2,2,2,2,1,1,2,2,2,2,1,1,2,2,2,2,2,2,1},
    {1,2,1,1,1,1,1,1,1,1,1,1,2,1,1,2,1,1,1,1,1,1,1,1,1,1,2,1},
    {1,2,1,1,1,1,1,1,1,1,1,1,2,1,1,2,1,1,1,1,1,1,1,1,1,1,2,1},
    {1,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,1},
    {1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1},
};

/* Ghost house positions */
#define GHOST_HOUSE_X  13
#define GHOST_HOUSE_Y  14
#define GHOST_DOOR_Y   12

/* Starting positions */
#define PAC_START_X    13
#define PAC_START_Y    23
#define BLINKY_START_X 13
#define BLINKY_START_Y 11
#define PINKY_START_X  13
#define PINKY_START_Y  14
#define INKY_START_X   11
#define INKY_START_Y   14
#define CLYDE_START_X  15
#define CLYDE_START_Y  14

/* Tunnel tiles (row 14) */
#define TUNNEL_Y       14

/* Direction vectors */
static const WORD dir_dx[4] = { 1, 0, -1, 0 };
static const WORD dir_dy[4] = { 0, 1, 0, -1 };

/* Check if a tile is passable for pac-man */
static WORD tile_passable(UBYTE tile)
{
    return tile != T_WALL && tile != T_GHOST_DOOR;
}

/* Check if a tile is passable for ghosts */
static WORD tile_passable_ghost(UBYTE tile, WORD state)
{
    if (tile == T_WALL) return 0;
    if (tile == T_GHOST_DOOR && state == GHOST_EATEN) return 1;
    if (tile == T_GHOST_DOOR) return 0;
    return 1;
}

/* Get maze tile, with wrapping for tunnel */
static UBYTE get_tile(GameState *gs, WORD tx, WORD ty)
{
    if (ty < 0 || ty >= MAZE_H) return T_WALL;
    if (tx < 0 || tx >= MAZE_W) {
        /* Tunnel row is passable */
        if (ty == TUNNEL_Y) return T_EMPTY;
        return T_WALL;
    }
    return gs->maze[ty][tx];
}

/* Distance squared between two tiles (for ghost targeting) */
static LONG dist_sq(WORD x1, WORD y1, WORD x2, WORD y2)
{
    LONG dx = (LONG)(x2 - x1);
    LONG dy = (LONG)(y2 - y1);
    return dx * dx + dy * dy;
}

/* Opposite direction */
static WORD opposite_dir(WORD d)
{
    return (d + 2) & 3;
}

/* Convert tile to pixel */
static WORD tile_to_px(WORD t)
{
    return t * TILE_SIZE;
}

/* Convert pixel to tile */
static WORD px_to_tile(WORD p)
{
    return p / TILE_SIZE;
}

/* Check if entity is aligned to tile center */
static WORD is_centered(WORD px, WORD py)
{
    return (px % TILE_SIZE == 0) && (py % TILE_SIZE == 0);
}

void game_init(GameState *gs)
{
    memset(gs, 0, sizeof(GameState));
    gs->lives = 3;
    gs->level = 1;
    gs->state = STATE_TITLE;
    gs->num_players = 1;
    gs->hiscore = 10000;
}

static void reset_maze(GameState *gs)
{
    WORD x, y;
    gs->dots_total = 0;
    gs->dots_eaten = 0;
    gs->dots_eaten_global = 0;

    for (y = 0; y < MAZE_H; y++) {
        for (x = 0; x < MAZE_W; x++) {
            gs->maze[y][x] = maze_template[y][x];
            if (maze_template[y][x] == T_DOT || maze_template[y][x] == T_POWER)
                gs->dots_total++;
        }
    }
}

static void place_ghost(Ghost *g, WORD tx, WORD ty, WORD in_house)
{
    g->tile_x = tx;
    g->tile_y = ty;
    g->pixel_x = tile_to_px(tx);
    g->pixel_y = tile_to_px(ty);
    g->sub_x = 0;
    g->sub_y = 0;
    g->dir = DIR_UP;
    g->next_dir = DIR_UP;
    g->state = GHOST_SCATTER;
    g->home = in_house;
    g->fright_timer = 0;
}

void game_start_level(GameState *gs)
{
    reset_maze(gs);
    game_start_life(gs);
    gs->mode = 0; /* scatter */
    gs->mode_timer = 0;
    gs->mode_phase = 0;
    gs->fruit_active = 0;
}

void game_start_life(GameState *gs)
{
    /* Place pac-man */
    gs->pac.tile_x = PAC_START_X;
    gs->pac.tile_y = PAC_START_Y;
    gs->pac.pixel_x = tile_to_px(PAC_START_X);
    gs->pac.pixel_y = tile_to_px(PAC_START_Y);
    gs->pac.sub_x = 0;
    gs->pac.sub_y = 0;
    gs->pac.dir = DIR_LEFT;
    gs->pac.next_dir = DIR_LEFT;
    gs->pac.anim_frame = 0;
    gs->pac.anim_timer = 0;
    gs->pac.mouth_open = 1;

    /* Place ghosts */
    place_ghost(&gs->ghosts[GHOST_BLINKY], BLINKY_START_X, BLINKY_START_Y, 0);
    gs->ghosts[GHOST_BLINKY].dir = DIR_LEFT;
    gs->ghosts[GHOST_BLINKY].dot_counter = 0;

    place_ghost(&gs->ghosts[GHOST_PINKY], PINKY_START_X, PINKY_START_Y, 1);
    gs->ghosts[GHOST_PINKY].dot_counter = 0;

    place_ghost(&gs->ghosts[GHOST_INKY], INKY_START_X, INKY_START_Y, 1);
    gs->ghosts[GHOST_INKY].dot_counter = 30;

    place_ghost(&gs->ghosts[GHOST_CLYDE], CLYDE_START_X, CLYDE_START_Y, 1);
    gs->ghosts[GHOST_CLYDE].dot_counter = 60;

    gs->fright_active = 0;
    gs->fright_timer = 0;
    gs->ghosts_eaten = 0;
    gs->popup_active = 0;

    gs->state = STATE_READY;
    gs->state_timer = READY_TIME;
}

/* Move entity by speed, handling tile transitions */
static void move_entity(WORD *px, WORD *py, WORD *sx, WORD *sy,
                         WORD *tx, WORD *ty, WORD dir, WORD speed)
{
    if (dir == DIR_NONE) return;

    *sx += dir_dx[dir] * speed;
    *sy += dir_dy[dir] * speed;

    /* Convert fractional to pixel */
    while (*sx >= 256) { (*px)++; *sx -= 256; }
    while (*sx < 0)    { (*px)--; *sx += 256; }
    while (*sy >= 256) { (*py)++; *sy -= 256; }
    while (*sy < 0)    { (*py)--; *sy += 256; }

    /* Update tile */
    *tx = px_to_tile(*px + TILE_SIZE / 2);
    *ty = px_to_tile(*py + TILE_SIZE / 2);

    /* Tunnel wrapping */
    if (*px < -TILE_SIZE) {
        *px = MAZE_W * TILE_SIZE;
        *sx = 0;
    } else if (*px >= MAZE_W * TILE_SIZE) {
        *px = -TILE_SIZE;
        *sx = 0;
    }
}

/* Choose best direction toward target, never reversing */
static WORD choose_direction(GameState *gs, Ghost *g, WORD target_x, WORD target_y)
{
    WORD best_dir = g->dir;
    LONG best_dist = 0x7FFFFFFFL;
    WORD d;
    WORD opp = opposite_dir(g->dir);

    /* Priority: UP, LEFT, DOWN, RIGHT (classic Pac-Man) */
    static const WORD priority[4] = { DIR_UP, DIR_LEFT, DIR_DOWN, DIR_RIGHT };

    for (d = 0; d < 4; d++) {
        WORD try_dir = priority[d];
        WORD nx, ny;
        UBYTE tile;
        LONG dd;

        if (try_dir == opp) continue; /* no reversals */

        nx = g->tile_x + dir_dx[try_dir];
        ny = g->tile_y + dir_dy[try_dir];
        tile = get_tile(gs, nx, ny);

        if (!tile_passable_ghost(tile, g->state)) continue;

        /* Ghosts can't go up in certain tiles (classic restriction) */
        if (try_dir == DIR_UP) {
            if ((g->tile_y == 12 || g->tile_y == 24) &&
                (g->tile_x == 12 || g->tile_x == 15)) {
                if (g->state != GHOST_EATEN) continue;
            }
        }

        dd = dist_sq(nx, ny, target_x, target_y);
        if (dd < best_dist) {
            best_dist = dd;
            best_dir = try_dir;
        }
    }
    return best_dir;
}

/* Ghost leaves the ghost house */
static void ghost_leave_house(Ghost *g)
{
    /* Move to door position */
    g->pixel_x = tile_to_px(GHOST_HOUSE_X);
    g->pixel_y = tile_to_px(GHOST_DOOR_Y);
    g->tile_x = GHOST_HOUSE_X;
    g->tile_y = GHOST_DOOR_Y;
    g->sub_x = 0;
    g->sub_y = 0;
    g->dir = DIR_LEFT;
    g->home = 0;
}

/* Update ghost targeting */
static void update_ghost_target(GameState *gs, Ghost *g, WORD id)
{
    if (g->state == GHOST_SCATTER) {
        g->target_x = scatter_target[id][0];
        g->target_y = scatter_target[id][1];
        return;
    }

    if (g->state == GHOST_EATEN) {
        /* Return to ghost house */
        g->target_x = GHOST_HOUSE_X;
        g->target_y = GHOST_DOOR_Y;
        return;
    }

    if (g->state == GHOST_FRIGHT) {
        /* Random-ish target: use frame counter for pseudo-random */
        g->target_x = (gs->frame * 7 + id * 13) % MAZE_W;
        g->target_y = (gs->frame * 11 + id * 17) % MAZE_H;
        return;
    }

    /* Chase mode: each ghost has unique targeting */
    switch (id) {
    case GHOST_BLINKY:
        /* Targets pac-man directly */
        g->target_x = gs->pac.tile_x;
        g->target_y = gs->pac.tile_y;
        break;

    case GHOST_PINKY:
        /* Targets 4 tiles ahead of pac-man */
        g->target_x = gs->pac.tile_x + dir_dx[gs->pac.dir] * 4;
        g->target_y = gs->pac.tile_y + dir_dy[gs->pac.dir] * 4;
        /* Classic bug: UP also adds 4 tiles to the left */
        if (gs->pac.dir == DIR_UP) {
            g->target_x -= 4;
        }
        break;

    case GHOST_INKY: {
        /* Complex: 2 tiles ahead of pac, then double vector from Blinky */
        WORD ahead_x = gs->pac.tile_x + dir_dx[gs->pac.dir] * 2;
        WORD ahead_y = gs->pac.tile_y + dir_dy[gs->pac.dir] * 2;
        g->target_x = ahead_x + (ahead_x - gs->ghosts[GHOST_BLINKY].tile_x);
        g->target_y = ahead_y + (ahead_y - gs->ghosts[GHOST_BLINKY].tile_y);
        break;
    }

    case GHOST_CLYDE: {
        /* Targets pac-man when far, scatter corner when close */
        LONG d = dist_sq(g->tile_x, g->tile_y,
                         gs->pac.tile_x, gs->pac.tile_y);
        if (d > 64) { /* > 8 tiles away */
            g->target_x = gs->pac.tile_x;
            g->target_y = gs->pac.tile_y;
        } else {
            g->target_x = scatter_target[GHOST_CLYDE][0];
            g->target_y = scatter_target[GHOST_CLYDE][1];
        }
        break;
    }
    }
}

static void update_ghost(GameState *gs, Ghost *g, WORD id)
{
    WORD speed;
    WORD in_tunnel;

    /* Ghost in house: wait for dot counter */
    if (g->home) {
        if (gs->dots_eaten_global >= g->dot_counter) {
            ghost_leave_house(g);
        }
        return;
    }

    /* Determine speed */
    in_tunnel = (g->tile_y == TUNNEL_Y && (g->tile_x < 6 || g->tile_x >= 22));
    if (g->state == GHOST_EATEN) {
        speed = GHOST_EATEN_SPD;
    } else if (g->state == GHOST_FRIGHT) {
        speed = GHOST_FRIGHT_SPD;
    } else if (in_tunnel) {
        speed = GHOST_TUNNEL_SPD;
    } else {
        speed = GHOST_SPEED;
        /* Speed up slightly with levels */
        if (gs->level >= 2) speed += 16;
        if (gs->level >= 5) speed += 16;
    }

    /* At tile center: choose direction */
    if (is_centered(g->pixel_x, g->pixel_y)) {
        /* Check if eaten ghost reached home */
        if (g->state == GHOST_EATEN) {
            if (g->tile_x == GHOST_HOUSE_X &&
                (g->tile_y == GHOST_DOOR_Y || g->tile_y == GHOST_DOOR_Y + 1)) {
                g->state = gs->mode ? GHOST_CHASE : GHOST_SCATTER;
                g->pixel_y = tile_to_px(GHOST_DOOR_Y);
                g->tile_y = GHOST_DOOR_Y;
                g->sub_y = 0;
            }
        }

        update_ghost_target(gs, g, id);
        g->dir = choose_direction(gs, g, g->target_x, g->target_y);
    }

    move_entity(&g->pixel_x, &g->pixel_y, &g->sub_x, &g->sub_y,
                &g->tile_x, &g->tile_y, g->dir, speed);
}

static void reverse_all_ghosts(GameState *gs)
{
    WORD i;
    for (i = 0; i < NUM_GHOSTS; i++) {
        if (!gs->ghosts[i].home && gs->ghosts[i].state != GHOST_EATEN) {
            gs->ghosts[i].dir = opposite_dir(gs->ghosts[i].dir);
        }
    }
}

static void enter_fright_mode(GameState *gs)
{
    WORD i;
    WORD fright_time = FRIGHT_TIME;

    /* Reduce fright time at higher levels */
    if (gs->level >= 3) fright_time = 250;
    if (gs->level >= 5) fright_time = 150;
    if (gs->level >= 8) fright_time = 50;
    if (gs->level >= 11) fright_time = 0; /* no fright at high levels */

    if (fright_time == 0) return;

    gs->fright_active = 1;
    gs->fright_timer = fright_time;
    gs->ghosts_eaten = 0;

    for (i = 0; i < NUM_GHOSTS; i++) {
        if (gs->ghosts[i].state != GHOST_EATEN && !gs->ghosts[i].home) {
            gs->ghosts[i].state = GHOST_FRIGHT;
            gs->ghosts[i].fright_timer = fright_time;
            gs->ghosts[i].dir = opposite_dir(gs->ghosts[i].dir);
        }
    }
}

static void end_fright_mode(GameState *gs)
{
    WORD i;
    gs->fright_active = 0;
    gs->fright_timer = 0;

    for (i = 0; i < NUM_GHOSTS; i++) {
        if (gs->ghosts[i].state == GHOST_FRIGHT) {
            gs->ghosts[i].state = gs->mode ? GHOST_CHASE : GHOST_SCATTER;
            gs->ghosts[i].fright_timer = 0;
        }
    }
}

/* Scatter/Chase mode timer */
static const LONG mode_durations[] = {
    SCATTER1_TIME, CHASE1_TIME,
    SCATTER2_TIME, CHASE2_TIME,
    SCATTER3_TIME, CHASE3_TIME,
    SCATTER4_TIME, 0 /* permanent chase */
};
#define NUM_MODE_PHASES 8

static void update_mode(GameState *gs)
{
    WORD i;

    if (gs->fright_active) return;

    gs->mode_timer++;

    if (gs->mode_phase < NUM_MODE_PHASES - 1) {
        if (gs->mode_timer >= mode_durations[gs->mode_phase]) {
            gs->mode_timer = 0;
            gs->mode_phase++;
            gs->mode = gs->mode_phase & 1; /* even=scatter, odd=chase */

            /* Reverse ghost directions on mode change */
            reverse_all_ghosts(gs);

            /* Update ghost states */
            for (i = 0; i < NUM_GHOSTS; i++) {
                if (gs->ghosts[i].state != GHOST_EATEN &&
                    gs->ghosts[i].state != GHOST_FRIGHT &&
                    !gs->ghosts[i].home) {
                    gs->ghosts[i].state = gs->mode ? GHOST_CHASE : GHOST_SCATTER;
                }
            }
        }
    }
}

static void update_pac(GameState *gs, InputState *input)
{
    WORD speed = PACMAN_SPEED;
    WORD next_tx, next_ty;
    UBYTE next_tile;

    /* Buffer direction input */
    if (input->dir != DIR_NONE) {
        gs->pac.next_dir = input->dir;
    }

    /* Speed boost when eating dots (classic behavior) */
    if (gs->fright_active) speed += 16;

    /* At tile center: try to turn */
    if (is_centered(gs->pac.pixel_x, gs->pac.pixel_y)) {
        /* Try buffered direction first */
        next_tx = gs->pac.tile_x + dir_dx[gs->pac.next_dir];
        next_ty = gs->pac.tile_y + dir_dy[gs->pac.next_dir];
        next_tile = get_tile(gs, next_tx, next_ty);

        if (tile_passable(next_tile)) {
            gs->pac.dir = gs->pac.next_dir;
        } else {
            /* Try to continue current direction */
            next_tx = gs->pac.tile_x + dir_dx[gs->pac.dir];
            next_ty = gs->pac.tile_y + dir_dy[gs->pac.dir];
            next_tile = get_tile(gs, next_tx, next_ty);

            if (!tile_passable(next_tile)) {
                /* Stop: wall ahead */
                return;
            }
        }

        /* Eat dots */
        {
            UBYTE cur = gs->maze[gs->pac.tile_y][gs->pac.tile_x];
            if (cur == T_DOT) {
                gs->maze[gs->pac.tile_y][gs->pac.tile_x] = T_EMPTY;
                gs->score += SCORE_DOT;
                gs->dots_eaten++;
                gs->dots_eaten_global++;
                gs->ev_flags |= EV_CHOMP;
            } else if (cur == T_POWER) {
                gs->maze[gs->pac.tile_y][gs->pac.tile_x] = T_EMPTY;
                gs->score += SCORE_POWER;
                gs->dots_eaten++;
                gs->dots_eaten_global++;
                gs->ev_flags |= EV_POWER;
                enter_fright_mode(gs);
            }
        }

        /* Fruit trigger: at 70 and 170 dots */
        if (!gs->fruit_active &&
            (gs->dots_eaten == 70 || gs->dots_eaten == 170)) {
            gs->fruit_active = 1;
            gs->fruit_timer = 500; /* ~10 seconds */
            gs->fruit_type = (gs->level - 1) % MAX_FRUIT_TYPE;
            gs->fruit_x = tile_to_px(13);
            gs->fruit_y = tile_to_px(17);
        }

        /* Check level complete */
        if (gs->dots_eaten >= gs->dots_total) {
            gs->state = STATE_LEVEL_DONE;
            gs->state_timer = LEVEL_DONE_TIME;
            return;
        }

        /* Extra life */
        if (!gs->extra_life_given && gs->score >= EXTRA_LIFE_SCORE) {
            gs->extra_life_given = 1;
            gs->lives++;
            gs->ev_flags |= EV_EXTRA;
        }
    }

    /* Animate mouth */
    gs->pac.anim_timer++;
    if (gs->pac.anim_timer >= 3) {
        gs->pac.anim_timer = 0;
        gs->pac.mouth_open = !gs->pac.mouth_open;
    }

    move_entity(&gs->pac.pixel_x, &gs->pac.pixel_y,
                &gs->pac.sub_x, &gs->pac.sub_y,
                &gs->pac.tile_x, &gs->pac.tile_y,
                gs->pac.dir, speed);
}

static void check_ghost_collision(GameState *gs)
{
    WORD i;
    for (i = 0; i < NUM_GHOSTS; i++) {
        Ghost *g = &gs->ghosts[i];
        WORD dx, dy;

        if (g->home) continue;

        dx = gs->pac.pixel_x - g->pixel_x;
        dy = gs->pac.pixel_y - g->pixel_y;
        if (dx < 0) dx = -dx;
        if (dy < 0) dy = -dy;

        if (dx < 6 && dy < 6) {
            if (g->state == GHOST_FRIGHT) {
                /* Eat ghost */
                g->state = GHOST_EATEN;
                g->fright_timer = 0;
                gs->ghosts_eaten++;

                /* Score: 200, 400, 800, 1600 */
                {
                    LONG ghost_score = SCORE_GHOST1;
                    WORD e;
                    for (e = 1; e < gs->ghosts_eaten; e++) ghost_score *= 2;
                    gs->score += ghost_score;
                    gs->popup_active = 1;
                    gs->popup_timer = EAT_GHOST_TIME;
                    gs->popup_x = g->pixel_x;
                    gs->popup_y = g->pixel_y;
                    gs->popup_score = ghost_score;
                }

                gs->ev_flags |= EV_EAT_GHOST;
                gs->state = STATE_EAT_GHOST;
                gs->state_timer = EAT_GHOST_TIME;
            } else if (g->state != GHOST_EATEN) {
                /* Pac-man dies */
                gs->state = STATE_DYING;
                gs->state_timer = DYING_TIME;
                gs->ev_flags |= EV_DIE;
                return;
            }
        }
    }
}

void game_update(GameState *gs, InputState *input)
{
    WORD i;
    gs->frame++;

    switch (gs->state) {
    case STATE_TITLE:
        gs->title_blink++;
        if (input->start) {
            gs->lives = 3;
            gs->score = 0;
            gs->level = 1;
            gs->extra_life_given = 0;
            game_start_level(gs);
        }
        break;

    case STATE_READY:
        gs->state_timer--;
        if (gs->state_timer <= 0) {
            gs->state = STATE_PLAYING;
        }
        break;

    case STATE_PLAYING:
        update_pac(gs, input);
        if (gs->state != STATE_PLAYING) break; /* level done or dying */

        update_mode(gs);

        for (i = 0; i < NUM_GHOSTS; i++) {
            update_ghost(gs, &gs->ghosts[i], i);
        }

        /* Fright timer */
        if (gs->fright_active) {
            gs->fright_timer--;
            if (gs->fright_timer <= 0) {
                end_fright_mode(gs);
            }
        }

        /* Fruit timer */
        if (gs->fruit_active) {
            gs->fruit_timer--;
            if (gs->fruit_timer <= 0) {
                gs->fruit_active = 0;
            }
            /* Check pac-man eats fruit */
            if (gs->fruit_active) {
                WORD dx = gs->pac.pixel_x - gs->fruit_x;
                WORD dy = gs->pac.pixel_y - gs->fruit_y;
                if (dx < 0) dx = -dx;
                if (dy < 0) dy = -dy;
                if (dx < 6 && dy < 6) {
                    gs->score += fruit_scores[gs->fruit_type];
                    gs->popup_active = 1;
                    gs->popup_timer = 60;
                    gs->popup_x = gs->fruit_x;
                    gs->popup_y = gs->fruit_y;
                    gs->popup_score = fruit_scores[gs->fruit_type];
                    gs->fruit_active = 0;
                    gs->ev_flags |= EV_FRUIT;
                }
            }
        }

        /* Score popup timer */
        if (gs->popup_active) {
            gs->popup_timer--;
            if (gs->popup_timer <= 0) gs->popup_active = 0;
        }

        check_ghost_collision(gs);

        /* Update siren speed based on dots eaten */
        gs->siren_speed = gs->dots_eaten * 3 / gs->dots_total;
        break;

    case STATE_EAT_GHOST:
        gs->state_timer--;
        if (gs->popup_active) {
            gs->popup_timer--;
            if (gs->popup_timer <= 0) gs->popup_active = 0;
        }
        if (gs->state_timer <= 0) {
            gs->state = STATE_PLAYING;
        }
        break;

    case STATE_DYING:
        gs->state_timer--;
        if (gs->state_timer <= 0) {
            gs->lives--;
            if (gs->lives <= 0) {
                if (gs->score > gs->hiscore) gs->hiscore = gs->score;
                gs->state = STATE_GAMEOVER;
                gs->state_timer = 150;
            } else {
                game_start_life(gs);
            }
        }
        break;

    case STATE_GAMEOVER:
        gs->state_timer--;
        gs->title_blink++;
        if (gs->state_timer <= 0 && input->start) {
            gs->state = STATE_TITLE;
        }
        break;

    case STATE_LEVEL_DONE:
        gs->state_timer--;
        if (gs->state_timer <= 0) {
            gs->level++;
            game_start_level(gs);
        }
        break;
    }
}
