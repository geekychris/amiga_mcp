/*
 * Jump Quest
 * A platformer for the Amiga
 *
 * Main entry point, game loop, state machine
 */
#include "game.h"
#include <string.h>
#include <bridge_client.h>

Game game;

/* Particles */
static Particle particles[MAX_PARTICLES];

void particles_init(void) {
    memset(particles, 0, sizeof(particles));
}

void particles_spawn(int x, int y, int count, int color) {
    int i, j;
    for (i = 0, j = 0; i < MAX_PARTICLES && j < count; i++) {
        if (particles[i].life <= 0) {
            particles[i].x = x;
            particles[i].y = y;
            particles[i].vx = (WORD)((i * 7 + 3) % 9 - 4);
            particles[i].vy = (WORD)(-(i * 5 % 8 + 2));
            particles[i].life = 20 + (i & 7);
            particles[i].color = (BYTE)color;
            j++;
        }
    }
}

void particles_update(void) {
    int i;
    for (i = 0; i < MAX_PARTICLES; i++) {
        if (particles[i].life > 0) {
            particles[i].x += particles[i].vx;
            particles[i].y += particles[i].vy;
            particles[i].vy += 1; /* gravity */
            particles[i].life--;
        }
    }
}

void particles_draw(struct RastPort *rp, int cam_x) {
    int i, sx, sy;
    for (i = 0; i < MAX_PARTICLES; i++) {
        if (particles[i].life > 0) {
            sx = particles[i].x - cam_x;
            sy = particles[i].y;
            if (sx >= 0 && sx < SCREEN_W && sy >= 0 && sy < HUD_Y) {
                SetAPen(rp, particles[i].color);
                RectFill(rp, sx, sy, sx + 1, sy + 1);
            }
        }
    }
}

/* Window for IDCMP */
static struct Window *gameWindow = NULL;

static struct Window *open_game_window(void) {
    struct NewWindow nw;
    memset(&nw, 0, sizeof(nw));
    nw.Width = SCREEN_W;
    nw.Height = SCREEN_H;
    nw.IDCMPFlags = IDCMP_RAWKEY;
    nw.Flags = WFLG_BORDERLESS | WFLG_BACKDROP | WFLG_ACTIVATE | WFLG_RMBTRAP;
    nw.Screen = gameScreen;
    nw.Type = CUSTOMSCREEN;
    return OpenWindow(&nw);
}

/* Initialize a level */
static void start_level(int level_num) {
    const LevelDef *ld;
    const EntitySpawn *s;
    Player *p;

    level_load(level_num);
    ld = level_current();

    enemies_init();
    enemies_spawn(ld->entities);
    items_init();
    items_spawn(ld->entities);
    particles_init();

    p = &game.player[game.active_player];

    /* Find player start */
    for (s = ld->entities; s->type != ENT_NONE; s++) {
        if (s->type == ENT_PLAYER_START) {
            player_init(p, p->character, s->tx * TILE_SIZE, s->ty * TILE_SIZE - PLAYER_H + TILE_SIZE);
            /* Preserve score and lives from previous level */
            break;
        }
    }

    game.cam_x = 0;
    game.state = STATE_PLAYING;
    game.transition = 0;
}

/* Update camera to follow player */
static void update_camera(Player *p) {
    int target_x = p->x - SCREEN_W / 3;
    int max_x = level_width_pixels() - SCREEN_W;

    if (target_x < 0) target_x = 0;
    if (target_x > max_x) target_x = max_x;

    /* Smooth camera, snap to tile boundary */
    if (game.cam_x < target_x)
        game.cam_x += (target_x - game.cam_x + 3) / 4;
    else if (game.cam_x > target_x)
        game.cam_x -= (game.cam_x - target_x + 3) / 4;

    /* Snap to tile boundary to prevent partial-tile artifacts */
    game.cam_x = (game.cam_x / TILE_SIZE) * TILE_SIZE;
}

