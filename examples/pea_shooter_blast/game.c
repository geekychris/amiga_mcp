/*
 * Pea Shooter Blast - Game logic
 */
#include <string.h>
#include "game.h"

/* Default tunables */
Tunables g_tune = {
    200,    /* tank_speed (2.00 pixels/frame) */
    350,    /* jump_power (3.50) */
    25,     /* gravity (0.25) */
    500,    /* bullet_speed (5.00) */
    3,      /* start_lives */
    100,    /* enemy_speed (1.00) */
};

/* Level generation (in levels.c) */
extern void levels_generate(void);

/* --- Collision helpers --- */

static WORD tile_at(WORD level, WORD tx, WORD ty)
{
    if (tx < 0 || tx >= MAP_W || ty < 0 || ty >= MAP_H)
        return TILE_GROUND; /* out of bounds = solid */
    return level_maps[level][ty][tx];
}

static WORD tile_is_solid(WORD level, WORD tx, WORD ty)
{
    UBYTE t = tile_at(level, tx, ty);
    return (tile_props[t] & TPROP_SOLID) ? 1 : 0;
}

static WORD point_in_solid(WORD level, Fixed px, Fixed py)
{
    WORD tx = FIX_INT(px) / TILE_W;
    WORD ty = FIX_INT(py) / TILE_H;
    return tile_is_solid(level, tx, ty);
}

/* Check if a tile is a platform (one-way) */
static WORD tile_is_platform(WORD level, WORD tx, WORD ty)
{
    UBYTE t = tile_at(level, tx, ty);
    return (tile_props[t] & TPROP_PLATFORM) ? 1 : 0;
}

/* --- Enemy spawn data per level --- */

typedef struct {
    WORD x, y;   /* tile position */
    WORD type;
} EnemySpawn;

static EnemySpawn level1_enemies[] = {
    { 15, 12, ENEMY_WALKER },
    { 28, 12, ENEMY_WALKER },
    { 40, 12, ENEMY_FLYER },
    { 55,  9, ENEMY_TURRET },
    { 65, 12, ENEMY_HOPPER },
    { 78, 12, ENEMY_WALKER },
    { 85, 12, ENEMY_FLYER },
    { 95,  8, ENEMY_TURRET },
    { 115, 12, ENEMY_BOSS },  /* boss */
    { -1, -1, -1 }
};

static EnemySpawn level2_enemies[] = {
    { 12, 12, ENEMY_WALKER },
    { 25, 12, ENEMY_HOPPER },
    { 35, 12, ENEMY_FLYER },
    { 45,  7, ENEMY_TURRET },
    { 55, 12, ENEMY_WALKER },
    { 65,  5, ENEMY_FLYER },
    { 80, 10, ENEMY_HOPPER },
    { 90, 12, ENEMY_WALKER },
    { 100, 12, ENEMY_TURRET },
    { 115, 12, ENEMY_BOSS },
    { -1, -1, -1 }
};

static EnemySpawn level3_enemies[] = {
    { 12, 12, ENEMY_WALKER },
    { 22, 12, ENEMY_HOPPER },
    { 30,  5, ENEMY_FLYER },
    { 42, 12, ENEMY_WALKER },
    { 52, 12, ENEMY_FLYER },
    { 60,  8, ENEMY_TURRET },
    { 72, 12, ENEMY_HOPPER },
    { 82,  4, ENEMY_TURRET },
    { 92, 12, ENEMY_WALKER },
    { 100,  7, ENEMY_FLYER },
    { 112, 12, ENEMY_BOSS },
    { -1, -1, -1 }
};

static EnemySpawn *enemy_spawns[3] = {
    level1_enemies,
    level2_enemies,
    level3_enemies
};

/* --- RNG --- */

static ULONG rng_state = 12345;

static WORD rng(void)
{
    rng_state = rng_state * 1103515245UL + 12345UL;
    return (WORD)((rng_state >> 16) & 0x7FFF);
}

/* --- Init --- */

