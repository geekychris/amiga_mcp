/*
 * Moon Patrol - Game logic
 * Scrolling, physics, collision, terrain generation, enemies, checkpoints
 */
#include <string.h>
#include "game.h"

/* Simple PRNG */
static ULONG rng_state = 54321;
static WORD rng(void)
{
    rng_state = rng_state * 1103515245UL + 12345UL;
    return (WORD)((rng_state >> 16) & 0x7FFF);
}

/* ---- Terrain generation ---- */

static void generate_terrain_chunk(GameState *gs, LONG start_x, LONG end_x)
{
    LONG x;
    WORD base_h = GROUND_Y;

    for (x = start_x; x < end_x && x < TERRAIN_LEN; x++) {
        WORD idx = (WORD)(x % TERRAIN_LEN);
        WORD world_x = (WORD)(gs->terrain_gen_x + (x - start_x));
        WORD r;

        gs->terrain[idx] = TERRAIN_FLAT;
        gs->terrain_h[idx] = base_h;

        /* Generate features based on world position */
        r = rng() % 100;

        /* Increase feature density with difficulty */
        if (world_x > 30) {  /* leave initial area clear */
            if (r < 3 + gs->difficulty) {
                /* Small crater */
                WORD w, cx;
                gs->terrain[idx] = TERRAIN_CRATER_SM;
                /* Mark a few pixels as crater */
                for (w = 0; w < 16 && (x + w) < end_x; w++) {
                    cx = (WORD)((x + w) % TERRAIN_LEN);
                    gs->terrain[cx] = TERRAIN_CRATER_SM;
                    gs->terrain_h[cx] = base_h + 20;  /* gap in ground */
                }
                x += 15;
            } else if (r < 5 + gs->difficulty) {
                /* Large crater */
                WORD w, cx;
                gs->terrain[idx] = TERRAIN_CRATER_LG;
                for (w = 0; w < 28 && (x + w) < end_x; w++) {
                    cx = (WORD)((x + w) % TERRAIN_LEN);
                    gs->terrain[cx] = TERRAIN_CRATER_LG;
                    gs->terrain_h[cx] = base_h + 30;
                }
                x += 27;
            } else if (r < 8 + gs->difficulty * 2) {
                /* Rock/boulder */
                gs->terrain[idx] = TERRAIN_ROCK;
                gs->terrain_h[idx] = base_h;
            } else if (r < 12 + gs->difficulty) {
                /* Hill (cosmetic bump) */
                WORD w, cx;
                for (w = 0; w < 24 && (x + w) < end_x; w++) {
                    cx = (WORD)((x + w) % TERRAIN_LEN);
                    gs->terrain[cx] = TERRAIN_HILL;
                    /* Parabolic bump, max 8 pixels high */
                    {
                        WORD hw = w - 12;
                        WORD bump = 8 - (hw * hw / 18);
                        if (bump < 0) bump = 0;
                        gs->terrain_h[cx] = base_h - bump;
                    }
                }
                x += 23;
            } else if (r < 14 + gs->difficulty * 2) {
                /* Ground mine */
                gs->terrain[idx] = TERRAIN_MINE;
                gs->terrain_h[idx] = base_h;
            }
        }
    }

    gs->terrain_gen_x += (end_x - start_x);
}

/* ---- Enemy spawning ---- */

