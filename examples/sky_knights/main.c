/*
 * SKY KNIGHTS - Arcade clone for Amiga
 *
 * 1-2 player sky_knightsing on flying ostriches.
 * Defeat enemies by being higher during collision.
 * Collect eggs before they hatch!
 *
 * Controls:
 *   P1: Arrow keys / WASD + Space to flap
 *   P2: Joystick port 2 + fire to flap
 *   F1: 1 Player start
 *   F2: 2 Player start
 *   ESC: Quit
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
#include <stdio.h>
#include <string.h>

#include "bridge_client.h"
#include "game.h"
#include "draw.h"
#include "input.h"
#include "sound.h"

/* Libraries */
struct IntuitionBase *IntuitionBase = NULL;
struct GfxBase       *GfxBase = NULL;

/* Screen & double buffering */
static struct Screen *screen = NULL;
static struct Window *window = NULL;
static struct ScreenBuffer *sbuf[2] = { NULL, NULL };
static struct RastPort rp_buf[2];
static WORD cur_buf = 0;

/* Safe port for double buffering */
static struct MsgPort *safe_port = NULL;
static BOOL safe_pending = FALSE;

/* Game state */
static GameState gs;

/* Startup delay to suppress accidental input */
static WORD startup_delay = 30;

/* Color palette: 16 colors */
static UWORD palette[16] = {
    0x000,  /*  0: Black (bg) */
    0xFFF,  /*  1: White (text) */
    0x840,  /*  2: Brown (platforms) */
    0x520,  /*  3: Dark brown (platform shadow) */
    0xFC0,  /*  4: Yellow (P1 ostrich) */
    0x06F,  /*  5: Blue (P1 rider) */
    0x0CF,  /*  6: Cyan (P2 ostrich) */
    0xF22,  /*  7: Red (P2 rider/enemies) */
    0xF80,  /*  8: Orange (Bounder) */
    0xAAA,  /*  9: Gray (Hunter) */
    0x22A,  /* 10: Dark blue (Shadow Lord) */
    0x0E0,  /* 11: Green (eggs) */
    0xFF0,  /* 12: Bright yellow (score) */
    0xCA8,  /* 13: Tan (ground) */
    0xF8F,  /* 14: Pink (egg about to hatch) */
    0xCCC,  /* 15: Light gray */
};

/* --- Screen setup --- */

static WORD setup_display(void)
{
    WORD i;

    screen = OpenScreenTags(NULL,
        SA_Width,     SCREEN_W,
        SA_Height,    SCREEN_H,
        SA_Depth,     4,
        SA_DisplayID, LORES_KEY,
        SA_Title,     (ULONG)"Sky Knights",
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

    /* Double buffering with safe port */
    sbuf[0] = AllocScreenBuffer(screen, NULL, SB_SCREEN_BITMAP);
    sbuf[1] = AllocScreenBuffer(screen, NULL, 0);
    if (!sbuf[0] || !sbuf[1]) return 0;

    safe_port = CreateMsgPort();
    if (!safe_port) return 0;

    sbuf[0]->sb_DBufInfo->dbi_SafeMessage.mn_ReplyPort = safe_port;
    sbuf[1]->sb_DBufInfo->dbi_SafeMessage.mn_ReplyPort = safe_port;

    InitRastPort(&rp_buf[0]);
    rp_buf[0].BitMap = sbuf[0]->sb_BitMap;
    InitRastPort(&rp_buf[1]);
    rp_buf[1].BitMap = sbuf[1]->sb_BitMap;

    SetRast(&rp_buf[0], 0);
    SetRast(&rp_buf[1], 0);

    /* Borderless window for input */
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
        WA_IDCMP,      IDCMP_RAWKEY,
        TAG_DONE);

    if (!window) return 0;

    cur_buf = 1;
    safe_pending = FALSE;

    return 1;
}

static void wait_safe(void)
{
    if (safe_pending) {
        while (!GetMsg(safe_port)) WaitPort(safe_port);
        safe_pending = FALSE;
    }
}

static void swap_buffers(void)
{
    WaitBlit();
    wait_safe();
    while (!ChangeScreenBuffer(screen, sbuf[cur_buf])) WaitTOF();
    safe_pending = TRUE;
    cur_buf ^= 1;
}

static void cleanup_display(void)
{
    /* Drain safe port */
    if (safe_pending && safe_port) {
        while (!GetMsg(safe_port)) WaitPort(safe_port);
    }

    WaitBlit();

    if (screen && sbuf[0]) {
        ChangeScreenBuffer(screen, sbuf[0]);
        WaitTOF();
        WaitTOF();
    }

    if (window) { CloseWindow(window); window = NULL; }

    if (sbuf[1]) { FreeScreenBuffer(screen, sbuf[1]); sbuf[1] = NULL; }
    if (sbuf[0]) { FreeScreenBuffer(screen, sbuf[0]); sbuf[0] = NULL; }
    if (safe_port) { DeleteMsgPort(safe_port); safe_port = NULL; }
    if (screen) { CloseScreen(screen); screen = NULL; }
}

