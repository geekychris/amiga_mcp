/*
 * DEFENDER for Amiga
 *
 * Classic Defender arcade clone with:
 * - Wraparound scrolling world (6 screens wide)
 * - Scanner/minimap showing full world
 * - 6 enemy types: landers, mutants, baiters, bombers, pods, swarmers
 * - Human rescue mechanic
 * - Smart bombs and hyperspace
 * - Multi-layer parallax scrolling
 * - MOD music via ptplayer + procedural SFX
 * - High score table saved to disk
 * - Keyboard + joystick input
 * - AmigaBridge integration for debug/testing
 *
 * Controls:
 *   Left/Right or Joystick  Face direction + thrust
 *   Up/Down or Joy Up/Down  Vertical movement
 *   Space/Alt or Joy Fire   Shoot laser
 *   S/Z                     Smart bomb
 *   H/X                     Hyperspace
 *   Return                  Start game / confirm
 *   ESC                     Quit
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

#include "bridge_client.h"
#include "game.h"
#include "draw.h"
#include "input.h"
#include "sound.h"
#include "score.h"

/* Libraries */
struct IntuitionBase *IntuitionBase = NULL;
struct GfxBase       *GfxBase = NULL;

/* Screen & double buffering */
static struct Screen *screen = NULL;
static struct Window *window = NULL;
static struct ScreenBuffer *sbuf[2] = { NULL, NULL };
static struct RastPort rp_buf[2];
static WORD cur_buf = 0;
static struct MsgPort *safe_port = NULL;
static BOOL safe_pending = FALSE;

/* Game state */
static GameState gs;
static InputData input_data;
static ScoreTable score_table;

/* High score entry */
static char entry_name[HISCORE_NAMELEN];
static WORD entry_cursor = 0;
static WORD entry_rank = -1;

/* Startup delay */
static WORD startup_delay = 20;

/* Debug: last input value for bridge monitoring */
static LONG debug_input = 0;

/* Palette: 16 colors (4 bitplanes) */
static UWORD palette[16] = {
    0x000,  /*  0: Black (background) */
    0xFFF,  /*  1: White (text, lasers, humans) */
    0x336,  /*  2: Dim star / scanner border */
    0x220,  /*  3: Dark terrain / scanner bg */
    0x460,  /*  4: Terrain green */
    0x5A0,  /*  5: Terrain highlight */
    0xEE0,  /*  6: Ship body yellow / explosion yellow */
    0xF80,  /*  7: Thrust / swarmer / explosion orange */
    0x0F0,  /*  8: Lander green */
    0xF0F,  /*  9: Mutant magenta / pod */
    0xF00,  /* 10: Bomber red / explosion red / mine */
    0x0CF,  /* 11: HUD cyan / baiter */
    0x000,  /* 12: unused */
    0x000,  /* 13: unused */
    0x000,  /* 14: unused */
    0x000,  /* 15: unused */
};

/* ---- Display setup ---- */

static WORD setup_display(void)
{
    WORD i;

    screen = OpenScreenTags(NULL,
        SA_Width,     SCREEN_W,
        SA_Height,    SCREEN_H,
        SA_Depth,     SCREEN_DEPTH,
        SA_DisplayID, (ULONG)0x00000000,  /* LORES_KEY */
        SA_Title,     (ULONG)"Defender",
        SA_ShowTitle, FALSE,
        SA_Quiet,     TRUE,
        SA_Type,      CUSTOMSCREEN,
        TAG_DONE);

    if (!screen) return 0;

    /* Ensure our screen is frontmost */
    ScreenToFront(screen);

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

    /* Double buffering */
    sbuf[0] = AllocScreenBuffer(screen, NULL, SB_SCREEN_BITMAP);
    sbuf[1] = AllocScreenBuffer(screen, NULL, 0);
    if (!sbuf[0] || !sbuf[1]) return 0;

    safe_port = CreateMsgPort();
    if (!safe_port) return 0;
    sbuf[0]->sb_DBufInfo->dbi_SafeMessage.mn_ReplyPort = safe_port;
    sbuf[1]->sb_DBufInfo->dbi_SafeMessage.mn_ReplyPort = safe_port;

    /* RastPorts */
    InitRastPort(&rp_buf[0]);
    rp_buf[0].BitMap = sbuf[0]->sb_BitMap;
    InitRastPort(&rp_buf[1]);
    rp_buf[1].BitMap = sbuf[1]->sb_BitMap;

    /* Window for IDCMP */
    window = OpenWindowTags(NULL,
        WA_CustomScreen,  (ULONG)screen,
        WA_Left,          0,
        WA_Top,           0,
        WA_Width,         SCREEN_W,
        WA_Height,        SCREEN_H,
        WA_Backdrop,      TRUE,
        WA_Borderless,    TRUE,
        WA_Activate,      TRUE,
        WA_RMBTrap,       TRUE,
        WA_IDCMP,         IDCMP_RAWKEY,
        TAG_DONE);

    if (!window) return 0;

    /* Make absolutely sure we have keyboard focus */
    ScreenToFront(screen);
    ActivateWindow(window);

    return 1;
}

