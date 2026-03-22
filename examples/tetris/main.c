/*
 * StakAttack - A colorful block-stacking game for the Amiga
 *
 * Uses ptplayer for ProTracker MOD music playback,
 * double-buffered 320x256 screen with 16 colors.
 */

#include <exec/types.h>
#include <exec/memory.h>
#include <intuition/intuition.h>
#include <intuition/screens.h>
#include <graphics/gfx.h>
#include <graphics/gfxbase.h>
#include <graphics/rastport.h>
#include <graphics/view.h>
#include <dos/dos.h>
#include <proto/exec.h>
#include <proto/intuition.h>
#include <proto/graphics.h>
#include <proto/dos.h>
#include <hardware/custom.h>

#include <string.h>
#include <stdio.h>

#include "bridge_client.h"
#include "ptplayer.h"
#include "game.h"
#include "draw.h"
#include "input.h"

/* Custom chip base for ptplayer (must be a pointer for asm register params) */
#define CUSTOM_BASE ((void *)0xDFF000)

/* Libraries */
struct IntuitionBase *IntuitionBase = NULL;
struct GfxBase *GfxBase = NULL;

/* Display */
static struct Screen *screen = NULL;
static struct Window *window = NULL;
static struct ScreenBuffer *sbuf[2] = {NULL, NULL};
static struct MsgPort *sbport[2] = {NULL, NULL};
static struct RastPort rport[2];
static int cur_buf = 0;
static BOOL safe_to_write[2] = {TRUE, TRUE};

/* Audio */
static UBYTE *mod_data = NULL;
static ULONG mod_size = 0;
static BOOL music_playing = FALSE;

/* SFX samples */
static BYTE *sfx_drop_data = NULL;
static BYTE *sfx_clear_data = NULL;
static BYTE *sfx_lock_data = NULL;
static BYTE *sfx_move_data = NULL;

#define SFX_DROP_LEN  256
#define SFX_CLEAR_LEN 512
#define SFX_LOCK_LEN  128
#define SFX_MOVE_LEN  64

static SfxStructure sfx_drop_sfx;
static SfxStructure sfx_clear_sfx;
static SfxStructure sfx_lock_sfx;
static SfxStructure sfx_move_sfx;

/* Game state */
static GameState gs;

/* ---- Sound Effects ---- */

static ULONG sfx_rng_state = 0x12345678;

static ULONG sfx_rng(void)
{
    sfx_rng_state = sfx_rng_state * 1103515245 + 12345;
    return (sfx_rng_state >> 16) & 0x7FFF;
}