static void try_spawn_enemy(GameState *gs)
{
    WORD i;
    WORD spawn_chance = 2 + gs->difficulty * 2;
    WORD r = rng() % 100;

    if (r >= spawn_chance) return;

    /* Find free slot */
    for (i = 0; i < MAX_ENEMIES; i++) {
        if (!gs->enemies[i].active) {
            WORD type_r = rng() % 100;
            Enemy *e = &gs->enemies[i];

            e->active = 1;
            e->flash = 0;
            e->bomb_timer = 60 + (rng() % 60);

            if (type_r < 40) {
                /* Small UFO */
                e->type = ENEMY_UFO_SM;
                e->x = SCREEN_W + 10;
                e->y = 30 + (rng() % 40);
                e->dx = -2 - (rng() % 2);
                e->dy = 0;
                e->hp = 1;
            } else if (type_r < 65) {
                /* Large UFO */
                e->type = ENEMY_UFO_LG;
                e->x = SCREEN_W + 10;
                e->y = 20 + (rng() % 30);
                e->dx = -1;
                e->dy = 0;
                e->hp = 2;
            } else {
                /* Meteor */
                e->type = ENEMY_METEOR;
                e->x = SCREEN_W + (rng() % 60);
                e->y = -10;
                e->dx = -(1 + (rng() % 2));
                e->dy = 2 + (rng() % 2);
                e->hp = 1;
            }
            break;
        }
    }
}

/* ---- Initialization ---- */

void game_init(GameState *gs)
{
    WORD i;

    memset(gs, 0, sizeof(GameState));

    gs->lives = 3;
    gs->score = 0;
    gs->state = STATE_TITLE;
    gs->frame = 0;
    gs->difficulty = 0;

    gs->scroll_x = 0;
    gs->scroll_speed = SPEED_NORMAL;
    gs->terrain_gen_x = 0;

    /* Init buggy */
    gs->buggy.x = BUGGY_X;
    gs->buggy.y = BUGGY_GROUND_Y;
    gs->buggy.vy = 0;
    gs->buggy.on_ground = 1;
    gs->buggy.alive = 1;
    gs->buggy.wheel_frame = 0;

    /* Clear bullets */
    for (i = 0; i < MAX_FWD_BULLETS; i++) gs->fwd_bullets[i].active = 0;
    for (i = 0; i < MAX_UP_BULLETS; i++) gs->up_bullets[i].active = 0;
    for (i = 0; i < MAX_ENEMIES; i++) gs->enemies[i].active = 0;
    for (i = 0; i < MAX_BOMBS; i++) gs->bombs[i].active = 0;
    for (i = 0; i < MAX_EXPLOSIONS; i++) gs->explosions[i].active = 0;

    /* Init terrain - flat for a while then obstacles */
    for (i = 0; i < TERRAIN_LEN; i++) {
        gs->terrain[i] = TERRAIN_FLAT;
        gs->terrain_h[i] = GROUND_Y;
    }

    gs->checkpoint_cur = 0;
    gs->checkpoint_dist = CHECKPOINT_DIST;

    /* Generate initial terrain */
    generate_terrain_chunk(gs, 0, TERRAIN_LEN);

    /* Clear event flags */
    gs->ev_shoot = 0;
    gs->ev_explode = 0;
    gs->ev_jump = 0;
    gs->ev_checkpoint = 0;
    gs->ev_death = 0;
}

/* ---- Spawn explosion ---- */

static void spawn_explosion(GameState *gs, WORD x, WORD y)
{
    WORD i;
    for (i = 0; i < MAX_EXPLOSIONS; i++) {
        if (!gs->explosions[i].active) {
            gs->explosions[i].x = x;
            gs->explosions[i].y = y;
            gs->explosions[i].radius = 2;
            gs->explosions[i].life = 12;
            gs->explosions[i].active = 1;
            break;
        }
    }
}

/* ---- Update functions ---- */

