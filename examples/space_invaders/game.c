#include <exec/types.h>
#include <string.h>
#include "game.h"
#include "sound.h"

static ULONG rng_state = 12345;

void game_srand(ULONG seed) { rng_state = seed; }

UWORD game_rand(void)
{
    rng_state = rng_state * 1103515245 + 12345;
    return (UWORD)((rng_state >> 16) & 0x7FFF);
}

/* Point values per alien type */
static const WORD alien_points[] = { 30, 20, 10 };

/* Get alien type for a row (0=top) */
static WORD alien_type_for_row(WORD row)
{
    if (row == 0) return ALIEN_TYPE_C;
    if (row <= 2) return ALIEN_TYPE_B;
    return ALIEN_TYPE_A;
}

static void init_shields(GameState *gs)
{
    int s, x, y;
    WORD spacing = SCREEN_W / (SHIELD_COUNT + 1);

    for (s = 0; s < SHIELD_COUNT; s++) {
        gs->shields[s].x = spacing * (s + 1) - SHIELD_W / 2;
        gs->shields[s].y = SHIELD_Y;
        memset(gs->shields[s].pixels, 0, sizeof(gs->shields[s].pixels));

        /* Create arch shape */
        for (y = 0; y < SHIELD_H; y++) {
            for (x = 0; x < SHIELD_W; x++) {
                /* Top arch */
                if (y < 4) {
                    WORD cx = SHIELD_W / 2;
                    WORD dx = x - cx;
                    WORD dy = y;
                    if (dx * dx + dy * dy * 4 <= (SHIELD_W / 2) * (SHIELD_W / 2))
                        gs->shields[s].pixels[y][x] = 1;
                }
                /* Body */
                else if (y < SHIELD_H - 4) {
                    gs->shields[s].pixels[y][x] = 1;
                }
                /* Bottom cutout (arch underneath) */
                else {
                    WORD cx = SHIELD_W / 2;
                    WORD dx = x - cx;
                    WORD margin = SHIELD_H - y;
                    if (dx < -3 || dx > 3)
                        gs->shields[s].pixels[y][x] = 1;
                    else if (margin > 2)
                        gs->shields[s].pixels[y][x] = 1;
                }
            }
        }
    }
}

static void spawn_explosion(GameState *gs, WORD x, WORD y, UBYTE color)
{
    int i;
    for (i = 0; i < MAX_EXPLOSIONS; i++) {
        if (!gs->explosions[i].active) {
            gs->explosions[i].x = x;
            gs->explosions[i].y = y;
            gs->explosions[i].timer = 10;
            gs->explosions[i].active = TRUE;
            gs->explosions[i].color = color;
            return;
        }
    }
}

void game_init(GameState *gs)
{
    memset(gs, 0, sizeof(GameState));
    gs->state = STATE_TITLE;
    gs->lives = 3;
    gs->hiscore = 0;
    gs->wave = 1;
    gs->player_x = SCREEN_W / 2 - PLAYER_W / 2;
}

void game_init_wave(GameState *gs)
{
    int r, c, i;

    /* Init alien grid */
    for (r = 0; r < ALIEN_ROWS; r++)
        for (c = 0; c < ALIEN_COLS; c++)
            gs->swarm.alive[r][c] = TRUE;

    gs->swarm.grid_x = ALIEN_START_X;
    gs->swarm.grid_y = ALIEN_START_Y;
    gs->swarm.dir = 1;
    gs->swarm.move_timer = 0;
    gs->swarm.anim_frame = 0;
    gs->swarm.alive_count = ALIEN_ROWS * ALIEN_COLS;
    gs->swarm.fire_timer = 0;

    /* Speed increases each wave */
    gs->swarm.move_delay = 2;
    if (gs->wave >= 3) gs->swarm.move_delay = 1;

    gs->swarm.fire_delay = 30 - (gs->wave - 1) * 5;
    if (gs->swarm.fire_delay < 8) gs->swarm.fire_delay = 8;

    /* Clear bullets */
    gs->player_bullet.active = FALSE;
    for (i = 0; i < MAX_ALIEN_BULLETS; i++)
        gs->alien_bullets[i].active = FALSE;

    /* Clear UFO */
    gs->ufo.active = FALSE;
    gs->ufo.timer = 300 + (game_rand() % 400);

    /* Clear explosions */
    for (i = 0; i < MAX_EXPLOSIONS; i++)
        gs->explosions[i].active = FALSE;

    /* Reset shields */
    init_shields(gs);

    /* Center player */
    gs->player_x = SCREEN_W / 2 - PLAYER_W / 2;

    gs->state = STATE_PLAYING;
    gs->state_timer = 0;
    gs->march_note = 0;

    /* Clear events */
    gs->ev_shoot = FALSE;
    gs->ev_alien_hit = FALSE;
    gs->ev_player_hit = FALSE;
    gs->ev_ufo_hit = FALSE;
    gs->ev_march = FALSE;
}

