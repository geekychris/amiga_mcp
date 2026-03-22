/*
 * Planet Patrol - Game logic, AI, physics, collisions, levels
 */
#include <proto/exec.h>
#include <exec/types.h>
#include <string.h>
#include "game.h"

/* Simple RNG */
static ULONG game_rng_state = 54321;
static LONG game_rand(void)
{
    game_rng_state = game_rng_state * 1103515245 + 12345;
    return (LONG)((game_rng_state >> 16) & 0x7FFF);
}

/* Approximate sine: input 0-255, output -256 to 256 */
static WORD sin_tab[256];
static WORD sin_tab_init = 0;

static void init_sin_tab(void)
{
    WORD i;
    /* Simple sine approximation using parabolic segments */
    for (i = 0; i < 64; i++) {
        LONG v = (LONG)i * (128 - (LONG)i * 2) * 256 / (64 * 64);
        sin_tab[i] = (WORD)v;
        sin_tab[128 - i] = (WORD)v;
        sin_tab[128 + i] = (WORD)(-v);
        sin_tab[255 - i] = (WORD)(-v);
    }
    /* Fix exact points */
    sin_tab[0] = 0;
    sin_tab[64] = 256;
    sin_tab[128] = 0;
    sin_tab[192] = -256;
    sin_tab_init = 1;
}

static WORD sin_approx(WORD angle)
{
    return sin_tab[angle & 255];
}

/* ---- World distance (shortest path with wrap) ---- */
LONG world_dist(LONG a, LONG b)
{
    LONG d = a - b;
    LONG half = TO_FP(WORLD_W) / 2;
    if (d > half) d -= TO_FP(WORLD_W);
    if (d < -half) d += TO_FP(WORLD_W);
    return d;
}

/* Convert world X to screen X relative to viewport */
WORD world_to_screen_x(LONG wx, LONG viewport_x)
{
    LONG d = world_dist(wx, viewport_x);
    return (WORD)FROM_FP(d);
}

/* Wrap world X coordinate */
static LONG wrap_wx(LONG wx)
{
    LONG wfp = TO_FP(WORLD_W);
    while (wx >= wfp) wx -= wfp;
    while (wx < 0) wx += wfp;
    return wx;
}

/* ---- Terrain generation ---- */
void game_generate_terrain(GameState *gs, WORD seed)
{
    WORD x;
    game_rng_state = (ULONG)seed * 7919 + 1;

    if (!sin_tab_init) init_sin_tab();

    for (x = 0; x < WORLD_W; x++) {
        LONG h = 200; /* base terrain Y */
        /* Layered sine waves for mountainous profile */
        h += sin_approx((WORD)(x * 256 / 320 + seed * 17)) * 15 / 256;
        h += sin_approx((WORD)(x * 256 / 120 + seed * 53)) * 10 / 256;
        h += sin_approx((WORD)(x * 256 / 60 + seed * 97)) * 5 / 256;
        h += sin_approx((WORD)(x * 256 / 30 + seed * 31)) * 3 / 256;

        /* Clamp to play area */
        if (h < PLAY_BOT - 60) h = PLAY_BOT - 60;
        if (h > PLAY_BOT - 5) h = PLAY_BOT - 5;

        gs->terrain_h[x] = (WORD)h;
    }
}

/* ---- Spawn helpers ---- */

static void spawn_particle(GameState *gs, WORD sx, WORD sy, WORD color)
{
    WORD i;
    for (i = 0; i < MAX_PARTICLES; i++) {
        Particle *p = &gs->particles[i];
        if (p->life <= 0) {
            p->x = sx;
            p->y = sy;
            p->vx = (WORD)(game_rand() % 7 - 3);
            p->vy = (WORD)(game_rand() % 7 - 3);
            p->life = 10 + (WORD)(game_rand() % 10);
            p->color = color;
            return;
        }
    }
}

static void spawn_explosion(GameState *gs, LONG wx, LONG wy)
{
    WORD i;
    WORD sx, sy;

    for (i = 0; i < MAX_EXPLOSIONS; i++) {
        Explosion *e = &gs->explosions[i];
        if (!e->active) {
            e->wx = wx;
            e->wy = wy;
            e->radius = 2;
            e->life = 15;
            e->active = 1;
            break;
        }
    }

    /* Also spawn particles */
    sx = world_to_screen_x(wx, gs->viewport_x);
    sy = (WORD)FROM_FP(wy);
    for (i = 0; i < 6; i++) {
        spawn_particle(gs, sx, sy, (WORD)(COL_EXPL_RED + game_rand() % 3));
    }
}

