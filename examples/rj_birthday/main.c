/*
 * RJ'S 70TH BIRTHDAY BASH
 *
 * A tribute to RJ Mical - creator of Intuition, Amiga, Atari Lynx, 3DO
 *
 * Arcade party management game:
 *   Navigate 6 themed rooms in RJ's house
 *   Keep guests happy, catch gifts, serve brownies & sushi
 *   Dodge scooters, demo classic hardware, blow out candles!
 *
 * Controls: Joystick port 2 / Arrow keys + Space
 * ESC to quit
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

/* Custom chip base for ptplayer */
#define CUSTOM_BASE ((void *)0xdff000)

/* MOD data */
static UBYTE *mod_data = NULL;
static ULONG  mod_size = 0;
static UBYTE *bday_mod_data = NULL;
static ULONG  bday_mod_size = 0;
static WORD   music_playing = 0;
static WORD   bday_playing = 0;

/* Game state */
static GameState gs;
static InputState inp;

/* Palette: 16 carefully chosen colors for all rooms */
static UWORD palette[NUM_COLORS] = {
    0x113,  /*  0: COL_BG       - dark blue-black */
    0xFFF,  /*  1: COL_WHITE    - white */
    0x830,  /*  2: COL_BROWN    - brown (wood) */
    0x520,  /*  3: COL_DKBROWN  - dark brown */
    0xF00,  /*  4: COL_RED      - red */
    0x0A0,  /*  5: COL_GREEN    - green */
    0x23C,  /*  6: COL_BLUE     - blue */
    0x8CF,  /*  7: COL_LTBLUE   - light blue */
    0xFE0,  /*  8: COL_YELLOW   - yellow */
    0xF80,  /*  9: COL_ORANGE   - orange */
    0xF5A,  /* 10: COL_PINK     - pink/magenta */
    0x6E6,  /* 11: COL_LTGREEN  - light green */
    0xDA8,  /* 12: COL_TAN      - tan/beige */
    0x888,  /* 13: COL_GREY     - grey */
    0x800,  /* 14: COL_DKRED    - dark red (carpet) */
    0xFF8,  /* 15: COL_BTYELLOW - bright yellow */
};

