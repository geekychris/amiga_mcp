/*
 * game.c - Game logic, AI, waves, collision for Ace Pilot
 */
#include <string.h>
#include "game.h"
#include "input.h"

/* Default tunables */
Tunables g_tune = {
    60,     /* enemy_speed (base * 10) */
    50,     /* enemy_accuracy (1-100) */
    60,     /* enemy_fire_rate (frames between shots) */
    100,    /* spawn_rate (frames between spawns) */
    1,      /* difficulty */
    3,      /* start_lives */
    10000,  /* extra_life_score */
    80,     /* player_speed (base * 10) */
    1,      /* display_mode: color by default */
    0,      /* god_mode */
    0,      /* invert_pitch */
    1       /* num_players */
};

/* RNG */
static ULONG rng_state = 98765;

static ULONG rng(void)
{
    rng_state = rng_state * 1103515245UL + 12345UL;
    return (rng_state >> 16) & 0x7FFF;
}

static LONG rng_range(LONG lo, LONG hi)
{
    ULONG r = rng();
    if (hi <= lo) return lo;
    return lo + (LONG)(r % (ULONG)(hi - lo));
}

/* Get color for object type based on display mode and depth */
WORD get_obj_color(WORD obj_type, LONG depth)
{
    if (g_tune.display_mode == DISPLAY_CLASSIC) {
        /* Classic green-on-black with depth shading */
        if (depth < 200)  return 2;   /* bright green (near) */
        if (depth < 800)  return 4;   /* green (medium) */
        if (depth < 1500) return 5;   /* dark green (far) */
        return 13;                     /* dim green (very far) */
    }

    /* Color mode */
    switch (obj_type) {
        case OBJ_ENEMY:    return (depth < 500) ? 8 : 11;   /* red / dark red */
        case OBJ_BLIMP:    return 3;   /* cyan */
        case OBJ_GROUND:   return 6;   /* brown/tan */
        case OBJ_GRID:     return 5;   /* dark green */
        case OBJ_BULLET_P: return 1;   /* white */
        case OBJ_BULLET_E: return 8;   /* red */
        case OBJ_HUD:      return 1;   /* white */
        default:           return 7;   /* grey */
    }
}

/* Distance squared between two points (integer, XZ plane) */
static LONG dist2_xz(LONG x1, LONG z1, LONG x2, LONG z2)
{
    LONG dx = (x1 - x2) >> 4;  /* shift down to prevent overflow */
    LONG dz = (z1 - z2) >> 4;
    return dx * dx + dz * dz;
}

/* Distance squared 3D */
static LONG dist2_3d(const Vec3 *a, const Vec3 *b)
{
    LONG dx = (a->x - b->x) >> 4;
    LONG dy = (a->y - b->y) >> 4;
    LONG dz = (a->z - b->z) >> 4;
    return dx * dx + dy * dy + dz * dz;
}

/* Angle from (0,0) to (dx,dz) as 0-255 */
static WORD angle_to(LONG dx, LONG dz)
{
    WORD best_a = 0;
    LONG best_dot = -0x7FFFFFFF;
    WORD a;

    /* Brute force: check every 4th angle, then refine */
    for (a = 0; a < 256; a += 4) {
        LONG dot = (dx * sin_tab[a] + dz * cos_tab[a]) >> 16;
        if (dot > best_dot) {
            best_dot = dot;
            best_a = a;
        }
    }
    /* Refine */
    for (a = (best_a - 3) & 255; a != ((best_a + 4) & 255); a = (a + 1) & 255) {
        LONG dot = (dx * sin_tab[a] + dz * cos_tab[a]) >> 16;
        if (dot > best_dot) {
            best_dot = dot;
            best_a = a;
        }
    }
    return best_a;
}

/* Shortest angle difference (-128 to 127) */
static WORD angle_diff(WORD a, WORD b)
{
    WORD d = (b - a) & 255;
    if (d > 128) d -= 256;
    return d;
}

