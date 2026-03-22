/*
 * Defender - Sound: ptplayer MOD music + procedural SFX
 */
#include <proto/exec.h>
#include <proto/dos.h>
#include <exec/memory.h>
#include <string.h>

#include "game.h"
#include "sound.h"
#include "ptplayer.h"

#define CUSTOM_BASE ((void *)0xdff000)

/* MOD data */
static UBYTE *mod_data = NULL;
static ULONG  mod_size = 0;
static WORD   music_playing = 0;

/* SFX sample lengths */
#define SFX_LASER_LEN       128
#define SFX_EXPLODE_LEN     256
#define SFX_EXPLODE_BIG_LEN 512
#define SFX_PICKUP_LEN      256
#define SFX_HUMAN_DIE_LEN   256
#define SFX_BOMB_LEN        1024
#define SFX_HYPER_LEN       512
#define SFX_DIE_LEN         1024
#define SFX_LEVEL_LEN       512

/* SFX chip RAM buffers */
static BYTE *sfx_laser_data = NULL;
static BYTE *sfx_explode_data = NULL;
static BYTE *sfx_explode_big_data = NULL;
static BYTE *sfx_pickup_data = NULL;
static BYTE *sfx_human_die_data = NULL;
static BYTE *sfx_bomb_data = NULL;
static BYTE *sfx_hyper_data = NULL;
static BYTE *sfx_die_data = NULL;
static BYTE *sfx_level_data = NULL;

/* SFX structures */
static SfxStructure sfx_laser_s;
static SfxStructure sfx_explode_s;
static SfxStructure sfx_explode_big_s;
static SfxStructure sfx_pickup_s;
static SfxStructure sfx_human_die_s;
static SfxStructure sfx_bomb_s;
static SfxStructure sfx_hyper_s;
static SfxStructure sfx_die_s;
static SfxStructure sfx_level_s;

/* Simple RNG for noise generation */
static ULONG sfx_rng_state = 12345;
static WORD sfx_rng(void)
{
    sfx_rng_state = sfx_rng_state * 1103515245 + 12345;
    return (WORD)((sfx_rng_state >> 16) & 0x7FFF);
}

static void setup_sfx(SfxStructure *s, BYTE *data, WORD len, WORD per, WORD vol, BYTE pri)
{
    s->sfx_ptr = data;
    s->sfx_len = len / 2;
    s->sfx_per = per;
    s->sfx_vol = vol;
    s->sfx_cha = -1;
    s->sfx_pri = pri;
}

