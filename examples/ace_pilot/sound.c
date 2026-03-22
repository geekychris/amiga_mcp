/*
 * sound.c - Sound effect synthesis for Ace Pilot
 * Uses ptplayer for playback via SFX structures
 */
#include <exec/memory.h>
#include <proto/exec.h>
#include "sound.h"

/* SFX sample lengths */
#define SFX_GUN_LEN     64
#define SFX_EXPLODE_LEN 512
#define SFX_HIT_LEN     256
#define SFX_DIE_LEN     1024

/* Sample data in chip RAM */
static BYTE *sfx_gun_data = NULL;
static BYTE *sfx_explode_data = NULL;
static BYTE *sfx_hit_data = NULL;
static BYTE *sfx_die_data = NULL;

/* SFX structures for ptplayer */
static SfxStructure sfx_gun_sfx;
static SfxStructure sfx_explode_sfx;
static SfxStructure sfx_hit_sfx;
static SfxStructure sfx_die_sfx;

/* Simple RNG for noise */
static ULONG sfx_rng_state = 54321;

static WORD sfx_rng(void)
{
    sfx_rng_state = sfx_rng_state * 1103515245UL + 12345UL;
    return (WORD)((sfx_rng_state >> 16) & 0x7FFF);
}

void sound_init(void)
{
    WORD i;

    /* Machine gun burst: rapid clicking, high-pitched */
    sfx_gun_data = (BYTE *)AllocMem(SFX_GUN_LEN, MEMF_CHIP | MEMF_CLEAR);
    if (sfx_gun_data) {
        for (i = 0; i < SFX_GUN_LEN; i++) {
            WORD phase = (i * 30) & 0xFF;
            WORD env = 127 - (i * 127) / SFX_GUN_LEN;
            sfx_gun_data[i] = (BYTE)((phase > 128 ? 64 : -64) * env / 127);
        }
    }

    /* Explosion: white noise with long decay */
    sfx_explode_data = (BYTE *)AllocMem(SFX_EXPLODE_LEN, MEMF_CHIP | MEMF_CLEAR);
    if (sfx_explode_data) {
        for (i = 0; i < SFX_EXPLODE_LEN; i++) {
            WORD env = 127 - (i * 127) / SFX_EXPLODE_LEN;
            sfx_explode_data[i] = (BYTE)((sfx_rng() % 256 - 128) * env / 127);
        }
    }

    /* Hit: short noise burst */
    sfx_hit_data = (BYTE *)AllocMem(SFX_HIT_LEN, MEMF_CHIP | MEMF_CLEAR);
    if (sfx_hit_data) {
        for (i = 0; i < SFX_HIT_LEN; i++) {
            WORD env = 127 - (i * 127) / SFX_HIT_LEN;
            sfx_hit_data[i] = (BYTE)((sfx_rng() % 200 - 100) * env / 127);
        }
    }

    /* Die: descending wail + noise */
    sfx_die_data = (BYTE *)AllocMem(SFX_DIE_LEN, MEMF_CHIP | MEMF_CLEAR);
    if (sfx_die_data) {
        for (i = 0; i < SFX_DIE_LEN; i++) {
            WORD env = 127 - (i * 127) / SFX_DIE_LEN;
            WORD freq = 6 + (i * 30) / SFX_DIE_LEN;
            WORD tone = ((i * freq) & 0xFF) > 128 ? 60 : -60;
            WORD noise = (sfx_rng() % 60) - 30;
            sfx_die_data[i] = (BYTE)(((tone + noise) * env) / 127);
        }
    }

    /* Setup SFX structures */
    sfx_gun_sfx.sfx_ptr = sfx_gun_data;
    sfx_gun_sfx.sfx_len = SFX_GUN_LEN / 2;
    sfx_gun_sfx.sfx_per = 180;
    sfx_gun_sfx.sfx_vol = 40;
    sfx_gun_sfx.sfx_cha = -1;
    sfx_gun_sfx.sfx_pri = 20;

    sfx_explode_sfx.sfx_ptr = sfx_explode_data;
    sfx_explode_sfx.sfx_len = SFX_EXPLODE_LEN / 2;
    sfx_explode_sfx.sfx_per = 300;
    sfx_explode_sfx.sfx_vol = 64;
    sfx_explode_sfx.sfx_cha = -1;
    sfx_explode_sfx.sfx_pri = 50;

    sfx_hit_sfx.sfx_ptr = sfx_hit_data;
    sfx_hit_sfx.sfx_len = SFX_HIT_LEN / 2;
    sfx_hit_sfx.sfx_per = 220;
    sfx_hit_sfx.sfx_vol = 50;
    sfx_hit_sfx.sfx_cha = -1;
    sfx_hit_sfx.sfx_pri = 40;

    sfx_die_sfx.sfx_ptr = sfx_die_data;
    sfx_die_sfx.sfx_len = SFX_DIE_LEN / 2;
    sfx_die_sfx.sfx_per = 350;
    sfx_die_sfx.sfx_vol = 64;
    sfx_die_sfx.sfx_cha = -1;
    sfx_die_sfx.sfx_pri = 60;
}

void sound_cleanup(void)
{
    if (sfx_gun_data)     FreeMem(sfx_gun_data, SFX_GUN_LEN);
    if (sfx_explode_data) FreeMem(sfx_explode_data, SFX_EXPLODE_LEN);
    if (sfx_hit_data)     FreeMem(sfx_hit_data, SFX_HIT_LEN);
    if (sfx_die_data)     FreeMem(sfx_die_data, SFX_DIE_LEN);
}

void sfx_gunfire(void)
{
    if (sfx_gun_data)
        mt_playfx(CUSTOM_BASE, &sfx_gun_sfx);
}

void sfx_explosion(void)
{
    if (sfx_explode_data)
        mt_playfx(CUSTOM_BASE, &sfx_explode_sfx);
}

void sfx_hit(void)
{
    if (sfx_hit_data)
        mt_playfx(CUSTOM_BASE, &sfx_hit_sfx);
}

void sfx_die(void)
{
    if (sfx_die_data)
        mt_playfx(CUSTOM_BASE, &sfx_die_sfx);
}