static WORD find_free_enemy(GameState *gs)
{
    WORD i;
    for (i = 0; i < MAX_ENEMIES; i++)
        if (!gs->enemies[i].active) return i;
    return -1;
}

static void spawn_enemy(GameState *gs, WORD type, LONG wx, LONG wy)
{
    WORD idx = find_free_enemy(gs);
    Enemy *e;
    if (idx < 0) return;

    e = &gs->enemies[idx];
    e->wx = wx;
    e->wy = wy;
    e->vx = 0;
    e->vy = 0;
    e->active = 1;
    e->type = type;
    e->hp = 1;
    e->state = 0;
    e->timer = 0;
    e->target_human = -1;
    e->anim_frame = 0;

    if (type == ENT_LANDER) {
        e->state = LANDER_SEEKING;
    } else if (type == ENT_BAITER) {
        e->hp = 2;
    } else if (type == ENT_POD) {
        e->hp = 1;
    }

    gs->enemies_alive++;
}

static void kill_enemy(GameState *gs, WORD idx)
{
    Enemy *e = &gs->enemies[idx];
    if (!e->active) return;

    /* Score */
    switch (e->type) {
    case ENT_LANDER:  gs->score += SCORE_LANDER;  break;
    case ENT_MUTANT:  gs->score += SCORE_MUTANT;  break;
    case ENT_BAITER:  gs->score += SCORE_BAITER;  break;
    case ENT_BOMBER:  gs->score += SCORE_BOMBER;  break;
    case ENT_POD:     gs->score += SCORE_POD;     break;
    case ENT_SWARMER: gs->score += SCORE_SWARMER; break;
    }

    if (gs->score > gs->hiscore)
        gs->hiscore = gs->score;

    /* Pod splits into swarmers */
    if (e->type == ENT_POD) {
        WORD j, count = 3 + (WORD)(game_rand() % 3);
        for (j = 0; j < count; j++) {
            spawn_enemy(gs, ENT_SWARMER,
                e->wx + TO_FP(game_rand() % 20 - 10),
                e->wy + TO_FP(game_rand() % 20 - 10));
        }
    }

    /* If lander was carrying a human, release them */
    if (e->type == ENT_LANDER && e->target_human >= 0) {
        Human *h = &gs->humans[e->target_human];
        if (h->state == HUMAN_GRABBED) {
            h->state = HUMAN_FALLING;
            h->grabbed_by = -1;
        }
    }

    spawn_explosion(gs, e->wx, e->wy);
    gs->ev_explode = 1;

    e->active = 0;
    gs->enemies_alive--;
    if (gs->enemies_alive < 0) gs->enemies_alive = 0;
}

/* ---- Ship functions ---- */

static void ship_fire(GameState *gs)
{
    WORD i;
    if (gs->ship.fire_cooldown > 0) return;

    for (i = 0; i < MAX_PLAYER_BULLETS; i++) {
        Bullet *b = &gs->player_bullets[i];
        if (!b->active) {
            b->wx = gs->ship.wx + TO_FP(gs->ship.facing > 0 ? SHIP_W : -4);
            b->wy = gs->ship.wy + TO_FP(SHIP_H / 2);
            b->vx = TO_FP(gs->ship.facing > 0 ? 16 : -16);
            b->active = 1;
            b->facing = gs->ship.facing;
            gs->ship.fire_cooldown = 5;
            gs->ev_laser = 1;
            return;
        }
    }
}

static void smart_bomb(GameState *gs)
{
    WORD i;
    if (gs->ship.smart_bombs <= 0) return;
    gs->ship.smart_bombs--;

    /* Kill all on-screen enemies */
    for (i = 0; i < MAX_ENEMIES; i++) {
        Enemy *e = &gs->enemies[i];
        if (e->active) {
            WORD sx = world_to_screen_x(e->wx, gs->viewport_x);
            if (sx >= -20 && sx < SCREEN_W + 20) {
                kill_enemy(gs, i);
            }
        }
    }

    /* Kill on-screen mines */
    for (i = 0; i < MAX_MINES; i++) {
        Mine *m = &gs->mines[i];
        if (m->active) {
            WORD sx = world_to_screen_x(m->wx, gs->viewport_x);
            if (sx >= -20 && sx < SCREEN_W + 20) {
                spawn_explosion(gs, m->wx, m->wy);
                m->active = 0;
            }
        }
    }

    gs->ev_bomb = 1;
}