static void update_buggy(GameState *gs, InputState *inp)
{
    Buggy *b = &gs->buggy;
    WORD terrain_idx;
    WORD ground_h;

    if (!b->alive) return;

    /* Speed control */
    if (inp->right) {
        gs->scroll_speed = SPEED_FAST;
    } else if (inp->left) {
        gs->scroll_speed = SPEED_SLOW;
    } else {
        gs->scroll_speed = SPEED_NORMAL;
    }

    /* Jump */
    if ((inp->jump) && b->on_ground) {
        b->vy = JUMP_VEL;
        b->on_ground = 0;
        gs->ev_jump = 1;
    }

    /* Gravity */
    if (!b->on_ground) {
        b->vy += GRAVITY;
    }

    /* Apply vertical velocity (fixed point /100) */
    b->y += b->vy / 100;

    /* Get ground height at buggy position */
    terrain_idx = (WORD)((gs->scroll_x / 100 + BUGGY_X + BUGGY_W / 2) % TERRAIN_LEN);
    if (terrain_idx < 0) terrain_idx += TERRAIN_LEN;
    ground_h = gs->terrain_h[terrain_idx] - BUGGY_H;

    /* Check ground collision */
    if (b->y >= ground_h) {
        WORD t_type = gs->terrain[terrain_idx];

        if (t_type == TERRAIN_CRATER_SM || t_type == TERRAIN_CRATER_LG) {
            /* Falling into crater = death */
            if (b->on_ground || b->vy > 0) {
                /* Only die if not jumping over */
                if (b->y >= GROUND_Y - BUGGY_H + 5) {
                    b->alive = 0;
                    gs->state = STATE_DYING;
                    gs->state_timer = 40;
                    gs->ev_death = 1;
                    spawn_explosion(gs, b->x + BUGGY_W / 2, b->y + BUGGY_H / 2);
                    return;
                }
            }
        } else {
            b->y = ground_h;
            b->vy = 0;
            b->on_ground = 1;
        }
    } else {
        b->on_ground = 0;
    }

    /* Check rock collision */
    {
        WORD rock_idx = (WORD)((gs->scroll_x / 100 + BUGGY_X + BUGGY_W) % TERRAIN_LEN);
        if (rock_idx < 0) rock_idx += TERRAIN_LEN;
        if (gs->terrain[rock_idx] == TERRAIN_ROCK && b->on_ground) {
            /* Hit a rock! */
            b->alive = 0;
            gs->state = STATE_DYING;
            gs->state_timer = 40;
            gs->ev_death = 1;
            spawn_explosion(gs, b->x + BUGGY_W / 2, b->y + BUGGY_H / 2);
            return;
        }
    }

    /* Check mine collision */
    {
        WORD mine_idx = (WORD)((gs->scroll_x / 100 + BUGGY_X + BUGGY_W / 2) % TERRAIN_LEN);
        if (mine_idx < 0) mine_idx += TERRAIN_LEN;
        if (gs->terrain[mine_idx] == TERRAIN_MINE && b->on_ground) {
            b->alive = 0;
            gs->state = STATE_DYING;
            gs->state_timer = 40;
            gs->ev_death = 1;
            gs->ev_explode = 1;
            spawn_explosion(gs, b->x + BUGGY_W / 2, GROUND_Y);
            /* Remove the mine */
            gs->terrain[mine_idx] = TERRAIN_FLAT;
            return;
        }
    }

    /* Wheel animation */
    b->wheel_frame = (WORD)((gs->scroll_x / 200) & 1);

    /* Fire bullets */
    if (inp->fire) {
        WORD i;
        WORD fired = 0;

        /* Forward bullet */
        for (i = 0; i < MAX_FWD_BULLETS; i++) {
            if (!gs->fwd_bullets[i].active) {
                gs->fwd_bullets[i].x = b->x + BUGGY_W;
                gs->fwd_bullets[i].y = b->y + 4;
                gs->fwd_bullets[i].dx = 5;
                gs->fwd_bullets[i].dy = 0;
                gs->fwd_bullets[i].active = 1;
                fired = 1;
                break;
            }
        }

        /* Upward bullet */
        for (i = 0; i < MAX_UP_BULLETS; i++) {
            if (!gs->up_bullets[i].active) {
                gs->up_bullets[i].x = b->x + BUGGY_W / 2;
                gs->up_bullets[i].y = b->y;
                gs->up_bullets[i].dx = 0;
                gs->up_bullets[i].dy = -5;
                gs->up_bullets[i].active = 1;
                fired = 1;
                break;
            }
        }

        if (fired) {
            gs->ev_shoot = 1;
        }
    }
}

