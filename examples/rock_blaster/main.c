/*
 * ROCK BLASTER for Amiga
 *
 * Classic rock_blaster-style game with:
 * - Vector graphics (line-drawn ship, rocks, particles)
 * - Physics-based movement with thrust/drag
 * - Screen wrapping
 * - 3 rock sizes (large -> medium -> small)
 * - Particle explosions
 * - MOD music via ptplayer
 * - Sound effects (shoot, explode, thrust, death)
 * - Joystick + keyboard input
 * - AmigaBridge integration for debug/tuning
 *
 * Controls:
 *   Joystick port 2 or keyboard arrows + space/alt
 *   ESC to quit
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
#include "ptplayer.h"

/* Libraries */
struct IntuitionBase *IntuitionBase = NULL;
struct GfxBase       *GfxBase = NULL;

/* Screen & double buffering */
static struct Screen *screen = NULL;
static struct Window *window = NULL;
static struct ScreenBuffer *sbuf[2] = { NULL, NULL };
static struct RastPort rp_buf[2];    /* one RastPort per buffer */
static WORD cur_buf = 0;             /* which buffer we're drawing to */
static struct MsgPort *db_port[2] = { NULL, NULL };
static BOOL safe_to_write[2] = { TRUE, TRUE };

/* Custom chip base for ptplayer */
#define CUSTOM_BASE ((void *)0xdff000)

/* MOD data loaded into chip RAM */
static UBYTE *mod_data = NULL;
static ULONG  mod_size = 0;

/* Sound effect samples in chip RAM */
#define SFX_SHOOT_LEN    128
#define SFX_EXPLODE_LEN  512
#define SFX_EXPLODE_SM_LEN 256
#define SFX_THRUST_LEN   64
#define SFX_DIE_LEN      1024

static BYTE *sfx_shoot_data = NULL;
static BYTE *sfx_explode_data = NULL;
static BYTE *sfx_explode_sm_data = NULL;
static BYTE *sfx_die_data = NULL;

/* SFX structures for ptplayer */
static SfxStructure sfx_shoot_sfx;
static SfxStructure sfx_explode_sfx;
static SfxStructure sfx_explode_sm_sfx;
static SfxStructure sfx_die_sfx;

/* Game state */
static GameState gs;

/* Amiga palette: 16 colors for 4 bitplanes */
static UWORD palette[16] = {
    0x001,  /*  0: dark blue (background) */
    0xFFF,  /*  1: white (ship, text) */
    0x08F,  /*  2: blue */
    0x0F8,  /*  3: cyan */
    0x0F0,  /*  4: green */
    0x888,  /*  5: dim grey (small rocks) */
    0xAAA,  /*  6: medium grey (medium rocks) */
    0xCCC,  /*  7: light grey (large rocks) */
    0xF00,  /*  8: red */
    0xF80,  /*  9: orange (flame, explosions) */
    0xFF0,  /* 10: yellow */
    0xF0F,  /* 11: magenta */
    0x80F,  /* 12: purple */
    0x444,  /* 13: dark grey */
    0xFA0,  /* 14: bright orange */
    0xFE0,  /* 15: bright yellow (explosion) */
};

/* --- Sound effect generation --- */

static ULONG sfx_rng_state = 98765;
static WORD sfx_rng(void)
{
    sfx_rng_state = sfx_rng_state * 1103515245UL + 12345UL;
    return (WORD)((sfx_rng_state >> 16) & 0x7FFF);
}