static void hyperspace(GameState *gs)
{
    gs->ship.wx = TO_FP(game_rand() % WORLD_W);
    gs->ship.wy = TO_FP(PLAY_TOP + 20 + game_rand() % (PLAY_BOT - PLAY_TOP - 60));
    gs->ship.vx = 0;
    gs->ship.vy = 0;
    gs->ship.invuln_timer = 30;
    gs->ev_hyper = 1;

    /* 1 in 6 chance of death */
    if (game_rand() % 6 == 0) {
        gs->ship.alive = 0;
        gs->lives--;
        gs->state = STATE_DYING;
        gs->state_timer = 40;
        gs->ev_die = 1;
        spawn_explosion(gs, gs->ship.wx, gs->ship.wy);
    }
}

/* ---- Enemy AI ---- */

static void lander_ai(Enemy *e, GameState *gs)
{
    WORD i;
    LONG dx, dy;

    switch (e->state) {
    case LANDER_SEEKING:
        /* Find nearest alive, walking human */
        if (e->target_human < 0 || !gs->humans[e->target_human].active ||
            gs->humans[e->target_human].state != HUMAN_WALKING) {
            LONG best_dist = 0x7FFFFFFF;
            e->target_human = -1;
            for (i = 0; i < MAX_HUMANS; i++) {
                if (gs->humans[i].state == HUMAN_WALKING) {
                    LONG d = world_dist(e->wx, gs->humans[i].wx);
                    if (d < 0) d = -d;
                    if (d < best_dist) {
                        best_dist = d;
                        e->target_human = i;
                    }
                }
            }
        }

        if (e->target_human >= 0) {
            /* Move toward human */
            Human *h = &gs->humans[e->target_human];
            dx = world_dist(h->wx, e->wx);
            dy = h->wy - e->wy;

            if (dx > TO_FP(2)) e->vx = TO_FP(1);
            else if (dx < -TO_FP(2)) e->vx = -TO_FP(1);
            else { e->vx = 0; e->state = LANDER_DESCENDING; }

            /* Slowly descend */
            e->vy = TO_FP(1) / 2;
        } else {
            /* No humans left, become aggressive */
            dx = world_dist(gs->ship.wx, e->wx);
            if (dx > 0) e->vx = FP_ONE;
            else e->vx = -FP_ONE;
            e->vy = (game_rand() % 3 - 1) * FP_ONE / 2;
        }
        break;

    case LANDER_DESCENDING:
        if (e->target_human >= 0 && gs->humans[e->target_human].state == HUMAN_WALKING) {
            Human *h = &gs->humans[e->target_human];
            dx = world_dist(h->wx, e->wx);
            dy = h->wy - e->wy;

            /* Horizontal tracking */
            if (dx > FP_ONE) e->vx = FP_ONE / 2;
            else if (dx < -FP_ONE) e->vx = -FP_ONE / 2;
            else e->vx = 0;

            /* Descend toward human */
            e->vy = FP_ONE;

            /* Check if close enough to grab */
            if (dx > -TO_FP(8) && dx < TO_FP(8) && dy > -TO_FP(8) && dy < TO_FP(8)) {
                e->state = LANDER_GRABBING;
                h->state = HUMAN_GRABBED;
                h->grabbed_by = (WORD)(e - gs->enemies);
            }
        } else {
            e->state = LANDER_SEEKING;
        }
        break;

    case LANDER_GRABBING:
        /* Lock human to lander */
        if (e->target_human >= 0) {
            Human *h = &gs->humans[e->target_human];
            if (h->state == HUMAN_GRABBED) {
                h->wx = e->wx;
                h->wy = e->wy + TO_FP(8);
            }
        }
        e->vx = 0;
        e->vy = -FP_ONE; /* ascend */
        e->state = LANDER_ASCENDING;
        break;

    case LANDER_ASCENDING:
        e->vx = 0;
        e->vy = -FP_ONE;

        /* Update grabbed human position */
        if (e->target_human >= 0) {
            Human *h = &gs->humans[e->target_human];
            if (h->state == HUMAN_GRABBED) {
                h->wx = e->wx;
                h->wy = e->wy + TO_FP(8);
            }
        }

        /* If reached top, mutate */
        if (FROM_FP(e->wy) <= PLAY_TOP + 5) {
            if (e->target_human >= 0) {
                gs->humans[e->target_human].state = HUMAN_DEAD;
                gs->humans_alive--;
                gs->ev_human_die = 1;
            }
            /* Transform lander into mutant */
            e->type = ENT_MUTANT;
            e->state = 0;
            e->target_human = -1;
            e->wy = TO_FP(PLAY_TOP + 10);
        }
        break;
    }
}