/* --- Bridge hooks --- */

static int hook_reset(const char *args, char *buf, int bufsz)
{
    game_init(&gs);
    strncpy(buf, "Game reset", bufsz);
    return 0;
}

static int hook_status(const char *args, char *buf, int bufsz)
{
    sprintf(buf, "Wave:%ld P1:%ld P2:%ld State:%ld",
            (long)gs.wave, (long)gs.score[0], (long)gs.score[1], (long)gs.state);
    return 0;
}

/* --- Main --- */

int main(void)
{
    WORD running = 1;

    IntuitionBase = (struct IntuitionBase *)OpenLibrary("intuition.library", 39);
    GfxBase = (struct GfxBase *)OpenLibrary("graphics.library", 39);
    if (!IntuitionBase || !GfxBase) {
        if (IntuitionBase) CloseLibrary((struct Library *)IntuitionBase);
        if (GfxBase) CloseLibrary((struct Library *)GfxBase);
        return 20;
    }

    /* Init bridge */
    ab_init("sky_knights");
    AB_I("Sky Knights starting up");

    /* Register bridge variables */
    ab_register_var("score_p1", AB_TYPE_I32, &gs.score[0]);
    ab_register_var("score_p2", AB_TYPE_I32, &gs.score[1]);
    ab_register_var("wave", AB_TYPE_I32, &gs.wave);
    ab_register_var("state", AB_TYPE_I32, &gs.state);

    /* Register hooks */
    ab_register_hook("reset", "Reset game", hook_reset);
    ab_register_hook("status", "Get game status", hook_status);

    /* Open display */
    if (!setup_display()) {
        AB_E("Failed to setup display");
        cleanup_display();
        ab_cleanup();
        CloseLibrary((struct Library *)GfxBase);
        CloseLibrary((struct Library *)IntuitionBase);
        return 20;
    }

    /* Init sound */
    sound_init();

    /* Init game */
    game_init(&gs);

    AB_I("Entering main loop");

    /* Main loop */
    while (running) {
        struct IntuiMessage *msg;
        InputState input;
        struct RastPort *rp;

        /* Process IDCMP */
        while ((msg = (struct IntuiMessage *)GetMsg(window->UserPort))) {
            ULONG cl = msg->Class;
            UWORD code = msg->Code;
            ReplyMsg((struct Message *)msg);

            if (cl == IDCMP_RAWKEY) {
                if (code & 0x80) {
                    input_key_up(code & 0x7F);
                } else {
                    input_key_down(code);
                }
            }
        }

        /* Read input */
        memset(&input, 0, sizeof(input));
        input_read(&input);

        /* Startup delay - suppress all input */
        if (startup_delay > 0) {
            startup_delay--;
            input.p1 = 0;
            input.p2 = 0;
            input.sys = 0;
            /* Clear any stale key state */
            input_reset();
        } else {
            /* ESC to quit */
            if (input.sys & INP_ESC) {
                running = 0;
            }
        }

        /* Update game */
        game_update(&gs, &input);

        /* Handle sound events */
        if (gs.ev_flags & EV_FLAP) sound_flap();
        if (gs.ev_flags & EV_KILL) sound_kill();
        if (gs.ev_flags & EV_EGG)  sound_egg();
        if (gs.ev_flags & EV_DIE)  sound_die();
        if (gs.ev_flags & EV_WAVE) sound_wave();
        gs.ev_flags = 0;

        /* Render */
        rp = &rp_buf[cur_buf];

        switch (gs.state) {
        case STATE_TITLE:
            draw_title(rp, &gs);
            break;
        case STATE_WAVE_INTRO:
            draw_wave_intro(rp, &gs);
            break;
        case STATE_PLAYING:
            draw_playing(rp, &gs);
            break;
        case STATE_GAMEOVER:
            draw_gameover(rp, &gs);
            break;
        }

        /* Update sound */
        sound_update();

        /* Poll bridge */
        ab_poll();

        /* VBlank sync + swap */
        WaitTOF();
        swap_buffers();
    }

    AB_I("Shutting down");

    /* Cleanup */
    sound_cleanup();
    ab_cleanup();

    /* Drain safe port */
    if (safe_pending && safe_port) {
        while (!GetMsg(safe_port)) WaitPort(safe_port);
    }

    WaitBlit();

    if (screen && sbuf[0]) {
        ChangeScreenBuffer(screen, sbuf[0]);
        WaitTOF();
        WaitTOF();
    }

    if (window) CloseWindow(window);
    if (sbuf[1]) FreeScreenBuffer(screen, sbuf[1]);
    if (sbuf[0]) FreeScreenBuffer(screen, sbuf[0]);
    if (safe_port) DeleteMsgPort(safe_port);
    if (screen) CloseScreen(screen);

    CloseLibrary((struct Library *)GfxBase);
    CloseLibrary((struct Library *)IntuitionBase);

    return 0;
}