/* Damage shield pixels in a radius around (sx, sy) relative to shield */
static void damage_shield(Shield *sh, WORD sx, WORD sy, WORD radius)
{
    WORD y, x;
    for (y = sy - radius; y <= sy + radius; y++) {
        for (x = sx - radius; x <= sx + radius; x++) {
            if (x >= 0 && x < SHIELD_W && y >= 0 && y < SHIELD_H) {
                WORD dx = x - sx, dy = y - sy;
                if (dx * dx + dy * dy <= radius * radius + 1)
                    sh->pixels[y][x] = 0;
            }
        }
    }
}

/* Check bullet vs shield collision, returns TRUE if hit */
static BOOL check_bullet_shield(GameState *gs, Bullet *b, BOOL from_above)
{
    int s;
    for (s = 0; s < SHIELD_COUNT; s++) {
        Shield *sh = &gs->shields[s];
        WORD bx = b->x - sh->x;
        WORD by = b->y - sh->y;

        if (bx >= 0 && bx < SHIELD_W && by >= 0 && by < SHIELD_H) {
            if (sh->pixels[by][bx]) {
                damage_shield(sh, bx, by, 2);
                b->active = FALSE;
                return TRUE;
            }
        }
    }
    return FALSE;
}

/* Find the lowest alive alien row in each column, used for firing */
static WORD find_lowest_alien(GameState *gs, WORD col)
{
    WORD row;
    for (row = ALIEN_ROWS - 1; row >= 0; row--) {
        if (gs->swarm.alive[row][col]) return row;
    }
    return -1;
}

static void alien_fire(GameState *gs)
{
    int slot = -1, i, tries;
    for (i = 0; i < MAX_ALIEN_BULLETS; i++) {
        if (!gs->alien_bullets[i].active) { slot = i; break; }
    }
    if (slot < 0) return;

    for (tries = 0; tries < 22; tries++) {
        WORD col = game_rand() % ALIEN_COLS;
        WORD row = find_lowest_alien(gs, col);
        if (row >= 0) {
            gs->alien_bullets[slot].x = gs->swarm.grid_x + col * ALIEN_CELL_W + ALIEN_W / 2;
            gs->alien_bullets[slot].y = gs->swarm.grid_y + row * ALIEN_CELL_H + ALIEN_H;
            gs->alien_bullets[slot].active = TRUE;
            return;
        }
    }
}