static void mutant_ai(Enemy *e, GameState *gs)
{
    LONG dx = world_dist(gs->ship.wx, e->wx);
    LONG dy = gs->ship.wy - e->wy;
    WORD speed = FP_ONE * 2;

    /* Aggressive homing */
    if (dx > FP_ONE) e->vx = speed;
    else if (dx < -FP_ONE) e->vx = -speed;
    else e->vx = dx / 2;

    if (dy > FP_ONE) e->vy = speed;
    else if (dy < -FP_ONE) e->vy = -speed;
    else e->vy = dy / 2;

    /* Add some jitter */
    e->vx += (game_rand() % 3 - 1) * FP_ONE / 4;
    e->vy += (game_rand() % 3 - 1) * FP_ONE / 4;
}

static void bomber_ai(Enemy *e, GameState *gs)
{
    /* Horizontal patrol */
    if (e->timer <= 0) {
        e->vx = (game_rand() & 1) ? FP_ONE : -FP_ONE;
        e->vy = (game_rand() % 3 - 1) * FP_ONE / 4;
        e->timer = 60 + (WORD)(game_rand() % 60);
    }
    e->timer--;

    /* Drop mine every ~120 frames */
    if (game_rand() % 120 == 0) {
        WORD i;
        for (i = 0; i < MAX_MINES; i++) {
            if (!gs->mines[i].active) {
                gs->mines[i].wx = e->wx;
                gs->mines[i].wy = e->wy;
                gs->mines[i].active = 1;
                break;
            }
        }
    }
}

static void pod_ai(Enemy *e, GameState *gs)
{
    (void)gs;
    /* Slow drift */
    if (e->timer <= 0) {
        e->vx = (game_rand() % 3 - 1) * FP_ONE / 2;
        e->vy = (game_rand() % 3 - 1) * FP_ONE / 2;
        e->timer = 90 + (WORD)(game_rand() % 90);
    }
    e->timer--;
}

static void swarmer_ai(Enemy *e, GameState *gs)
{
    LONG dx = world_dist(gs->ship.wx, e->wx);
    LONG dy = gs->ship.wy - e->wy;
    WORD speed = FP_ONE + FP_ONE / 2;

    /* Erratic homing */
    if (dx > 0) e->vx = speed;
    else e->vx = -speed;

    if (dy > 0) e->vy = speed / 2;
    else e->vy = -speed / 2;

    /* Heavy jitter */
    e->vx += (game_rand() % 5 - 2) * FP_ONE / 2;
    e->vy += (game_rand() % 5 - 2) * FP_ONE / 2;
}

static void baiter_ai(Enemy *e, GameState *gs)
{
    LONG dx = world_dist(gs->ship.wx, e->wx);
    LONG dy = gs->ship.wy - e->wy;
    WORD speed = FP_ONE * 3;

    /* Very fast, direct homing */
    if (dx > FP_ONE) e->vx = speed;
    else if (dx < -FP_ONE) e->vx = -speed;
    else e->vx = dx;

    if (dy > FP_ONE) e->vy = speed;
    else if (dy < -FP_ONE) e->vy = -speed;
    else e->vy = dy;

    /* Occasional fire */
    if (game_rand() % 60 == 0) {
        WORD i;
        for (i = 0; i < MAX_ENEMY_BULLETS; i++) {
            Bullet *b = &gs->enemy_bullets[i];
            if (!b->active) {
                b->wx = e->wx;
                b->wy = e->wy;
                b->vx = dx > 0 ? TO_FP(4) : -TO_FP(4);
                b->active = 1;
                b->facing = dx > 0 ? 1 : -1;
                break;
            }
        }
    }
}

/* ---- Collision helpers ---- */

static WORD check_collision(LONG ax, LONG ay, WORD aw, WORD ah,
                            LONG bx, LONG by, WORD bw, WORD bh,
                            LONG viewport_x)
{
    /* World-space AABB with wraparound */
    LONG dx = world_dist(ax, bx);
    LONG dy = ay - by;
    LONG hw = TO_FP((aw + bw) / 2);
    LONG hh = TO_FP((ah + bh) / 2);

    (void)viewport_x;

    if (dx < 0) dx = -dx;
    if (dy < 0) dy = -dy;

    return (dx < hw && dy < hh) ? 1 : 0;
}