/* Initialize player */
static void init_player(PlayerState *p, WORD idx)
{
    p->cam.pos.x = idx ? -200 : 200;
    p->cam.pos.y = 300;
    p->cam.pos.z = -WORLD_HALF + 200;
    p->cam.yaw = 0;
    p->cam.pitch = 0;
    p->score = 0;
    p->lives = (WORD)g_tune.start_lives;
    p->alive = 1;
    p->respawn_timer = 0;
    p->fire_cooldown = 0;
    p->gun_side = 1;
    p->speed = (WORD)(g_tune.player_speed);
    p->invuln = 100;
}

/* Setup ground targets */
void game_setup_ground(GameWorld *w)
{
    WORD i = 0;
    WORD gx, gz;

    memset(w->ground, 0, sizeof(w->ground));

    /* Runway at origin */
    if (i < MAX_GROUND) {
        w->ground[i].pos.x = 0;
        w->ground[i].pos.y = GROUND_Y;
        w->ground[i].pos.z = 0;
        w->ground[i].yaw = 0;
        w->ground[i].model_id = MODEL_RUNWAY;
        w->ground[i].score_value = 0;
        w->ground[i].active = 1;
        i++;
    }

    /* Hangars near runway */
    if (i < MAX_GROUND) {
        w->ground[i].pos.x = 100;
        w->ground[i].pos.y = GROUND_Y;
        w->ground[i].pos.z = 40;
        w->ground[i].yaw = 0;
        w->ground[i].model_id = MODEL_HANGAR;
        w->ground[i].score_value = SCORE_HANGAR;
        w->ground[i].active = 1;
        i++;
    }
    if (i < MAX_GROUND) {
        w->ground[i].pos.x = -100;
        w->ground[i].pos.y = GROUND_Y;
        w->ground[i].pos.z = 40;
        w->ground[i].yaw = 0;
        w->ground[i].model_id = MODEL_HANGAR;
        w->ground[i].score_value = SCORE_HANGAR;
        w->ground[i].active = 1;
        i++;
    }

    /* Fuel tanks */
    if (i < MAX_GROUND) {
        w->ground[i].pos.x = 300;
        w->ground[i].pos.y = GROUND_Y;
        w->ground[i].pos.z = -200;
        w->ground[i].yaw = 0;
        w->ground[i].model_id = MODEL_FUEL_TANK;
        w->ground[i].score_value = SCORE_FUEL;
        w->ground[i].active = 1;
        i++;
    }
    if (i < MAX_GROUND) {
        w->ground[i].pos.x = -400;
        w->ground[i].pos.y = GROUND_Y;
        w->ground[i].pos.z = 300;
        w->ground[i].yaw = 0;
        w->ground[i].model_id = MODEL_FUEL_TANK;
        w->ground[i].score_value = SCORE_FUEL;
        w->ground[i].active = 1;
        i++;
    }

    /* Pyramids */
    if (i < MAX_GROUND) {
        w->ground[i].pos.x = -600;
        w->ground[i].pos.y = GROUND_Y;
        w->ground[i].pos.z = -600;
        w->ground[i].yaw = 32;
        w->ground[i].model_id = MODEL_PYRAMID;
        w->ground[i].score_value = SCORE_PYRAMID;
        w->ground[i].active = 1;
        i++;
    }
    if (i < MAX_GROUND) {
        w->ground[i].pos.x = 700;
        w->ground[i].pos.y = GROUND_Y;
        w->ground[i].pos.z = 500;
        w->ground[i].yaw = 96;
        w->ground[i].model_id = MODEL_PYRAMID;
        w->ground[i].score_value = SCORE_PYRAMID;
        w->ground[i].active = 1;
        i++;
    }

    /* Scatter trees */
    for (gx = -1; gx <= 1 && i < MAX_GROUND; gx++) {
        for (gz = -1; gz <= 1 && i < MAX_GROUND; gz++) {
            if (gx == 0 && gz == 0) continue;  /* skip center (runway) */
            w->ground[i].pos.x = gx * 500 + (LONG)(rng() % 200) - 100;
            w->ground[i].pos.y = GROUND_Y;
            w->ground[i].pos.z = gz * 500 + (LONG)(rng() % 200) - 100;
            w->ground[i].yaw = (WORD)(rng() & 255);
            w->ground[i].model_id = MODEL_TREE;
            w->ground[i].score_value = SCORE_TREE;
            w->ground[i].active = 1;
            i++;
        }
    }
}