void game_init(GameState *gs)
{
    memset(gs, 0, sizeof(GameState));
    gs->lives = (WORD)g_tune.start_lives;
    gs->state = STATE_PLAYING;

    /* Generate all level data */
    levels_generate();

    /* Load level 0 */
    game_load_level(gs, 0);
}

void game_load_level(GameState *gs, WORD level_num)
{
    WORD i;
    EnemySpawn *spawns;

    gs->level = level_num;
    if (gs->level > 2) gs->level = 2;

    /* Reset tank position */
    gs->tank.x = FIX(32);
    gs->tank.y = FIX(12 * TILE_H);
    gs->tank.dx = 0;
    gs->tank.dy = 0;
    gs->tank.on_ground = 0;
    gs->tank.facing = 0;
    gs->tank.alive = 1;
    gs->tank.gun_timer = 0;
    gs->tank.invuln_timer = INVULN_TIME;
    gs->tank.jump_held = 0;
    gs->tank.anim_frame = 0;
    if (gs->tank.health <= 0)
        gs->tank.health = MAX_HEALTH;

    /* Clear entities */
    for (i = 0; i < MAX_BULLETS; i++) gs->bullets[i].active = 0;
    for (i = 0; i < MAX_ENEMIES; i++) gs->enemies[i].active = 0;
    for (i = 0; i < MAX_ENEMY_BULLETS; i++) gs->enemy_bullets[i].active = 0;
    for (i = 0; i < MAX_PARTICLES; i++) gs->particles[i].active = 0;
    for (i = 0; i < MAX_POWERUPS; i++) gs->powerups[i].active = 0;
    for (i = 0; i < MAX_EXPLOSIONS; i++) gs->explosions[i].active = 0;

    /* Reset scroll */
    gs->scroll.pixel_x = 0;
    gs->scroll.target_x = 0;
    gs->scroll.tile_col = 0;
    gs->scroll.fine_x = 0;
    gs->scroll.last_col = -1;

    /* Spawn enemies from level data */
    spawns = enemy_spawns[gs->level];
    for (i = 0; spawns[i].x >= 0 && i < MAX_ENEMIES; i++) {
        gs->enemies[i].x = FIX(spawns[i].x * TILE_W);
        gs->enemies[i].y = FIX(spawns[i].y * TILE_H);
        gs->enemies[i].type = spawns[i].type;
        gs->enemies[i].active = 1;
        gs->enemies[i].facing = 1;  /* face left initially */
        gs->enemies[i].state = 0;
        gs->enemies[i].timer = 0;
        gs->enemies[i].anim_frame = 0;
        gs->enemies[i].fire_timer = 0;

        switch (spawns[i].type) {
            case ENEMY_WALKER:  gs->enemies[i].health = 2; break;
            case ENEMY_FLYER:   gs->enemies[i].health = 3; break;
            case ENEMY_TURRET:  gs->enemies[i].health = 4; break;
            case ENEMY_HOPPER:  gs->enemies[i].health = 2; break;
            case ENEMY_BOSS:    gs->enemies[i].health = 30 + gs->level * 10; break;
        }
    }

    /* Spawn powerups from map */
    {
        WORD pi = 0;
        WORD mx, my;
        for (my = 0; my < MAP_H && pi < MAX_POWERUPS; my++) {
            for (mx = 0; mx < MAP_W && pi < MAX_POWERUPS; mx++) {
                if (level_maps[gs->level][my][mx] == TILE_POWERUP) {
                    gs->powerups[pi].x = FIX(mx * TILE_W);
                    gs->powerups[pi].y = FIX(my * TILE_H);
                    gs->powerups[pi].type = (pi & 1) ? POWERUP_HEALTH : POWERUP_WEAPON;
                    gs->powerups[pi].active = 1;
                    gs->powerups[pi].anim_frame = 0;
                    pi++;
                    /* Clear the tile so it doesn't draw as powerup tile */
                    level_maps[gs->level][my][mx] = TILE_EMPTY;
                }
            }
        }
    }

    gs->state = STATE_PLAYING;
    gs->state_timer = 0;
}

/* --- Tank update --- */

