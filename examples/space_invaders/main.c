#include <exec/types.h>
#include <exec/memory.h>
#include <intuition/intuition.h>
#include <intuition/screens.h>
#include <graphics/gfx.h>
#include <graphics/rastport.h>
#include <graphics/view.h>
#include <proto/exec.h>
#include <proto/intuition.h>
#include <proto/graphics.h>
#include <string.h>
#include <stdlib.h>

#include <bridge_client.h>

#include "game.h"
#include "draw.h"
#include "input.h"
#include "sound.h"

/* Libraries */
struct IntuitionBase *IntuitionBase = NULL;
struct GfxBase *GfxBase = NULL;

/* Screen and double buffering */
static struct Screen *screen = NULL;
static struct Window *win = NULL;
static struct ScreenBuffer *sbuf[2] = {NULL, NULL};
static struct MsgPort *safe_port = NULL;
static BOOL safe_pending = FALSE;
static struct RastPort rp_buf[2];
static WORD cur_buf = 0;

/* Game state */
static GameState gs;
static InputState input;

/* Color palette - LoadRGB32 format: 32-bit per gun */
static const ULONG palette[16 * 3] = {
    0x00000000, 0x00000000, 0x00000000,  /* 0: black */
    0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF,  /* 1: white */
    0x00000000, 0xDDDDDDDD, 0x00000000,  /* 2: green */
    0x00000000, 0x88888888, 0x00000000,  /* 3: dark green */
    0xEEEEEEEE, 0x22222222, 0x22222222,  /* 4: red */
    0xFFFFFFFF, 0xAAAAAAAA, 0x00000000,  /* 5: orange */
    0x00000000, 0xEEEEEEEE, 0xEEEEEEEE,  /* 6: cyan */
    0xEEEEEEEE, 0x22222222, 0xEEEEEEEE,  /* 7: magenta */
    0xFFFFFFFF, 0xFFFFFFFF, 0x00000000,  /* 8: yellow */
    0x44444444, 0x44444444, 0xFFFFFFFF,  /* 9: blue */
    0x88888888, 0x88888888, 0xFFFFFFFF,  /* 10: light blue */
    0xFFFFFFFF, 0x66666666, 0x44444444,  /* 11: light red */
    0x44444444, 0xFFFFFFFF, 0x44444444,  /* 12: bright green */
    0x88888888, 0x88888888, 0x88888888,  /* 13: gray */
    0xFFFFFFFF, 0x88888888, 0x00000000,  /* 14: dark orange */
    0xAAAAAAAA, 0xAAAAAAAA, 0xAAAAAAAA,  /* 15: light gray */
};

/* Bridge hooks */
static int hook_reset(const char *args, char *buf, int bufsz)
{
    game_init(&gs);
    return 0;
}

static int hook_status(const char *args, char *buf, int bufsz)
{
    char tmp[16];
    char *p = buf;

    strcpy(p, "state=");
    p += 6;
    switch (gs.state) {
        case STATE_TITLE: strcpy(p, "title"); break;
        case STATE_PLAYING: strcpy(p, "playing"); break;
        case STATE_DYING: strcpy(p, "dying"); break;
        case STATE_GAMEOVER: strcpy(p, "gameover"); break;
        case STATE_WAVE_CLEAR: strcpy(p, "wave_clear"); break;
        default: strcpy(p, "?"); break;
    }
    p += strlen(p);

    strcpy(p, " score=");
    p += 7;
    {
        LONG v = gs.score;
        int i = 0;
        if (v == 0) { tmp[i++] = '0'; }
        else { while (v > 0) { tmp[i++] = '0' + (char)(v % 10); v /= 10; } }
        while (i > 0) *p++ = tmp[--i];
    }

    strcpy(p, " wave=");
    p += 6;
    {
        LONG v = (LONG)gs.wave;
        int i = 0;
        if (v == 0) { tmp[i++] = '0'; }
        else { while (v > 0) { tmp[i++] = '0' + (char)(v % 10); v /= 10; } }
        while (i > 0) *p++ = tmp[--i];
    }

    strcpy(p, " lives=");
    p += 7;
    *p++ = '0' + (char)gs.lives;
    *p = '\0';

    return 0;
}

static void set_palette(struct Screen *scr)
{
    ULONG table[2 + 16 * 3 + 1];
    int i;

    table[0] = (16L << 16) | 0;
    for (i = 0; i < 16 * 3; i++) {
        table[1 + i] = palette[i];
    }
    table[1 + 16 * 3] = 0;

    LoadRGB32(&scr->ViewPort, table);
}

static struct Screen *open_screen(void)
{
    struct TagItem tags[] = {
        {SA_Width, SCREEN_W},
        {SA_Height, SCREEN_H},
        {SA_Depth, SCREEN_DEPTH},
        {SA_DisplayID, PAL_MONITOR_ID | LORES_KEY},
        {SA_Type, CUSTOMSCREEN},
        {SA_Quiet, TRUE},
        {SA_ShowTitle, FALSE},
        {SA_Behind, FALSE},
        {TAG_DONE, 0}
    };

    return OpenScreenTagList(NULL, tags);
}

/* Wait for safe to write to back buffer */
static void wait_safe(void)
{
    if (safe_pending) {
        while (!GetMsg(safe_port))
            WaitPort(safe_port);
        safe_pending = FALSE;
    }
}

