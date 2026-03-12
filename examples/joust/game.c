/*
 * JOUST - Game logic
 * Physics, collision, enemy AI, wave management
 */
#include <string.h>
#include "game.h"

/* Platform layout */
static const Platform default_platforms[] = {
    { 0,   GROUND_Y, 320 },   /* ground - full width */
    { 20,  180, 80  },         /* lower left */
    { 220, 180, 80  },         /* lower right */
    { 110, 140, 100 },         /* middle center */
    { 0,   100, 70  },         /* upper left */
    { 250, 100, 70  },         /* upper right */
    { 130,  60, 60  },         /* top center */
};
#define NUM_DEFAULT_PLATFORMS 7

/* Simple pseudo-random */
static ULONG rng_state = 12345;
static WORD rng(void)
{
    rng_state = rng_state * 1103515245UL + 12345UL;
    return (WORD)((rng_state >> 16) & 0x7FFF);
}

void game_init(GameState *gs)
{
    WORD i;
    memset(gs, 0, sizeof(GameState));

    gs->state = STATE_TITLE;
    gs->wave = 0;
    gs->num_players = 1;
    gs->title_selection = 0;

    /* Setup platforms */
    gs->num_platforms = NUM_DEFAULT_PLATFORMS;
    for (i = 0; i < NUM_DEFAULT_PLATFORMS; i++) {
        gs->platforms[i] = default_platforms[i];
    }

    /* Init players */
    for (i = 0; i < MAX_PLAYERS; i++) {
        gs->players[i].lives = 3;
        gs->players[i].active = 0;
        gs->players[i].alive = 0;
        gs->score[i] = 0;
    }
}

static void spawn_player(GameState *gs, WORD idx)
{
    Player *p = &gs->players[idx];
    p->x = (idx == 0) ? FIX(80) : FIX(220);
    p->y = FIX(GROUND_Y - PLAYER_H);
    p->dx = 0;
    p->dy = 0;
    p->alive = 1;
    p->active = 1;
    p->facing = (idx == 0) ? 0 : 1;
    p->anim_frame = 0;
    p->anim_timer = 0;
    p->on_ground = 1;
    p->respawn_timer = 0;
    p->flapping = 0;
}

void game_start(GameState *gs)
{
    WORD i;

    gs->wave = 0;
    gs->state = STATE_WAVE_INTRO;
    gs->state_timer = 100;

    for (i = 0; i < MAX_ENEMIES; i++)
        gs->enemies[i].active = 0;
    for (i = 0; i < MAX_EGGS; i++)
        gs->eggs[i].active = 0;

    /* Reset players */
    for (i = 0; i < gs->num_players; i++) {
        gs->players[i].lives = 3;
        gs->score[i] = 0;
        spawn_player(gs, i);
    }

    gs->wave = 1;
    gs->ev_flags |= EV_WAVE;
}

static void spawn_enemy(GameState *gs, WORD type)
{
    WORD i;
    for (i = 0; i < MAX_ENEMIES; i++) {
        if (!gs->enemies[i].active) {
            Enemy *e = &gs->enemies[i];
            e->active = 1;
            e->type = type;
            e->facing = rng() & 1;

            /* Spawn at random platform */
            {
                WORD pi = 1 + (rng() % (gs->num_platforms - 1)); /* not ground */
                Platform *pl = &gs->platforms[pi];
                e->x = FIX(pl->x + (rng() % pl->w));
                e->y = FIX(pl->y - PLAYER_H);
            }

            e->dx = e->facing ? -ENEMY_SLOW : ENEMY_SLOW;
            e->dy = 0;
            e->anim_frame = 0;
            e->anim_timer = 0;
            e->on_ground = 0;
            e->flap_timer = 20 + (rng() % 30);

            /* Adjust speed by type */
            if (type == ETYPE_HUNTER) {
                e->dx = e->facing ? -ENEMY_MED : ENEMY_MED;
            } else if (type == ETYPE_SHADOW) {
                e->dx = e->facing ? -ENEMY_FAST : ENEMY_FAST;
            }
            return;
        }
    }
}