static void build_sfx(void)
{
    int i;

    /* Drop: deep thud */
    sfx_drop_data = (BYTE *)AllocMem(SFX_DROP_LEN, MEMF_CHIP | MEMF_CLEAR);
    if (sfx_drop_data) {
        for (i = 0; i < SFX_DROP_LEN; i++) {
            int env = 127 - (i * 127 / SFX_DROP_LEN);
            int wave = (i % 16 < 8) ? 80 : -80;
            sfx_drop_data[i] = (BYTE)((wave * env) / 127);
        }
        sfx_drop_sfx.sfx_ptr = (APTR)sfx_drop_data;
        sfx_drop_sfx.sfx_len = SFX_DROP_LEN / 2;
        sfx_drop_sfx.sfx_per = 400;
        sfx_drop_sfx.sfx_vol = 64;
        sfx_drop_sfx.sfx_cha = -1;
        sfx_drop_sfx.sfx_pri = 10;
    }

    /* Clear: rising sparkle */
    sfx_clear_data = (BYTE *)AllocMem(SFX_CLEAR_LEN, MEMF_CHIP | MEMF_CLEAR);
    if (sfx_clear_data) {
        for (i = 0; i < SFX_CLEAR_LEN; i++) {
            int env = 127 - (i * 100 / SFX_CLEAR_LEN);
            int freq = 4 + (i * 12 / SFX_CLEAR_LEN);
            int wave = (i % freq < freq / 2) ? 70 : -70;
            int noise = (int)(sfx_rng() & 0x1F) - 16;
            sfx_clear_data[i] = (BYTE)(((wave + noise) * env) / 127);
        }
        sfx_clear_sfx.sfx_ptr = (APTR)sfx_clear_data;
        sfx_clear_sfx.sfx_len = SFX_CLEAR_LEN / 2;
        sfx_clear_sfx.sfx_per = 200;
        sfx_clear_sfx.sfx_vol = 55;
        sfx_clear_sfx.sfx_cha = -1;
        sfx_clear_sfx.sfx_pri = 20;
    }

    /* Lock: short click */
    sfx_lock_data = (BYTE *)AllocMem(SFX_LOCK_LEN, MEMF_CHIP | MEMF_CLEAR);
    if (sfx_lock_data) {
        for (i = 0; i < SFX_LOCK_LEN; i++) {
            int env = (i < 8) ? 127 : 127 - ((i - 8) * 127 / (SFX_LOCK_LEN - 8));
            int wave = (i % 6 < 3) ? 60 : -60;
            sfx_lock_data[i] = (BYTE)((wave * env) / 127);
        }
        sfx_lock_sfx.sfx_ptr = (APTR)sfx_lock_data;
        sfx_lock_sfx.sfx_len = SFX_LOCK_LEN / 2;
        sfx_lock_sfx.sfx_per = 300;
        sfx_lock_sfx.sfx_vol = 45;
        sfx_lock_sfx.sfx_cha = -1;
        sfx_lock_sfx.sfx_pri = 5;
    }

    /* Move: tiny tick */
    sfx_move_data = (BYTE *)AllocMem(SFX_MOVE_LEN, MEMF_CHIP | MEMF_CLEAR);
    if (sfx_move_data) {
        for (i = 0; i < SFX_MOVE_LEN; i++) {
            int env = (i < 4) ? 127 : 127 - ((i - 4) * 127 / (SFX_MOVE_LEN - 4));
            sfx_move_data[i] = (BYTE)((((i % 4 < 2) ? 40 : -40) * env) / 127);
        }
        sfx_move_sfx.sfx_ptr = (APTR)sfx_move_data;
        sfx_move_sfx.sfx_len = SFX_MOVE_LEN / 2;
        sfx_move_sfx.sfx_per = 200;
        sfx_move_sfx.sfx_vol = 30;
        sfx_move_sfx.sfx_cha = -1;
        sfx_move_sfx.sfx_pri = 2;
    }
}

static void free_sfx(void)
{
    if (sfx_drop_data) FreeMem(sfx_drop_data, SFX_DROP_LEN);
    if (sfx_clear_data) FreeMem(sfx_clear_data, SFX_CLEAR_LEN);
    if (sfx_lock_data) FreeMem(sfx_lock_data, SFX_LOCK_LEN);
    if (sfx_move_data) FreeMem(sfx_move_data, SFX_MOVE_LEN);
}

/* ---- MOD file loading ---- */

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

/* ---- Display Setup ---- */

static BOOL setup_display(void)
{
    int i;

    screen = OpenScreenTags(NULL,
        SA_Width,     320,
        SA_Height,    256,
        SA_Depth,     4,
        SA_DisplayID, 0x00000000,  /* LORES_KEY */
        SA_Title,     (ULONG)"StakAttack",
        SA_ShowTitle, FALSE,
        SA_Quiet,     TRUE,
        SA_Type,      CUSTOMSCREEN,
        TAG_DONE);
    if (!screen) return FALSE;

    /* Set palette */
    for (i = 0; i < NUM_COLORS; i++) {
        SetRGB4(&screen->ViewPort,
                i,
                (palette[i] >> 8) & 0xF,
                (palette[i] >> 4) & 0xF,
                palette[i] & 0xF);
    }

    /* Allocate double-buffer screen buffers (same order as asteroids) */
    sbuf[0] = AllocScreenBuffer(screen, NULL, SB_SCREEN_BITMAP);
    sbuf[1] = AllocScreenBuffer(screen, NULL, 0);
    if (!sbuf[0] || !sbuf[1]) return FALSE;

    sbport[0] = CreateMsgPort();
    sbport[1] = CreateMsgPort();
    if (!sbport[0] || !sbport[1]) return FALSE;

    sbuf[0]->sb_DBufInfo->dbi_SafeMessage.mn_ReplyPort = sbport[0];
    sbuf[1]->sb_DBufInfo->dbi_SafeMessage.mn_ReplyPort = sbport[1];

    /* Init RastPorts for each buffer */
    InitRastPort(&rport[0]);
    rport[0].BitMap = sbuf[0]->sb_BitMap;
    InitRastPort(&rport[1]);
    rport[1].BitMap = sbuf[1]->sb_BitMap;

    /* Clear both buffers */
    SetRast(&rport[0], 0);
    SetRast(&rport[1], 0);

    /* Open borderless window for input (same pattern as asteroids) */
    window = OpenWindowTags(NULL,
        WA_Left,         0,
        WA_Top,          0,
        WA_Width,        320,
        WA_Height,       256,
        WA_CustomScreen, (ULONG)screen,
        WA_Borderless,   TRUE,
        WA_Backdrop,     TRUE,
        WA_Activate,     TRUE,
        WA_RMBTrap,      TRUE,
        WA_IDCMP,        IDCMP_RAWKEY | IDCMP_CLOSEWINDOW,
        TAG_DONE);
    if (!window) return FALSE;

    /* Ensure our screen and window have focus */
    ScreenToFront(screen);
    ActivateWindow(window);

    cur_buf = 1;
    safe_to_write[0] = TRUE;
    safe_to_write[1] = TRUE;

    return TRUE;
}