/* Check level end */
static int check_level_end(Player *p) {
    const EntitySpawn *s;
    const LevelDef *ld = level_current();
    int px_center = p->x + PLAYER_W / 2;
    int py_center = p->y + PLAYER_H / 2;

    for (s = ld->entities; s->type != ENT_NONE; s++) {
        if (s->type == ENT_LEVEL_END) {
            int ex = s->tx * TILE_SIZE;
            int ey = s->ty * TILE_SIZE;
            if (px_center > ex && px_center < ex + TILE_SIZE * 2 &&
                py_center > ey - TILE_SIZE && py_center < ey + TILE_SIZE * 2) {
                return 1;
            }
        }
    }
    return 0;
}

/* Bridge hooks */
static int hook_reset(const char *args, char *result, int bufSize) {
    game.state = STATE_TITLE;
    (void)args;
    return 0;
}

static int hook_skip_level(const char *args, char *result, int bufSize) {
    game.current_level++;
    if (game.current_level >= 3) game.current_level = 0;
    start_level(game.current_level);
    (void)args;
    return 0;
}

int main(void) {
    struct RastPort *rp;
    Player *p;
    UWORD inp;
    int selected;
    int running = 1;
    int bridge_counter = 0;
    (void)0; /* placeholders removed */

    /* Initialize bridge */
    ab_init("jump_quest");
    AB_I("Jump Quest starting");

    /* Init graphics */
    if (!gfx_init()) {
        ab_cleanup();
        return 5;
    }

    /* Open window for keyboard input */
    gameWindow = open_game_window();
    if (!gameWindow) {
        gfx_cleanup();
        ab_cleanup();
        return 5;
    }

    /* Init sound */
    if (!sound_init()) {
        AB_W("Sound init failed, continuing without sound");
    }

    /* Init input */
    input_init();

    /* Register bridge variables */
    ab_register_var("score", AB_TYPE_I32, &game.player[0].score);
    ab_register_var("lives", AB_TYPE_I32, &game.player[0].lives);
    ab_register_var("health", AB_TYPE_I32, &game.player[0].health);
    ab_register_var("level", AB_TYPE_I32, &game.current_level);
    ab_register_var("state", AB_TYPE_I32, &game.state);
    ab_register_hook("reset", "Reset to title screen", hook_reset);
    ab_register_hook("skip_level", "Skip to next level", hook_skip_level);

    /* Initialize game state */
    memset(&game, 0, sizeof(game));

title:
    sound_music_stop();
    game.state = STATE_TITLE;

    rp = gfx_backbuffer();
    selected = title_screen(rp);
    if (selected < 0) goto quit;

    /* Set up players */
    game.player[0].character = selected;
    game.player[0].lives = 3;
    game.player[0].score = 0;
    game.player[0].health = 3;
    game.player[0].max_health = 5;

    if (game.num_players == 2) {
        game.player[1].character = (selected == CHAR_RJ) ? CHAR_DALE : CHAR_RJ;
        game.player[1].lives = 3;
        game.player[1].score = 0;
        game.player[1].health = 3;
        game.player[1].max_health = 5;
    }

    game.active_player = 0;
    game.current_level = 0;

    /* Start music and first level */
    sound_music_start();
    start_level(0);

    /* Main game loop */
    while (running) {
        rp = gfx_backbuffer();
        p = &game.player[game.active_player];

        switch (game.state) {
        case STATE_PLAYING:
            inp = input_read();

            /* Update */
            player_update(p, inp);
            enemies_update();
            items_update();
            particles_update();
            update_camera(p);
            sound_music_tick();

            /* Check level end */
            if (check_level_end(p)) {
                game.state = STATE_LEVELWIN;
                game.transition = 100;
                sound_levelwin();
            }

            /* Check death */
            if (p->health <= 0) {
                game.state = STATE_DYING;
                game.transition = 80;
                sound_die();
            }

            /* Draw - tiles cover entire game area, no separate clear needed */
            level_draw(rp, game.cam_x);
            items_draw(rp, game.cam_x);
            enemies_draw(rp, game.cam_x);
            player_draw(rp, p, game.cam_x);
            particles_draw(rp, game.cam_x);
            hud_draw(rp, p, game.current_level);
            break;

        case STATE_DYING:
            game.transition--;
            particles_update();
            sound_music_tick();

            /* Flash screen */
            level_draw(rp, game.cam_x);
            items_draw(rp, game.cam_x);
            enemies_draw(rp, game.cam_x);
            particles_draw(rp, game.cam_x);
            hud_draw(rp, p, game.current_level);

            if (game.transition == 70) {
                particles_spawn(p->x, p->y, 8, COL_RED);
            }

            if (game.transition <= 0) {
                p->lives--;
                if (p->lives <= 0) {
                    /* Check 2P mode */
                    if (game.num_players == 2 && game.active_player == 0 &&
                        game.player[1].lives > 0) {
                        game.active_player = 1;
                        start_level(game.current_level);
                    } else if (game.num_players == 2 && game.active_player == 1 &&
                               game.player[0].lives > 0) {
                        game.active_player = 0;
                        start_level(game.current_level);
                    } else {
                        game.state = STATE_GAMEOVER;
                        game.transition = 150;
                        sound_music_stop();
                    }
                } else {
                    /* Respawn */
                    p->health = 3;
                    start_level(game.current_level);
                }
            }
            break;

        case STATE_LEVELWIN:
            game.transition--;
            sound_music_tick();

            level_draw(rp, game.cam_x);
            items_draw(rp, game.cam_x);
            player_draw(rp, p, game.cam_x);
            hud_draw(rp, p, game.current_level);

            /* Bonus text */
            SetAPen(rp, COL_WHITE);
            Move(rp, 80, 100);
            Text(rp, "LEVEL COMPLETE!", 15L);

            SetAPen(rp, COL_YELLOW);
            Move(rp, 100, 120);
            Text(rp, "BONUS: +500", 11L);

            if (game.transition <= 0) {
                p->score += 500;
                game.current_level++;

                if (game.current_level >= 3) {
                    /* Game won! */
                    game.state = STATE_GAMEOVER;
                    game.transition = 200;
                    sound_music_stop();
                } else {
                    /* In 2P, swap players between levels */
                    if (game.num_players == 2) {
                        game.active_player ^= 1;
                        p = &game.player[game.active_player];
                    }
                    start_level(game.current_level);
                }
            }
            break;

        case STATE_GAMEOVER:
            game.transition--;

            /* Game over screen */
            SetAPen(rp, COL_BLACK);
            RectFill(rp, 0, 0, SCREEN_W - 1, SCREEN_H - 1);

            if (game.current_level >= 3) {
                SetAPen(rp, COL_YELLOW);
                Move(rp, 60, 80);
                Text(rp, "CONGRATULATIONS!", 16L);
                SetAPen(rp, COL_WHITE);
                Move(rp, 40, 110);
                Text(rp, "YOU SAVED THE DAY!", 18L);
            } else {
                SetAPen(rp, COL_RED);
                Move(rp, 100, 80);
                Text(rp, "GAME OVER", 9L);
            }

            SetAPen(rp, COL_WHITE);
            Move(rp, 80, 150);
            Text(rp, "FINAL SCORE:", 12L);
            {
                char sbuf[16];
                int slen = 0;
                long sv = game.player[0].score;
                if (game.num_players == 2)
                    sv += game.player[1].score;
                if (sv == 0) { sbuf[0] = '0'; slen = 1; }
                else {
                    while (sv > 0) { sbuf[slen++] = '0' + (char)(sv % 10); sv /= 10; }
                    /* reverse */
                    { int a; for (a = 0; a < slen/2; a++) {
                        char t = sbuf[a]; sbuf[a] = sbuf[slen-1-a]; sbuf[slen-1-a] = t;
                    }}
                }
                Move(rp, 200, 150);
                Text(rp, sbuf, (long)slen);
            }

            hud_draw(rp, &game.player[0], game.current_level);

            if (game.transition <= 0) {
                /* Wait for input */
                inp = input_read();
                if (inp & (INP_JUMP | INP_START)) {
                    goto title;
                }
            }
            break;
        }

        gfx_swap();

        /* Escape to quit */
        if (input_check_esc()) {
            running = 0;
        }

        /* Bridge updates every 30 frames */
        bridge_counter++;
        if (bridge_counter >= 30) {
            bridge_counter = 0;
            ab_push_var("score");
            ab_push_var("lives");
            ab_push_var("health");
            ab_push_var("level");
            ab_push_var("state");
            ab_heartbeat();
        }
        ab_poll();
    }

quit:
    sound_cleanup();
    if (gameWindow) CloseWindow(gameWindow);
    gfx_cleanup();
    ab_cleanup();
    return 0;
}
