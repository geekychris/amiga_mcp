/*
 * Uranus Lander - Sound effects
 * Procedurally generated samples in chip RAM, played via ptplayer
 */
#include <proto/exec.h>
#include <exec/memory.h>
#include "sound.h"
#include "ptplayer.h"

#define CUSTOM_BASE ((void *)0xdff000)

/* Sample sizes */
#define SFX_THRUST_LEN   3072
#define SFX_CRASH_LEN    16384
#define SFX_LAND_LEN     512
#define SFX_BEEP_LEN     128

/* Sample buffers in chip RAM */
static BYTE *thrust_data = NULL;
static BYTE *crash_data  = NULL;
static BYTE *land_data   = NULL;
static BYTE *beep_data   = NULL;

/* SFX structures for ptplayer */
static SfxStructure thrust_sfx;
static SfxStructure crash_sfx;
static SfxStructure land_sfx;
static SfxStructure beep_sfx;

/* Simple RNG */
static ULONG sfx_rng_state = 54321;
static WORD sfx_rng(void)
{
    sfx_rng_state = sfx_rng_state * 1103515245UL + 12345UL;
    return (WORD)((sfx_rng_state >> 16) & 0x7FFF);
}

void sound_init(void)
{
    WORD i;

    /* Thrust: long, deep, rumbling rocket roar
     * Layered: heavy filtered noise + low-freq throb + subtle crackle
     * Designed to loop seamlessly (start and end at ~0) */
    thrust_data = (BYTE *)AllocMem(SFX_THRUST_LEN, MEMF_CHIP | MEMF_CLEAR);
    if (thrust_data) {
        WORD prev1 = 0, prev2 = 0;
        for (i = 0; i < SFX_THRUST_LEN; i++) {
            /* Primary: heavy white noise */
            WORD noise = (sfx_rng() % 220) - 110;
            /* Low-freq throb at ~8Hz: creates the pulsing rocket chug */
            WORD throb = ((i * 2) & 0xFF);
            WORD throb_env = (throb < 128) ? (throb / 2) : ((255 - throb) / 2);
            /* Crackle: occasional spikes for texture */
            WORD crackle = ((sfx_rng() & 0x1F) == 0) ? (sfx_rng() % 80 - 40) : 0;
            /* Mix */
            WORD raw = (noise * (40 + throb_env)) / 100 + crackle;
            /* Two-tap low-pass filter for deep bass emphasis */
            WORD filtered = (raw + prev1 + prev1 + prev2) / 4;
            prev2 = prev1;
            prev1 = filtered;
            /* Clamp to full range for maximum volume */
            if (filtered > 127) filtered = 127;
            if (filtered < -127) filtered = -127;
            thrust_data[i] = (BYTE)filtered;
        }
        /* Smooth the loop point: blend last 32 samples toward first */
        for (i = 0; i < 32; i++) {
            WORD blend = (thrust_data[i] * i + thrust_data[SFX_THRUST_LEN - 32 + i] * (31 - i)) / 31;
            thrust_data[SFX_THRUST_LEN - 32 + i] = (BYTE)blend;
        }
    }

    /* Crash: massive multi-second explosion
     * 16384 samples at period 600 = ~2.8 seconds
     * Four phases: instant blast, roaring fireball, rumbling debris, fading echo */
    crash_data = (BYTE *)AllocMem(SFX_CRASH_LEN, MEMF_CHIP | MEMF_CLEAR);
    if (crash_data) {
        WORD prev1 = 0, prev2 = 0;
        LONG li;
        for (li = 0; li < (LONG)SFX_CRASH_LEN; li++) {
            LONG env;
            WORD noise, rumble, crackle, raw, filtered;

            /* Four-phase envelope */
            if (li < 128) {
                /* Phase 1: instant blast ramp (sharp attack) */
                env = (li * 127L) / 128;
            } else if (li < 3000) {
                /* Phase 2: roaring fireball at full intensity */
                env = 127;
            } else if (li < 8000) {
                /* Phase 3: rumbling debris - slow decay */
                env = 127L - ((li - 3000L) * 60L) / 5000;
            } else {
                /* Phase 4: fading echo tail */
                env = 67L - ((li - 8000L) * 67L) / ((LONG)SFX_CRASH_LEN - 8000L);
            }
            if (env < 0) env = 0;

            /* Heavy white noise (main explosion texture) */
            noise = (sfx_rng() % 256) - 128;

            /* Deep rumble (low-freq square wave that slows down over time) */
            {
                WORD freq = 3 + (WORD)(li / 2048);
                rumble = (((li * freq) & 0xFF) > 128) ? 60 : -60;
            }

            /* Random crackle pops (debris impacts) */
            crackle = ((sfx_rng() & 0xF) == 0) ? (sfx_rng() % 100 - 50) : 0;

            /* Mix shifts over time: noise-heavy early, rumble-heavy late */
            if (li < 3000) {
                raw = (WORD)((noise * 80L + rumble * 30L + crackle * 40L) / 100);
            } else {
                LONG rumble_mix = 30L + ((li - 3000L) * 50L) / ((LONG)SFX_CRASH_LEN - 3000L);
                raw = (WORD)((noise * (80L - rumble_mix) + rumble * rumble_mix + crackle * 20L) / 100);
            }

            /* Two-tap low-pass filter for deep bass */
            filtered = (WORD)((raw + prev1 + prev1 + prev2) / 4);
            prev2 = prev1;
            prev1 = filtered;

            /* Apply envelope */
            filtered = (WORD)((filtered * env) / 127);

            if (filtered > 127) filtered = 127;
            if (filtered < -127) filtered = -127;
            crash_data[li] = (BYTE)filtered;
        }
    }

    /* Landing: ascending chime tone */
    land_data = (BYTE *)AllocMem(SFX_LAND_LEN, MEMF_CHIP | MEMF_CLEAR);
    if (land_data) {
        for (i = 0; i < SFX_LAND_LEN; i++) {
            WORD env = (i < SFX_LAND_LEN / 4) ? (i * 127 / (SFX_LAND_LEN / 4)) :
                       (127 - ((i - SFX_LAND_LEN / 4) * 127) / (SFX_LAND_LEN * 3 / 4));
            WORD freq = 30 - (i * 20) / SFX_LAND_LEN;
            WORD tone = ((i * freq) & 0xFF) > 128 ? 80 : -80;
            if (env < 0) env = 0;
            land_data[i] = (BYTE)((tone * env) / 127);
        }
    }

    /* Low fuel beep: short high-pitched tone */
    beep_data = (BYTE *)AllocMem(SFX_BEEP_LEN, MEMF_CHIP | MEMF_CLEAR);
    if (beep_data) {
        for (i = 0; i < SFX_BEEP_LEN; i++) {
            WORD env = 127 - (i * 127) / SFX_BEEP_LEN;
            beep_data[i] = (BYTE)(((((i * 40) & 0xFF) > 128) ? 64 : -64) * env / 127);
        }
    }

    /* Setup SFX structures */
    thrust_sfx.sfx_ptr = thrust_data;
    thrust_sfx.sfx_len = SFX_THRUST_LEN / 2;
    thrust_sfx.sfx_per = 500;   /* mid-bass: audible rumble, not sub-bass */
    thrust_sfx.sfx_vol = 64;    /* max volume */
    thrust_sfx.sfx_cha = 2;     /* fixed channel 2 - won't compete for allocation */
    thrust_sfx.sfx_pri = 100;   /* very high priority */

    crash_sfx.sfx_ptr = crash_data;
    crash_sfx.sfx_len = SFX_CRASH_LEN / 2;
    crash_sfx.sfx_per = 500;    /* mid-bass for punchy explosion */
    crash_sfx.sfx_vol = 64;     /* maximum volume */
    crash_sfx.sfx_cha = 3;      /* fixed channel 3 - dedicated for crash */
    crash_sfx.sfx_pri = 127;    /* absolute highest priority */

    land_sfx.sfx_ptr = land_data;
    land_sfx.sfx_len = SFX_LAND_LEN / 2;
    land_sfx.sfx_per = 180;
    land_sfx.sfx_vol = 56;
    land_sfx.sfx_cha = -1;
    land_sfx.sfx_pri = 50;

    beep_sfx.sfx_ptr = beep_data;
    beep_sfx.sfx_len = SFX_BEEP_LEN / 2;
    beep_sfx.sfx_per = 150;
    beep_sfx.sfx_vol = 48;
    beep_sfx.sfx_cha = -1;
    beep_sfx.sfx_pri = 30;
}