static void swap_buffers(void)
{
    /* Make sure the buffer we're about to display is safe */
    if (!safe_to_write[cur_buf]) {
        while (!GetMsg(sbport[cur_buf]))
            WaitPort(sbport[cur_buf]);
        safe_to_write[cur_buf] = TRUE;
    }

    WaitBlit();

    /* Swap: show the buffer we just drew to */
    ChangeScreenBuffer(screen, sbuf[cur_buf]);
    safe_to_write[cur_buf] = FALSE;

    /* Switch to the other buffer for next frame's drawing */
    cur_buf ^= 1;

    /* Make sure the other buffer (which we'll draw to next) is safe */
    if (!safe_to_write[cur_buf]) {
        while (!GetMsg(sbport[cur_buf]))
            WaitPort(sbport[cur_buf]);
        safe_to_write[cur_buf] = TRUE;
    }
}

static void close_display(void)
{
    if (window) { CloseWindow(window); window = NULL; }
    if (sbuf[1]) { FreeScreenBuffer(screen, sbuf[1]); sbuf[1] = NULL; }
    if (sbuf[0]) { FreeScreenBuffer(screen, sbuf[0]); sbuf[0] = NULL; }
    if (sbport[1]) { DeleteMsgPort(sbport[1]); sbport[1] = NULL; }
    if (sbport[0]) { DeleteMsgPort(sbport[0]); sbport[0] = NULL; }
    if (screen) { CloseScreen(screen); screen = NULL; }
}

/* ---- Bridge hooks ---- */

static int hook_reset(const char *args, char *response, int maxLen)
{
    game_start(&gs);
    strncpy(response, "game reset", maxLen);
    return 0;
}

/* ---- Main ---- */