static void wait_safe(void)
{
    if (safe_pending) {
        while (!GetMsg(safe_port))
            WaitPort(safe_port);
        safe_pending = FALSE;
    }
}

static void swap_buffers(void)
{
    WaitBlit();
    wait_safe();
    ChangeScreenBuffer(screen, sbuf[cur_buf]);
    safe_pending = TRUE;
    cur_buf ^= 1;
}

static void cleanup_display(void)
{
    if (window) { CloseWindow(window); window = NULL; }
    wait_safe();
    if (sbuf[1]) { FreeScreenBuffer(screen, sbuf[1]); sbuf[1] = NULL; }
    if (sbuf[0]) { FreeScreenBuffer(screen, sbuf[0]); sbuf[0] = NULL; }
    if (safe_port) { DeleteMsgPort(safe_port); safe_port = NULL; }
    if (screen) { CloseScreen(screen); screen = NULL; }
}

/* ---- Bridge hooks ---- */

static int hook_reset(const char *args, char *buf, int bufsz)
{
    (void)args;
    game_init(&gs, 1);
    gs.state = STATE_TITLE;
    sprintf(buf, "Game reset to title");
    return 0;
}

static int hook_next_level(const char *args, char *buf, int bufsz)
{
    (void)args;
    gs.level++;
    gs.state = STATE_LEVEL_START;
    gs.state_timer = 45;
    game_spawn_wave(&gs);
    sprintf(buf, "Skipped to level %ld", (long)gs.level);
    return 0;
}

static int hook_add_life(const char *args, char *buf, int bufsz)
{
    (void)args;
    gs.lives++;
    sprintf(buf, "Lives: %ld", (long)gs.lives);
    return 0;
}

static int hook_add_bomb(const char *args, char *buf, int bufsz)
{
    (void)args;
    gs.ship.smart_bombs++;
    sprintf(buf, "Bombs: %ld", (long)gs.ship.smart_bombs);
    return 0;
}

static int hook_kill_all(const char *args, char *buf, int bufsz)
{
    WORD i;
    (void)args;
    for (i = 0; i < MAX_ENEMIES; i++)
        gs.enemies[i].active = 0;
    for (i = 0; i < MAX_MINES; i++)
        gs.mines[i].active = 0;
    gs.enemies_alive = 0;
    sprintf(buf, "All enemies killed");
    return 0;
}

static int hook_invuln(const char *args, char *buf, int bufsz)
{
    (void)args;
    gs.ship.invuln_timer = gs.ship.invuln_timer > 0 ? 0 : 30000;
    sprintf(buf, "Invuln: %s", gs.ship.invuln_timer > 0 ? "ON" : "OFF");
    return 0;
}

static int hook_status(const char *args, char *buf, int bufsz)
{
    (void)args;
    sprintf(buf, "L%ld S%ld Lives%ld Bombs%ld E%ld H%ld St%ld F%ld In%ld",
        (long)gs.level, (long)gs.score, (long)gs.lives,
        (long)gs.ship.smart_bombs, (long)gs.enemies_alive,
        (long)gs.humans_alive, (long)gs.state, (long)gs.frame,
        (long)debug_input);
    return 0;
}

/* Debug hook: show which raw key codes are held */
extern volatile UBYTE input_keys[128]; /* from input.c */
static int hook_keys(const char *args, char *buf, int bufsz)
{
    WORD i;
    WORD pos = 0;
    (void)args;
    for (i = 0; i < 128 && pos < bufsz - 10; i++) {
        if (input_keys[i]) {
            sprintf(buf + pos, "%lx ", (long)i);
            pos = strlen(buf);
        }
    }
    if (pos == 0) sprintf(buf, "none");
    return 0;
}

/* ---- Main ---- */