static void update_tank(GameState *gs, WORD inp_left, WORD inp_right,
                        WORD inp_jump, WORD inp_fire)
{
    Tank *t = &gs->tank;
    Fixed move_speed;
    WORD tx_left, tx_right, ty_top, ty_bot;

    if (!t->alive) return;

    /* Invulnerability countdown */
    if (t->invuln_timer > 0) t->invuln_timer--;

    /* Gun cooldown */
    if (t->gun_timer > 0) t->gun_timer--;

    /* Horizontal movement */
    move_speed = (Fixed)((g_tune.tank_speed * 65536L) / 100);

    if (inp_left) {
        t->dx = -move_speed;
        t->facing = 1;
    } else if (inp_right) {
        t->dx = move_speed;
        t->facing = 0;
    } else {
        t->dx = 0;
    }

    /* Jump */
    if (inp_jump && t->on_ground && !t->jump_held) {
        t->dy = -((Fixed)((g_tune.jump_power * 65536L) / 100));
        t->on_ground = 0;
        t->jump_held = 1;
        sfx_jump();
    }
    if (!inp_jump) {
        t->jump_held = 0;
        /* Cut jump short if released early */
        if (t->dy < 0) {
            t->dy = t->dy / 2;
        }
    }

    /* Gravity */
    {
        Fixed grav = (Fixed)((g_tune.gravity * 65536L) / 100);
        t->dy += grav;
        if (t->dy > TANK_MAX_FALL) t->dy = TANK_MAX_FALL;
    }

    /* Horizontal collision */
    {
        Fixed new_x = t->x + t->dx;
        WORD px_left, px_right, px_top, px_bot;

        /* Clamp to map bounds */
        if (new_x < 0) new_x = 0;
        if (FIX_INT(new_x) + TANK_W > MAP_W * TILE_W)
            new_x = FIX((MAP_W * TILE_W) - TANK_W);

        px_left = FIX_INT(new_x);
        px_right = px_left + TANK_W - 1;
        px_top = FIX_INT(t->y);
        px_bot = px_top + TANK_H - 1;

        tx_left = px_left / TILE_W;
        tx_right = px_right / TILE_W;
        ty_top = px_top / TILE_H;
        ty_bot = px_bot / TILE_H;

        if (tile_is_solid(gs->level, tx_left, ty_top) ||
            tile_is_solid(gs->level, tx_left, ty_bot) ||
            tile_is_solid(gs->level, tx_right, ty_top) ||
            tile_is_solid(gs->level, tx_right, ty_bot)) {
            /* Blocked - don't move */
            t->dx = 0;
        } else {
            t->x = new_x;
        }
    }

    /* Vertical collision */
    {
        Fixed new_y = t->y + t->dy;
        WORD px_left, px_right, px_top, px_bot;

        px_left = FIX_INT(t->x);
        px_right = px_left + TANK_W - 1;
        px_top = FIX_INT(new_y);
        px_bot = px_top + TANK_H - 1;

        tx_left = px_left / TILE_W;
        tx_right = px_right / TILE_W;
        ty_top = px_top / TILE_H;
        ty_bot = px_bot / TILE_H;

        t->on_ground = 0;

        if (t->dy >= 0) {
            /* Falling - check floor */
            if (tile_is_solid(gs->level, tx_left, ty_bot) ||
                tile_is_solid(gs->level, tx_right, ty_bot) ||
                tile_is_platform(gs->level, tx_left, ty_bot) ||
                tile_is_platform(gs->level, tx_right, ty_bot)) {
                /* Land on top of tile */
                t->y = FIX(ty_bot * TILE_H - TANK_H);
                t->dy = 0;
                t->on_ground = 1;
            } else {
                t->y = new_y;
            }
        } else {
            /* Rising - check ceiling */
            if (tile_is_solid(gs->level, tx_left, ty_top) ||
                tile_is_solid(gs->level, tx_right, ty_top)) {
                t->y = FIX((ty_top + 1) * TILE_H);
                t->dy = 0;
            } else {
                t->y = new_y;
            }
        }
    }

    /* Check spike damage */
    {
        WORD px = FIX_INT(t->x) + TANK_W / 2;
        WORD py = FIX_INT(t->y) + TANK_H;
        WORD tx = px / TILE_W;
        WORD ty = py / TILE_H;
        UBYTE tt = tile_at(gs->level, tx, ty);
        if ((tile_props[tt] & TPROP_DAMAGE) && t->invuln_timer == 0) {
            t->health--;
            t->invuln_timer = INVULN_TIME;
            sfx_player_hit();
            if (t->health <= 0) {
                t->alive = 0;
                gs->state = STATE_DEAD;
                gs->state_timer = 120;
                game_spawn_particles(gs, t->x, t->y, 20, 8);
                game_spawn_explosion(gs, t->x + FIX(TANK_W/2),
                                     t->y + FIX(TANK_H/2), 20);
                sfx_explode();
            }
        }
    }

    /* Check exit gate */
    {
        WORD px = FIX_INT(t->x) + TANK_W / 2;
        WORD py = FIX_INT(t->y) + TANK_H / 2;
        WORD tx = px / TILE_W;
        WORD ty = py / TILE_H;
        UBYTE tt = tile_at(gs->level, tx, ty);
        if (tile_props[tt] & TPROP_EXIT) {
            gs->state = STATE_LEVELCLEAR;
            gs->state_timer = 120;
        }
    }

    /* Fire bullet */
    if (inp_fire && t->gun_timer == 0) {
        WORD i;
        for (i = 0; i < MAX_BULLETS; i++) {
            if (!gs->bullets[i].active) {
                Fixed bspeed = (Fixed)((g_tune.bullet_speed * 65536L) / 100);
                gs->bullets[i].x = t->x + (t->facing ? 0 : FIX(TANK_W));
                gs->bullets[i].y = t->y + FIX(4);
                gs->bullets[i].dx = t->facing ? -bspeed : bspeed;
                gs->bullets[i].dy = 0;
                gs->bullets[i].life = BULLET_LIFE;
                gs->bullets[i].active = 1;
                gs->bullets[i].power = 1 + t->weapon_level / 2;
                gs->bullets[i].size = (t->weapon_level < 3) ? 0 :
                                      (t->weapon_level < 6) ? 1 : 2;
                t->gun_timer = TANK_GUN_COOLDOWN - t->weapon_level / 2;
                if (t->gun_timer < 2) t->gun_timer = 2;
                sfx_shoot();
                break;
            }
        }
    }

    /* Animation */
    if (t->dx != 0) {
        t->anim_frame = (gs->frame / 4) & 3;
    } else {
        t->anim_frame = 0;
    }
}