/* Spawn a wave of enemies */
void game_spawn_wave(GameWorld *w)
{
    WORD base_count;
    WORD has_blimp;

    w->wave++;

    /* Number of enemies scales with wave and difficulty */
    base_count = 2 + w->wave + (WORD)(g_tune.difficulty - 1);
    if (base_count > MAX_ENEMIES) base_count = MAX_ENEMIES;

    /* One blimp every 3 waves */
    has_blimp = (w->wave % 3 == 0) ? 1 : 0;

    w->enemies_to_spawn = base_count;
    w->enemies_alive = 0;
    w->spawn_timer = 0;

    /* Mark blimp for first spawn if applicable */
    if (has_blimp && w->enemies_to_spawn > 0) {
        /* First enemy will be blimp - handled in spawn logic */
    }
}

/* Spawn a single enemy */
static void spawn_one_enemy(GameWorld *w)
{
    WORD i;
    WORD is_blimp;
    Enemy *e;
    WORD edge;

    for (i = 0; i < MAX_ENEMIES; i++) {
        if (!w->enemies[i].active) break;
    }
    if (i >= MAX_ENEMIES) return;

    e = &w->enemies[i];

    /* First of every 3rd wave is a blimp */
    is_blimp = (w->enemies_alive == 0 && (w->wave % 3 == 0)) ? 1 : 0;

    /* Spawn at world edge */
    edge = (WORD)(rng() & 3);
    switch (edge) {
        case 0: e->pos.x = -WORLD_HALF; e->pos.z = rng_range(-WORLD_HALF, WORLD_HALF); break;
        case 1: e->pos.x =  WORLD_HALF; e->pos.z = rng_range(-WORLD_HALF, WORLD_HALF); break;
        case 2: e->pos.z = -WORLD_HALF; e->pos.x = rng_range(-WORLD_HALF, WORLD_HALF); break;
        default: e->pos.z = WORLD_HALF; e->pos.x = rng_range(-WORLD_HALF, WORLD_HALF); break;
    }

    if (is_blimp) {
        e->pos.y = 400 + rng_range(0, 200);
        e->model_id = MODEL_BLIMP;
        e->speed = (WORD)(g_tune.enemy_speed / 3);
        e->health = 3;
    } else {
        e->pos.y = 200 + rng_range(0, 300);
        e->model_id = MODEL_BIPLANE;
        e->speed = (WORD)(g_tune.enemy_speed + w->wave * 5);
        e->health = 1;
    }

    e->yaw = (WORD)(rng() & 255);
    e->pitch = 0;
    e->ai_state = AI_PATROL;
    e->ai_timer = 50 + (WORD)(rng() % 100);
    e->fire_cooldown = (WORD)(g_tune.enemy_fire_rate);
    e->active = 1;
    w->enemies_alive++;
    w->enemies_to_spawn--;
}

/* Find nearest player to enemy */
static WORD nearest_player(GameWorld *w, Enemy *e)
{
    LONG d0, d1;
    if (g_tune.num_players < 2 || !w->players[1].alive) return 0;
    if (!w->players[0].alive) return 1;

    d0 = dist2_3d(&e->pos, &w->players[0].cam.pos);
    d1 = dist2_3d(&e->pos, &w->players[1].cam.pos);
    return (d1 < d0) ? 1 : 0;
}