static void update_playing(GameState *gs, InputState *input)
{
    int i, r, c;

    /* Clear events */
    gs->ev_shoot = FALSE;
    gs->ev_alien_hit = FALSE;
    gs->ev_player_hit = FALSE;
    gs->ev_ufo_hit = FALSE;
    gs->ev_march = FALSE;

    /* Player movement */
    if (input->left) gs->player_x -= PLAYER_SPEED;
    if (input->right) gs->player_x += PLAYER_SPEED;
    gs->player_x += input->mouse_dx;

    if (gs->player_x < PLAYER_MIN_X) gs->player_x = PLAYER_MIN_X;
    if (gs->player_x > PLAYER_MAX_X) gs->player_x = PLAYER_MAX_X;

    /* Player fire */
    if (input->fire_pressed && !gs->player_bullet.active) {
        gs->player_bullet.x = gs->player_x + PLAYER_W / 2;
        gs->player_bullet.y = PLAYER_Y - 2;
        gs->player_bullet.active = TRUE;
        gs->ev_shoot = TRUE;
    }

    /* Update player bullet */
    if (gs->player_bullet.active) {
        gs->player_bullet.y -= PLAYER_BULLET_SPEED;
        if (gs->player_bullet.y < 30) {
            gs->player_bullet.active = FALSE;
        }
        /* Check vs shields (from below) */
        check_bullet_shield(gs, &gs->player_bullet, FALSE);
    }

    /* Player bullet vs aliens */
    if (gs->player_bullet.active) {
        WORD bx = gs->player_bullet.x;
        WORD by = gs->player_bullet.y;

        for (r = 0; r < ALIEN_ROWS; r++) {
            for (c = 0; c < ALIEN_COLS; c++) {
                if (!gs->swarm.alive[r][c]) continue;
                WORD ax = gs->swarm.grid_x + c * ALIEN_CELL_W;
                WORD ay = gs->swarm.grid_y + r * ALIEN_CELL_H;
                if (bx >= ax && bx < ax + ALIEN_W &&
                    by >= ay && by < ay + ALIEN_H) {
                    gs->swarm.alive[r][c] = FALSE;
                    gs->swarm.alive_count--;
                    gs->player_bullet.active = FALSE;
                    gs->score += alien_points[alien_type_for_row(r)];
                    if (gs->score > gs->hiscore) gs->hiscore = gs->score;
                    gs->ev_alien_hit = TRUE;
                    spawn_explosion(gs, ax + ALIEN_W / 2, ay + ALIEN_H / 2, COL_WHITE);
                    goto bullet_done;
                }
            }
        }
    }
bullet_done:

    /* Player bullet vs UFO */
    if (gs->player_bullet.active && gs->ufo.active) {
        WORD bx = gs->player_bullet.x;
        WORD by = gs->player_bullet.y;
        if (bx >= gs->ufo.x && bx < gs->ufo.x + UFO_W &&
            by >= UFO_Y && by < UFO_Y + UFO_H) {
            gs->ufo.active = FALSE;
            gs->player_bullet.active = FALSE;
            /* UFO scores: 50, 100, 150, or 300 */
            {
                static const WORD ufo_scores[] = {50, 100, 150, 300};
                gs->ufo.score_val = ufo_scores[game_rand() % 4];
            }
            gs->score += gs->ufo.score_val;
            if (gs->score > gs->hiscore) gs->hiscore = gs->score;
            gs->ev_ufo_hit = TRUE;
            spawn_explosion(gs, gs->ufo.x + UFO_W / 2, UFO_Y + UFO_H / 2, COL_RED);
        }
    }

    /* Alien movement */
    gs->swarm.move_timer++;
    {
        /* Speed based on alive count - fewer aliens = much faster */
        WORD alive = gs->swarm.alive_count;
        WORD delay, step;

        /* Delay: frames between each march step */
        if (alive <= 1)       delay = 1;
        else if (alive <= 5)  delay = 1;
        else if (alive <= 10) delay = 1;
        else if (alive <= 20) delay = 1;
        else if (alive <= 35) delay = 2;
        else                  delay = gs->swarm.move_delay;

        /* Step: pixels moved per march step */
        if (alive <= 1)       step = 10;
        else if (alive <= 5)  step = 8;
        else if (alive <= 10) step = 5;
        else if (alive <= 20) step = 4;
        else                  step = 3;

        if (gs->swarm.move_timer >= delay) {
            gs->swarm.move_timer = 0;
            gs->swarm.anim_frame ^= 1;
            gs->ev_march = TRUE;
            gs->march_note = (gs->march_note + 1) % 4;

            /* Find actual grid bounds (skip empty edge columns) */
            WORD left_col = ALIEN_COLS, right_col = -1;
            for (c = 0; c < ALIEN_COLS; c++) {
                for (r = 0; r < ALIEN_ROWS; r++) {
                    if (gs->swarm.alive[r][c]) {
                        if (c < left_col) left_col = c;
                        if (c > right_col) right_col = c;
                        break;
                    }
                }
            }

            WORD left_edge = gs->swarm.grid_x + left_col * ALIEN_CELL_W;
            WORD right_edge = gs->swarm.grid_x + (right_col + 1) * ALIEN_CELL_W;

            /* Check if we need to drop down and reverse */
            BOOL drop = FALSE;
            if (gs->swarm.dir > 0 && right_edge >= SCREEN_W - 8) drop = TRUE;
            if (gs->swarm.dir < 0 && left_edge <= 8) drop = TRUE;

            if (drop) {
                gs->swarm.grid_y += 6;
                gs->swarm.dir = -gs->swarm.dir;
            } else {
                gs->swarm.grid_x += gs->swarm.dir * step;
            }
        }
    }

    /* Alien firing */
    gs->swarm.fire_timer++;
    if (gs->swarm.fire_timer >= gs->swarm.fire_delay) {
        gs->swarm.fire_timer = 0;
        alien_fire(gs);
    }

    /* Update alien bullets */
    for (i = 0; i < MAX_ALIEN_BULLETS; i++) {
        if (!gs->alien_bullets[i].active) continue;
        gs->alien_bullets[i].y += ALIEN_BULLET_SPEED;

        /* Off screen */
        if (gs->alien_bullets[i].y > SCREEN_H) {
            gs->alien_bullets[i].active = FALSE;
            continue;
        }

        /* Check vs shields */
        check_bullet_shield(gs, &gs->alien_bullets[i], TRUE);
        if (!gs->alien_bullets[i].active) continue;

        /* Check vs player */
        {
            WORD bx = gs->alien_bullets[i].x;
            WORD by = gs->alien_bullets[i].y;
            if (bx >= gs->player_x && bx < gs->player_x + PLAYER_W &&
                by >= PLAYER_Y && by < PLAYER_Y + PLAYER_H) {
                gs->alien_bullets[i].active = FALSE;
                gs->ev_player_hit = TRUE;
                gs->state = STATE_DYING;
                gs->state_timer = 60;
                spawn_explosion(gs, gs->player_x + PLAYER_W / 2, PLAYER_Y + PLAYER_H / 2, COL_YELLOW);
            }
        }
    }

    /* UFO logic */
    if (!gs->ufo.active) {
        gs->ufo.timer--;
        if (gs->ufo.timer <= 0) {
            gs->ufo.active = TRUE;
            gs->ufo.dir = (game_rand() & 1) ? 1 : -1;
            gs->ufo.x = (gs->ufo.dir > 0) ? -UFO_W : SCREEN_W;
        }
    } else {
        gs->ufo.x += gs->ufo.dir * UFO_SPEED;
        if (gs->ufo.x < -UFO_W - 10 || gs->ufo.x > SCREEN_W + 10) {
            gs->ufo.active = FALSE;
            gs->ufo.timer = 400 + (game_rand() % 600);
        }
    }

    /* Update explosions */
    for (i = 0; i < MAX_EXPLOSIONS; i++) {
        if (gs->explosions[i].active) {
            gs->explosions[i].timer--;
            if (gs->explosions[i].timer <= 0)
                gs->explosions[i].active = FALSE;
        }
    }

    /* Aliens destroy shields on contact */
    {
        WORD bottom_alien_y = 0;
        for (r = ALIEN_ROWS - 1; r >= 0; r--) {
            for (c = 0; c < ALIEN_COLS; c++) {
                if (gs->swarm.alive[r][c]) {
                    WORD ay = gs->swarm.grid_y + r * ALIEN_CELL_H + ALIEN_H;
                    if (ay > bottom_alien_y) bottom_alien_y = ay;
                }
            }
        }

        /* If aliens reached shield level, destroy overlapping shield pixels */
        if (bottom_alien_y >= SHIELD_Y) {
            int s;
            for (s = 0; s < SHIELD_COUNT; s++) {
                Shield *sh = &gs->shields[s];
                int sy, sx;
                for (sy = 0; sy < SHIELD_H; sy++) {
                    for (sx = 0; sx < SHIELD_W; sx++) {
                        WORD wy = sh->y + sy;
                        if (wy >= gs->swarm.grid_y && wy < bottom_alien_y) {
                            sh->pixels[sy][sx] = 0;
                        }
                    }
                }
            }
        }

        /* Game over if aliens reached player */
        if (bottom_alien_y >= PLAYER_Y) {
            gs->state = STATE_GAMEOVER;
            gs->state_timer = 120;
        }
    }

    /* Wave clear */
    if (gs->swarm.alive_count <= 0) {
        gs->state = STATE_WAVE_CLEAR;
        gs->state_timer = 90;
    }
}

void game_update(GameState *gs, InputState *input)
{
    switch (gs->state) {
    case STATE_TITLE:
        gs->title_blink++;
        if (input->fire_pressed) {
            gs->score = 0;
            gs->lives = 3;
            gs->wave = 1;
            game_init_wave(gs);
        }
        break;

    case STATE_PLAYING:
        update_playing(gs, input);
        break;

    case STATE_DYING:
        gs->state_timer--;
        if (gs->state_timer <= 0) {
            gs->lives--;
            if (gs->lives <= 0) {
                gs->state = STATE_GAMEOVER;
                gs->state_timer = 150;
            } else {
                gs->state = STATE_PLAYING;
                gs->player_x = SCREEN_W / 2 - PLAYER_W / 2;
            }
        }
        break;

    case STATE_GAMEOVER:
        gs->state_timer--;
        if (gs->state_timer <= 0 || input->fire_pressed) {
            gs->state = STATE_TITLE;
        }
        break;

    case STATE_WAVE_CLEAR:
        gs->state_timer--;
        if (gs->state_timer <= 0) {
            gs->wave++;
            game_init_wave(gs);
        }
        break;
    }
}