/* --- Bullet update --- */

static void update_bullets(GameState *gs)
{
    WORD i;
    for (i = 0; i < MAX_BULLETS; i++) {
        Bullet *b = &gs->bullets[i];
        if (!b->active) continue;

        b->x += b->dx;
        b->y += b->dy;
        b->life--;

        if (b->life <= 0) {
            b->active = 0;
            continue;
        }

        /* Check tile collision */
        {
            WORD px = FIX_INT(b->x);
            WORD py = FIX_INT(b->y);
            WORD tx = px / TILE_W;
            WORD ty = py / TILE_H;
            if (tile_is_solid(gs->level, tx, ty)) {
                b->active = 0;
                game_spawn_particles(gs, b->x, b->y, 3, 10);
            }
        }

        /* Off map? */
        if (FIX_INT(b->x) < 0 || FIX_INT(b->x) >= MAP_W * TILE_W ||
            FIX_INT(b->y) < 0 || FIX_INT(b->y) >= MAP_H * TILE_H) {
            b->active = 0;
        }
    }
}

/* --- Enemy AI --- */

static void enemy_fire(GameState *gs, Enemy *e)
{
    WORD i;
    for (i = 0; i < MAX_ENEMY_BULLETS; i++) {
        if (!gs->enemy_bullets[i].active) {
            Fixed dx = 0, dy = 0;
            Fixed espeed = FIX(2);

            /* Aim toward player */
            if (gs->tank.x < e->x) dx = -espeed;
            else dx = espeed;

            gs->enemy_bullets[i].x = e->x + FIX(8);
            gs->enemy_bullets[i].y = e->y + FIX(4);
            gs->enemy_bullets[i].dx = dx;
            gs->enemy_bullets[i].dy = dy;
            gs->enemy_bullets[i].life = 60;
            gs->enemy_bullets[i].active = 1;
            break;
        }
    }
}