/* AI update for one enemy */
static void ai_update(GameWorld *w, Enemy *e)
{
    WORD target_p;
    Vec3 *tp;
    LONG dx, dy, dz;
    WORD target_yaw, target_pitch, dyaw, dpitch;
    WORD turn_rate;
    LONG detect_range;
    target_p = nearest_player(w, e);
    if (!w->players[target_p].alive) {
        /* No living target, just patrol */
        e->ai_state = AI_PATROL;
    }
    tp = &w->players[target_p].cam.pos;

    dx = tp->x - e->pos.x;
    dy = tp->y - e->pos.y;
    dz = tp->z - e->pos.z;

    target_yaw = angle_to(dx, dz);
    target_pitch = angle_to(dy, dz);  /* simplified pitch */

    turn_rate = 2 + (WORD)(g_tune.difficulty / 3);
    detect_range = (800 + g_tune.difficulty * 200);
    detect_range = (detect_range >> 4) * (detect_range >> 4);  /* squared, pre-shifted */

    e->ai_timer--;

    switch (e->ai_state) {
    case AI_PATROL:
        /* Lazy circles */
        e->yaw = (e->yaw + 1) & 255;
        e->pitch = 0;

        /* Detect player */
        if (w->players[target_p].alive && dist2_xz(e->pos.x, e->pos.z, tp->x, tp->z) < detect_range) {
            e->ai_state = AI_ATTACK;
            e->ai_timer = 200 + (WORD)(rng() % 100);
        }
        if (e->ai_timer <= 0) {
            e->ai_timer = 100 + (WORD)(rng() % 100);
            e->yaw = (e->yaw + 64) & 255;  /* change direction */
        }
        break;

    case AI_ATTACK:
        /* Turn toward player */
        dyaw = angle_diff(e->yaw, target_yaw);
        if (dyaw > turn_rate)       e->yaw = (e->yaw + turn_rate) & 255;
        else if (dyaw < -turn_rate) e->yaw = (e->yaw - turn_rate) & 255;
        else                         e->yaw = target_yaw;

        dpitch = angle_diff(e->pitch, target_pitch);
        if (dpitch > 1)       e->pitch = (e->pitch + 1) & 255;
        else if (dpitch < -1) e->pitch = (e->pitch - 1) & 255;

        /* Fire if roughly aligned */
        e->fire_cooldown--;
        if (e->fire_cooldown <= 0) {
            WORD aim_err = dyaw < 0 ? -dyaw : dyaw;
            WORD fire_threshold = 10 + (100 - (WORD)g_tune.enemy_accuracy) / 5;
            if (aim_err < fire_threshold && w->players[target_p].alive) {
                /* Fire bullet toward player */
                WORD bi;
                for (bi = 0; bi < MAX_BULLETS; bi++) {
                    if (!w->bullets[bi].active) {
                        Bullet *b = &w->bullets[bi];
                        LONG bspeed = 15;
                        LONG bdx, bdy, bdz;
                        LONG bdist;

                        b->pos = e->pos;
                        bdx = tp->x - e->pos.x;
                        bdy = tp->y - e->pos.y;
                        bdz = tp->z - e->pos.z;

                        /* Normalize direction (approximate) */
                        bdist = (bdx < 0 ? -bdx : bdx) + (bdy < 0 ? -bdy : bdy) + (bdz < 0 ? -bdz : bdz);
                        if (bdist < 1) bdist = 1;
                        b->vx = bdx * bspeed / bdist;
                        b->vy = bdy * bspeed / bdist;
                        b->vz = bdz * bspeed / bdist;
                        b->life = 80;
                        b->owner = 2;  /* enemy */
                        b->active = 1;
                        break;
                    }
                }
                e->fire_cooldown = (WORD)(g_tune.enemy_fire_rate - g_tune.difficulty * 3);
                if (e->fire_cooldown < 20) e->fire_cooldown = 20;
            }
        }

        /* Switch to evade if too close or timer expired */
        if (dist2_3d(&e->pos, tp) < (100 >> 4) * (100 >> 4)) {
            e->ai_state = AI_EVADE;
            e->ai_timer = 60 + (WORD)(rng() % 40);
        }
        if (e->ai_timer <= 0) {
            e->ai_state = AI_EVADE;
            e->ai_timer = 80;
        }
        break;

    case AI_EVADE:
        /* Turn away from player */
        dyaw = angle_diff(e->yaw, (target_yaw + 128) & 255);
        if (dyaw > turn_rate)       e->yaw = (e->yaw + turn_rate) & 255;
        else if (dyaw < -turn_rate) e->yaw = (e->yaw - turn_rate) & 255;

        /* Climb */
        e->pitch = 240;  /* slight pitch up */

        if (e->ai_timer <= 0) {
            e->ai_state = AI_ATTACK;
            e->ai_timer = 150 + (WORD)(rng() % 100);
        }
        break;
    }

    /* Move enemy forward based on yaw/pitch */
    {
        LONG mx, my, mz;
        mx = (sin_tab[e->yaw & 255] * e->speed) >> 16;
        mz = (cos_tab[e->yaw & 255] * e->speed) >> 16;
        my = (-sin_tab[e->pitch & 255] * e->speed) >> 20;  /* less vertical movement */

        e->pos.x += mx;
        e->pos.y += my;
        e->pos.z += mz;
    }

    /* Clamp altitude */
    if (e->pos.y < MIN_ALT) e->pos.y = MIN_ALT;
    if (e->pos.y > MAX_ALT) e->pos.y = MAX_ALT;

    /* Wrap world bounds */
    if (e->pos.x > WORLD_HALF) e->pos.x -= WORLD_SIZE;
    if (e->pos.x < -WORLD_HALF) e->pos.x += WORLD_SIZE;
    if (e->pos.z > WORLD_HALF) e->pos.z -= WORLD_SIZE;
    if (e->pos.z < -WORLD_HALF) e->pos.z += WORLD_SIZE;
}

