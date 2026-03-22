/*
 * ACE PILOT for Amiga
 *
 * 3D wireframe dogfighting game inspired by the 1980 Atari arcade.
 * First-person view, enemy biplanes and blimps, ground targets.
 * Split-screen two-player mode.
 * Two display modes: classic green vectors or multi-color.
 *
 * Controls:
 *   Joystick/Arrows: Pitch and Yaw
 *   Space/Fire:      Machine gun
 *   Q/E:             Throttle up/down
 *   P:               Toggle display mode
 *   F1:              1 Player
 *   F2:              2 Players
 *   ESC:             Quit
 */

#include <proto/exec.h>
#include <proto/intuition.h>
#include <proto/graphics.h>
#include <proto/dos.h>
#include <intuition/intuition.h>
#include <intuition/screens.h>
#include <graphics/gfxbase.h>
#include <graphics/view.h>
#include <exec/memory.h>
#include <hardware/custom.h>
#include <string.h>
#include <stdio.h>

#include "bridge_client.h"
#include "engine3d.h"
#include "game.h"
#include "render.h"
#include "input.h"
#include "sound.h"
#include "ptplayer.h"

/* Libraries */
struct IntuitionBase *IntuitionBase = NULL;
struct GfxBase       *GfxBase = NULL;

/* Screen & double buffering */
static struct Screen *screen = NULL;
static struct Window *window = NULL;
static struct ScreenBuffer *sbuf[2] = { NULL, NULL };
static struct RastPort rp_buf[2];
static WORD cur_buf = 0;
static struct MsgPort *db_port[2] = { NULL, NULL };
static BOOL safe_to_write[2] = { TRUE, TRUE };

/* MOD data */
static UBYTE *mod_data = NULL;
static ULONG  mod_size = 0;
static WORD   music_playing = 0;

/* Game world */
static GameWorld world;

/* Palette: designed for both classic green and color modes */
static UWORD palette[16] = {
    0x000,  /*  0: black (background) */
    0xFFF,  /*  1: white (text, crosshair, player bullets) */
    0x0F0,  /*  2: bright green (classic near) */
    0x0EE,  /*  3: cyan (blimps) */
    0x0A0,  /*  4: green (classic medium / trees) */
    0x060,  /*  5: dark green (classic far / grid) */
    0xA70,  /*  6: brown/tan (ground buildings) */
    0x888,  /*  7: grey (text, divider) */
    0xF00,  /*  8: red (enemies, enemy bullets) */
    0xF80,  /*  9: orange (explosions) */
    0xFF0,  /* 10: yellow (HUD score) */
    0x800,  /* 11: dark red (far enemies) */
    0x04F,  /* 12: blue */
    0x040,  /* 13: dim green (classic very far / stars) */
    0xFA0,  /* 14: bright orange (close explosions) */
    0xFE0,  /* 15: bright yellow (prompts) */
};

/* --- MOD loading --- */
static UBYTE *load_file_to_chip(const char *path, ULONG *out_size)
{
    BPTR fh;
    UBYTE *buf = NULL;
    LONG len;

    fh = Open((CONST_STRPTR)path, MODE_OLDFILE);
    if (!fh) return NULL;

    Seek(fh, 0, OFFSET_END);
    len = Seek(fh, 0, OFFSET_BEGINNING);
    if (len <= 0) { Close(fh); return NULL; }

    buf = (UBYTE *)AllocMem(len, MEMF_CHIP);
    if (!buf) { Close(fh); return NULL; }

    if (Read(fh, buf, len) != len) {
        FreeMem(buf, len);
        Close(fh);
        return NULL;
    }

    Close(fh);
    *out_size = (ULONG)len;
    return buf;
}