static void build_sfx(void)
{
    LONG i;
    WORD env;

    /* Laser: fast descending pulse */
    sfx_laser_data = AllocMem(SFX_LASER_LEN, MEMF_CHIP | MEMF_CLEAR);
    if (sfx_laser_data) {
        for (i = 0; i < SFX_LASER_LEN; i++) {
            env = 127 - (WORD)(i * 127 / SFX_LASER_LEN);
            sfx_laser_data[i] = ((i * (20 - i * 15 / SFX_LASER_LEN)) & 0xFF) > 128 ? (BYTE)(env) : (BYTE)(-env);
        }
        setup_sfx(&sfx_laser_s, sfx_laser_data, SFX_LASER_LEN, 160, 50, 30);
    }

    /* Explode small: noise + fast decay */
    sfx_explode_data = AllocMem(SFX_EXPLODE_LEN, MEMF_CHIP | MEMF_CLEAR);
    if (sfx_explode_data) {
        for (i = 0; i < SFX_EXPLODE_LEN; i++) {
            env = 127 - (WORD)(i * 127 / SFX_EXPLODE_LEN);
            sfx_explode_data[i] = (BYTE)((sfx_rng() % 256 - 128) * env / 127);
        }
        setup_sfx(&sfx_explode_s, sfx_explode_data, SFX_EXPLODE_LEN, 250, 48, 40);
    }

    /* Explode big: longer noise */
    sfx_explode_big_data = AllocMem(SFX_EXPLODE_BIG_LEN, MEMF_CHIP | MEMF_CLEAR);
    if (sfx_explode_big_data) {
        for (i = 0; i < SFX_EXPLODE_BIG_LEN; i++) {
            env = 127 - (WORD)(i * 127 / SFX_EXPLODE_BIG_LEN);
            sfx_explode_big_data[i] = (BYTE)((sfx_rng() % 256 - 128) * env / 127);
        }
        setup_sfx(&sfx_explode_big_s, sfx_explode_big_data, SFX_EXPLODE_BIG_LEN, 300, 64, 50);
    }

    /* Pickup human: rising arpeggio */
    sfx_pickup_data = AllocMem(SFX_PICKUP_LEN, MEMF_CHIP | MEMF_CLEAR);
    if (sfx_pickup_data) {
        for (i = 0; i < SFX_PICKUP_LEN; i++) {
            WORD freq;
            if (i < SFX_PICKUP_LEN / 3) freq = 20;
            else if (i < 2 * SFX_PICKUP_LEN / 3) freq = 15;
            else freq = 10;
            env = 100 - (WORD)(i * 60 / SFX_PICKUP_LEN);
            sfx_pickup_data[i] = ((i * freq) & 0xFF) > 128 ? (BYTE)(env) : (BYTE)(-env);
        }
        setup_sfx(&sfx_pickup_s, sfx_pickup_data, SFX_PICKUP_LEN, 200, 50, 35);
    }

    /* Human death: descending buzz */
    sfx_human_die_data = AllocMem(SFX_HUMAN_DIE_LEN, MEMF_CHIP | MEMF_CLEAR);
    if (sfx_human_die_data) {
        for (i = 0; i < SFX_HUMAN_DIE_LEN; i++) {
            WORD freq = 10 + (WORD)(i * 20 / SFX_HUMAN_DIE_LEN);
            env = 127 - (WORD)(i * 100 / SFX_HUMAN_DIE_LEN);
            sfx_human_die_data[i] = ((i * freq) & 0xFF) > 128 ? (BYTE)(env) : (BYTE)(-env);
        }
        setup_sfx(&sfx_human_die_s, sfx_human_die_data, SFX_HUMAN_DIE_LEN, 350, 45, 40);
    }

    /* Smart bomb: white noise crescendo */
    sfx_bomb_data = AllocMem(SFX_BOMB_LEN, MEMF_CHIP | MEMF_CLEAR);
    if (sfx_bomb_data) {
        for (i = 0; i < SFX_BOMB_LEN; i++) {
            env = (WORD)(i * 127 / (SFX_BOMB_LEN / 2));
            if (env > 127) env = 127 - (env - 127);
            if (env < 0) env = 0;
            sfx_bomb_data[i] = (BYTE)((sfx_rng() % 256 - 128) * env / 127);
        }
        setup_sfx(&sfx_bomb_s, sfx_bomb_data, SFX_BOMB_LEN, 200, 64, 60);
    }

    /* Hyperspace: metallic sweep */
    sfx_hyper_data = AllocMem(SFX_HYPER_LEN, MEMF_CHIP | MEMF_CLEAR);
    if (sfx_hyper_data) {
        for (i = 0; i < SFX_HYPER_LEN; i++) {
            WORD freq = 5 + (WORD)(i * 30 / SFX_HYPER_LEN);
            WORD noise = (WORD)(sfx_rng() % 64 - 32);
            env = 110 - (WORD)(i * 80 / SFX_HYPER_LEN);
            sfx_hyper_data[i] = (BYTE)((((i * freq) & 0xFF) > 128 ? env : -env) + noise / 4);
        }
        setup_sfx(&sfx_hyper_s, sfx_hyper_data, SFX_HYPER_LEN, 250, 55, 55);
    }

    /* Player death: long descending tone + noise */
    sfx_die_data = AllocMem(SFX_DIE_LEN, MEMF_CHIP | MEMF_CLEAR);
    if (sfx_die_data) {
        for (i = 0; i < SFX_DIE_LEN; i++) {
            WORD freq = 20 - (WORD)(i * 15 / SFX_DIE_LEN);
            WORD tone, noise;
            if (freq < 3) freq = 3;
            env = 127 - (WORD)(i * 100 / SFX_DIE_LEN);
            tone = ((i * freq) & 0xFF) > 128 ? env : -env;
            noise = (WORD)((sfx_rng() % 128 - 64) * env / 200);
            sfx_die_data[i] = (BYTE)(tone / 2 + noise);
        }
        setup_sfx(&sfx_die_s, sfx_die_data, SFX_DIE_LEN, 350, 64, 60);
    }

    /* Level complete: celebration arpeggio */
    sfx_level_data = AllocMem(SFX_LEVEL_LEN, MEMF_CHIP | MEMF_CLEAR);
    if (sfx_level_data) {
        for (i = 0; i < SFX_LEVEL_LEN; i++) {
            WORD freq;
            WORD quarter = SFX_LEVEL_LEN / 4;
            if (i < quarter) freq = 20;
            else if (i < 2 * quarter) freq = 16;
            else if (i < 3 * quarter) freq = 12;
            else freq = 10;
            env = 110 - (WORD)((i % quarter) * 30 / quarter);
            sfx_level_data[i] = ((i * freq) & 0xFF) > 128 ? (BYTE)(env) : (BYTE)(-env);
        }
        setup_sfx(&sfx_level_s, sfx_level_data, SFX_LEVEL_LEN, 200, 55, 50);
    }
}