static void update_bullets(GameState *gs)
{
    WORD i;

    for (i = 0; i < MAX_FWD_BULLETS; i++) {
        Bullet *b = &gs->fwd_bullets[i];
        if (!b->active) continue;
        b->x += b->dx;
        if (b->x > SCREEN_W + 10) {
            b->active = 0;
            continue;
        }
        /* Check if bullet hits a rock */
        {
            WORD world_bx = (WORD)(gs->scroll_x / 100 + b->x);
            WORD tidx = (WORD)(world_bx % TERRAIN_LEN);
            if (tidx < 0) tidx += TERRAIN_LEN;
            if (gs->terrain[tidx] == TERRAIN_ROCK && b->y >= GROUND_Y - 12) {
                b->active = 0;
                gs->terrain[tidx] = TERRAIN_FLAT;
                gs->score += 100;
                gs->ev_explode = 1;
                spawn_explosion(gs, b->x, GROUND_Y - 6);
            }
        }
    }

    for (i = 0; i < MAX_UP_BULLETS; i++) {
        Bullet *b = &gs->up_bullets[i];
        if (!b->active) continue;
        b->y += b->dy;
        if (b->y < -10) {
            b->active = 0;
        }
    }
}

static void update_enemies(GameState *gs)
{
    WORD i;

    for (i = 0; i < MAX_ENEMIES; i++) {
        Enemy *e = &gs->enemies[i];
        if (!e->active) continue;

        e->x += e->dx;
        e->y += e->dy;
        e->flash++;

        /* Off screen? */
        if (e->x < -30 || e->y > SCREEN_H + 10) {
            e->active = 0;
            continue;
        }

        /* UFO bomb dropping */
        if ((e->type == ENEMY_UFO_SM || e->type == ENEMY_UFO_LG) && e->bomb_timer > 0) {
            e->bomb_timer--;
            if (e->bomb_timer == 0) {
                WORD j;
                for (j = 0; j < MAX_BOMBS; j++) {
                    if (!gs->bombs[j].active) {
                        gs->bombs[j].x = e->x + 4;
                        gs->bombs[j].y = e->y + 10;
                        gs->bombs[j].dy = 2;
                        gs->bombs[j].active = 1;
                        break;
                    }
                }
                /* Large UFO drops multiple bombs */
                if (e->type == ENEMY_UFO_LG) {
                    e->bomb_timer = 40 + (rng() % 30);
                } else {
                    e->bomb_timer = 0; /* single bomb */
                }
            }
        }

        /* Meteor hits ground */
        if (e->type == ENEMY_METEOR && e->y >= GROUND_Y - 4) {
            e->active = 0;
            gs->ev_explode = 1;
            spawn_explosion(gs, e->x, GROUND_Y - 4);
        }
    }

    /* Try spawning new enemies periodically */
    if ((gs->frame % 60) == 0) {
        try_spawn_enemy(gs);
    }
}