/* --- Display setup --- */
static WORD setup_display(void)
{
    WORD i;

    screen = OpenScreenTags(NULL,
        SA_Width,     SCREEN_W,
        SA_Height,    SCREEN_H,
        SA_Depth,     4,
        SA_DisplayID, LORES_KEY,
        SA_Title,     (ULONG)"Ace Pilot",
        SA_ShowTitle, FALSE,
        SA_Quiet,     TRUE,
        SA_Type,      CUSTOMSCREEN,
        TAG_DONE);

    if (!screen) return 0;

    /* Set palette */
    {
        struct ViewPort *vp = &screen->ViewPort;
        for (i = 0; i < 16; i++) {
            SetRGB4(vp, i,
                (palette[i] >> 8) & 0xF,
                (palette[i] >> 4) & 0xF,
                palette[i] & 0xF);
        }
    }

    sbuf[0] = AllocScreenBuffer(screen, NULL, SB_SCREEN_BITMAP);
    sbuf[1] = AllocScreenBuffer(screen, NULL, 0);
    if (!sbuf[0] || !sbuf[1]) return 0;

    db_port[0] = CreateMsgPort();
    db_port[1] = CreateMsgPort();
    if (!db_port[0] || !db_port[1]) return 0;

    sbuf[0]->sb_DBufInfo->dbi_SafeMessage.mn_ReplyPort = db_port[0];
    sbuf[1]->sb_DBufInfo->dbi_SafeMessage.mn_ReplyPort = db_port[1];

    InitRastPort(&rp_buf[0]);
    rp_buf[0].BitMap = sbuf[0]->sb_BitMap;
    InitRastPort(&rp_buf[1]);
    rp_buf[1].BitMap = sbuf[1]->sb_BitMap;

    SetRast(&rp_buf[0], 0);
    SetRast(&rp_buf[1], 0);

    window = OpenWindowTags(NULL,
        WA_Left,       0,
        WA_Top,        0,
        WA_Width,      SCREEN_W,
        WA_Height,     SCREEN_H,
        WA_CustomScreen, (ULONG)screen,
        WA_Borderless, TRUE,
        WA_Backdrop,   TRUE,
        WA_Activate,   TRUE,
        WA_RMBTrap,    TRUE,
        WA_IDCMP,      IDCMP_RAWKEY | IDCMP_CLOSEWINDOW,
        TAG_DONE);

    if (!window) return 0;

    cur_buf = 1;
    safe_to_write[0] = TRUE;
    safe_to_write[1] = TRUE;

    return 1;
}

static void swap_buffers(void)
{
    if (!safe_to_write[cur_buf]) {
        while (!GetMsg(db_port[cur_buf]))
            WaitPort(db_port[cur_buf]);
        safe_to_write[cur_buf] = TRUE;
    }

    WaitBlit();
    ChangeScreenBuffer(screen, sbuf[cur_buf]);
    safe_to_write[cur_buf] = FALSE;

    cur_buf ^= 1;

    if (!safe_to_write[cur_buf]) {
        while (!GetMsg(db_port[cur_buf]))
            WaitPort(db_port[cur_buf]);
        safe_to_write[cur_buf] = TRUE;
    }
}

static void cleanup_display(void)
{
    WORD i;
    if (window) { CloseWindow(window); window = NULL; }

    if (!safe_to_write[0]) {
        while (!GetMsg(db_port[0])) WaitPort(db_port[0]);
    }
    if (!safe_to_write[1]) {
        while (!GetMsg(db_port[1])) WaitPort(db_port[1]);
    }

    for (i = 0; i < 2; i++) {
        if (sbuf[i]) { FreeScreenBuffer(screen, sbuf[i]); sbuf[i] = NULL; }
        if (db_port[i]) { DeleteMsgPort(db_port[i]); db_port[i] = NULL; }
    }

    if (screen) { CloseScreen(screen); screen = NULL; }
}

/* --- Bridge hooks --- */
static int hook_reset(const char *args, char *buf, int bufsz)
{
    game_init(&world);
    strncpy(buf, "Game reset", bufsz);
    return 0;
}

static int hook_next_wave(const char *args, char *buf, int bufsz)
{
    WORD i;
    for (i = 0; i < MAX_ENEMIES; i++)
        world.enemies[i].active = 0;
    world.enemies_alive = 0;
    world.enemies_to_spawn = 0;
    game_spawn_wave(&world);
    sprintf(buf, "Wave %ld", (long)world.wave);
    return 0;
}

static int hook_toggle_display(const char *args, char *buf, int bufsz)
{
    g_tune.display_mode = 1 - g_tune.display_mode;
    sprintf(buf, "Display: %s",
            g_tune.display_mode == DISPLAY_CLASSIC ? "classic" : "color");
    return 0;
}

static int hook_status(const char *args, char *buf, int bufsz)
{
    sprintf(buf, "W%ld P1:%ld P2:%ld E:%ld",
            (long)world.wave,
            (long)world.players[0].score,
            (long)world.players[1].score,
            (long)world.enemies_alive);
    return 0;
}