void game_start_wave(GameState *gs)
{
    WORD i;
    WORD num_enemies;
    WORD bounders, hunters, shadows;

    /* Clear old enemies and eggs */
    for (i = 0; i < MAX_ENEMIES; i++)
        gs->enemies[i].active = 0;
    for (i = 0; i < MAX_EGGS; i++)
        gs->eggs[i].active = 0;

    /* Calculate enemy counts based on wave */
    num_enemies = 2 + gs->wave;
    if (num_enemies > MAX_ENEMIES) num_enemies = MAX_ENEMIES;

    bounders = 0;
    hunters = 0;
    shadows = 0;

    if (gs->wave <= 2) {
        bounders = num_enemies;
    } else if (gs->wave <= 4) {
        bounders = num_enemies / 2;
        hunters = num_enemies - bounders;
    } else {
        bounders = num_enemies / 3;
        hunters = num_enemies / 3;
        shadows = num_enemies - bounders - hunters;
        if (shadows < 1) shadows = 1;
    }

    for (i = 0; i < bounders; i++) spawn_enemy(gs, ETYPE_BOUNDER);
    for (i = 0; i < hunters; i++)  spawn_enemy(gs, ETYPE_HUNTER);
    for (i = 0; i < shadows; i++) spawn_enemy(gs, ETYPE_SHADOW);
}

/* Check if entity is standing on a platform */
static WORD check_platform(GameState *gs, Fixed fx, Fixed fy, WORD w)
{
    WORD px = FIX_INT(fx);
    WORD py = FIX_INT(fy) + PLAYER_H;
    WORD i;

    for (i = 0; i < gs->num_platforms; i++) {
        Platform *pl = &gs->platforms[i];
        if (py >= pl->y && py <= pl->y + PLAT_THICK) {
            if (px + w > pl->x && px < pl->x + pl->w) {
                return pl->y;
            }
        }
    }
    return -1;
}

/* Horizontal wrap */
static Fixed wrap_x(Fixed fx)
{
    WORD px = FIX_INT(fx);
    if (px < -PLAYER_W) return FIX(SCREEN_W);
    if (px > SCREEN_W) return FIX(-PLAYER_W + 1);
    return fx;
}

static void update_player(GameState *gs, Player *p, UWORD input)
{
    WORD plat_y;

    if (!p->alive) {
        if (p->respawn_timer > 0) {
            p->respawn_timer--;
            if (p->respawn_timer == 0 && p->lives > 0) {
                WORD idx = (WORD)(p - gs->players);
                spawn_player(gs, idx);
            }
        }
        return;
    }

    /* Horizontal input */
    if (input & INP_LEFT) {
        p->dx = -MOVE_SPEED;
        p->facing = 1;
    } else if (input & INP_RIGHT) {
        p->dx = MOVE_SPEED;
        p->facing = 0;
    } else {
        /* Friction */
        if (p->dx > 0) {
            p->dx -= 8192;
            if (p->dx < 0) p->dx = 0;
        } else if (p->dx < 0) {
            p->dx += 8192;
            if (p->dx > 0) p->dx = 0;
        }
    }

    /* Flap */
    if (input & INP_FLAP) {
        p->dy += FLAP_IMPULSE / 4; /* spread over frames for smoother feel */
        if (p->dy < MAX_RISE_VEL) p->dy = MAX_RISE_VEL;
        p->flapping = 1;
        p->on_ground = 0;
        gs->ev_flags |= EV_FLAP;
    } else {
        p->flapping = 0;
    }

    /* Gravity */
    p->dy += GRAVITY;
    if (p->dy > MAX_FALL_VEL) p->dy = MAX_FALL_VEL;

    /* Move */
    p->x += p->dx;
    p->y += p->dy;

    /* Horizontal wrap */
    p->x = wrap_x(p->x);

    /* Ceiling */
    if (FIX_INT(p->y) < 10) {
        p->y = FIX(10);
        p->dy = 0;
    }

    /* Platform collision */
    plat_y = check_platform(gs, p->x, p->y, PLAYER_W);
    if (plat_y >= 0 && p->dy >= 0) {
        p->y = FIX(plat_y - PLAYER_H);
        p->dy = 0;
        p->on_ground = 1;
    } else {
        p->on_ground = 0;
    }

    /* Floor */
    if (FIX_INT(p->y) + PLAYER_H >= SCREEN_H) {
        p->y = FIX(SCREEN_H - PLAYER_H);
        p->dy = 0;
        p->on_ground = 1;
    }

    /* Animation */
    p->anim_timer++;
    if (p->anim_timer > 6) {
        p->anim_timer = 0;
        p->anim_frame ^= 1;
    }
}