static void update_enemies(GameState *gs)
{
    WORD i;
    Fixed espeed = (Fixed)((g_tune.enemy_speed * 65536L) / 100);

    for (i = 0; i < MAX_ENEMIES; i++) {
        Enemy *e = &gs->enemies[i];
        if (!e->active) continue;

        e->timer++;
        e->fire_timer++;

        switch (e->type) {
            case ENEMY_WALKER:
                /* Walk back and forth */
                e->dx = e->facing ? -espeed : espeed;
                e->x += e->dx;

                /* Check wall ahead */
                {
                    WORD px = FIX_INT(e->x) + (e->facing ? 0 : 15);
                    WORD py = FIX_INT(e->y) + 8;
                    if (tile_is_solid(gs->level, px / TILE_W, py / TILE_H)) {
                        e->facing ^= 1;
                    }
                }
                /* Check floor ahead (don't walk off edges) */
                {
                    WORD px = FIX_INT(e->x) + (e->facing ? 0 : 15);
                    WORD py = FIX_INT(e->y) + 17;
                    if (!tile_is_solid(gs->level, px / TILE_W, py / TILE_H) &&
                        !tile_is_platform(gs->level, px / TILE_W, py / TILE_H)) {
                        e->facing ^= 1;
                    }
                }
                e->anim_frame = (gs->frame / 6) & 3;
                break;

            case ENEMY_FLYER:
                /* Sine wave flight */
                e->dx = e->facing ? -espeed : espeed;
                e->x += e->dx;
                /* Sine bob using frame counter */
                {
                    WORD bob = (e->timer * 4) & 0xFF;
                    /* Simple approximation: triangle wave */
                    if (bob < 64) e->dy = FIX(1);
                    else if (bob < 192) e->dy = -FIX(1);
                    else e->dy = FIX(1);
                    e->y += e->dy / 4;
                }
                /* Turn at map edges */
                if (FIX_INT(e->x) < TILE_W || FIX_INT(e->x) > (MAP_W - 2) * TILE_W)
                    e->facing ^= 1;
                e->anim_frame = (gs->frame / 4) & 3;

                /* Shoot occasionally */
                if (e->fire_timer > 80) {
                    enemy_fire(gs, e);
                    e->fire_timer = 0;
                }
                break;

            case ENEMY_TURRET:
                /* Stationary, shoots at player */
                e->facing = (gs->tank.x < e->x) ? 1 : 0;
                if (e->fire_timer > 50) {
                    enemy_fire(gs, e);
                    e->fire_timer = 0;
                }
                e->anim_frame = (gs->frame / 8) & 1;
                break;

            case ENEMY_HOPPER:
                /* Hop toward player */
                if (e->state == 0) {
                    /* On ground, waiting */
                    if (e->timer > 30) {
                        e->facing = (gs->tank.x < e->x) ? 1 : 0;
                        e->dx = e->facing ? -espeed * 2 : espeed * 2;
                        e->dy = -FIX(3);
                        e->state = 1;
                        e->timer = 0;
                    }
                } else {
                    /* In air */
                    e->x += e->dx;
                    e->dy += TANK_GRAVITY;
                    e->y += e->dy;

                    /* Check landing */
                    {
                        WORD py = FIX_INT(e->y) + 15;
                        WORD px = FIX_INT(e->x) + 8;
                        if (e->dy > 0 &&
                            (tile_is_solid(gs->level, px / TILE_W, py / TILE_H) ||
                             tile_is_platform(gs->level, px / TILE_W, py / TILE_H))) {
                            e->y = FIX((py / TILE_H) * TILE_H - 16);
                            e->dy = 0;
                            e->dx = 0;
                            e->state = 0;
                            e->timer = 0;
                        }
                    }
                }
                e->anim_frame = (e->state == 1) ? 1 : 0;
                break;

            case ENEMY_BOSS:
                /* Boss behavior: move back and forth, shoot frequently */
                if (e->state == 0) {
                    e->dx = espeed;
                    if (e->timer > 60) { e->state = 1; e->timer = 0; }
                } else if (e->state == 1) {
                    e->dx = -espeed;
                    if (e->timer > 60) { e->state = 0; e->timer = 0; }
                }
                e->x += e->dx;

                /* Boss shoots more often as health decreases */
                {
                    WORD fire_rate = 20 + e->health;
                    if (fire_rate < 15) fire_rate = 15;
                    if (e->fire_timer > fire_rate) {
                        enemy_fire(gs, e);
                        e->fire_timer = 0;
                    }
                }

                /* Vertical bob */
                e->y += (e->timer & 32) ? FIX(1) / 4 : -FIX(1) / 4;

                e->anim_frame = (gs->frame / 4) & 3;
                break;
        }
    }
}

