/*
 * MOON PATROL - Amiga arcade game clone
 *
 * Side-scrolling moon buggy action with parallax scrolling,
 * terrain obstacles, UFOs, meteors, and the iconic bouncy bass.
 *
 * Controls:
 *   Left/Right or Joystick  Slow down/Speed up
 *   Up/Space/W or Joy Up    Jump
 *   A or Fire button         Shoot (forward + upward)
 *   ESC                      Quit
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

/* Double buffer synchronization - SAFE PORT pattern */
static struct MsgPort *safe_port = NULL;
static BOOL safe_pending = FALSE;

/* Game state */
static GameState gs;

/* Startup delay to suppress accidental input */
static WORD startup_delay = 30;

/* ---- Palette: 16 colors for Moon Patrol ---- */
static UWORD palette[16] = {
    0x001,  /*  0: Black (sky) */
    0xFFF,  /*  1: White (stars, text) */
    0x214,  /*  2: Dark purple (far mountains) */
    0x426,  /*  3: Purple (near mountains) */
    0x840,  /*  4: Brown (ground) */
    0x520,  /*  5: Dark brown (ground detail) */
    0xFD0,  /*  6: Yellow (buggy body) */
    0xAAA,  /*  7: Gray (buggy wheels) */
    0xF00,  /*  8: Red (explosions) */
    0xF80,  /*  9: Orange (bullets/fire) */
    0x0EF,  /* 10: Cyan (UFOs) */
    0x0F0,  /* 11: Green (checkpoint text) */
    0x44F,  /* 12: Blue (bombs) */
    0xFF0,  /* 13: Bright yellow (score) */
    0x555,  /* 14: Dark gray (rocks) */
    0xF8F,  /* 15: Pink (meteors) */
};

/* ---- Display setup ---- */