static void update_bombs(GameState *gs)
{
    WORD i;
    Buggy *buggy = &gs->buggy;

    for (i = 0; i < MAX_BOMBS; i++) {
        Bomb *b = &gs->bombs[i];
        if (!b->active) continue;

        b->y += b->dy;

        /* Hit ground */
        if (b->y >= GROUND_Y) {
            b->active = 0;
            gs->ev_explode = 1;
            spawn_explosion(gs, b->x, GROUND_Y);

            /* Check proximity to buggy */
            if (buggy->alive) {
                WORD dx = b->x - (buggy->x + BUGGY_W / 2);
                if (dx < 0) dx = -dx;
                if (dx < 20) {
                    buggy->alive = 0;
                    gs->state = STATE_DYING;
                    gs->state_timer = 40;
                    gs->ev_death = 1;
                    spawn_explosion(gs, buggy->x + BUGGY_W / 2,
                                    buggy->y + BUGGY_H / 2);
                }
            }
            continue;
        }

        /* Direct hit on buggy */
        if (buggy->alive) {
            if (b->x >= buggy->x && b->x <= buggy->x + BUGGY_W &&
                b->y >= buggy->y && b->y <= buggy->y + BUGGY_H) {
                b->active = 0;
                buggy->alive = 0;
                gs->state = STATE_DYING;
                gs->state_timer = 40;
                gs->ev_death = 1;
                gs->ev_explode = 1;
                spawn_explosion(gs, buggy->x + BUGGY_W / 2,
                                buggy->y + BUGGY_H / 2);
            }
        }
    }
}

static void check_bullet_enemy_collisions(GameState *gs)
{
    WORD i, j;

    /* Forward bullets vs enemies */
    for (i = 0; i < MAX_FWD_BULLETS; i++) {
        Bullet *b = &gs->fwd_bullets[i];
        if (!b->active) continue;

        for (j = 0; j < MAX_ENEMIES; j++) {
            Enemy *e = &gs->enemies[j];
            WORD ew, eh;
            if (!e->active) continue;

            ew = (e->type == ENEMY_UFO_LG) ? 20 : 12;
            eh = (e->type == ENEMY_UFO_LG) ? 14 : 10;

            if (b->x >= e->x && b->x <= e->x + ew &&
                b->y >= e->y && b->y <= e->y + eh) {
                b->active = 0;
                e->hp--;
                if (e->hp <= 0) {
                    e->active = 0;
                    gs->ev_explode = 1;
                    spawn_explosion(gs, e->x + ew / 2, e->y + eh / 2);

                    switch (e->type) {
                        case ENEMY_UFO_SM: gs->score += 200; break;
                        case ENEMY_UFO_LG: gs->score += 500; break;
                        case ENEMY_METEOR: gs->score += 300; break;
                    }
                }
                break;
            }
        }
    }

    /* Upward bullets vs enemies */
    for (i = 0; i < MAX_UP_BULLETS; i++) {
        Bullet *b = &gs->up_bullets[i];
        if (!b->active) continue;

        for (j = 0; j < MAX_ENEMIES; j++) {
            Enemy *e = &gs->enemies[j];
            WORD ew, eh;
            if (!e->active) continue;

            ew = (e->type == ENEMY_UFO_LG) ? 20 : 12;
            eh = (e->type == ENEMY_UFO_LG) ? 14 : 10;

            if (b->x >= e->x && b->x <= e->x + ew &&
                b->y >= e->y && b->y <= e->y + eh) {
                b->active = 0;
                e->hp--;
                if (e->hp <= 0) {
                    e->active = 0;
                    gs->ev_explode = 1;
                    spawn_explosion(gs, e->x + ew / 2, e->y + eh / 2);

                    switch (e->type) {
                        case ENEMY_UFO_SM: gs->score += 200; break;
                        case ENEMY_UFO_LG: gs->score += 500; break;
                        case ENEMY_METEOR: gs->score += 300; break;
                    }
                }
                break;
            }
        }
    }
}

static void check_enemy_buggy_collision(GameState *gs)
{
    WORD i;
    Buggy *buggy = &gs->buggy;

    if (!buggy->alive) return;

    for (i = 0; i < MAX_ENEMIES; i++) {
        Enemy *e = &gs->enemies[i];
        WORD ew;
        if (!e->active) continue;

        ew = (e->type == ENEMY_UFO_LG) ? 20 : 12;

        /* Check overlap */
        if (buggy->x + BUGGY_W > e->x && buggy->x < e->x + ew &&
            buggy->y + BUGGY_H > e->y && buggy->y < e->y + 10) {
            buggy->alive = 0;
            e->active = 0;
            gs->state = STATE_DYING;
            gs->state_timer = 40;
            gs->ev_death = 1;
            gs->ev_explode = 1;
            spawn_explosion(gs, buggy->x + BUGGY_W / 2, buggy->y + BUGGY_H / 2);
            spawn_explosion(gs, e->x + ew / 2, e->y + 5);
            break;
        }
    }
}

