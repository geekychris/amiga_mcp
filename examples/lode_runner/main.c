/*
 * Lode Runner - An Amiga clone
 * Uses AmigaBridge for remote monitoring.
 */

#include <exec/types.h>
#include <intuition/intuition.h>
#include <graphics/gfx.h>
#include <graphics/rastport.h>
#include <hardware/cia.h>
#include <hardware/custom.h>
#include <proto/exec.h>
#include <proto/intuition.h>
#include <proto/graphics.h>
#include <proto/dos.h>

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "bridge_client.h"

#include "game.h"
#include "gfx.h"
#include "input.h"
#include "level.h"
#include "render.h"
#include "player.h"
#include "enemy.h"
#include "editor.h"
#include "sound.h"
#include "modplay.h"

struct IntuitionBase *IntuitionBase = NULL;
struct GfxBase *GfxBase = NULL;

extern struct Custom custom;

/* Global game state - accessible from all modules via extern */
GameState gs;

static LONG bridge_ok = 0;
static int transition_frames = 0;

/* Bridge variables for remote monitoring */
static LONG var_score = 0;
static LONG var_lives = 0;
static LONG var_level = 0;
static LONG var_gold = 0;
static LONG var_state = 0;
static LONG var_player_gx = 0;
static LONG var_player_gy = 0;

/* Hook: get game status */
static int hook_status(const char *args, char *resultBuf, int bufSize)
{
    sprintf(resultBuf,
            "state=%ld level=%ld score=%ld lives=%ld gold=%ld/%ld player=(%ld,%ld) frame=%ld",
            (long)gs.state, (long)gs.level_num, (long)gs.score,
            (long)gs.lives, (long)gs.gold_collected, (long)gs.gold_total,
            (long)gs.player.gx, (long)gs.player.gy, (long)gs.frame);
    resultBuf[bufSize - 1] = '\0';
    return 0;
}

/* Hook: reset game */
static int hook_reset(const char *args, char *resultBuf, int bufSize)
{
    gs.state = STATE_TITLE;
    transition_frames = 0;
    strncpy(resultBuf, "Game reset to title", bufSize - 1);
    resultBuf[bufSize - 1] = '\0';
    return 0;
}

/* Hook: goto specific level */
static int hook_goto_level(const char *args, char *resultBuf, int bufSize)
{
    int lvl;

    if (!args || !args[0]) {
        strncpy(resultBuf, "ERROR: specify level number", bufSize - 1);
        resultBuf[bufSize - 1] = '\0';
        return 1;
    }

    lvl = atoi(args);
    if (lvl < 1 || lvl > MAX_LEVELS) {
        sprintf(resultBuf, "ERROR: level must be 1-%ld", (long)MAX_LEVELS);
        resultBuf[bufSize - 1] = '\0';
        return 1;
    }

    gs.level_num = lvl;
    level_load(&gs, lvl);
    gs.state = STATE_PLAYING;
    sprintf(resultBuf, "Loaded level %ld", (long)lvl);
    resultBuf[bufSize - 1] = '\0';
    return 0;
}

static void update_bridge_vars(void)
{
    var_score = (LONG)gs.score;
    var_lives = (LONG)gs.lives;
    var_level = (LONG)gs.level_num;
    var_gold = (LONG)gs.gold_collected;
    var_state = (LONG)gs.state;
    var_player_gx = (LONG)gs.player.gx;
    var_player_gy = (LONG)gs.player.gy;
}

static void draw_title_wrapper(struct RastPort *rp)
{
    render_title(rp, &gs);
}

/* Game over drawing now handled by render_gameover() in render.c */

/* Level complete drawing now handled by render_level_done() in render.c */

/* Draw a full frame on both buffers (used during state transitions) */
static void draw_both_buffers(void (*draw_fn)(struct RastPort *))
{
    struct RastPort *rp;

    rp = gfx_backbuffer();
    draw_fn(rp);
    gfx_swap();
    gfx_vsync();

    rp = gfx_backbuffer();
    draw_fn(rp);
    gfx_swap();
    gfx_vsync();
}