void sound_init(void)
{
    build_sfx();
    mt_install_cia(CUSTOM_BASE, NULL, 1); /* PAL */
}

void sound_cleanup(void)
{
    mt_end(CUSTOM_BASE);
    mt_remove_cia(CUSTOM_BASE);

    if (sfx_laser_data)       FreeMem(sfx_laser_data, SFX_LASER_LEN);
    if (sfx_explode_data)     FreeMem(sfx_explode_data, SFX_EXPLODE_LEN);
    if (sfx_explode_big_data) FreeMem(sfx_explode_big_data, SFX_EXPLODE_BIG_LEN);
    if (sfx_pickup_data)      FreeMem(sfx_pickup_data, SFX_PICKUP_LEN);
    if (sfx_human_die_data)   FreeMem(sfx_human_die_data, SFX_HUMAN_DIE_LEN);
    if (sfx_bomb_data)        FreeMem(sfx_bomb_data, SFX_BOMB_LEN);
    if (sfx_hyper_data)       FreeMem(sfx_hyper_data, SFX_HYPER_LEN);
    if (sfx_die_data)         FreeMem(sfx_die_data, SFX_DIE_LEN);
    if (sfx_level_data)       FreeMem(sfx_level_data, SFX_LEVEL_LEN);
    if (mod_data)             FreeMem(mod_data, mod_size);
}

WORD sound_load_mod(const char *filename)
{
    BPTR fh;
    LONG len;

    fh = Open((STRPTR)filename, MODE_OLDFILE);
    if (!fh) return 0;

    Seek(fh, 0, OFFSET_END);
    len = Seek(fh, 0, OFFSET_BEGINNING);
    if (len <= 0) { Close(fh); return 0; }

    mod_data = AllocMem(len, MEMF_CHIP);
    if (!mod_data) { Close(fh); return 0; }

    Read(fh, mod_data, len);
    Close(fh);
    mod_size = len;
    return 1;
}

void sound_start_music(void)
{
    if (mod_data) {
        mt_init(CUSTOM_BASE, mod_data, NULL, 0);
        mt_MusicChannels = 2;
        mt_Enable = 1;
        music_playing = 1;
    }
}

void sound_stop_music(void)
{
    if (music_playing) {
        mt_Enable = 0;
        mt_end(CUSTOM_BASE);
        music_playing = 0;
    }
}

void sfx_laser(void)       { if (sfx_laser_data) mt_playfx(CUSTOM_BASE, &sfx_laser_s); }
void sfx_explode(void)     { if (sfx_explode_data) mt_playfx(CUSTOM_BASE, &sfx_explode_s); }
void sfx_explode_big(void) { if (sfx_explode_big_data) mt_playfx(CUSTOM_BASE, &sfx_explode_big_s); }
void sfx_pickup(void)      { if (sfx_pickup_data) mt_playfx(CUSTOM_BASE, &sfx_pickup_s); }
void sfx_human_die(void)   { if (sfx_human_die_data) mt_playfx(CUSTOM_BASE, &sfx_human_die_s); }
void sfx_bomb(void)        { if (sfx_bomb_data) mt_playfx(CUSTOM_BASE, &sfx_bomb_s); }
void sfx_hyper(void)       { if (sfx_hyper_data) mt_playfx(CUSTOM_BASE, &sfx_hyper_s); }
void sfx_die(void)         { if (sfx_die_data) mt_playfx(CUSTOM_BASE, &sfx_die_s); }
void sfx_level(void)       { if (sfx_level_data) mt_playfx(CUSTOM_BASE, &sfx_level_s); }

void sound_update(GameState *gs)
{
    if (gs->ev_laser)       { sfx_laser();       gs->ev_laser = 0; }
    if (gs->ev_explode)     { sfx_explode();     gs->ev_explode = 0; }
    if (gs->ev_explode_big) { sfx_explode_big(); gs->ev_explode_big = 0; }
    if (gs->ev_pickup)      { sfx_pickup();      gs->ev_pickup = 0; }
    if (gs->ev_human_die)   { sfx_human_die();   gs->ev_human_die = 0; }
    if (gs->ev_bomb)        { sfx_bomb();         gs->ev_bomb = 0; }
    if (gs->ev_hyper)       { sfx_hyper();        gs->ev_hyper = 0; }
    if (gs->ev_die)         { sfx_die();          gs->ev_die = 0; }
    if (gs->ev_level)       { sfx_level();        gs->ev_level = 0; }
}