/* Fire a bullet from player */
static void player_fire(GameWorld *w, WORD pidx)
{
    WORD i;
    PlayerState *p = &w->players[pidx];
    Camera *c = &p->cam;
    LONG bspeed = 20;
    LONG converge_dist = 400;  /* bullets converge at this distance ahead */
    LONG wing_offset = 25;     /* lateral distance from center to each gun */
    LONG side;

    /* Camera right vector scaled by wing offset */
    LONG right_x = (cos_tab[c->yaw & 255] * wing_offset) >> 16;
    LONG right_z = -(sin_tab[c->yaw & 255] * wing_offset) >> 16;

    /* Convergence point ahead of camera */
    LONG conv_x = c->pos.x + ((sin_tab[c->yaw & 255] * converge_dist) >> 16);
    LONG conv_y = c->pos.y + ((-sin_tab[c->pitch & 255] * converge_dist) >> 16);
    LONG conv_z = c->pos.z + ((cos_tab[c->yaw & 255] * converge_dist) >> 16);

    /* Alternate left/right gun */
    side = p->gun_side;
    p->gun_side = -p->gun_side;
    if (p->gun_side == 0) p->gun_side = 1;

    for (i = 0; i < MAX_BULLETS; i++) {
        if (!w->bullets[i].active) {
            Bullet *b = &w->bullets[i];
            LONG spawn_x, spawn_y, spawn_z;
            LONG dx, dy, dz;

            /* Spawn at wing position, slightly ahead of camera */
            spawn_x = c->pos.x + side * right_x + ((sin_tab[c->yaw & 255] * 20) >> 16);
            spawn_y = c->pos.y - 5;  /* slightly below eye level */
            spawn_z = c->pos.z + side * right_z + ((cos_tab[c->yaw & 255] * 20) >> 16);

            b->pos.x = spawn_x;
            b->pos.y = spawn_y;
            b->pos.z = spawn_z;

            /* Velocity aimed at convergence point */
            dx = conv_x - spawn_x;
            dy = conv_y - spawn_y;
            dz = conv_z - spawn_z;

            /* Scale to bullet speed (converge_dist is approximate length) */
            b->vx = dx * bspeed / converge_dist;
            b->vy = dy * bspeed / converge_dist;
            b->vz = dz * bspeed / converge_dist;

            b->life = 60;
            b->owner = pidx;
            b->active = 1;
            sfx_gunfire();
            break;
        }
    }
}