static int hook_set_difficulty(const char *args, char *buf, int bufsz)
{
    LONG d;
    if (!args || !args[0]) {
        sprintf(buf, "Difficulty: %ld", (long)g_tune.difficulty);
        return 0;
    }
    d = 0;
    while (*args >= '0' && *args <= '9') {
        d = d * 10 + (*args - '0');
        args++;
    }
    if (d < 1) d = 1;
    if (d > 10) d = 10;
    g_tune.difficulty = d;
    sprintf(buf, "Difficulty set to %ld", (long)d);
    return 0;
}

/* --- Main --- */
int main(void)
{
    WORD running = 1;
    WORD game_state = STATE_TITLE;
    WORD mode_press = 0;
    LONG var_score_p1 = 0;
    LONG var_score_p2 = 0;
    LONG var_lives_p1 = 0;
    LONG var_wave = 0;
    LONG var_enemies = 0;
    LONG var_state = 0;
    WORD push_timer = 0;

    /* Open libraries */
    IntuitionBase = (struct IntuitionBase *)OpenLibrary("intuition.library", 39);
    GfxBase = (struct GfxBase *)OpenLibrary("graphics.library", 39);
    if (!IntuitionBase || !GfxBase) {
        if (IntuitionBase) CloseLibrary((struct Library *)IntuitionBase);
        if (GfxBase) CloseLibrary((struct Library *)GfxBase);
        return 20;
    }

    /* Init bridge */
    ab_init("ace_pilot");
    AB_I("Ace Pilot starting up");

    /* Register tunables */
    ab_register_var("enemy_speed",     AB_TYPE_I32, &g_tune.enemy_speed);
    ab_register_var("enemy_accuracy",  AB_TYPE_I32, &g_tune.enemy_accuracy);
    ab_register_var("enemy_fire_rate", AB_TYPE_I32, &g_tune.enemy_fire_rate);
    ab_register_var("spawn_rate",      AB_TYPE_I32, &g_tune.spawn_rate);
    ab_register_var("difficulty",      AB_TYPE_I32, &g_tune.difficulty);
    ab_register_var("start_lives",     AB_TYPE_I32, &g_tune.start_lives);
    ab_register_var("extra_life_score",AB_TYPE_I32, &g_tune.extra_life_score);
    ab_register_var("player_speed",    AB_TYPE_I32, &g_tune.player_speed);
    ab_register_var("display_mode",    AB_TYPE_I32, &g_tune.display_mode);
    ab_register_var("god_mode",        AB_TYPE_I32, &g_tune.god_mode);
    ab_register_var("invert_pitch",    AB_TYPE_I32, &g_tune.invert_pitch);
    ab_register_var("num_players",     AB_TYPE_I32, &g_tune.num_players);

    /* Monitoring vars */
    ab_register_var("score_p1", AB_TYPE_I32, &var_score_p1);
    ab_register_var("score_p2", AB_TYPE_I32, &var_score_p2);
    ab_register_var("lives_p1", AB_TYPE_I32, &var_lives_p1);
    ab_register_var("wave",     AB_TYPE_I32, &var_wave);
    ab_register_var("enemies",  AB_TYPE_I32, &var_enemies);
    ab_register_var("state",    AB_TYPE_I32, &var_state);

    /* Hooks */
    ab_register_hook("reset",          "Reset game to title", hook_reset);
    ab_register_hook("next_wave",      "Skip to next wave",   hook_next_wave);
    ab_register_hook("toggle_display", "Switch display mode",  hook_toggle_display);
    ab_register_hook("status",         "Get game status",     hook_status);
    ab_register_hook("set_difficulty", "Set difficulty 1-10",  hook_set_difficulty);

    /* Init engine */
    engine3d_init();

    /* Open display */
    if (!setup_display()) {
        AB_E("Failed to setup display");
        cleanup_display();
        ab_cleanup();
        CloseLibrary((struct Library *)GfxBase);
        CloseLibrary((struct Library *)IntuitionBase);
        return 20;
    }

    /* Build sound effects */
    sound_init();

    /* Load MOD music */
    mod_data = load_file_to_chip("DH2:Dev/blue_danube.mod", &mod_size);
    if (mod_data) {
        AB_I("Loaded music (%ld bytes)", (long)mod_size);
        mt_install_cia(CUSTOM_BASE, NULL, 1);  /* PAL */
        mt_init(CUSTOM_BASE, mod_data, NULL, 0);
        mt_MusicChannels = 2;
        mt_Enable = 1;
        music_playing = 1;
    } else {
        AB_W("Could not load blue_danube.mod - no music");
    }

    AB_I("Entering main loop");

    /* Clear any pending CTRL-C from previous instance being stopped */
    SetSignal(0L, SIGBREAKF_CTRL_C);

    /* --- Main loop --- */
    while (running) {
        struct IntuiMessage *msg;
        UWORD inp1, inp2;
        struct RastPort *rp;

        /* Process IDCMP */
        while ((msg = (struct IntuiMessage *)GetMsg(window->UserPort))) {
            ULONG cl = msg->Class;
            UWORD code = msg->Code;
            ReplyMsg((struct Message *)msg);

            if (cl == IDCMP_CLOSEWINDOW) {
                running = 0;
            } else if (cl == IDCMP_RAWKEY) {
                if (code & 0x80) {
                    input_key_up(code & 0x7F);
                } else {
                    input_key_down(code);
                }
            }
        }

        /* Ctrl-C */
        if (SetSignal(0L, SIGBREAKF_CTRL_C) & SIGBREAKF_CTRL_C) {
            running = 0;
        }

        /* Read input */
        inp1 = input_read_p1();
        inp2 = input_read_p2();

        if (inp1 & INPUT_ESC) {
            if (game_state == STATE_TITLE) {
                running = 0;
            } else {
                game_state = STATE_TITLE;
            }
        }

        /* Get back buffer */
        rp = &rp_buf[cur_buf];

        switch (game_state) {
        case STATE_TITLE:
            render_title(rp);

            /* Toggle display mode */
            if ((inp1 & INPUT_MODE) && !mode_press) {
                g_tune.display_mode = 1 - g_tune.display_mode;
                mode_press = 1;
            }
            if (!(inp1 & INPUT_MODE)) mode_press = 0;

            /* Player count selection */
            if (inp1 & INPUT_TWO_P) g_tune.num_players = 2;
            if (inp1 & INPUT_START) g_tune.num_players = 1;

            /* Start game */
            if (inp1 & INPUT_FIRE) {
                game_init(&world);
                game_state = STATE_PLAYING;
                AB_I("Game started, %ld player(s)", (long)g_tune.num_players);
            }
            break;

        case STATE_PLAYING:
        case STATE_DYING:
        case STATE_GAMEOVER:
            /* Update game */
            game_update(&world, inp1, inp2);

            /* Check for game over -> title transition */
            if (world.state == STATE_GAMEOVER && world.state_timer <= 0) {
                if (inp1 & INPUT_FIRE) {
                    game_state = STATE_TITLE;
                    break;
                }
            }

            /* Render */
            if (g_tune.num_players >= 2) {
                /* Split screen */
                Viewport vp_top  = { 0, 0, SCREEN_W, SCREEN_H / 2 - 1 };
                Viewport vp_bot  = { 0, SCREEN_H / 2 + 1, SCREEN_W, SCREEN_H / 2 - 1 };

                render_scene(rp, &world, 0, &vp_top);
                render_hud(rp, &world, 0, &vp_top);

                render_scene(rp, &world, 1, &vp_bot);
                render_hud(rp, &world, 1, &vp_bot);

                render_divider(rp);
            } else {
                /* Single player full screen */
                Viewport vp_full = { 0, 0, SCREEN_W, SCREEN_H };

                render_scene(rp, &world, 0, &vp_full);
                render_hud(rp, &world, 0, &vp_full);
            }

            /* Game over overlay */
            if (world.state == STATE_GAMEOVER) {
                render_gameover(rp, &world);
            }
            break;
        }

        /* Update monitoring vars and push periodically */
        var_score_p1 = world.players[0].score;
        var_score_p2 = world.players[1].score;
        var_lives_p1 = world.players[0].lives;
        var_wave     = world.wave;
        var_enemies  = world.enemies_alive;
        var_state    = game_state;

        push_timer++;
        if (push_timer >= 50) {
            push_timer = 0;
            ab_push_var("score_p1");
            ab_push_var("lives_p1");
            ab_push_var("wave");
            ab_push_var("enemies");
        }

        /* Poll bridge */
        ab_poll();

        /* Sync and swap */
        WaitTOF();
        swap_buffers();
    }

    AB_I("Shutting down");

    /* Cleanup */
    if (music_playing) {
        mt_end(CUSTOM_BASE);
        mt_remove_cia(CUSTOM_BASE);
    }
    if (mod_data) FreeMem(mod_data, mod_size);

    sound_cleanup();
    input_reset();
    cleanup_display();
    ab_cleanup();

    CloseLibrary((struct Library *)GfxBase);
    CloseLibrary((struct Library *)IntuitionBase);

    return 0;
}