/* --- File loading --- */

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
        SA_Depth,     DEPTH,
        SA_DisplayID, LORES_KEY,
        SA_Title,     (ULONG)"RJ's 70th Birthday Bash",
        SA_ShowTitle, FALSE,
        SA_Quiet,     TRUE,
        SA_Type,      CUSTOMSCREEN,
        TAG_DONE);

    if (!screen) return 0;

    /* Set palette */
    {
        struct ViewPort *vp = &screen->ViewPort;
        for (i = 0; i < NUM_COLORS; i++) {
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

    /* Borderless window for input */
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
        WA_IDCMP,        IDCMP_RAWKEY | IDCMP_CLOSEWINDOW,
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
    game_init(&gs);
    strncpy(buf, "Game reset", bufsz);
    return 0;
}

static int hook_add_life(const char *args, char *buf, int bufsz)
{
    gs.lives++;
    sprintf(buf, "Lives: %ld", (long)gs.lives);
    return 0;
}

static int hook_set_room(const char *args, char *buf, int bufsz)
{
    WORD room = 0;
    if (args && args[0] >= '0' && args[0] <= '5')
        room = args[0] - '0';
    gs.player.x = room * ROOM_W + 160;
    gs.current_room = room;
    sprintf(buf, "Room: %ld", (long)room);
    return 0;
}

/* --- Main --- */

int main(void)
{
    WORD running = 1;

    /* Open libraries */
    IntuitionBase = (struct IntuitionBase *)OpenLibrary("intuition.library", 39);
    GfxBase = (struct GfxBase *)OpenLibrary("graphics.library", 39);
    if (!IntuitionBase || !GfxBase) {
        if (IntuitionBase) CloseLibrary((struct Library *)IntuitionBase);
        if (GfxBase) CloseLibrary((struct Library *)GfxBase);
        return 20;
    }

    /* Init bridge */
    ab_init("rj_birthday");
    AB_I("RJ's 70th Birthday Bash starting!");

    /* Register debug variables */
    ab_register_var("score",      AB_TYPE_I32, &gs.score);
    ab_register_var("lives",      AB_TYPE_I32, &gs.lives);
    ab_register_var("room",       AB_TYPE_I32, &gs.current_room);
    ab_register_var("happiness",  AB_TYPE_I32, &gs.happiness);
    ab_register_var("wave",       AB_TYPE_I32, &gs.wave);
    ab_register_var("clock",      AB_TYPE_I32, &gs.party_clock);

    /* Register hooks */
    ab_register_hook("reset",    "Reset game",        hook_reset);
    ab_register_hook("add_life", "Add extra life",    hook_add_life);
    ab_register_hook("set_room", "Jump to room 0-5",  hook_set_room);

    /* Init tables */
    game_init_tables();

    /* Load guest names */
    game_load_names(&gs, "PROGDIR:guests.txt");
    AB_I("Loaded %ld guest names", (long)gs.name_count);

    /* Load high scores */
    game_load_hiscores(&gs, "PROGDIR:hiscores.dat");
    AB_I("Loaded %ld high scores", (long)gs.hi_count);

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
    sinistar_load_voices();
    AB_I("Sinistar voices loaded");

    /* Load MOD music */
    mod_data = load_file_to_chip("PROGDIR:party.mod", &mod_size);
    bday_mod_data = load_file_to_chip("PROGDIR:birthday.mod", &bday_mod_size);

    if (mod_data) {
        AB_I("Loaded party.mod (%ld bytes)", (long)mod_size);
        mt_install_cia(CUSTOM_BASE, NULL, 1);  /* PAL */
        mt_init(CUSTOM_BASE, mod_data, NULL, 0);
        mt_MusicChannels = 2;  /* 2 music, 2 SFX */
        mt_Enable = 1;
        music_playing = 1;
    } else {
        AB_W("No party.mod found - no music");
        /* Still install CIA for SFX */
        mt_install_cia(CUSTOM_BASE, NULL, 1);
    }
    if (bday_mod_data) {
        AB_I("Loaded birthday.mod (%ld bytes)", (long)bday_mod_size);
    } else {
        AB_W("No birthday.mod found");
    }

    /* Init game */
    gs.state = GS_TITLE;
    gs.title_blink = 0;
    input_init();
    memset(&inp, 0, sizeof(inp));

    AB_I("Entering main loop");

    /* Clear any pending Ctrl-C and drain stale input */
    SetSignal(0L, SIGBREAKF_CTRL_C);
    Delay(10);  /* Wait 200ms for input to settle */
    {
        struct IntuiMessage *stale;
        while ((stale = (struct IntuiMessage *)GetMsg(window->UserPort)))
            ReplyMsg((struct Message *)stale);
    }

    /* Main loop */
    while (running) {
        /* Read input */
        input_read(&inp, window);

        /* Q from title screen = quit */
        if (gs.state == GS_TITLE && inp.last_char == 'q') {
            running = 0;
        }
        /* ESC during gameplay = back to title via credits */
        if ((inp.bits & INP_ESC) && gs.state == GS_PLAYING) {
            gs.state = GS_CREDITS;
            gs.credits_scroll = 0;
        }

        /* Update game */
        game_update(&gs, &inp);

        /* Check if Sinistar voice clip finished playing */
        sinistar_check_done();

        /* Switch to birthday music on win/gameover/credits */
        if ((gs.state == GS_WIN || gs.state == GS_GAMEOVER ||
             gs.state == GS_CREDITS) && !bday_playing && bday_mod_data) {
            mt_end(CUSTOM_BASE);
            mt_init(CUSTOM_BASE, bday_mod_data, NULL, 0);
            mt_MusicChannels = 2;
            mt_Enable = 1;
            bday_playing = 1;
            music_playing = 0;
        }
        /* Switch back to party music on title/playing */
        if ((gs.state == GS_TITLE || gs.state == GS_PLAYING)
            && !music_playing && mod_data) {
            mt_end(CUSTOM_BASE);
            mt_init(CUSTOM_BASE, mod_data, NULL, 0);
            mt_MusicChannels = 2;
            mt_Enable = 1;
            music_playing = 1;
            bday_playing = 0;
        }

        /* Draw to back buffer */
        {
            struct RastPort *rp = &rp_buf[cur_buf];

            switch (gs.state) {
                case GS_TITLE:
                    draw_title(rp, &gs);
                    break;
                case GS_PLAYING:
                    draw_clear(rp);
                    rooms_draw_bg(rp, &gs);
                    rooms_draw_details(rp, &gs);
                    draw_items(rp, &gs);
                    draw_guests(rp, &gs);
                    draw_cops(rp, &gs);
                    draw_player(rp, &gs);
                    draw_puffs(rp, &gs);
                    draw_hud(rp, &gs);
                    draw_message(rp, &gs);
                    break;
                case GS_GAMEOVER:
                    draw_gameover(rp, &gs);
                    break;
                case GS_WIN:
                    draw_win(rp, &gs);
                    break;
                case GS_CREDITS:
                    draw_credits(rp, &gs);
                    break;
                case GS_HISCORE:
                    draw_hiscore(rp, &gs);
                    break;
                case GS_ENTER_NAME:
                case GS_ADD_GUEST:
                    draw_enter_name(rp, &gs);
                    break;
                case GS_HELP:
                    draw_help(rp, &gs);
                    break;
                case GS_GUEST_EDIT:
                    draw_guest_edit(rp, &gs);
                    break;
            }
        }

        /* Poll bridge */
        ab_poll();

        /* VSync + swap */
        WaitTOF();
        swap_buffers();
    }

    AB_I("Shutting down (bits=0x%lx)", (long)inp.bits);

    /* Cleanup */
    if (music_playing || bday_playing) {
        mt_end(CUSTOM_BASE);
    }
    mt_remove_cia(CUSTOM_BASE);

    if (mod_data) FreeMem(mod_data, mod_size);
    if (bday_mod_data) FreeMem(bday_mod_data, bday_mod_size);

    sound_cleanup();
    sinistar_cleanup_voices();
    cleanup_display();
    ab_cleanup();
    CloseLibrary((struct Library *)GfxBase);
    CloseLibrary((struct Library *)IntuitionBase);

    return 0;
}