static void build_sfx(void)
{
    WORD i;

    /* Shoot: short sharp blip, descending pitch */
    sfx_shoot_data = (BYTE *)AllocMem(SFX_SHOOT_LEN, MEMF_CHIP | MEMF_CLEAR);
    if (sfx_shoot_data) {
        for (i = 0; i < SFX_SHOOT_LEN; i++) {
            WORD t = (i * 127) / SFX_SHOOT_LEN;
            WORD env = 127 - t;
            sfx_shoot_data[i] = (BYTE)((((i * 20) & 0xFF) > 128 ? 64 : -64) * env / 127);
        }
    }

    /* Explode large: white noise with decay */
    sfx_explode_data = (BYTE *)AllocMem(SFX_EXPLODE_LEN, MEMF_CHIP | MEMF_CLEAR);
    if (sfx_explode_data) {
        for (i = 0; i < SFX_EXPLODE_LEN; i++) {
            WORD env = 127 - (i * 127) / SFX_EXPLODE_LEN;
            sfx_explode_data[i] = (BYTE)((sfx_rng() % 256 - 128) * env / 127);
        }
    }

    /* Explode small: shorter noise burst */
    sfx_explode_sm_data = (BYTE *)AllocMem(SFX_EXPLODE_SM_LEN, MEMF_CHIP | MEMF_CLEAR);
    if (sfx_explode_sm_data) {
        for (i = 0; i < SFX_EXPLODE_SM_LEN; i++) {
            WORD env = 127 - (i * 127) / SFX_EXPLODE_SM_LEN;
            sfx_explode_sm_data[i] = (BYTE)((sfx_rng() % 256 - 128) * env / 127);
        }
    }

    /* Die: descending tone + noise */
    sfx_die_data = (BYTE *)AllocMem(SFX_DIE_LEN, MEMF_CHIP | MEMF_CLEAR);
    if (sfx_die_data) {
        for (i = 0; i < SFX_DIE_LEN; i++) {
            WORD env = 127 - (i * 127) / SFX_DIE_LEN;
            WORD freq = 8 + (i * 40) / SFX_DIE_LEN;
            WORD tone = ((i * freq) & 0xFF) > 128 ? 60 : -60;
            WORD noise = (sfx_rng() % 80) - 40;
            sfx_die_data[i] = (BYTE)(((tone + noise) * env) / 127);
        }
    }

    /* Setup SFX structures */
    sfx_shoot_sfx.sfx_ptr = sfx_shoot_data;
    sfx_shoot_sfx.sfx_len = SFX_SHOOT_LEN / 2;
    sfx_shoot_sfx.sfx_per = 200;
    sfx_shoot_sfx.sfx_vol = 50;
    sfx_shoot_sfx.sfx_cha = -1;
    sfx_shoot_sfx.sfx_pri = 30;

    sfx_explode_sfx.sfx_ptr = sfx_explode_data;
    sfx_explode_sfx.sfx_len = SFX_EXPLODE_LEN / 2;
    sfx_explode_sfx.sfx_per = 300;
    sfx_explode_sfx.sfx_vol = 64;
    sfx_explode_sfx.sfx_cha = -1;
    sfx_explode_sfx.sfx_pri = 50;

    sfx_explode_sm_sfx.sfx_ptr = sfx_explode_sm_data;
    sfx_explode_sm_sfx.sfx_len = SFX_EXPLODE_SM_LEN / 2;
    sfx_explode_sm_sfx.sfx_per = 250;
    sfx_explode_sm_sfx.sfx_vol = 48;
    sfx_explode_sm_sfx.sfx_cha = -1;
    sfx_explode_sm_sfx.sfx_pri = 40;

    sfx_die_sfx.sfx_ptr = sfx_die_data;
    sfx_die_sfx.sfx_len = SFX_DIE_LEN / 2;
    sfx_die_sfx.sfx_per = 350;
    sfx_die_sfx.sfx_vol = 64;
    sfx_die_sfx.sfx_cha = -1;
    sfx_die_sfx.sfx_pri = 60;
}

/* SFX callback functions (called from game.c) */
void sfx_shoot(void)
{
    if (sfx_shoot_data)
        mt_playfx(CUSTOM_BASE, &sfx_shoot_sfx);
}

void sfx_explode_large(void)
{
    if (sfx_explode_data)
        mt_playfx(CUSTOM_BASE, &sfx_explode_sfx);
}

void sfx_explode_small(void)
{
    if (sfx_explode_sm_data)
        mt_playfx(CUSTOM_BASE, &sfx_explode_sm_sfx);
}

void sfx_thrust_tick(void) { /* not used as continuous SFX */ }

void sfx_die(void)
{
    if (sfx_die_data)
        mt_playfx(CUSTOM_BASE, &sfx_die_sfx);
}

/* --- MOD loading --- */