static void update_enemy(GameState *gs, Enemy *e)
{
    WORD plat_y;

    if (!e->active) return;

    /* AI: flap periodically */
    e->flap_timer--;
    if (e->flap_timer <= 0) {
        e->dy += FLAP_IMPULSE / 3;
        if (e->dy < MAX_RISE_VEL) e->dy = MAX_RISE_VEL;
        /* Vary flap timing by type */
        if (e->type == ETYPE_BOUNDER)
            e->flap_timer = 30 + (rng() % 40);
        else if (e->type == ETYPE_HUNTER)
            e->flap_timer = 20 + (rng() % 30);
        else
            e->flap_timer = 15 + (rng() % 20);
    }

    /* Gravity */
    e->dy += GRAVITY;
    if (e->dy > MAX_FALL_VEL) e->dy = MAX_FALL_VEL;

    /* Move */
    e->x += e->dx;
    e->y += e->dy;

    /* Horizontal wrap */
    e->x = wrap_x(e->x);

    /* Ceiling */
    if (FIX_INT(e->y) < 10) {
        e->y = FIX(10);
        e->dy = 0;
    }

    /* Platform collision */
    plat_y = check_platform(gs, e->x, e->y, PLAYER_W);
    if (plat_y >= 0 && e->dy >= 0) {
        e->y = FIX(plat_y - PLAYER_H);
        e->dy = 0;
        e->on_ground = 1;
    } else {
        e->on_ground = 0;
    }

    /* Floor */
    if (FIX_INT(e->y) + PLAYER_H >= SCREEN_H) {
        e->y = FIX(SCREEN_H - PLAYER_H);
        e->dy = 0;
        e->on_ground = 1;
    }

    /* Occasionally change direction (hunters/shadows seek players) */
    if (e->type >= ETYPE_HUNTER && (gs->frame & 63) == 0) {
        /* Find nearest active player */
        WORD i;
        Fixed best_dist = 0x7FFFFFFFL;
        WORD best_dir = e->facing;
        for (i = 0; i < gs->num_players; i++) {
            if (gs->players[i].alive) {
                Fixed dist = gs->players[i].x - e->x;
                if (dist < 0) dist = -dist;
                if (dist < best_dist) {
                    best_dist = dist;
                    best_dir = (gs->players[i].x < e->x) ? 1 : 0;
                }
            }
        }
        e->facing = best_dir;
        {
            Fixed spd = ENEMY_SLOW;
            if (e->type == ETYPE_HUNTER) spd = ENEMY_MED;
            else if (e->type == ETYPE_SHADOW) spd = ENEMY_FAST;
            e->dx = e->facing ? -spd : spd;
        }
    }

    /* Random direction change for bounders */
    if (e->type == ETYPE_BOUNDER && (rng() & 127) == 0) {
        e->facing ^= 1;
        e->dx = e->facing ? -ENEMY_SLOW : ENEMY_SLOW;
    }

    /* Animation */
    e->anim_timer++;
    if (e->anim_timer > 8) {
        e->anim_timer = 0;
        e->anim_frame ^= 1;
    }
}

static void spawn_egg(GameState *gs, Fixed x, Fixed y, WORD hatch_type)
{
    WORD i;
    for (i = 0; i < MAX_EGGS; i++) {
        if (!gs->eggs[i].active) {
            gs->eggs[i].active = 1;
            gs->eggs[i].x = x;
            gs->eggs[i].y = y;
            gs->eggs[i].dy = 0;
            gs->eggs[i].timer = EGG_HATCH_TIME;
            gs->eggs[i].on_ground = 0;
            gs->eggs[i].hatch_type = hatch_type;
            return;
        }
    }
}

static void update_eggs(GameState *gs)
{
    WORD i;
    for (i = 0; i < MAX_EGGS; i++) {
        Egg *eg = &gs->eggs[i];
        if (!eg->active) continue;

        /* Gravity */
        if (!eg->on_ground) {
            eg->dy += GRAVITY;
            if (eg->dy > MAX_FALL_VEL) eg->dy = MAX_FALL_VEL;
            eg->y += eg->dy;

            /* Platform check */
            {
                WORD plat_y = check_platform(gs, eg->x, eg->y, 8);
                if (plat_y >= 0 && eg->dy >= 0) {
                    eg->y = FIX(plat_y - 8);
                    eg->dy = 0;
                    eg->on_ground = 1;
                }
            }

            /* Floor */
            if (FIX_INT(eg->y) + 8 >= SCREEN_H) {
                eg->y = FIX(SCREEN_H - 8);
                eg->dy = 0;
                eg->on_ground = 1;
            }
        }

        /* Hatch timer */
        eg->timer--;
        if (eg->timer <= 0) {
            /* Hatch into a stronger enemy */
            WORD new_type = eg->hatch_type;
            if (new_type < ETYPE_SHADOW) new_type++;
            spawn_enemy(gs, new_type);
            eg->active = 0;
        }

        /* Player pickup collision */
        {
            WORD j;
            WORD ex = FIX_INT(eg->x);
            WORD ey = FIX_INT(eg->y);
            for (j = 0; j < gs->num_players; j++) {
                Player *p = &gs->players[j];
                if (!p->alive) continue;
                {
                    WORD px = FIX_INT(p->x);
                    WORD py = FIX_INT(p->y);
                    if (px + PLAYER_W > ex && px < ex + 8 &&
                        py + PLAYER_H > ey && py < ey + 8) {
                        gs->score[j] += SCORE_EGG;
                        eg->active = 0;
                        gs->ev_flags |= EV_EGG;
                    }
                }
            }
        }
    }
}