int main(void)
{
    BOOL running = TRUE;
    struct IntuiMessage *imsg;
    UWORD input;
    int game_input;

    IntuitionBase = (struct IntuitionBase *)OpenLibrary("intuition.library", 39);
    GfxBase = (struct GfxBase *)OpenLibrary("graphics.library", 39);
    if (!IntuitionBase || !GfxBase) goto cleanup;

    /* Bridge init */
    ab_init("stakattack");
    ab_register_var("score", AB_TYPE_U32, &gs.score);
    ab_register_var("level", AB_TYPE_I32, &gs.level);
    ab_register_var("lines", AB_TYPE_I32, &gs.lines);
    ab_register_hook("reset", "Reset game", hook_reset);

    AB_I("StakAttack starting");

    /* Install input handler (reads keyboard regardless of window focus) */
    input_init();
    AB_I("Input handler installed");

    /* Build sound effects */
    build_sfx();

    /* Load MOD music */
    mod_data = load_file_to_chip("DH2:Dev/stakattack.mod", &mod_size);
    if (mod_data) {
        AB_I("Loaded stakattack.mod (%ld bytes)", (long)mod_size);
        mt_install_cia(CUSTOM_BASE, NULL, 1); /* PAL */
        mt_init(CUSTOM_BASE, mod_data, NULL, 0);
        mt_MusicChannels = 2;
        mt_mastervol(CUSTOM_BASE, 40);
        mt_Enable = 1;
        music_playing = TRUE;
    } else {
        AB_W("Could not load stakattack.mod");
    }

    /* Setup display */
    if (!setup_display()) {
        AB_E("Display setup failed");
        goto cleanup;
    }

    /* Init game */
    game_init(&gs);

    AB_I("Entering main loop");

    /* ---- Main Loop ---- */
    while (running) {
        /* Process window messages */
        while ((imsg = (struct IntuiMessage *)GetMsg(window->UserPort))) {
            if (imsg->Class == IDCMP_RAWKEY) {
                input_handle_key(imsg->Code, imsg->Qualifier);
            } else if (imsg->Class == IDCMP_CLOSEWINDOW) {
                running = FALSE;
            }
            ReplyMsg((struct Message *)imsg);
        }

        /* Check for Ctrl-C */
        if (SetSignal(0, 0) & SIGBREAKF_CTRL_C) {
            running = FALSE;
        }

        /* Read input */
        input = input_read();
        if (input & INPUT_ESC) running = FALSE;

        {
            /* Edge detection for buttons that need press-not-hold */
            UWORD edges = input_edge(input);

            /* Map input to game input */
            game_input = 0;
            if (input & INPUT_LEFT)   game_input |= GINPUT_LEFT;
            if (input & INPUT_RIGHT)  game_input |= GINPUT_RIGHT;
            if (input & INPUT_DOWN)   game_input |= GINPUT_DOWN;

            /* Rotate on edge only (up arrow, W) */
            if (edges & INPUT_UP)     game_input |= GINPUT_ROTATE;

            /* Space and fire: hard drop during gameplay, start otherwise */
            if (edges & INPUT_FIRE) {
                if (gs.state == STATE_PLAYING)
                    game_input |= GINPUT_DROP;
                else
                    game_input |= GINPUT_START;
            }

            /* Start/pause on edge only (enter, alt, joystick fire, P) */
            if (edges & (INPUT_START | INPUT_PAUSE))
                game_input |= GINPUT_START;
        }

        /* Update game */
        game_update(&gs, game_input);

        /* Play SFX based on game events */
        if (gs.just_locked && sfx_lock_data)
            mt_playfx(CUSTOM_BASE, &sfx_lock_sfx);
        if (gs.just_cleared && sfx_clear_data)
            mt_playfx(CUSTOM_BASE, &sfx_clear_sfx);
        if (gs.just_dropped && sfx_drop_data)
            mt_playfx(CUSTOM_BASE, &sfx_drop_sfx);

        /* Draw */
        {
            struct RastPort *rp = &rport[cur_buf];

            draw_clear(rp);

            switch (gs.state) {
            case STATE_TITLE:
                draw_title(rp);
                break;

            case STATE_PLAYING:
            case STATE_PAUSED:
                draw_field(rp, &gs);
                draw_ghost_piece(rp, &gs);
                draw_current_piece(rp, &gs);
                draw_next_piece(rp, &gs);
                draw_hud(rp, &gs);
                if (gs.clear_timer > 0)
                    draw_line_clear_flash(rp, &gs);
                if (gs.state == STATE_PAUSED)
                    draw_paused(rp);
                break;

            case STATE_GAMEOVER:
                draw_field(rp, &gs);
                draw_hud(rp, &gs);
                draw_gameover(rp, &gs);
                break;
            }
        }

        ab_poll();
        WaitTOF();
        swap_buffers();
    }

    /* ---- Cleanup ---- */
cleanup:
    if (music_playing) {
        mt_end(CUSTOM_BASE);
        mt_remove_cia(CUSTOM_BASE);
    }

    close_display();
    input_cleanup();
    free_sfx();

    if (mod_data) FreeMem(mod_data, mod_size);

    ab_cleanup();

    if (GfxBase) CloseLibrary((struct Library *)GfxBase);
    if (IntuitionBase) CloseLibrary((struct Library *)IntuitionBase);

    return 0;
}