static UBYTE *load_file_to_chip(const char *path, ULONG *out_size)
{
    BPTR fh;
    UBYTE *buf = NULL;
    LONG len;
    fh = Open((CONST_STRPTR)path, MODE_OLDFILE);
    if (!fh) return NULL;

    /* Get file size via Seek */
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

/* --- Screen setup with double buffering --- */

static WORD setup_display(void)
{
    WORD i;

    screen = OpenScreenTags(NULL,
        SA_Width,     SCREEN_W,
        SA_Height,    SCREEN_H,
        SA_Depth,     4,
        SA_DisplayID, LORES_KEY,
        SA_Title,     (ULONG)"Chris's Mega Astro Blaster",
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

    /* Allocate two screen buffers for double buffering */
    sbuf[0] = AllocScreenBuffer(screen, NULL, SB_SCREEN_BITMAP);
    sbuf[1] = AllocScreenBuffer(screen, NULL, 0);
    if (!sbuf[0] || !sbuf[1]) return 0;

    /* Create message ports for safe buffer switching */
    db_port[0] = CreateMsgPort();
    db_port[1] = CreateMsgPort();
    if (!db_port[0] || !db_port[1]) return 0;

    sbuf[0]->sb_DBufInfo->dbi_SafeMessage.mn_ReplyPort = db_port[0];
    sbuf[1]->sb_DBufInfo->dbi_SafeMessage.mn_ReplyPort = db_port[1];

    /* Init RastPorts for each buffer */
    InitRastPort(&rp_buf[0]);
    rp_buf[0].BitMap = sbuf[0]->sb_BitMap;
    InitRastPort(&rp_buf[1]);
    rp_buf[1].BitMap = sbuf[1]->sb_BitMap;

    /* Clear both buffers */
    SetRast(&rp_buf[0], 0);
    SetRast(&rp_buf[1], 0);

    /* Open a borderless window for IDCMP input */
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

    cur_buf = 1; /* We'll draw to buffer 1 first, display is showing buffer 0 */
    safe_to_write[0] = TRUE;
    safe_to_write[1] = TRUE;

    return 1;
}

static void swap_buffers(void)
{
    /* Make sure the buffer we're about to display is safe */
    if (!safe_to_write[cur_buf]) {
        while (!GetMsg(db_port[cur_buf]))
            WaitPort(db_port[cur_buf]);
        safe_to_write[cur_buf] = TRUE;
    }

    /* Wait for blitter to finish drawing */
    WaitBlit();

    /* Swap: show the buffer we just drew to */
    ChangeScreenBuffer(screen, sbuf[cur_buf]);
    safe_to_write[cur_buf] = FALSE;

    /* Switch to the other buffer for next frame's drawing */
    cur_buf ^= 1;

    /* Make sure the other buffer (which we'll draw to next) is safe */
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

    /* Wait for any pending buffer swaps */
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
    game_init(&gs);
    strncpy(buf, "Game reset", bufsz);
    return 0;
}

static int hook_next_level(const char *args, char *buf, int bufsz)
{
    WORD i;
    for (i = 0; i < MAX_ROCKS; i++)
        gs.rocks[i].active = 0;
    gs.rock_count = 0;
    gs.level++;
    game_spawn_rocks(&gs, (WORD)(g_tune.start_rocks +
                     (gs.level - 1) * g_tune.rocks_per_level));
    sprintf(buf, "Level %ld", (long)gs.level);
    return 0;
}

static int hook_add_life(const char *args, char *buf, int bufsz)
{
    gs.lives++;
    sprintf(buf, "Lives: %ld", (long)gs.lives);
    return 0;
}

/* --- Main --- */

int main(void)
{
    WORD running = 1;
    WORD music_playing = 0;

    /* Open libraries */
    IntuitionBase = (struct IntuitionBase *)OpenLibrary("intuition.library", 39);
    GfxBase = (struct GfxBase *)OpenLibrary("graphics.library", 39);
    if (!IntuitionBase || !GfxBase) {
        if (IntuitionBase) CloseLibrary((struct Library *)IntuitionBase);
        if (GfxBase) CloseLibrary((struct Library *)GfxBase);
        return 20;
    }

    /* Init bridge */
    ab_init("rock_blaster");
    AB_I("Rock Blaster starting up");

    /* Register tunables */
    ab_register_var("rock_speed",      AB_TYPE_I32, &g_tune.rock_speed);
    ab_register_var("ship_thrust",     AB_TYPE_I32, &g_tune.ship_thrust);
    ab_register_var("bullet_speed",    AB_TYPE_I32, &g_tune.bullet_speed);
    ab_register_var("start_lives",     AB_TYPE_I32, &g_tune.start_lives);
    ab_register_var("start_rocks",     AB_TYPE_I32, &g_tune.start_rocks);
    ab_register_var("rocks_per_level", AB_TYPE_I32, &g_tune.rocks_per_level);
    ab_register_var("score",           AB_TYPE_I32, &gs.score);
    ab_register_var("lives",           AB_TYPE_I32, &gs.lives);
    ab_register_var("level",           AB_TYPE_I32, &gs.level);

    /* Register hooks */
    ab_register_hook("reset",      "Reset game to initial state", hook_reset);
    ab_register_hook("next_level", "Skip to next level",          hook_next_level);
    ab_register_hook("add_life",   "Add one extra life",          hook_add_life);

    /* Init trig tables */
    game_init_tables();

    /* Open screen with double buffering */
    if (!setup_display()) {
        AB_E("Failed to setup display");
        cleanup_display();
        ab_cleanup();
        CloseLibrary((struct Library *)GfxBase);
        CloseLibrary((struct Library *)IntuitionBase);
        return 20;
    }

    /* Build sound effects */
    build_sfx();

    /* Load MOD music */
    mod_data = load_file_to_chip("DH2:Dev/axelf.mod", &mod_size);
    if (mod_data) {
        AB_I("Loaded axelf.mod (%ld bytes)", (long)mod_size);
        /* Install CIA timer for music playback */
        mt_install_cia(CUSTOM_BASE, NULL, 1); /* PAL */
        mt_init(CUSTOM_BASE, mod_data, NULL, 0);
        mt_MusicChannels = 2; /* Reserve 2 channels for music, 2 for SFX */
        mt_Enable = 1;
        music_playing = 1;
    } else {
        AB_W("Could not load axelf.mod - no music");
    }

    /* Init game */
    game_init(&gs);
    gs.state = STATE_TITLE;
    input_reset();

    AB_I("Entering main loop");

    /* Main loop */
    while (running) {
        struct IntuiMessage *msg;
        UWORD inp;

        /* Process window messages */
        while ((msg = (struct IntuiMessage *)GetMsg(window->UserPort))) {
            ULONG cl = msg->Class;
            UWORD code = msg->Code;
            ReplyMsg((struct Message *)msg);

            if (cl == IDCMP_CLOSEWINDOW) {
                running = 0;
            }
            else if (cl == IDCMP_RAWKEY) {
                if (code & 0x80) {
                    /* Key up */
                    input_key_up(code & 0x7F);
                } else {
                    input_key_down(code);
                }
            }
        }

        /* Check Ctrl-C */
        if (SetSignal(0L, SIGBREAKF_CTRL_C) & SIGBREAKF_CTRL_C) {
            running = 0;
        }

        /* Read input */
        inp = input_read();

        if (inp & INPUT_ESC) {
            running = 0;
        }

        /* Get the back buffer's RastPort */
        {
            struct RastPort *rp = &rp_buf[cur_buf];

            /* State machine */
            if (gs.state == STATE_TITLE) {
                draw_clear(rp);
                draw_title(rp);
                if (inp & INPUT_FIRE) {
                    game_init(&gs);
                }
            }
            else {
                /* Update game */
                game_update(&gs, (inp & INPUT_LEFT) ? 1 : 0,
                                 (inp & INPUT_RIGHT) ? 1 : 0,
                                 (inp & INPUT_UP) ? 1 : 0,
                                 (inp & INPUT_FIRE) ? 1 : 0);

                /* Draw everything to back buffer */
                draw_clear(rp);
                draw_rocks(rp, gs.rocks);
                draw_bullets(rp, gs.bullets);
                draw_particles(rp, gs.particles);
                draw_ship(rp, &gs.ship, gs.frame);
                draw_hud(rp, &gs);

                if (gs.state == STATE_GAMEOVER) {
                    draw_gameover(rp, gs.score);
                }
            }
        }

        /* Poll bridge */
        ab_poll();

        /* Swap buffers and sync to VBlank */
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
    if (sfx_shoot_data)      FreeMem(sfx_shoot_data, SFX_SHOOT_LEN);
    if (sfx_explode_data)    FreeMem(sfx_explode_data, SFX_EXPLODE_LEN);
    if (sfx_explode_sm_data) FreeMem(sfx_explode_sm_data, SFX_EXPLODE_SM_LEN);
    if (sfx_die_data)        FreeMem(sfx_die_data, SFX_DIE_LEN);

    input_reset();

    cleanup_display();

    ab_cleanup();
    CloseLibrary((struct Library *)GfxBase);
    CloseLibrary((struct Library *)IntuitionBase);

    return 0;
}