/* ---- Game init ---- */

void game_init(GameState *gs, WORD level)
{
    WORD i;

    if (!sin_tab_init) init_sin_tab();

    memset(gs, 0, sizeof(GameState));

    gs->level = level;
    gs->lives = 3;
    gs->state = STATE_LEVEL_START;
    gs->state_timer = 45;

    /* Ship */
    gs->ship.wx = TO_FP(WORLD_W / 2);
    gs->ship.wy = TO_FP(PLAY_TOP + (PLAY_BOT - PLAY_TOP) / 2);
    gs->ship.facing = 1;
    gs->ship.alive = 1;
    gs->ship.smart_bombs = 3;
    gs->ship.carried_human = -1;

    /* Viewport */
    gs->viewport_x = gs->ship.wx - TO_FP(SHIP_SCREEN_X_R);

    /* Terrain */
    game_generate_terrain(gs, level * 17 + 42);

    /* Humans */
    gs->humans_alive = MAX_HUMANS;
    for (i = 0; i < MAX_HUMANS; i++) {
        Human *h = &gs->humans[i];
        WORD hx = (WORD)(WORLD_W / MAX_HUMANS * i + game_rand() % (WORLD_W / MAX_HUMANS));
        h->wx = TO_FP(hx);
        h->wy = TO_FP(gs->terrain_h[hx % WORLD_W] - 6);
        h->active = 1;
        h->state = HUMAN_WALKING;
        h->grabbed_by = -1;
        h->walk_dir = (game_rand() & 1) ? 1 : -1;
        h->walk_timer = 30 + (WORD)(game_rand() % 60);
    }
}

/* ---- Wave spawning ---- */

void game_spawn_wave(GameState *gs)
{
    WORD i;
    WORD num_landers, num_bombers, num_pods;
    WORD level = gs->level;

    /* Clear existing enemies */
    for (i = 0; i < MAX_ENEMIES; i++)
        gs->enemies[i].active = 0;
    for (i = 0; i < MAX_MINES; i++)
        gs->mines[i].active = 0;
    for (i = 0; i < MAX_ENEMY_BULLETS; i++)
        gs->enemy_bullets[i].active = 0;
    gs->enemies_alive = 0;

    /* Determine counts */
    num_landers = 4 + level;
    if (num_landers > 12) num_landers = 12;

    num_bombers = level >= 2 ? level - 1 : 0;
    if (num_bombers > 5) num_bombers = 5;

    num_pods = level >= 3 ? level - 2 : 0;
    if (num_pods > 4) num_pods = 4;

    /* Spawn landers at random positions across the world, above terrain */
    for (i = 0; i < num_landers && i < MAX_ENEMIES; i++) {
        LONG wx = TO_FP(game_rand() % WORLD_W);
        LONG wy = TO_FP(PLAY_TOP + 10 + game_rand() % 40);
        spawn_enemy(gs, ENT_LANDER, wx, wy);
    }

    /* Spawn bombers */
    for (i = 0; i < num_bombers; i++) {
        LONG wx = TO_FP(game_rand() % WORLD_W);
        LONG wy = TO_FP(PLAY_TOP + 20 + game_rand() % 60);
        spawn_enemy(gs, ENT_BOMBER, wx, wy);
    }

    /* Spawn pods */
    for (i = 0; i < num_pods; i++) {
        LONG wx = TO_FP(game_rand() % WORLD_W);
        LONG wy = TO_FP(PLAY_TOP + 30 + game_rand() % 50);
        spawn_enemy(gs, ENT_POD, wx, wy);
    }

    /* Baiter timer */
    gs->baiter_timer = 1500 - level * 150;
    if (gs->baiter_timer < 450) gs->baiter_timer = 450;
}

/* ---- Main game update ---- */