/* --- Enemy bullet update --- */

static void update_enemy_bullets(GameState *gs)
{
    WORD i;
    for (i = 0; i < MAX_ENEMY_BULLETS; i++) {
        EnemyBullet *b = &gs->enemy_bullets[i];
        if (!b->active) continue;

        b->x += b->dx;
        b->y += b->dy;
        b->life--;

        if (b->life <= 0) { b->active = 0; continue; }

        /* Tile collision */
        {
            WORD px = FIX_INT(b->x);
            WORD py = FIX_INT(b->y);
            if (tile_is_solid(gs->level, px / TILE_W, py / TILE_H)) {
                b->active = 0;
            }
        }

        /* Hit player? */
        if (gs->tank.alive && gs->tank.invuln_timer == 0) {
            WORD dx = FIX_INT(b->x) - FIX_INT(gs->tank.x) - TANK_W/2;
            WORD dy = FIX_INT(b->y) - FIX_INT(gs->tank.y) - TANK_H/2;
            if (dx > -12 && dx < 12 && dy > -10 && dy < 10) {
                b->active = 0;
                gs->tank.health--;
                gs->tank.invuln_timer = INVULN_TIME;
                sfx_player_hit();
                if (gs->tank.health <= 0) {
                    gs->tank.alive = 0;
                    gs->state = STATE_DEAD;
                    gs->state_timer = 120;
                    game_spawn_particles(gs, gs->tank.x, gs->tank.y, 20, 8);
                    game_spawn_explosion(gs, gs->tank.x + FIX(TANK_W/2),
                                         gs->tank.y + FIX(TANK_H/2), 20);
                    sfx_explode();
                }
            }
        }
    }
}

/* --- Collision: player bullets vs enemies --- */

static void check_bullet_enemy_collisions(GameState *gs)
{
    WORD i, j;
    for (i = 0; i < MAX_BULLETS; i++) {
        Bullet *b = &gs->bullets[i];
        if (!b->active) continue;

        for (j = 0; j < MAX_ENEMIES; j++) {
            Enemy *e = &gs->enemies[j];
            if (!e->active) continue;

            /* Simple box collision */
            {
                WORD dx = FIX_INT(b->x) - FIX_INT(e->x) - 8;
                WORD dy = FIX_INT(b->y) - FIX_INT(e->y) - 8;
                WORD rad = (e->type == ENEMY_BOSS) ? 16 : 10;

                if (dx > -rad && dx < rad && dy > -rad && dy < rad) {
                    e->health -= b->power;
                    b->active = 0;
                    sfx_hit();
                    game_spawn_particles(gs, b->x, b->y, 4, 10);

                    if (e->health <= 0) {
                        /* Enemy destroyed */
                        e->active = 0;
                        sfx_explode();
                        game_spawn_particles(gs, e->x + FIX(8),
                                             e->y + FIX(8), 12, 9);
                        game_spawn_explosion(gs, e->x + FIX(8),
                                             e->y + FIX(8),
                                             e->type == ENEMY_BOSS ? 30 : 15);

                        /* Score based on enemy type */
                        switch (e->type) {
                            case ENEMY_WALKER:  gs->score += 100; break;
                            case ENEMY_FLYER:   gs->score += 200; break;
                            case ENEMY_TURRET:  gs->score += 150; break;
                            case ENEMY_HOPPER:  gs->score += 150; break;
                            case ENEMY_BOSS:    gs->score += 2000; break;
                        }
                    }
                    break;
                }
            }
        }
    }
}

