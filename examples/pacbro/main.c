/*
 * PACBRO - Pac-Man clone for Amiga
 *
 * Classic maze gameplay with 4 AI ghosts, power pellets,
 * fruit bonuses, authentic sounds, and alternating 2-player.
 *
 * Controls:
 *   Arrows/WASD or Joystick Port 2: Move
 *   Space/Return/F1 or Fire: Start
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
    0xFF0,  /*  1: Yellow (pac-man) */
    0xF00,  /*  2: Red (Blinky) */
    0xFBD,  /*  3: Pink (Pinky) */
    0x0FF,  /*  4: Cyan (Inky) */
    0xF80,  /*  5: Orange (Clyde) */
    0x22F,  /*  6: Blue (walls/frightened ghost) */
    0xFFF,  /*  7: White (text) */
    0x118,  /*  8: Dark blue (maze outline) */
    0xFDA,  /*  9: Peach (ghost face) */
    0xF9B,  /* 10: Dark pink */
    0xA00,  /* 11: Dark red */
    0x0F0,  /* 12: Green */
    0xA0F,  /* 13: Purple */
    0x888,  /* 14: Gray */
    0xEED,  /* 15: Cream (dots) */
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
        SA_Title,     (ULONG)"PacBro",
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

/* --- Bridge hooks --- */

static int hook_reset(const char *args, char *buf, int bufsz)
{
    game_init(&gs);
    strncpy(buf, "Game reset", bufsz);
    return 0;
}

static int hook_status(const char *args, char *buf, int bufsz)
{
    char num[12];
    int pos = 0;
    long v;
    int d, started;
    long dv;
    const char *p;

    p = "score=";
    while (*p && pos < bufsz - 1) buf[pos++] = *p++;

    v = (long)gs.score;
    started = 0;
    for (dv = 1000000L; dv > 0; dv /= 10) {
        d = (int)(v / dv);
        v -= (long)d * dv;
        if (d || started || dv == 1) {
            if (pos < bufsz - 1) buf[pos++] = (char)('0' + d);
            started = 1;
        }
    }

    p = " lives=";
    while (*p && pos < bufsz - 1) buf[pos++] = *p++;
    if (pos < bufsz - 1) buf[pos++] = (char)('0' + gs.lives);

    p = " level=";
    while (*p && pos < bufsz - 1) buf[pos++] = *p++;
    if (pos < bufsz - 1) buf[pos++] = (char)('0' + gs.level);

    buf[pos] = '\0';
    return 0;
}

/* --- Cleanup --- */

static void cleanup_all(void)
{
    sound_cleanup();
    ab_cleanup();

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

    if (GfxBase) { CloseLibrary((struct Library *)GfxBase); GfxBase = NULL; }
    if (IntuitionBase) { CloseLibrary((struct Library *)IntuitionBase); IntuitionBase = NULL; }
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
    ab_init("pacbro");
    AB_I("PacBro starting up");

    /* Register bridge variables */
    ab_register_var("score", AB_TYPE_I32, &gs.score);
    ab_register_var("lives", AB_TYPE_I32, &gs.lives);
    ab_register_var("level", AB_TYPE_I32, &gs.level);
    ab_register_var("state", AB_TYPE_I32, &gs.state);

    /* Register hooks */
    ab_register_hook("reset", "Reset game", hook_reset);
    ab_register_hook("status", "Get game status", hook_status);

    /* Open display */
    if (!setup_display()) {
        AB_E("Failed to setup display");
        cleanup_all();
        return 20;
    }

    /* Init sound */
    sound_init();

    /* Init game */
    game_init(&gs);
    draw_set_dirty();

    AB_I("Entering main loop");

    /* Main loop */
    while (running) {
        struct IntuiMessage *msg;
        InputState input;
        struct RastPort *rp;
        WORD prev_state = gs.state;
        WORD prev_level = gs.level;

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
        input.dir = DIR_NONE;
        input_read(&input);

        /* Startup delay - suppress all input */
        if (startup_delay > 0) {
            startup_delay--;
            memset(&input, 0, sizeof(input));
            input.dir = DIR_NONE;
            input_reset();
        } else {
            /* ESC to quit */
            if (input.quit) {
                running = 0;
            }
        }

        /* Update game */
        game_update(&gs, &input);

        /* Detect state transitions that require full maze redraw */
        if (gs.level != prev_level ||
            (prev_state == STATE_TITLE && gs.state == STATE_READY) ||
            (prev_state == STATE_GAMEOVER && gs.state == STATE_TITLE)) {
            draw_set_dirty();
        }

        /* Handle sound events */
        if (gs.ev_flags & EV_CHOMP)     sound_chomp();
        if (gs.ev_flags & EV_EAT_GHOST) sound_eat_ghost();
        if (gs.ev_flags & EV_DIE)       sound_die();
        if (gs.ev_flags & EV_FRUIT)     sound_fruit();
        if (gs.ev_flags & EV_EXTRA)     sound_extra_life();
        if (gs.ev_flags & EV_POWER)     sound_power();
        gs.ev_flags = 0;

        /* Siren control */
        if (gs.state == STATE_PLAYING) {
            if (gs.fright_active) {
                sound_siren_off();
            } else {
                sound_set_siren(gs.siren_speed);
            }
        } else {
            sound_siren_off();
        }

        /* Render */
        rp = &rp_buf[cur_buf];

        switch (gs.state) {
        case STATE_TITLE:
            draw_title(rp, &gs);
            break;
        case STATE_GAMEOVER:
            draw_gameover(rp, &gs);
            break;
        default:
            draw_game(rp, &gs);
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
    cleanup_all();

    return 0;
}