void sound_cleanup(void)
{
    if (thrust_data) { FreeMem(thrust_data, SFX_THRUST_LEN); thrust_data = NULL; }
    if (crash_data)  { FreeMem(crash_data,  SFX_CRASH_LEN);  crash_data  = NULL; }
    if (land_data)   { FreeMem(land_data,   SFX_LAND_LEN);   land_data   = NULL; }
    if (beep_data)   { FreeMem(beep_data,   SFX_BEEP_LEN);   beep_data   = NULL; }
}

/* Silent sample to kill channel noise */
static BYTE silence_buf[4] = { 0, 0, 0, 0 };
static SfxStructure silence_sfx;

void sfx_thrust_play(void)
{
    if (thrust_data)
        mt_playfx(CUSTOM_BASE, &thrust_sfx);
}

void sfx_thrust_stop(void)
{
    /* Play a tiny silent sample on the thrust channel to kill any lingering tone */
    silence_sfx.sfx_ptr = silence_buf;
    silence_sfx.sfx_len = 2;       /* 2 words = 4 bytes */
    silence_sfx.sfx_per = 500;
    silence_sfx.sfx_vol = 0;       /* silent */
    silence_sfx.sfx_cha = 2;       /* same channel as thrust */
    silence_sfx.sfx_pri = 100;
    mt_playfx(CUSTOM_BASE, &silence_sfx);
}

void sfx_crash_play(void)
{
    if (crash_data)
        mt_playfx(CUSTOM_BASE, &crash_sfx);
}

void sfx_land_play(void)
{
    if (land_data)
        mt_playfx(CUSTOM_BASE, &land_sfx);
}

void sfx_beep_play(void)
{
    if (beep_data)
        mt_playfx(CUSTOM_BASE, &beep_sfx);
}