void game_update(GameState *gs, WORD input)
{
    Ship *ship = &gs->ship;
    WORD i;
    WORD speed_mul;

    /* Speed multiplier for difficulty */
    speed_mul = 256 + gs->level * 8;
    if (speed_mul > 384) speed_mul = 384;

    /* ---- Ship update ---- */
    if (ship->alive) {
        /* Horizontal thrust */
        if (input & INPUT_LEFT) {
            ship->facing = -1;
            ship->vx -= SHIP_THRUST;
            ship->thrust_on = 1;
        } else if (input & INPUT_RIGHT) {
            ship->facing = 1;
            ship->vx += SHIP_THRUST;
            ship->thrust_on = 1;
        } else {
            ship->thrust_on = 0;
        }

        /* Vertical movement */
        if (input & INPUT_UP)
            ship->vy -= SHIP_THRUST;
        else if (input & INPUT_DOWN)
            ship->vy += SHIP_THRUST;

        /* Drag */
        ship->vx = ship->vx * (256 - SHIP_DRAG) / 256;
        ship->vy = ship->vy * (256 - SHIP_VDRAG) / 256;

        /* Clamp velocity */
        if (ship->vx > SHIP_MAX_VX) ship->vx = SHIP_MAX_VX;
        if (ship->vx < -SHIP_MAX_VX) ship->vx = -SHIP_MAX_VX;
        if (ship->vy > SHIP_MAX_VY) ship->vy = SHIP_MAX_VY;
        if (ship->vy < -SHIP_MAX_VY) ship->vy = -SHIP_MAX_VY;

        /* Move */
        ship->wx += ship->vx;
        ship->wy += ship->vy;

        /* Wrap X */
        ship->wx = wrap_wx(ship->wx);

        /* Clamp Y to play area */
        if (FROM_FP(ship->wy) < PLAY_TOP + 2)
            ship->wy = TO_FP(PLAY_TOP + 2);
        if (FROM_FP(ship->wy) > PLAY_BOT - SHIP_H)
            ship->wy = TO_FP(PLAY_BOT - SHIP_H);

        /* Fire */
        if (input & INPUT_FIRE)
            ship_fire(gs);
        if (ship->fire_cooldown > 0)
            ship->fire_cooldown--;

        /* Smart bomb */
        if (input & INPUT_BOMB)
            smart_bomb(gs);

        /* Hyperspace */
        if (input & INPUT_HYPER)
            hyperspace(gs);

        /* Invulnerability timer */
        if (ship->invuln_timer > 0)
            ship->invuln_timer--;

        /* Carrying a human? Deposit when near ground */
        if (ship->carried_human >= 0) {
            Human *h = &gs->humans[ship->carried_human];
            h->wx = ship->wx;
            h->wy = ship->wy + TO_FP(SHIP_H);

            /* Check if near ground */
            {
                WORD px = FROM_FP(ship->wx) % WORLD_W;
                WORD ground = gs->terrain_h[px];
                if (FROM_FP(ship->wy) + SHIP_H >= ground - 8) {
                    h->state = HUMAN_WALKING;
                    h->wy = TO_FP(ground - 6);
                    ship->carried_human = -1;
                    gs->score += SCORE_HUMAN_LAND;
                    gs->ev_pickup = 1;
                }
            }
        }
    }

    /* ---- Viewport ---- */
    {
        LONG target_vx;
        if (ship->facing > 0)
            target_vx = ship->wx - TO_FP(SHIP_SCREEN_X_R);
        else
            target_vx = ship->wx - TO_FP(SHIP_SCREEN_X_L);

        /* Smooth scroll toward target */
        {
            LONG diff = world_dist(target_vx, gs->viewport_x);
            gs->viewport_x += diff / 4;
        }
        gs->viewport_x = wrap_wx(gs->viewport_x);
    }

    /* ---- Player bullets ---- */
    for (i = 0; i < MAX_PLAYER_BULLETS; i++) {
        Bullet *b = &gs->player_bullets[i];
        if (b->active) {
            b->wx += b->vx;
            b->wx = wrap_wx(b->wx);

            /* Remove if too far from ship */
            {
                LONG d = world_dist(b->wx, ship->wx);
                if (d < 0) d = -d;
                if (d > TO_FP(SCREEN_W))
                    b->active = 0;
            }
        }
    }

    /* ---- Enemy bullets ---- */
    for (i = 0; i < MAX_ENEMY_BULLETS; i++) {
        Bullet *b = &gs->enemy_bullets[i];
        if (b->active) {
            b->wx += b->vx;
            b->wx = wrap_wx(b->wx);

            LONG d = world_dist(b->wx, ship->wx);
            if (d < 0) d = -d;
            if (d > TO_FP(SCREEN_W + 50))
                b->active = 0;
        }
    }

    /* ---- Enemy AI & movement ---- */
    for (i = 0; i < MAX_ENEMIES; i++) {
        Enemy *e = &gs->enemies[i];
        if (!e->active) continue;

        switch (e->type) {
        case ENT_LANDER:  lander_ai(e, gs);  break;
        case ENT_MUTANT:  mutant_ai(e, gs);  break;
        case ENT_BOMBER:  bomber_ai(e, gs);  break;
        case ENT_POD:     pod_ai(e, gs);     break;
        case ENT_SWARMER: swarmer_ai(e, gs); break;
        case ENT_BAITER:  baiter_ai(e, gs);  break;
        }

        /* Apply velocity with speed multiplier */
        e->wx += e->vx * speed_mul / 256;
        e->wy += e->vy * speed_mul / 256;
        e->wx = wrap_wx(e->wx);

        /* Clamp Y */
        if (FROM_FP(e->wy) < PLAY_TOP)
            e->wy = TO_FP(PLAY_TOP);
        if (FROM_FP(e->wy) > PLAY_BOT - 8)
            e->wy = TO_FP(PLAY_BOT - 8);

        e->anim_frame++;
    }

    /* ---- Human update ---- */
    for (i = 0; i < MAX_HUMANS; i++) {
        Human *h = &gs->humans[i];
        if (h->state == HUMAN_WALKING) {
            /* Walk on terrain */
            h->walk_timer--;
            if (h->walk_timer <= 0) {
                h->walk_dir = -h->walk_dir;
                h->walk_timer = 30 + (WORD)(game_rand() % 60);
            }
            h->wx += h->walk_dir * FP_ONE / 4;
            h->wx = wrap_wx(h->wx);

            /* Stick to terrain */
            {
                WORD px = FROM_FP(h->wx) % WORLD_W;
                h->wy = TO_FP(gs->terrain_h[px] - 6);
            }
        } else if (h->state == HUMAN_FALLING) {
            /* Apply gravity */
            h->wy += FP_ONE * 2;

            /* Check for ship catch */
            if (ship->alive && ship->carried_human < 0) {
                LONG dx = world_dist(h->wx, ship->wx);
                LONG dy = h->wy - ship->wy;
                if (dx > -TO_FP(16) && dx < TO_FP(16) &&
                    dy > -TO_FP(12) && dy < TO_FP(16)) {
                    h->state = HUMAN_GRABBED;
                    ship->carried_human = i;
                    gs->score += SCORE_HUMAN_CATCH;
                    gs->ev_pickup = 1;
                }
            }

            /* Check if landed on terrain */
            {
                WORD px = FROM_FP(h->wx) % WORLD_W;
                WORD ground = gs->terrain_h[px];
                if (FROM_FP(h->wy) >= ground - 6) {
                    /* Safe landing */
                    h->wy = TO_FP(ground - 6);
                    h->state = HUMAN_WALKING;
                    h->walk_timer = 30;
                }
            }

            /* Fell off bottom? */
            if (FROM_FP(h->wy) > PLAY_BOT + 20) {
                h->state = HUMAN_DEAD;
                gs->humans_alive--;
                gs->ev_human_die = 1;
            }
        }
        /* HUMAN_GRABBED is handled by lander/ship */
    }

    /* ---- Check planet destruction ---- */
    if (!gs->planet_destroyed) {
        WORD alive = 0;
        for (i = 0; i < MAX_HUMANS; i++) {
            if (gs->humans[i].state != HUMAN_DEAD)
                alive++;
        }
        gs->humans_alive = alive;

        if (alive == 0) {
            gs->planet_destroyed = 1;
            /* All enemies become mutants */
            for (i = 0; i < MAX_ENEMIES; i++) {
                if (gs->enemies[i].active && gs->enemies[i].type == ENT_LANDER) {
                    gs->enemies[i].type = ENT_MUTANT;
                    gs->enemies[i].state = 0;
                    gs->enemies[i].target_human = -1;
                }
            }
            gs->ev_explode_big = 1;
        }
    }

    /* ---- Collisions: player bullets vs enemies ---- */
    for (i = 0; i < MAX_PLAYER_BULLETS; i++) {
        Bullet *b = &gs->player_bullets[i];
        WORD j;
        if (!b->active) continue;

        for (j = 0; j < MAX_ENEMIES; j++) {
            Enemy *e = &gs->enemies[j];
            if (!e->active) continue;

            if (check_collision(b->wx, b->wy, 8, 2,
                                e->wx, e->wy, 10, 10,
                                gs->viewport_x)) {
                b->active = 0;
                e->hp--;
                if (e->hp <= 0)
                    kill_enemy(gs, j);
                break;
            }
        }

        /* Player bullets vs mines */
        if (b->active) {
            WORD j;
            for (j = 0; j < MAX_MINES; j++) {
                Mine *m = &gs->mines[j];
                if (!m->active) continue;
                if (check_collision(b->wx, b->wy, 8, 2,
                                    m->wx, m->wy, 6, 6,
                                    gs->viewport_x)) {
                    b->active = 0;
                    m->active = 0;
                    gs->score += SCORE_MINE;
                    spawn_explosion(gs, m->wx, m->wy);
                    gs->ev_explode = 1;
                    break;
                }
            }
        }
    }

    /* ---- Collisions: enemies vs ship ---- */
    if (ship->alive && ship->invuln_timer <= 0) {
        for (i = 0; i < MAX_ENEMIES; i++) {
            Enemy *e = &gs->enemies[i];
            if (!e->active) continue;

            if (check_collision(ship->wx, ship->wy, SHIP_W, SHIP_H,
                                e->wx, e->wy, 10, 10,
                                gs->viewport_x)) {
                /* Ship dies */
                ship->alive = 0;
                gs->lives--;
                gs->state = STATE_DYING;
                gs->state_timer = 50;
                gs->ev_die = 1;
                spawn_explosion(gs, ship->wx, ship->wy);

                /* Release carried human */
                if (ship->carried_human >= 0) {
                    gs->humans[ship->carried_human].state = HUMAN_FALLING;
                    ship->carried_human = -1;
                }
                break;
            }
        }
    }

    /* ---- Collisions: enemy bullets vs ship ---- */
    if (ship->alive && ship->invuln_timer <= 0) {
        for (i = 0; i < MAX_ENEMY_BULLETS; i++) {
            Bullet *b = &gs->enemy_bullets[i];
            if (!b->active) continue;

            if (check_collision(ship->wx, ship->wy, SHIP_W, SHIP_H,
                                b->wx, b->wy, 4, 4,
                                gs->viewport_x)) {
                b->active = 0;
                ship->alive = 0;
                gs->lives--;
                gs->state = STATE_DYING;
                gs->state_timer = 50;
                gs->ev_die = 1;
                spawn_explosion(gs, ship->wx, ship->wy);
                if (ship->carried_human >= 0) {
                    gs->humans[ship->carried_human].state = HUMAN_FALLING;
                    ship->carried_human = -1;
                }
                break;
            }
        }
    }

    /* ---- Collisions: mines vs ship ---- */
    if (ship->alive && ship->invuln_timer <= 0) {
        for (i = 0; i < MAX_MINES; i++) {
            Mine *m = &gs->mines[i];
            if (!m->active) continue;

            if (check_collision(ship->wx, ship->wy, SHIP_W, SHIP_H,
                                m->wx, m->wy, 6, 6,
                                gs->viewport_x)) {
                m->active = 0;
                ship->alive = 0;
                gs->lives--;
                gs->state = STATE_DYING;
                gs->state_timer = 50;
                gs->ev_die = 1;
                spawn_explosion(gs, ship->wx, ship->wy);
                if (ship->carried_human >= 0) {
                    gs->humans[ship->carried_human].state = HUMAN_FALLING;
                    ship->carried_human = -1;
                }
                break;
            }
        }
    }

    /* ---- Particles update ---- */
    for (i = 0; i < MAX_PARTICLES; i++) {
        Particle *p = &gs->particles[i];
        if (p->life > 0) {
            p->x += p->vx;
            p->y += p->vy;
            p->vy++; /* gravity */
            p->life--;
        }
    }

    /* ---- Explosions update ---- */
    for (i = 0; i < MAX_EXPLOSIONS; i++) {
        Explosion *e = &gs->explosions[i];
        if (e->active) {
            e->radius += 1;
            e->life--;
            if (e->life <= 0)
                e->active = 0;
        }
    }

    /* ---- Baiter timer ---- */
    if (gs->state == STATE_PLAYING) {
        gs->baiter_timer--;
        if (gs->baiter_timer <= 0 && gs->enemies_alive > 0) {
            /* Spawn a baiter */
            LONG wx = wrap_wx(gs->ship.wx + TO_FP(SCREEN_W));
            LONG wy = TO_FP(PLAY_TOP + 20 + game_rand() % 80);
            spawn_enemy(gs, ENT_BAITER, wx, wy);
            gs->baiter_timer = 600; /* next baiter in ~12 seconds */
        }
    }

    /* ---- Check wave clear ---- */
    if (gs->state == STATE_PLAYING && gs->enemies_alive <= 0) {
        gs->state = STATE_LEVEL_CLEAR;
        gs->state_timer = 120;
        gs->ev_level = 1;
    }
}