/* --- Collision: tank vs enemies --- */

static void check_tank_enemy_collisions(GameState *gs)
{
    WORD i;
    Tank *t = &gs->tank;

    if (!t->alive || t->invuln_timer > 0) return;

    for (i = 0; i < MAX_ENEMIES; i++) {
        Enemy *e = &gs->enemies[i];
        if (!e->active) continue;

        {
            WORD dx = FIX_INT(t->x) + TANK_W/2 - FIX_INT(e->x) - 8;
            WORD dy = FIX_INT(t->y) + TANK_H/2 - FIX_INT(e->y) - 8;
            if (dx > -14 && dx < 14 && dy > -12 && dy < 12) {
                t->health--;
                t->invuln_timer = INVULN_TIME;
                sfx_player_hit();
                if (t->health <= 0) {
                    t->alive = 0;
                    gs->state = STATE_DEAD;
                    gs->state_timer = 120;
                    game_spawn_particles(gs, t->x, t->y, 20, 8);
                    game_spawn_explosion(gs, t->x + FIX(TANK_W/2),
                                         t->y + FIX(TANK_H/2), 20);
                    sfx_explode();
                }
                break;
            }
        }
    }
}

/* --- Collision: tank vs powerups --- */

static void check_powerup_collisions(GameState *gs)
{
    WORD i;
    Tank *t = &gs->tank;

    if (!t->alive) return;

    for (i = 0; i < MAX_POWERUPS; i++) {
        Powerup *p = &gs->powerups[i];
        if (!p->active) continue;

        {
            WORD dx = FIX_INT(t->x) + TANK_W/2 - FIX_INT(p->x) - 8;
            WORD dy = FIX_INT(t->y) + TANK_H/2 - FIX_INT(p->y) - 8;
            if (dx > -14 && dx < 14 && dy > -14 && dy < 14) {
                p->active = 0;
                sfx_powerup();
                game_spawn_particles(gs, p->x + FIX(8), p->y + FIX(8), 8, 4);

                if (p->type == POWERUP_WEAPON) {
                    if (t->weapon_level < 7) t->weapon_level++;
                    gs->score += 50;
                } else {
                    if (t->health < MAX_HEALTH) t->health++;
                    gs->score += 25;
                }
            }
        }
    }
}

/* --- Particles --- */

void game_spawn_particles(GameState *gs, Fixed x, Fixed y,
                          WORD count, WORD color)
{
    WORD i, spawned = 0;
    for (i = 0; i < MAX_PARTICLES && spawned < count; i++) {
        if (!gs->particles[i].active) {
            gs->particles[i].x = x;
            gs->particles[i].y = y;
            gs->particles[i].dx = (rng() % 512 - 256) * 128;
            gs->particles[i].dy = (rng() % 512 - 256) * 128;
            gs->particles[i].life = 10 + (rng() % 20);
            gs->particles[i].color = color;
            gs->particles[i].active = 1;
            spawned++;
        }
    }
}

void game_spawn_explosion(GameState *gs, Fixed x, Fixed y, WORD radius)
{
    WORD i;
    for (i = 0; i < MAX_EXPLOSIONS; i++) {
        if (!gs->explosions[i].active) {
            gs->explosions[i].x = x;
            gs->explosions[i].y = y;
            gs->explosions[i].radius = radius;
            gs->explosions[i].life = 8;
            gs->explosions[i].active = 1;
            break;
        }
    }
}