static WORD setup_display(void)
{
    WORD i;

    screen = OpenScreenTags(NULL,
        SA_Width,     SCREEN_W,
        SA_Height,    SCREEN_H,
        SA_Depth,     4,
        SA_DisplayID, (ULONG)0x00000000,  /* LORES_KEY */
        SA_Title,     (ULONG)"Moon Patrol",
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

    /* Double buffering with SAFE PORT sync */
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

    /* Borderless window for IDCMP input */
    window = OpenWindowTags(NULL,
        WA_Left,         0,
        WA_Top,          0,
        WA_Width,        SCREEN_W,
        WA_Height,       SCREEN_H,
        WA_CustomScreen, (ULONG)screen,
        WA_Borderless,   TRUE,
        WA_Backdrop,     TRUE,
        WA_Activate,     TRUE,
        WA_RMBTrap,      TRUE,
        WA_IDCMP,        IDCMP_RAWKEY,
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

/* ---- Bridge hooks ---- */

static int hook_reset(const char *args, char *buf, int bufsz)
{
    game_init(&gs);
    gs.state = STATE_PLAYING;
    strncpy(buf, "Game reset", bufsz);
    return 0;
}

/* Simple status string builder avoiding sprintf %d issues */
static void build_status(char *buf, int bufsz)
{
    char num[16];
    int pos = 0;
    int i;
    const char *prefix;

    /* "score=NNNN lives=N cp=X" */
    prefix = "score=";
    for (i = 0; prefix[i] && pos < bufsz - 1; i++)
        buf[pos++] = prefix[i];

    {
        long s = (long)gs.score;
        char *p = num;
        int started = 0;
        long dv;
        for (dv = 1000000L; dv > 0; dv /= 10) {
            int digit = (int)(s / dv);
            s -= (long)digit * dv;
            if (digit || started || dv == 1) {
                *p++ = (char)('0' + digit);
                started = 1;
            }
        }
        *p = '\0';
    }
    for (i = 0; num[i] && pos < bufsz - 1; i++)
        buf[pos++] = num[i];

    prefix = " lives=";
    for (i = 0; prefix[i] && pos < bufsz - 1; i++)
        buf[pos++] = prefix[i];
    buf[pos++] = (char)('0' + gs.lives);

    prefix = " cp=";
    for (i = 0; prefix[i] && pos < bufsz - 1; i++)
        buf[pos++] = prefix[i];
    buf[pos++] = (char)('A' + gs.checkpoint_cur);

    buf[pos] = '\0';
}

static int hook_status_real(const char *args, char *buf, int bufsz)
{
    build_status(buf, bufsz);
    return 0;
}

/* ---- Cleanup ---- */

static void cleanup_all(void)
{
    /* Stop audio DMA first */
    sound_cleanup();

    /* Bridge cleanup */
    ab_cleanup();

    /* Wait for safe buffer */
    if (safe_pending && safe_port) {
        while (!GetMsg(safe_port)) WaitPort(safe_port);
    }

    WaitBlit();

    /* Restore front buffer before closing */
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

/* ---- Main ---- */

int main(void)
{
    WORD running = 1;
    InputState input;

    IntuitionBase = (struct IntuitionBase *)OpenLibrary((CONST_STRPTR)"intuition.library", 39);
    GfxBase = (struct GfxBase *)OpenLibrary((CONST_STRPTR)"graphics.library", 39);
    if (!IntuitionBase || !GfxBase) {
        if (IntuitionBase) CloseLibrary((struct Library *)IntuitionBase);
        if (GfxBase) CloseLibrary((struct Library *)GfxBase);
        return 20;
    }

    /* Init bridge */
    ab_init("moon_patrol");
    AB_I("Moon Patrol starting up");

    /* Register variables */
    ab_register_var("score",      AB_TYPE_I32, &gs.score);
    ab_register_var("lives",      AB_TYPE_I32, &gs.lives);
    ab_register_var("checkpoint", AB_TYPE_I32, &gs.checkpoint_cur);
    ab_register_var("state",      AB_TYPE_I32, &gs.state);

    /* Register hooks */
    ab_register_hook("reset",  "Reset game",       hook_reset);
    ab_register_hook("status", "Get game status",   hook_status_real);

    /* Setup display */
    if (!setup_display()) {
        AB_E("Failed to setup display");
        cleanup_all();
        return 20;
    }

    /* Init sound */
    sound_init();

    /* Init game */
    game_init(&gs);
    gs.state = STATE_TITLE;

    /* Init input */
    input_init(window);
    memset(&input, 0, sizeof(input));

    AB_I("Entering main loop");

    /* ---- Main loop ---- */
    while (running) {
        struct RastPort *rp = &rp_buf[cur_buf];

        /* Read input */
        input_update(&input);

        /* Startup delay - suppress all input */
        if (startup_delay > 0) {
            startup_delay--;
            memset(&input, 0, sizeof(input));
        } else {
            if (input.quit) {
                running = 0;
            }
        }

        /* State machine */
        switch (gs.state) {
            case STATE_TITLE:
                draw_title(rp, gs.frame);
                gs.frame++;
                if (input.fire || input.jump) {
                    game_init(&gs);
                    gs.state = STATE_PLAYING;
                }
                break;

            case STATE_PLAYING:
            case STATE_CHECKPOINT:
                game_update(&gs, &input);

                /* Sound events */
                if (gs.ev_shoot)      sound_shoot();
                if (gs.ev_explode)    sound_explode();
                if (gs.ev_jump)       sound_jump();
                if (gs.ev_checkpoint) sound_checkpoint();
                if (gs.ev_death)      sound_death();

                draw_game(rp, &gs);

                if (gs.state == STATE_CHECKPOINT) {
                    draw_checkpoint(rp, gs.checkpoint_cur);
                }
                break;

            case STATE_DYING:
                game_update(&gs, &input);
                if (gs.ev_explode) sound_explode();
                draw_game(rp, &gs);
                break;

            case STATE_GAMEOVER:
                game_update(&gs, &input);
                draw_game(rp, &gs);
                draw_gameover(rp, gs.score);
                if (input.fire || input.jump) {
                    gs.state = STATE_TITLE;
                }
                break;
        }

        /* Update sound sequencer */
        sound_update();

        /* Poll bridge */
        ab_poll();

        /* Sync and swap */
        WaitTOF();
        swap_buffers();
    }

    AB_I("Shutting down");
    cleanup_all();

    return 0;
}