int main(void)
{
    struct Window *win = NULL;
    struct RastPort *rp;
    int running = 1;
    int dx, dy;
    int dig_left, dig_right;
    int i;

    /* Open libraries */
    IntuitionBase = (struct IntuitionBase *)OpenLibrary((CONST_STRPTR)"intuition.library", 36);
    if (!IntuitionBase) return 1;

    GfxBase = (struct GfxBase *)OpenLibrary((CONST_STRPTR)"graphics.library", 36);
    if (!GfxBase) {
        CloseLibrary((struct Library *)IntuitionBase);
        return 1;
    }

    /* Connect to bridge */
    if (ab_init("lode_runner") == 0) {
        bridge_ok = 1;
        AB_I("Lode Runner starting");

        ab_register_var("score", AB_TYPE_I32, &var_score);
        ab_register_var("lives", AB_TYPE_I32, &var_lives);
        ab_register_var("level", AB_TYPE_I32, &var_level);
        ab_register_var("gold", AB_TYPE_I32, &var_gold);
        ab_register_var("game_state", AB_TYPE_I32, &var_state);
        ab_register_var("player_gx", AB_TYPE_I32, &var_player_gx);
        ab_register_var("player_gy", AB_TYPE_I32, &var_player_gy);

        ab_register_hook("status", "Get game status summary", hook_status);
        ab_register_hook("reset", "Reset to title screen", hook_reset);
        ab_register_hook("goto_level", "Load specific level (args=level number)", hook_goto_level);
    }

    /* Init graphics */
    if (gfx_init() != 0) {
        AB_E("Failed to open screen");
        if (bridge_ok) ab_cleanup();
        CloseLibrary((struct Library *)GfxBase);
        CloseLibrary((struct Library *)IntuitionBase);
        return 1;
    }

    /* Init sound + music */
    if (sound_init() != 0) {
        AB_W("Sound init failed - continuing without sound");
    }
    modplay_start();

    /* Open borderless backdrop window for IDCMP keyboard input */
    win = OpenWindowTags(NULL,
        WA_CustomScreen, (ULONG)gfx_screen(),
        WA_Left, 0, WA_Top, 0,
        WA_Width, SCREEN_W, WA_Height, SCREEN_H,
        WA_Borderless, (ULONG)TRUE,
        WA_Backdrop, (ULONG)TRUE,
        WA_RMBTrap, (ULONG)TRUE,
        WA_IDCMP, IDCMP_RAWKEY,
        WA_Activate, (ULONG)TRUE,
        TAG_DONE);

    /* Init input */
    input_init(win);

    /* Initialize game state */
    memset(&gs, 0, sizeof(gs));
    gs.lives = 5;
    gs.level_num = 1;
    gs.state = STATE_TITLE;

    AB_I("Game initialized, entering main loop");

    /* Draw title on both buffers */
    draw_both_buffers(draw_title_wrapper);

    while (running) {
        input_update();

        /* ESC or CTRL-C quits */
        if (input_key(KEY_ESC) ||
            (SetSignal(0L, SIGBREAKF_CTRL_C) & SIGBREAKF_CTRL_C)) {
            running = 0;
            break;
        }

        rp = gfx_backbuffer();

        switch (gs.state) {
        case STATE_TITLE:
            render_title(rp, &gs);
            if (input_fire()) {
                /* Start game */
                gs.score = 0;
                gs.lives = 5;
                gs.level_num = 1;
                level_load(&gs, gs.level_num);
                gs.state = STATE_PLAYING;
                AB_I("Game started");
            } else if (input_key(KEY_E)) {
                editor_init(&gs, 0);
                gs.state = STATE_EDITOR;
                AB_I("Entering editor");
            }
            break;

        case STATE_PLAYING:
            dx = input_dx();
            dy = input_dy();

            /* Digging: fire held + direction */
            dig_left = 0;
            dig_right = 0;
            if (input_fire_held()) {
                if (dx < 0) { dig_left = 1; dx = 0; }
                if (dx > 0) { dig_right = 1; dx = 0; }
            }

            /* Update game logic */
            player_update(&gs, dx, dy, dig_left, dig_right);
            enemy_update_all(&gs);
            level_tick_bricks(&gs);

            /* Check player vs enemy collisions */
            for (i = 0; i < gs.num_enemies; i++) {
                if (!gs.enemies[i].active) continue;
                if (gs.enemies[i].state == ES_TRAPPED) continue;
                if (gs.enemies[i].state == ES_DEAD) continue;
                if (gs.player.gx == gs.enemies[i].gx &&
                    gs.player.gy == gs.enemies[i].gy) {
                    gs.state = STATE_DYING;
                    gs.player.state = PS_DEAD;
                    transition_frames = 30;
                    AB_I("Player caught by enemy at (%ld,%ld)",
                         (long)gs.player.gx, (long)gs.player.gy);
                    break;
                }
            }

            /* Check if all gold collected - reveal hidden ladders */
            if (gs.state == STATE_PLAYING &&
                gs.gold_collected >= gs.gold_total) {
                level_reveal_hidden_ladders(&gs);
            }

            /* Check win: all gold collected AND player at top row */
            if (gs.state == STATE_PLAYING &&
                gs.gold_collected >= gs.gold_total &&
                gs.player.gy == 0) {
                gs.state = STATE_LEVEL_DONE;
                transition_frames = 60;
                AB_I("Level %ld complete!", (long)gs.level_num);
            }

            /* Render */
            render_playfield(rp, &gs);
            render_entities(rp, &gs);
            render_status(rp, &gs);
            break;

        case STATE_DYING:
            /* Death animation: flash player for 30 frames */
            transition_frames--;

            /* Still render the scene */
            render_playfield(rp, &gs);
            if (transition_frames % 4 < 2) {
                render_entities(rp, &gs);
            }
            render_status(rp, &gs);

            if (transition_frames <= 0) {
                gs.lives--;
                if (gs.lives <= 0) {
                    gs.state = STATE_GAMEOVER;
                    transition_frames = 30;
                    AB_I("Game over! Score: %ld", (long)gs.score);
                } else {
                    /* Reload current level */
                    level_load(&gs, gs.level_num);
                    gs.state = STATE_PLAYING;
                }
            }
            break;

        case STATE_LEVEL_DONE:
            /* Show level complete overlay */
            render_playfield(rp, &gs);
            render_entities(rp, &gs);
            render_status(rp, &gs);
            render_level_done(rp, &gs);

            transition_frames--;
            if (transition_frames <= 0) {
                gs.score += 1000;
                gs.level_num++;
                if (gs.level_num > MAX_LEVELS) gs.level_num = 1;
                level_load(&gs, gs.level_num);
                gs.state = STATE_PLAYING;
                AB_I("Starting level %ld", (long)gs.level_num);
            }
            break;

        case STATE_GAMEOVER:
            render_playfield(rp, &gs);
            render_status(rp, &gs);
            render_gameover(rp, &gs);

            if (transition_frames > 0) {
                transition_frames--;
            } else if (input_fire()) {
                gs.state = STATE_TITLE;
                draw_both_buffers(draw_title_wrapper);
                goto next_frame;
            }
            break;

        case STATE_EDITOR:
            editor_update(&gs);
            editor_draw(rp, &gs);

            if (input_key(KEY_ESC)) {
                gs.state = STATE_TITLE;
                draw_both_buffers(draw_title_wrapper);
                goto next_frame;
            }
            break;
        }

        gfx_swap();
        gfx_vsync();
        modplay_tick();

next_frame:
        gs.frame++;

        /* Update bridge every 150 frames (~3 sec at 50fps) */
        if (bridge_ok && (gs.frame % 150) == 0) {
            update_bridge_vars();
            ab_push_var("score");
            ab_push_var("lives");
            ab_push_var("level");
            ab_push_var("gold");
            ab_push_var("game_state");
            ab_push_var("player_gx");
            ab_push_var("player_gy");
            ab_heartbeat();
        }

        /* Poll bridge for incoming messages */
        if (bridge_ok) {
            ab_poll();
            if (!ab_is_connected()) {
                bridge_ok = 0;
            }
        }
    }

    AB_I("Lode Runner exiting");

    modplay_stop();
    sound_cleanup();
    if (win) CloseWindow(win);
    gfx_cleanup();
    if (bridge_ok) ab_cleanup();
    CloseLibrary((struct Library *)GfxBase);
    CloseLibrary((struct Library *)IntuitionBase);

    return 0;
}