static void update_particles(GameState *gs)
{
    WORD i;
    for (i = 0; i < MAX_PARTICLES; i++) {
        Particle *p = &gs->particles[i];
        if (!p->active) continue;
        p->x += p->dx;
        p->y += p->dy;
        p->dy += FIX(1) / 8; /* slight gravity */
        p->life--;
        if (p->life <= 0) p->active = 0;
    }
}

static void update_explosions(GameState *gs)
{
    WORD i;
    for (i = 0; i < MAX_EXPLOSIONS; i++) {
        Explosion *e = &gs->explosions[i];
        if (!e->active) continue;
        e->life--;
        if (e->life <= 0) e->active = 0;
    }
}

static void update_powerups(GameState *gs)
{
    WORD i;
    for (i = 0; i < MAX_POWERUPS; i++) {
        if (gs->powerups[i].active)
            gs->powerups[i].anim_frame = (gs->frame / 8) & 3;
    }
}

/* --- Scrolling --- */

static void update_scroll(GameState *gs)
{
    WORD tank_screen_x;
    LONG max_scroll;

    /* Target scroll: keep tank roughly 1/3 from left edge */
    gs->scroll.target_x = FIX_INT(gs->tank.x) - SCREEN_W / 3;

    /* Clamp scroll range */
    max_scroll = (MAP_W * TILE_W) - SCREEN_W;
    if (gs->scroll.target_x < 0) gs->scroll.target_x = 0;
    if (gs->scroll.target_x > max_scroll) gs->scroll.target_x = max_scroll;

    /* Smooth scroll toward target (lerp) */
    {
        LONG diff = gs->scroll.target_x - gs->scroll.pixel_x;
        if (diff > 4) gs->scroll.pixel_x += 4;
        else if (diff < -4) gs->scroll.pixel_x -= 4;
        else gs->scroll.pixel_x = gs->scroll.target_x;
    }

    /* Compute tile column and fine offset */
    gs->scroll.tile_col = (WORD)(gs->scroll.pixel_x / TILE_W);
    gs->scroll.fine_x = (WORD)(gs->scroll.pixel_x & (TILE_W - 1));
}

/* --- Main update --- */

void game_update(GameState *gs, WORD inp_left, WORD inp_right,
                 WORD inp_jump, WORD inp_fire)
{
    gs->frame++;

    switch (gs->state) {
        case STATE_PLAYING:
            update_tank(gs, inp_left, inp_right, inp_jump, inp_fire);
            update_bullets(gs);
            update_enemies(gs);
            update_enemy_bullets(gs);
            update_particles(gs);
            update_explosions(gs);
            update_powerups(gs);
            check_bullet_enemy_collisions(gs);
            check_tank_enemy_collisions(gs);
            check_powerup_collisions(gs);
            update_scroll(gs);
            break;

        case STATE_DEAD:
            update_particles(gs);
            update_explosions(gs);
            gs->state_timer--;
            if (gs->state_timer <= 0) {
                gs->lives--;
                if (gs->lives <= 0) {
                    gs->state = STATE_GAMEOVER;
                    gs->state_timer = 180;
                } else {
                    /* Respawn */
                    gs->tank.alive = 1;
                    gs->tank.health = MAX_HEALTH;
                    gs->tank.x = FIX(32);
                    gs->tank.y = FIX(12 * TILE_H);
                    gs->tank.dx = 0;
                    gs->tank.dy = 0;
                    gs->tank.invuln_timer = INVULN_TIME;
                    gs->scroll.pixel_x = 0;
                    gs->scroll.target_x = 0;
                    gs->state = STATE_PLAYING;
                }
            }
            break;

        case STATE_GAMEOVER:
            gs->state_timer--;
            if (gs->state_timer <= 0) {
                gs->state = STATE_TITLE;
            }
            break;

        case STATE_LEVELCLEAR:
            gs->state_timer--;
            if (gs->state_timer <= 0) {
                if (gs->level < 2) {
                    game_load_level(gs, (WORD)(gs->level + 1));
                } else {
                    /* Game won! */
                    gs->state = STATE_GAMEOVER;
                    gs->state_timer = 180;
                    gs->score += 5000;  /* completion bonus */
                }
            }
            break;
    }
}