int main(int argc, char *argv[])
{
    WORD running = 1;
    WORD input;
    struct IntuiMessage *imsg;

    (void)argc; (void)argv;

    /* Open libraries */
    IntuitionBase = (struct IntuitionBase *)OpenLibrary((STRPTR)"intuition.library", 39);
    GfxBase = (struct GfxBase *)OpenLibrary((STRPTR)"graphics.library", 39);
    if (!IntuitionBase || !GfxBase) {
        if (IntuitionBase) CloseLibrary((struct Library *)IntuitionBase);
        if (GfxBase) CloseLibrary((struct Library *)GfxBase);
        return 20;
    }

    /* Bridge */
    ab_init("defender");
    AB_I("Defender starting");

    /* Display */
    if (!setup_display()) {
        AB_E("Display setup failed");
        ab_cleanup();
        if (IntuitionBase) CloseLibrary((struct Library *)IntuitionBase);
        if (GfxBase) CloseLibrary((struct Library *)GfxBase);
        return 20;
    }

    /* Sound */
    sound_init();
    if (sound_load_mod("PROGDIR:defender.mod")) {
        AB_I("MOD loaded");
        sound_start_music();
    } else {
        AB_W("No MOD file found - continuing without music");
    }

    /* High scores */
    score_init(&score_table);
    score_load(&score_table);

    /* Input */
    input_init(&input_data);

    /* Game */
    game_init(&gs, 1);
    gs.state = STATE_TITLE;
    gs.hiscore = score_table.count > 0 ? score_table.entries[0].score : 0;

    /* Bridge vars */
    ab_register_var("score",        AB_TYPE_I32, &gs.score);
    ab_register_var("lives",        AB_TYPE_I32, &gs.lives);
    ab_register_var("level",        AB_TYPE_I32, &gs.level);
    ab_register_var("state",        AB_TYPE_I32, &gs.state);
    ab_register_var("smart_bombs",  AB_TYPE_I32, &gs.ship.smart_bombs);
    ab_register_var("humans_alive", AB_TYPE_I32, &gs.humans_alive);
    ab_register_var("enemies_alive",AB_TYPE_I32, &gs.enemies_alive);
    ab_register_var("input",        AB_TYPE_I32, &debug_input);

    /* Bridge hooks */
    ab_register_hook("reset",       "Reset to title",       hook_reset);
    ab_register_hook("next_level",  "Skip to next level",   hook_next_level);
    ab_register_hook("add_life",    "Add one life",         hook_add_life);
    ab_register_hook("add_bomb",    "Add smart bomb",       hook_add_bomb);
    ab_register_hook("kill_all",    "Kill all enemies",     hook_kill_all);
    ab_register_hook("invuln",      "Toggle invulnerability", hook_invuln);
    ab_register_hook("status",      "Get game status",      hook_status);
    ab_register_hook("keys",        "Show held raw key codes", hook_keys);

    AB_I("Entering main loop");

    /* ---- Main loop ---- */
    while (running) {
        struct RastPort *rp = &rp_buf[cur_buf];

        /* Process IDCMP messages */
        while ((imsg = (struct IntuiMessage *)GetMsg(window->UserPort))) {
            if (imsg->Class == IDCMP_RAWKEY) {
                input_key_event(&input_data, imsg->Code);
            }
            ReplyMsg((struct Message *)imsg);
        }

        /* Read input */
        input = input_read(&input_data);
        debug_input = (LONG)input;
        if (input & INPUT_QUIT)
            running = 0;

        /* Startup delay suppresses input */
        if (startup_delay > 0) {
            startup_delay--;
            input = 0;
        }

        /* Bridge */
        ab_poll();

        /* State machine */
        switch (gs.state) {
        case STATE_TITLE:
            draw_title(rp, &gs);
            if (gs.frame > 30 && (input & (INPUT_START | INPUT_FIRE))) {
                game_init(&gs, 1);
                gs.hiscore = score_table.count > 0 ? score_table.entries[0].score : 0;
                gs.state = STATE_LEVEL_START;
                gs.state_timer = 45;
                game_spawn_wave(&gs);
            }
            break;

        case STATE_LEVEL_START:
            game_update(&gs, 0); /* no input during level start */
            draw_all(rp, &gs);
            draw_level_message(rp, gs.level, "GET READY");
            gs.state_timer--;
            if (gs.state_timer <= 0)
                gs.state = STATE_PLAYING;
            break;

        case STATE_PLAYING:
            game_update(&gs, input);
            draw_all(rp, &gs);
            break;

        case STATE_DYING:
            game_update(&gs, 0);
            draw_all(rp, &gs);
            gs.state_timer--;
            if (gs.state_timer <= 0) {
                if (gs.lives > 0) {
                    gs.state = STATE_RESPAWNING;
                    gs.state_timer = 60;
                    gs.ship.alive = 1;
                    gs.ship.wy = TO_FP(PLAY_TOP + (PLAY_BOT - PLAY_TOP) / 2);
                    gs.ship.vx = 0;
                    gs.ship.vy = 0;
                    gs.ship.invuln_timer = 120;
                } else {
                    gs.state = STATE_GAMEOVER;
                    gs.state_timer = 180;
                }
            }
            break;

        case STATE_RESPAWNING:
            game_update(&gs, input);
            draw_all(rp, &gs);
            gs.state_timer--;
            if (gs.state_timer <= 0)
                gs.state = STATE_PLAYING;
            break;

        case STATE_LEVEL_CLEAR:
            draw_all(rp, &gs);
            {
                WORD bonus = gs.humans_alive * SCORE_WAVE_BONUS;
                char buf[40];
                sprintf(buf, "BONUS %ld", (long)bonus);
                draw_level_message(rp, gs.level, buf);
            }
            gs.state_timer--;
            if (gs.state_timer <= 0) {
                gs.score += gs.humans_alive * SCORE_WAVE_BONUS;
                if (gs.score > gs.hiscore)
                    gs.hiscore = gs.score;
                gs.level++;
                gs.state = STATE_LEVEL_START;
                gs.state_timer = 45;
                game_spawn_wave(&gs);
            }
            break;

        case STATE_GAMEOVER:
            draw_all(rp, &gs);
            draw_gameover(rp, &gs);
            gs.state_timer--;
            if (gs.state_timer <= 0 || (input & (INPUT_START | INPUT_FIRE))) {
                entry_rank = score_qualifies(&score_table, gs.score);
                if (entry_rank >= 0) {
                    gs.state = STATE_HISCORE_ENTRY;
                    gs.state_timer = 0;
                    entry_cursor = 0;
                    memset(entry_name, 0, HISCORE_NAMELEN);
                    entry_name[0] = 'A';
                } else {
                    gs.state = STATE_HISCORE_VIEW;
                    gs.state_timer = 300;
                }
            }
            break;

        case STATE_HISCORE_ENTRY:
            draw_all(rp, &gs);
            draw_hiscore_entry(rp, &gs, entry_name, entry_cursor);
            /* Simple letter picker: left/right to change letter, fire to confirm */
            if (input & INPUT_UP) {
                if (entry_name[entry_cursor] < 'Z')
                    entry_name[entry_cursor]++;
                else
                    entry_name[entry_cursor] = 'A';
            }
            if (input & INPUT_DOWN) {
                if (entry_name[entry_cursor] > 'A')
                    entry_name[entry_cursor]--;
                else
                    entry_name[entry_cursor] = 'Z';
            }
            if (input & INPUT_RIGHT) {
                if (entry_cursor < HISCORE_NAMELEN - 2) {
                    entry_cursor++;
                    if (entry_name[entry_cursor] == 0)
                        entry_name[entry_cursor] = 'A';
                }
            }
            if (input & INPUT_LEFT) {
                if (entry_cursor > 0) entry_cursor--;
            }
            if ((input & INPUT_FIRE) && gs.state_timer == 0) {
                gs.state_timer = 10; /* debounce */
            }
            if (gs.state_timer > 0) {
                gs.state_timer--;
                if (gs.state_timer == 0 && !(input & INPUT_FIRE)) {
                    /* Confirm on fire release after debounce */
                    score_insert(&score_table, entry_rank, entry_name, gs.score);
                    score_save(&score_table);
                    gs.state = STATE_HISCORE_VIEW;
                    gs.state_timer = 300;
                }
            }
            break;

        case STATE_HISCORE_VIEW:
            SetRast(rp, COL_BLACK);
            draw_hiscore_table(rp, &score_table);
            gs.state_timer--;
            if (gs.state_timer <= 0 || (input & (INPUT_START | INPUT_FIRE))) {
                gs.state = STATE_TITLE;
                gs.frame = 0;
                startup_delay = 20;
            }
            break;
        }

        /* Sound events */
        sound_update(&gs);

        /* Frame counter */
        gs.frame++;

        /* Sync and swap */
        WaitTOF();
        swap_buffers();
    }

    /* Cleanup */
    AB_I("Shutting down");
    input_cleanup();
    sound_stop_music();
    sound_cleanup();
    cleanup_display();

    ab_cleanup();

    if (GfxBase) CloseLibrary((struct Library *)GfxBase);
    if (IntuitionBase) CloseLibrary((struct Library *)IntuitionBase);

    return 0;
}