/* Swap buffers safely */
static void swap_buffers(void)
{
    WaitBlit();
    wait_safe();

    while (!ChangeScreenBuffer(screen, sbuf[cur_buf]))
        WaitTOF();

    safe_pending = TRUE;
    cur_buf ^= 1;
}

int main(int argc, char *argv[])
{
    BOOL running = TRUE;
    struct IntuiMessage *imsg;
    struct MsgPort *userport;
    int startup_delay = 10;  /* ignore fire for first 10 frames */

    /* Open libraries */
    IntuitionBase = (struct IntuitionBase *)OpenLibrary("intuition.library", 39);
    GfxBase = (struct GfxBase *)OpenLibrary("graphics.library", 39);
    if (!IntuitionBase || !GfxBase) goto cleanup;

    /* Open screen */
    screen = open_screen();
    if (!screen) goto cleanup;

    set_palette(screen);

    /* Safe port for double buffering */
    safe_port = CreateMsgPort();
    if (!safe_port) goto cleanup;

    /* Set up double buffering */
    sbuf[0] = AllocScreenBuffer(screen, NULL, SB_SCREEN_BITMAP);
    sbuf[1] = AllocScreenBuffer(screen, NULL, 0);
    if (!sbuf[0] || !sbuf[1]) goto cleanup;

    sbuf[0]->sb_DBufInfo->dbi_SafeMessage.mn_ReplyPort = safe_port;
    sbuf[1]->sb_DBufInfo->dbi_SafeMessage.mn_ReplyPort = safe_port;

    InitRastPort(&rp_buf[0]);
    rp_buf[0].BitMap = sbuf[0]->sb_BitMap;
    InitRastPort(&rp_buf[1]);
    rp_buf[1].BitMap = sbuf[1]->sb_BitMap;

    /* Create window for IDCMP input */
    {
        struct TagItem wtags[] = {
            {WA_CustomScreen, (ULONG)screen},
            {WA_Left, 0},
            {WA_Top, 0},
            {WA_Width, SCREEN_W},
            {WA_Height, SCREEN_H},
            {WA_IDCMP, IDCMP_RAWKEY},
            {WA_Flags, WFLG_BORDERLESS | WFLG_BACKDROP | WFLG_RMBTRAP | WFLG_ACTIVATE},
            {TAG_DONE, 0}
        };
        win = OpenWindowTagList(NULL, wtags);
        if (!win) goto cleanup;
        userport = win->UserPort;
    }

    /* Init sound */
    sound_init();

    /* Init game */
    game_init(&gs);
    input_init();

    /* Init bridge */
    ab_init("space_invaders");
    AB_I("Space Invaders started");

    ab_register_var("score",  AB_TYPE_I32, &gs.score);
    ab_register_var("lives",  AB_TYPE_I32, &gs.lives);
    ab_register_var("wave",   AB_TYPE_I32, &gs.wave);
    ab_register_var("hiscore", AB_TYPE_I32, &gs.hiscore);
    ab_register_var("state",  AB_TYPE_I32, &gs.state);
    ab_register_var("aliens_alive", AB_TYPE_I32, &gs.swarm.alive_count);

    ab_register_hook("reset",  "Reset game to title", hook_reset);
    ab_register_hook("status", "Get game status",     hook_status);

    /* Seed RNG from beam position */
    {
        volatile UWORD *vhpos = (volatile UWORD *)0xDFF006;
        game_srand((ULONG)*vhpos * 7919);
    }

    /* Main loop */
    while (running) {
        struct RastPort *rp = &rp_buf[cur_buf];

        /* Process IDCMP messages */
        while ((imsg = (struct IntuiMessage *)GetMsg(userport))) {
            if (imsg->Class == IDCMP_RAWKEY) {
                input_read(&input, imsg);
            }
            ReplyMsg((struct Message *)imsg);
        }

        /* Read mouse hardware */
        input_read_mouse(&input);

        /* Suppress fire during startup to avoid accidental trigger */
        if (startup_delay > 0) {
            startup_delay--;
            input.fire_pressed = FALSE;
        }

        if (input.quit) { running = FALSE; break; }

        /* Update game */
        game_update(&gs, &input);

        /* Handle sound events */
        if (gs.ev_shoot)      sound_play_shoot();
        if (gs.ev_alien_hit)  sound_play_alien_explode();
        if (gs.ev_player_hit) sound_play_player_explode();
        if (gs.ev_ufo_hit)    sound_play_alien_explode();
        if (gs.ev_march)      sound_play_march(gs.march_note);

        /* UFO sound */
        sound_play_ufo(gs.ufo.active);

        /* Draw */
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

        /* Sound update (timers, melody) */
        sound_update();

        /* Bridge poll */
        ab_poll();

        /* VSync and safe buffer swap */
        WaitTOF();
        swap_buffers();
    }

cleanup:
    AB_I("Space Invaders shutting down");
    ab_cleanup();
    sound_cleanup();

    /* Drain any pending safe message before freeing buffers */
    if (safe_pending && safe_port) {
        while (!GetMsg(safe_port))
            WaitPort(safe_port);
        safe_pending = FALSE;
    }

    if (win) CloseWindow(win);
    if (sbuf[1]) FreeScreenBuffer(screen, sbuf[1]);
    if (sbuf[0]) FreeScreenBuffer(screen, sbuf[0]);
    if (safe_port) DeleteMsgPort(safe_port);
    if (screen) CloseScreen(screen);
    if (GfxBase) CloseLibrary((struct Library *)GfxBase);
    if (IntuitionBase) CloseLibrary((struct Library *)IntuitionBase);

    return 0;
}
