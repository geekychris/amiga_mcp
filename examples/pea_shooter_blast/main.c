/*
 * PEA SHOOTER BLAST - A Blaster Master homage for Amiga
 *
 * Side-scrolling tank action with smooth scrolling tile engine,
 * jumping, 8 weapon levels, enemies, bosses, MOD music.
 *
 * Controls:
 *   A/D or Joystick   Move left/right
 *   W or Joy Up       Jump
 *   Space/Alt/Fire    Shoot
 *   ESC               Quit
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

/* Scroll state: the RasInfo offset for smooth scrolling */
static WORD scroll_offset_applied = 0;

/* Custom chip base for ptplayer */
#define CUSTOM_BASE ((void *)0xdff000)

/* MOD data */
static UBYTE *mod_data = NULL;
static ULONG  mod_size = 0;

/* SFX samples */
#define SFX_SHOOT_LEN     128
#define SFX_HIT_LEN       96
#define SFX_EXPLODE_LEN   512
#define SFX_POWERUP_LEN   256
#define SFX_JUMP_LEN      128
#define SFX_PLAYER_HIT_LEN 256

static BYTE *sfx_shoot_data = NULL;
static BYTE *sfx_hit_data = NULL;
static BYTE *sfx_explode_data = NULL;
static BYTE *sfx_powerup_data = NULL;
static BYTE *sfx_jump_data = NULL;
static BYTE *sfx_player_hit_data = NULL;

static SfxStructure sfx_shoot_sfx;
static SfxStructure sfx_hit_sfx;
static SfxStructure sfx_explode_sfx;
static SfxStructure sfx_powerup_sfx;
static SfxStructure sfx_jump_sfx;
static SfxStructure sfx_player_hit_sfx;

/* Game state */
static GameState gs;

/* Color palette: 16 colors, underground/mechanical theme */
static UWORD palette[16] = {
    0x112,  /*  0: very dark blue (BG) */
    0xFFF,  /*  1: white */
    0x0A0,  /*  2: green (tank body) */
    0x0E0,  /*  3: bright green (tank turret, pipes) */
    0x0F0,  /*  4: lime green (powerup health, HUD bar) */
    0x666,  /*  5: grey (rock) */
    0x842,  /*  6: brown (ground/platform) */
    0xAAA,  /*  7: light grey (metal) */
    0xF00,  /*  8: red (enemies, damage) */
    0xF80,  /*  9: orange (brick, enemy alt) */
    0xFF0,  /* 10: yellow (bullets, powerup weapon) */
    0xF0F,  /* 11: magenta (boss) */
    0x531,  /* 12: dark brown (dirt) */
    0x444,  /* 13: dark grey (treads) */
    0xFA0,  /* 14: yellow-orange (ladder) */
    0x8FF,  /* 15: bright cyan (gate) */
};

/* --- SFX generation --- */

static ULONG sfx_rng_state = 98765;
static WORD sfx_rng(void)
{
    sfx_rng_state = sfx_rng_state * 1103515245UL + 12345UL;
    return (WORD)((sfx_rng_state >> 16) & 0x7FFF);
}