/* Spawn explosion particles at position */
static void spawn_particles(GameWorld *w, const Vec3 *pos, WORD count)
{
    WORD i, j;
    for (j = 0; j < count; j++) {
        for (i = 0; i < MAX_PARTICLES; i++) {
            if (!w->particles[i].active) {
                Particle *p = &w->particles[i];
                p->pos = *pos;
                p->vx = rng_range(-8, 8);
                p->vy = rng_range(-2, 10);
                p->vz = rng_range(-8, 8);
                p->life = 15 + (WORD)(rng() % 20);
                p->active = 1;
                break;
            }
        }
    }
}

/* Initialize game world */
void game_init(GameWorld *w)
{
    memset(w, 0, sizeof(GameWorld));

    init_player(&w->players[0], 0);
    init_player(&w->players[1], 1);

    if (g_tune.num_players < 2) {
        w->players[1].alive = 0;
        w->players[1].lives = 0;
    }

    w->wave = 0;
    w->state = STATE_PLAYING;
    w->frame = 0;

    game_setup_ground(w);
    game_spawn_wave(w);
}

/* Update one player's movement */
static void update_player(GameWorld *w, WORD pidx, UWORD inp)
{
    PlayerState *p = &w->players[pidx];
    Camera *c = &p->cam;
    WORD turn_speed = 3;
    WORD pitch_speed = 2;
    LONG fwd_x, fwd_z, fwd_y;

    if (!p->alive) {
        if (p->respawn_timer > 0) {
            p->respawn_timer--;
            if (p->respawn_timer <= 0 && p->lives > 0) {
                p->alive = 1;
                p->invuln = 100;
                c->pos.x = pidx ? -200 : 200;
                c->pos.y = 300;
                c->pos.z = -WORLD_HALF + 200;
                c->yaw = 0;
                c->pitch = 0;
            }
        }
        return;
    }

    /* Invulnerability countdown */
    if (p->invuln > 0) p->invuln--;

    /* Yaw (turn) */
    if (inp & INPUT_LEFT)  c->yaw = (c->yaw - turn_speed) & 255;
    if (inp & INPUT_RIGHT) c->yaw = (c->yaw + turn_speed) & 255;

    /* Pitch */
    if (g_tune.invert_pitch) {
        if (inp & INPUT_UP)   c->pitch = (c->pitch + pitch_speed) & 255;
        if (inp & INPUT_DOWN) c->pitch = (c->pitch - pitch_speed) & 255;
    } else {
        if (inp & INPUT_UP)   c->pitch = (c->pitch - pitch_speed) & 255;
        if (inp & INPUT_DOWN) c->pitch = (c->pitch + pitch_speed) & 255;
    }

    /* Clamp pitch to reasonable range (avoid full loops) */
    /* 0=level, 224-255=looking up, 1-32=looking down */
    if (c->pitch > 32 && c->pitch < 128)  c->pitch = 32;
    if (c->pitch >= 128 && c->pitch < 224) c->pitch = 224;

    /* Throttle */
    if (inp & INPUT_THROT_UP) p->speed += 2;
    if (inp & INPUT_THROT_DN) p->speed -= 2;
    if (p->speed < 4) p->speed = 4;
    if (p->speed > 16) p->speed = 16;

    /* Move forward in facing direction */
    fwd_x = (sin_tab[c->yaw & 255] * p->speed) >> 16;
    fwd_z = (cos_tab[c->yaw & 255] * p->speed) >> 16;
    fwd_y = (-sin_tab[c->pitch & 255] * p->speed) >> 18;  /* less vertical */

    c->pos.x += fwd_x;
    c->pos.z += fwd_z;
    c->pos.y += fwd_y;

    /* Clamp altitude */
    if (c->pos.y < MIN_ALT) c->pos.y = MIN_ALT;
    if (c->pos.y > MAX_ALT) c->pos.y = MAX_ALT;

    /* Wrap world */
    if (c->pos.x > WORLD_HALF) c->pos.x -= WORLD_SIZE;
    if (c->pos.x < -WORLD_HALF) c->pos.x += WORLD_SIZE;
    if (c->pos.z > WORLD_HALF) c->pos.z -= WORLD_SIZE;
    if (c->pos.z < -WORLD_HALF) c->pos.z += WORLD_SIZE;

    /* Fire */
    if (p->fire_cooldown > 0) p->fire_cooldown--;
    if ((inp & INPUT_FIRE) && p->fire_cooldown <= 0) {
        player_fire(w, pidx);
        p->fire_cooldown = 6;
    }
}