/* Check combat between player and enemy */
static void check_combat(GameState *gs)
{
    WORD i, j;

    for (i = 0; i < gs->num_players; i++) {
        Player *p = &gs->players[i];
        if (!p->alive) continue;

        for (j = 0; j < MAX_ENEMIES; j++) {
            Enemy *e = &gs->enemies[j];
            if (!e->active) continue;

            {
                WORD px = FIX_INT(p->x);
                WORD py = FIX_INT(p->y);
                WORD ex = FIX_INT(e->x);
                WORD ey = FIX_INT(e->y);

                /* Bounding box overlap */
                if (px + PLAYER_W > ex && px < ex + PLAYER_W &&
                    py + PLAYER_H > ey && py < ey + PLAYER_H) {

                    /* Whoever is higher wins */
                    if (py < ey - 2) {
                        /* Player wins */
                        WORD pts = SCORE_BOUNDER;
                        if (e->type == ETYPE_HUNTER) pts = SCORE_HUNTER;
                        else if (e->type == ETYPE_SHADOW) pts = SCORE_SHADOW;
                        gs->score[i] += pts;

                        spawn_egg(gs, e->x, e->y, e->type);
                        e->active = 0;
                        gs->ev_flags |= EV_KILL;

                        /* Bounce player up */
                        p->dy = FLAP_IMPULSE / 2;
                    }
                    else if (ey < py - 2) {
                        /* Enemy wins - player dies */
                        p->alive = 0;
                        p->lives--;
                        p->respawn_timer = 100; /* 2 seconds */
                        gs->ev_flags |= EV_DIE;

                        if (p->lives <= 0) {
                            p->active = 0;
                            /* Check if all players dead */
                            {
                                WORD all_dead = 1;
                                WORD k;
                                for (k = 0; k < gs->num_players; k++) {
                                    if (gs->players[k].lives > 0 || gs->players[k].alive) {
                                        all_dead = 0;
                                        break;
                                    }
                                }
                                if (all_dead) {
                                    gs->state = STATE_GAMEOVER;
                                    gs->state_timer = 200;
                                }
                            }
                        }
                    }
                    else {
                        /* Equal height - bounce off */
                        p->dx = (px < ex) ? -MOVE_SPEED : MOVE_SPEED;
                        e->dx = (ex < px) ? -ENEMY_SLOW : ENEMY_SLOW;
                        p->dy = FLAP_IMPULSE / 3;
                        e->dy = FLAP_IMPULSE / 3;
                    }
                }
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
        /* Handle selection */
        if (input->sys & INP_START1) {
            gs->num_players = 1;
            game_start(gs);
        } else if (input->sys & INP_START2) {
            gs->num_players = 2;
            game_start(gs);
        }
        /* Animate title selection */
        if (input->p1 & INP_FLAP) {
            gs->title_selection ^= 1;
        }
        break;

    case STATE_WAVE_INTRO:
        gs->state_timer--;
        if (gs->state_timer <= 0) {
            game_start_wave(gs);
            gs->state = STATE_PLAYING;
        }
        break;

    case STATE_PLAYING:
        /* Update players */
        update_player(gs, &gs->players[0], input->p1);
        if (gs->num_players > 1) {
            update_player(gs, &gs->players[1], input->p2);
        }

        /* Update enemies */
        for (i = 0; i < MAX_ENEMIES; i++) {
            update_enemy(gs, &gs->enemies[i]);
        }

        /* Update eggs */
        update_eggs(gs);

        /* Combat */
        check_combat(gs);

        /* Check wave complete */
        {
            WORD alive = 0;
            for (i = 0; i < MAX_ENEMIES; i++) {
                if (gs->enemies[i].active) alive++;
            }
            /* Also count eggs as "alive" enemies (they can hatch) */
            for (i = 0; i < MAX_EGGS; i++) {
                if (gs->eggs[i].active) alive++;
            }
            gs->enemies_alive = alive;

            if (alive == 0) {
                /* Wave complete */
                for (i = 0; i < gs->num_players; i++) {
                    if (gs->players[i].alive) {
                        gs->score[i] += SCORE_WAVE;
                    }
                }
                gs->wave++;
                gs->state = STATE_WAVE_INTRO;
                gs->state_timer = 100;
                gs->ev_flags |= EV_WAVE;
            }
        }
        break;

    case STATE_GAMEOVER:
        gs->state_timer--;
        if (gs->state_timer <= 0) {
            game_init(gs);
        }
        break;
    }
}