static void build_sfx(void)
{
    WORD i;

    /* Shoot: quick blip */
    sfx_shoot_data = (BYTE *)AllocMem(SFX_SHOOT_LEN, MEMF_CHIP | MEMF_CLEAR);
    if (sfx_shoot_data) {
        for (i = 0; i < SFX_SHOOT_LEN; i++) {
            WORD t = (i * 127) / SFX_SHOOT_LEN;
            WORD env = 127 - t;
            sfx_shoot_data[i] = (BYTE)((((i * 25) & 0xFF) > 128 ? 64 : -64) * env / 127);
        }
    }

    /* Hit: short metallic ping */
    sfx_hit_data = (BYTE *)AllocMem(SFX_HIT_LEN, MEMF_CHIP | MEMF_CLEAR);
    if (sfx_hit_data) {
        for (i = 0; i < SFX_HIT_LEN; i++) {
            WORD env = 127 - (i * 127) / SFX_HIT_LEN;
            WORD wave = ((i * 35) & 0xFF) > 128 ? 50 : -50;
            sfx_hit_data[i] = (BYTE)((wave + (sfx_rng() % 30 - 15)) * env / 127);
        }
    }

    /* Explode: noise with decay */
    sfx_explode_data = (BYTE *)AllocMem(SFX_EXPLODE_LEN, MEMF_CHIP | MEMF_CLEAR);
    if (sfx_explode_data) {
        for (i = 0; i < SFX_EXPLODE_LEN; i++) {
            WORD env = 127 - (i * 127) / SFX_EXPLODE_LEN;
            sfx_explode_data[i] = (BYTE)((sfx_rng() % 256 - 128) * env / 127);
        }
    }

    /* Powerup: ascending tone */
    sfx_powerup_data = (BYTE *)AllocMem(SFX_POWERUP_LEN, MEMF_CHIP | MEMF_CLEAR);
    if (sfx_powerup_data) {
        for (i = 0; i < SFX_POWERUP_LEN; i++) {
            WORD freq = 15 + (i * 30) / SFX_POWERUP_LEN;
            WORD env = 100 - (i * 60) / SFX_POWERUP_LEN;
            if (env < 20) env = 20;
            sfx_powerup_data[i] = (BYTE)((((i * freq) & 0xFF) > 128 ? 64 : -64) * env / 127);
        }
    }

    /* Jump: quick rising tone */
    sfx_jump_data = (BYTE *)AllocMem(SFX_JUMP_LEN, MEMF_CHIP | MEMF_CLEAR);
    if (sfx_jump_data) {
        for (i = 0; i < SFX_JUMP_LEN; i++) {
            WORD freq = 30 - (i * 20) / SFX_JUMP_LEN;
            WORD env = 80 - (i * 80) / SFX_JUMP_LEN;
            if (env < 0) env = 0;
            sfx_jump_data[i] = (BYTE)((((i * freq) & 0xFF) > 128 ? 50 : -50) * env / 127);
        }
    }

    /* Player hit: descending buzz */
    sfx_player_hit_data = (BYTE *)AllocMem(SFX_PLAYER_HIT_LEN, MEMF_CHIP | MEMF_CLEAR);
    if (sfx_player_hit_data) {
        for (i = 0; i < SFX_PLAYER_HIT_LEN; i++) {
            WORD freq = 10 + (i * 25) / SFX_PLAYER_HIT_LEN;
            WORD env = 127 - (i * 127) / SFX_PLAYER_HIT_LEN;
            WORD tone = ((i * freq) & 0xFF) > 128 ? 50 : -50;
            WORD noise = (sfx_rng() % 60) - 30;
            sfx_player_hit_data[i] = (BYTE)(((tone + noise) * env) / 127);
        }
    }

    /* Setup structures */
    sfx_shoot_sfx.sfx_ptr = sfx_shoot_data;
    sfx_shoot_sfx.sfx_len = SFX_SHOOT_LEN / 2;
    sfx_shoot_sfx.sfx_per = 180;
    sfx_shoot_sfx.sfx_vol = 50;
    sfx_shoot_sfx.sfx_cha = -1;
    sfx_shoot_sfx.sfx_pri = 30;

    sfx_hit_sfx.sfx_ptr = sfx_hit_data;
    sfx_hit_sfx.sfx_len = SFX_HIT_LEN / 2;
    sfx_hit_sfx.sfx_per = 200;
    sfx_hit_sfx.sfx_vol = 40;
    sfx_hit_sfx.sfx_cha = -1;
    sfx_hit_sfx.sfx_pri = 25;

    sfx_explode_sfx.sfx_ptr = sfx_explode_data;
    sfx_explode_sfx.sfx_len = SFX_EXPLODE_LEN / 2;
    sfx_explode_sfx.sfx_per = 300;
    sfx_explode_sfx.sfx_vol = 64;
    sfx_explode_sfx.sfx_cha = -1;
    sfx_explode_sfx.sfx_pri = 50;

    sfx_powerup_sfx.sfx_ptr = sfx_powerup_data;
    sfx_powerup_sfx.sfx_len = SFX_POWERUP_LEN / 2;
    sfx_powerup_sfx.sfx_per = 250;
    sfx_powerup_sfx.sfx_vol = 50;
    sfx_powerup_sfx.sfx_cha = -1;
    sfx_powerup_sfx.sfx_pri = 40;

    sfx_jump_sfx.sfx_ptr = sfx_jump_data;
    sfx_jump_sfx.sfx_len = SFX_JUMP_LEN / 2;
    sfx_jump_sfx.sfx_per = 200;
    sfx_jump_sfx.sfx_vol = 35;
    sfx_jump_sfx.sfx_cha = -1;
    sfx_jump_sfx.sfx_pri = 20;

    sfx_player_hit_sfx.sfx_ptr = sfx_player_hit_data;
    sfx_player_hit_sfx.sfx_len = SFX_PLAYER_HIT_LEN / 2;
    sfx_player_hit_sfx.sfx_per = 300;
    sfx_player_hit_sfx.sfx_vol = 60;
    sfx_player_hit_sfx.sfx_cha = -1;
    sfx_player_hit_sfx.sfx_pri = 55;
}