/* Main game update */
void game_update(GameWorld *w, UWORD inp1, UWORD inp2)
{
    WORD i, j;

    w->frame++;

    if (w->state == STATE_GAMEOVER) {
        w->state_timer--;
        if (w->state_timer <= 0) {
            /* Return to title handled by main */
        }
        return;
    }

    if (w->state == STATE_DYING) {
        w->state_timer--;
        if (w->state_timer <= 0) {
            /* Check if any player has lives */
            if (w->players[0].lives > 0 || w->players[1].lives > 0) {
                w->state = STATE_PLAYING;
            } else {
                w->state = STATE_GAMEOVER;
                w->state_timer = 200;
            }
        }
        goto update_motion;
    }

    /* Update players */
    update_player(w, 0, inp1);
    if (g_tune.num_players >= 2) {
        update_player(w, 1, inp2);
    }

    /* Spawn enemies from wave */
    if (w->enemies_to_spawn > 0) {
        w->spawn_timer--;
        if (w->spawn_timer <= 0) {
            spawn_one_enemy(w);
            w->spawn_timer = (WORD)(g_tune.spawn_rate - g_tune.difficulty * 5);
            if (w->spawn_timer < 30) w->spawn_timer = 30;
        }
    }

    /* Check if wave cleared */
    if (w->enemies_alive <= 0 && w->enemies_to_spawn <= 0) {
        game_spawn_wave(w);
        /* Regenerate destroyed ground targets */
        game_setup_ground(w);
    }

    /* Update AI */
    for (i = 0; i < MAX_ENEMIES; i++) {
        if (w->enemies[i].active) {
            ai_update(w, &w->enemies[i]);
        }
    }

update_motion:
    /* Update bullets */
    for (i = 0; i < MAX_BULLETS; i++) {
        Bullet *b = &w->bullets[i];
        if (!b->active) continue;
        b->pos.x += b->vx;
        b->pos.y += b->vy;
        b->pos.z += b->vz;
        b->life--;
        if (b->life <= 0) b->active = 0;
        /* Remove if out of world */
        if (b->pos.y < -10 || b->pos.y > MAX_ALT + 100) b->active = 0;
    }

    /* Update particles */
    for (i = 0; i < MAX_PARTICLES; i++) {
        Particle *p = &w->particles[i];
        if (!p->active) continue;
        p->pos.x += p->vx;
        p->pos.y += p->vy;
        p->pos.z += p->vz;
        p->vy--;  /* gravity */
        if (p->pos.y < GROUND_Y) p->pos.y = GROUND_Y;
        p->life--;
        if (p->life <= 0) p->active = 0;
    }

    /* --- Collision detection --- */

    /* Player bullets vs enemies */
    for (i = 0; i < MAX_BULLETS; i++) {
        Bullet *b = &w->bullets[i];
        if (!b->active || b->owner == 2) continue;  /* skip enemy bullets */
        for (j = 0; j < MAX_ENEMIES; j++) {
            Enemy *e = &w->enemies[j];
            LONG rad;
            if (!e->active) continue;
            rad = models[e->model_id].radius;
            rad = (rad >> 4) * (rad >> 4);  /* squared, pre-shifted */
            if (dist2_3d(&b->pos, &e->pos) < rad) {
                b->active = 0;
                e->health--;
                if (e->health <= 0) {
                    spawn_particles(w, &e->pos, 12);
                    sfx_explosion();
                    /* Score */
                    {
                        WORD scorer = b->owner;
                        if (scorer < 2) {
                            LONG pts = (e->model_id == MODEL_BLIMP) ? SCORE_BLIMP : SCORE_PLANE;
                            LONG old_score = w->players[scorer].score;
                            w->players[scorer].score += pts;
                            /* Extra life check */
                            if (old_score / g_tune.extra_life_score < w->players[scorer].score / g_tune.extra_life_score) {
                                w->players[scorer].lives++;
                            }
                        }
                    }
                    e->active = 0;
                    w->enemies_alive--;
                } else {
                    sfx_hit();
                }
                break;
            }
        }
    }

    /* Player bullets vs ground targets */
    for (i = 0; i < MAX_BULLETS; i++) {
        Bullet *b = &w->bullets[i];
        if (!b->active || b->owner == 2) continue;
        /* Only hit ground targets if bullet is low */
        if (b->pos.y > 50) continue;
        for (j = 0; j < MAX_GROUND; j++) {
            GroundTarget *g = &w->ground[j];
            LONG rad;
            if (!g->active || g->score_value == 0) continue;  /* skip runway */
            rad = models[g->model_id].radius;
            rad = (rad >> 4) * (rad >> 4);
            if (dist2_3d(&b->pos, &g->pos) < rad) {
                b->active = 0;
                spawn_particles(w, &g->pos, 8);
                sfx_explosion();
                {
                    WORD scorer = b->owner;
                    if (scorer < 2) {
                        LONG old_score = w->players[scorer].score;
                        w->players[scorer].score += g->score_value;
                        if (old_score / g_tune.extra_life_score < w->players[scorer].score / g_tune.extra_life_score) {
                            w->players[scorer].lives++;
                        }
                    }
                }
                g->active = 0;
                break;
            }
        }
    }

    /* Enemy bullets vs players */
    for (i = 0; i < MAX_BULLETS; i++) {
        Bullet *b = &w->bullets[i];
        if (!b->active || b->owner != 2) continue;
        for (j = 0; j < 2; j++) {
            PlayerState *p = &w->players[j];
            LONG hit_rad;
            if (!p->alive || p->invuln > 0) continue;
            if (g_tune.god_mode) continue;
            hit_rad = (20 >> 4) * (20 >> 4);  /* player hit radius ~20 */
            if (dist2_3d(&b->pos, &p->cam.pos) < hit_rad) {
                b->active = 0;
                sfx_die();
                p->alive = 0;
                p->lives--;
                p->respawn_timer = 100;
                spawn_particles(w, &p->cam.pos, 16);
                if (p->lives <= 0) {
                    /* Check if all players dead */
                    if ((g_tune.num_players < 2 || !w->players[1 - j].alive) &&
                        w->players[1 - j].lives <= 0) {
                        w->state = STATE_DYING;
                        w->state_timer = 100;
                    }
                }
                break;
            }
        }
    }

    /* Enemy planes vs players (collision) */
    for (i = 0; i < MAX_ENEMIES; i++) {
        Enemy *e = &w->enemies[i];
        if (!e->active) continue;
        for (j = 0; j < 2; j++) {
            PlayerState *p = &w->players[j];
            LONG coll_rad;
            if (!p->alive || p->invuln > 0) continue;
            if (g_tune.god_mode) continue;
            coll_rad = (25 >> 4) * (25 >> 4);
            if (dist2_3d(&e->pos, &p->cam.pos) < coll_rad) {
                sfx_die();
                p->alive = 0;
                p->lives--;
                p->respawn_timer = 100;
                spawn_particles(w, &p->cam.pos, 16);
                /* Also destroy the enemy */
                spawn_particles(w, &e->pos, 10);
                e->active = 0;
                w->enemies_alive--;
                break;
            }
        }
    }
}