static void update_explosions(GameState *gs)
{
    WORD i;
    for (i = 0; i < MAX_EXPLOSIONS; i++) {
        Explosion *e = &gs->explosions[i];
        if (!e->active) continue;
        e->life--;
        if (e->life > 6) {
            e->radius += 2;
        }
        if (e->life <= 0) {
            e->active = 0;
        }
    }
}

static void update_scrolling(GameState *gs)
{
    LONG old_scroll = gs->scroll_x;

    gs->scroll_x += gs->scroll_speed;

    /* Generate new terrain as needed */
    {
        LONG world_px = gs->scroll_x / 100 + SCREEN_W + 100;
        if (world_px > gs->terrain_gen_x) {
            generate_terrain_chunk(gs, (LONG)(gs->terrain_gen_x % TERRAIN_LEN),
                                   (LONG)((gs->terrain_gen_x % TERRAIN_LEN) + 64));
        }
    }

    /* Checkpoint tracking */
    gs->checkpoint_dist -= (WORD)((gs->scroll_x - old_scroll) / 100);
    if (gs->checkpoint_dist <= 0) {
        gs->checkpoint_cur++;
        if (gs->checkpoint_cur >= 26) {
            /* Completed course - loop with higher difficulty */
            gs->checkpoint_cur = 0;
            gs->difficulty++;
        }
        gs->checkpoint_dist = CHECKPOINT_DIST;
        gs->score += 1000;
        gs->ev_checkpoint = 1;
        gs->state = STATE_CHECKPOINT;
        gs->state_timer = 60;
    }
}

/* ---- Main update ---- */

void game_update(GameState *gs, InputState *inp)
{
    gs->frame++;

    /* Clear event flags */
    gs->ev_shoot = 0;
    gs->ev_explode = 0;
    gs->ev_jump = 0;
    gs->ev_checkpoint = 0;
    gs->ev_death = 0;

    switch (gs->state) {
        case STATE_PLAYING:
            update_scrolling(gs);
            update_buggy(gs, inp);
            update_bullets(gs);
            update_enemies(gs);
            update_bombs(gs);
            check_bullet_enemy_collisions(gs);
            check_enemy_buggy_collision(gs);
            update_explosions(gs);
            break;

        case STATE_DYING:
            update_explosions(gs);
            gs->state_timer--;
            if (gs->state_timer <= 0) {
                gs->lives--;
                if (gs->lives <= 0) {
                    gs->state = STATE_GAMEOVER;
                    gs->state_timer = 180;
                } else {
                    /* Respawn buggy */
                    gs->buggy.alive = 1;
                    gs->buggy.y = BUGGY_GROUND_Y;
                    gs->buggy.vy = 0;
                    gs->buggy.on_ground = 1;
                    gs->scroll_speed = SPEED_NORMAL;
                    gs->state = STATE_PLAYING;
                }
            }
            break;

        case STATE_GAMEOVER:
            gs->state_timer--;
            /* Stay in gameover until player presses fire or timer runs out */
            if (gs->state_timer <= 0) {
                gs->state = STATE_TITLE;
            }
            break;

        case STATE_CHECKPOINT:
            /* Brief celebration, then back to playing */
            update_scrolling(gs);
            update_buggy(gs, inp);
            update_bullets(gs);
            update_explosions(gs);
            gs->state_timer--;
            if (gs->state_timer <= 0) {
                gs->state = STATE_PLAYING;
            }
            break;

        case STATE_TITLE:
            /* Handled in main loop */
            break;
    }
}