/* SFX callbacks */
void sfx_shoot(void)
{
    if (sfx_shoot_data) mt_playfx(CUSTOM_BASE, &sfx_shoot_sfx);
}
void sfx_hit(void)
{
    if (sfx_hit_data) mt_playfx(CUSTOM_BASE, &sfx_hit_sfx);
}
void sfx_explode(void)
{
    if (sfx_explode_data) mt_playfx(CUSTOM_BASE, &sfx_explode_sfx);
}
void sfx_powerup(void)
{
    if (sfx_powerup_data) mt_playfx(CUSTOM_BASE, &sfx_powerup_sfx);
}
void sfx_jump(void)
{
    if (sfx_jump_data) mt_playfx(CUSTOM_BASE, &sfx_jump_sfx);
}
void sfx_player_hit(void)
{
    if (sfx_player_hit_data) mt_playfx(CUSTOM_BASE, &sfx_player_hit_sfx);
}

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

/* --- Screen setup --- */

static WORD setup_display(void)
{
    WORD i;

    /* Use oversized bitmap width (352) for smooth scrolling */
    screen = OpenScreenTags(NULL,
        SA_Width,     BITMAP_W,
        SA_Height,    SCREEN_H,
        SA_Depth,     4,
        SA_DisplayID, LORES_KEY,
        SA_Title,     (ULONG)"Pea Shooter Blast",
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

static void apply_scroll(WORD fine_x)
{
    /* Use RasInfo to set horizontal scroll offset within the oversized bitmap */
    struct ViewPort *vp = &screen->ViewPort;
    vp->RasInfo->RxOffset = fine_x;
    ScrollVPort(vp);
    scroll_offset_applied = fine_x;
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

static int hook_next_level(const char *args, char *buf, int bufsz)
{
    if (gs.level < 2) {
        game_load_level(&gs, (WORD)(gs.level + 1));
        sprintf(buf, "Level %ld", (long)gs.level + 1);
    } else {
        strncpy(buf, "Already at last level", bufsz);
    }
    return 0;
}

static int hook_add_life(const char *args, char *buf, int bufsz)
{
    gs.lives++;
    sprintf(buf, "Lives: %ld", (long)gs.lives);
    return 0;
}

static int hook_heal(const char *args, char *buf, int bufsz)
{
    gs.tank.health = MAX_HEALTH;
    strncpy(buf, "Healed to full", bufsz);
    return 0;
}

static int hook_max_weapon(const char *args, char *buf, int bufsz)
{
    gs.tank.weapon_level = 7;
    strncpy(buf, "Weapon maxed", bufsz);
    return 0;
}

/* Level used by tile drawing (global for draw_tile_column) */
WORD g_current_level = 0;

/* --- Main --- */

int main(void)
{
    WORD running = 1;
    WORD music_playing = 0;

    IntuitionBase = (struct IntuitionBase *)OpenLibrary("intuition.library", 39);
    GfxBase = (struct GfxBase *)OpenLibrary("graphics.library", 39);
    if (!IntuitionBase || !GfxBase) {
        if (IntuitionBase) CloseLibrary((struct Library *)IntuitionBase);
        if (GfxBase) CloseLibrary((struct Library *)GfxBase);
        return 20;
    }

    /* Init bridge */
    ab_init("pea_shooter");
    AB_I("Pea Shooter Blast starting up");

    /* Register tunables */
    ab_register_var("tank_speed",    AB_TYPE_I32, &g_tune.tank_speed);
    ab_register_var("jump_power",    AB_TYPE_I32, &g_tune.jump_power);
    ab_register_var("gravity",       AB_TYPE_I32, &g_tune.gravity);
    ab_register_var("bullet_speed",  AB_TYPE_I32, &g_tune.bullet_speed);
    ab_register_var("start_lives",   AB_TYPE_I32, &g_tune.start_lives);
    ab_register_var("enemy_speed",   AB_TYPE_I32, &g_tune.enemy_speed);
    ab_register_var("score",         AB_TYPE_I32, &gs.score);
    ab_register_var("lives",         AB_TYPE_I32, &gs.lives);
    ab_register_var("level",         AB_TYPE_I32, &gs.level);
    ab_register_var("health",        AB_TYPE_I32, &gs.tank.health);
    ab_register_var("weapon_level",  AB_TYPE_I32, &gs.tank.weapon_level);

    /* Register hooks */
    ab_register_hook("reset",       "Reset game",          hook_reset);
    ab_register_hook("next_level",  "Skip to next level",  hook_next_level);
    ab_register_hook("add_life",    "Add extra life",      hook_add_life);
    ab_register_hook("heal",        "Heal to full HP",     hook_heal);
    ab_register_hook("max_weapon",  "Max weapon level",    hook_max_weapon);

    /* Open display */
    if (!setup_display()) {
        AB_E("Failed to setup display");
        cleanup_display();
        ab_cleanup();
        CloseLibrary((struct Library *)GfxBase);
        CloseLibrary((struct Library *)IntuitionBase);
        return 20;
    }

    /* Build SFX */
    build_sfx();

    /* Load MOD music */
    mod_data = load_file_to_chip("DH2:Dev/music.mod", &mod_size);
    if (mod_data) {
        AB_I("Loaded music.mod (%ld bytes)", (long)mod_size);
        mt_install_cia(CUSTOM_BASE, NULL, 1);
        mt_init(CUSTOM_BASE, mod_data, NULL, 0);
        mt_MusicChannels = 2;
        mt_Enable = 1;
        music_playing = 1;
    } else {
        AB_W("Could not load music.mod - no music");
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

        /* Process IDCMP */
        while ((msg = (struct IntuiMessage *)GetMsg(window->UserPort))) {
            ULONG cl = msg->Class;
            UWORD code = msg->Code;
            ReplyMsg((struct Message *)msg);

            if (cl == IDCMP_CLOSEWINDOW) {
                running = 0;
            }
            else if (cl == IDCMP_RAWKEY) {
                if (code & 0x80) {
                    input_key_up(code & 0x7F);
                } else {
                    input_key_down(code);
                }
            }
        }

        /* Ctrl-C check */
        if (SetSignal(0L, SIGBREAKF_CTRL_C) & SIGBREAKF_CTRL_C) {
            running = 0;
        }

        /* Read input */
        inp = input_read();

        if (inp & INPUT_ESC) {
            running = 0;
        }

        /* Rendering */
        {
            struct RastPort *rp = &rp_buf[cur_buf];

            if (gs.state == STATE_TITLE) {
                draw_title(rp, gs.frame);
                gs.frame++;
                if (inp & INPUT_FIRE) {
                    game_init(&gs);
                }
            }
            else {
                /* Update game */
                game_update(&gs, (inp & INPUT_LEFT) ? 1 : 0,
                                 (inp & INPUT_RIGHT) ? 1 : 0,
                                 (inp & INPUT_JUMP) ? 1 : 0,
                                 (inp & INPUT_FIRE) ? 1 : 0);

                /* Set global level for tile drawing */
                g_current_level = gs.level;

                /* Draw tiles (full redraw each frame for simplicity in Phase 1) */
                draw_all_tiles(rp, &gs.scroll);

                /* Draw entities */
                draw_powerups(rp, gs.powerups, &gs.scroll);
                draw_enemies(rp, gs.enemies, &gs.scroll);
                draw_enemy_bullets(rp, gs.enemy_bullets, &gs.scroll);
                draw_bullets(rp, gs.bullets, &gs.scroll);
                draw_tank(rp, &gs.tank, &gs.scroll);
                draw_particles(rp, gs.particles, &gs.scroll);
                draw_explosions(rp, gs.explosions, &gs.scroll);
                draw_hud(rp, &gs);

                if (gs.state == STATE_GAMEOVER) {
                    draw_gameover(rp, gs.score);
                }
                else if (gs.state == STATE_LEVELCLEAR) {
                    draw_levelclear(rp, gs.level);
                }
            }
        }

        /* Poll bridge */
        ab_poll();

        /* VBlank sync + swap */
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
    if (sfx_hit_data)        FreeMem(sfx_hit_data, SFX_HIT_LEN);
    if (sfx_explode_data)    FreeMem(sfx_explode_data, SFX_EXPLODE_LEN);
    if (sfx_powerup_data)    FreeMem(sfx_powerup_data, SFX_POWERUP_LEN);
    if (sfx_jump_data)       FreeMem(sfx_jump_data, SFX_JUMP_LEN);
    if (sfx_player_hit_data) FreeMem(sfx_player_hit_data, SFX_PLAYER_HIT_LEN);

    input_reset();
    cleanup_display();

    ab_cleanup();
    CloseLibrary((struct Library *)GfxBase);
    CloseLibrary((struct Library *)IntuitionBase);

    return 0;
}
