/*
 * sound.c - Sound effect synthesis for RJ's Birthday Bash
 * Uses ptplayer for playback via SFX structures
 */
#include <exec/memory.h>
#include <proto/exec.h>
#include <hardware/custom.h>
#include "game.h"
#include "ptplayer.h"

#define CUSTOM_BASE ((void *)0xdff000)

/* Sample lengths */
#define SFX_BELL_LEN    512
#define SFX_SLIDE_LEN   128
#define SFX_CHOMP_LEN   64
#define SFX_DING_LEN    256
#define SFX_BUZZ_LEN    256
#define SFX_CHEER_LEN   1024
#define SFX_PICK_LEN    64
#define SFX_CRASH_LEN   512
#define SFX_PARTY_LEN   2048
#define SFX_SINISTAR_LEN 4096

/* Sample data in chip RAM */
static BYTE *bell_data = NULL;
static BYTE *slide_data = NULL;
static BYTE *chomp_data = NULL;
static BYTE *ding_data = NULL;
static BYTE *buzz_data = NULL;
static BYTE *cheer_data = NULL;
static BYTE *pick_data = NULL;
static BYTE *crash_data = NULL;
static BYTE *party_data = NULL;
static BYTE *sinistar_data = NULL;

/* SFX structures */
static SfxStructure bell_sfx;
static SfxStructure slide_sfx;
static SfxStructure chomp_sfx;
static SfxStructure ding_sfx;
static SfxStructure buzz_sfx;
static SfxStructure cheer_sfx;
static SfxStructure pick_sfx;
static SfxStructure crash_sfx;
static SfxStructure party_sfx;
static SfxStructure sinistar_sfx;

/* RNG */
static ULONG snd_rng = 77777;
static WORD snd_rand(void)
{
    snd_rng = snd_rng * 1103515245UL + 12345UL;
    return (WORD)((snd_rng >> 16) & 0x7FFF);
}

/* Helper: setup an SfxStructure */
static void setup_sfx(SfxStructure *s, BYTE *data, WORD len,
                       WORD period, WORD vol, BYTE pri)
{
    s->sfx_ptr = data;
    s->sfx_len = len / 2;  /* length in words */
    s->sfx_per = period;
    s->sfx_vol = vol;
    s->sfx_cha = -1;       /* auto-select channel */
    s->sfx_pri = pri;
}

void sound_init(void)
{
    WORD i;

    /* Doorbell: two-tone ding-dong */
    bell_data = (BYTE *)AllocMem(SFX_BELL_LEN, MEMF_CHIP | MEMF_CLEAR);
    if (bell_data) {
        for (i = 0; i < SFX_BELL_LEN; i++) {
            WORD env = 127 - (i * 127) / SFX_BELL_LEN;
            WORD freq = (i < SFX_BELL_LEN / 2) ? 12 : 9;
            WORD phase = (i * freq) & 0xFF;
            WORD val = (phase < 128) ? 80 : -80;
            bell_data[i] = (BYTE)(val * env / 127);
        }
    }
    setup_sfx(&bell_sfx, bell_data, SFX_BELL_LEN, 280, 55, 30);

    /* Slide: swoosh sound */
    slide_data = (BYTE *)AllocMem(SFX_SLIDE_LEN, MEMF_CHIP | MEMF_CLEAR);
    if (slide_data) {
        for (i = 0; i < SFX_SLIDE_LEN; i++) {
            WORD env = 127 - (i * 127) / SFX_SLIDE_LEN;
            WORD freq = 3 + (i * 20) / SFX_SLIDE_LEN;
            WORD noise = (snd_rand() % 40) - 20;
            slide_data[i] = (BYTE)((((i * freq) & 0xFF) > 128 ? 40 : -40) * env / 127 + noise * env / 500);
        }
    }
    setup_sfx(&slide_sfx, slide_data, SFX_SLIDE_LEN, 200, 40, 20);

    /* Chomp: quick bite sound */
    chomp_data = (BYTE *)AllocMem(SFX_CHOMP_LEN, MEMF_CHIP | MEMF_CLEAR);
    if (chomp_data) {
        for (i = 0; i < SFX_CHOMP_LEN; i++) {
            WORD env = (i < 10) ? (i * 12) : (127 - ((i - 10) * 127) / (SFX_CHOMP_LEN - 10));
            WORD phase = (i * 25) & 0xFF;
            chomp_data[i] = (BYTE)((phase > 128 ? 60 : -60) * env / 127);
        }
    }
    setup_sfx(&chomp_sfx, chomp_data, SFX_CHOMP_LEN, 250, 50, 25);

    /* Ding: correct serve - bright bell */
    ding_data = (BYTE *)AllocMem(SFX_DING_LEN, MEMF_CHIP | MEMF_CLEAR);
    if (ding_data) {
        for (i = 0; i < SFX_DING_LEN; i++) {
            WORD env = 127 - (i * 127) / SFX_DING_LEN;
            WORD p1 = (i * 15) & 0xFF;
            WORD p2 = (i * 30) & 0xFF;
            WORD val = ((p1 < 128 ? 50 : -50) + (p2 < 128 ? 30 : -30));
            ding_data[i] = (BYTE)(val * env / 127);
        }
    }
    setup_sfx(&ding_sfx, ding_data, SFX_DING_LEN, 200, 55, 40);

    /* Buzzer: wrong serve */
    buzz_data = (BYTE *)AllocMem(SFX_BUZZ_LEN, MEMF_CHIP | MEMF_CLEAR);
    if (buzz_data) {
        for (i = 0; i < SFX_BUZZ_LEN; i++) {
            WORD env = 127 - (i * 127) / SFX_BUZZ_LEN;
            WORD val = (snd_rand() % 60 - 30) + (((i * 5) & 0xFF) > 128 ? 40 : -40);
            buzz_data[i] = (BYTE)(val * env / 127);
        }
    }
    setup_sfx(&buzz_sfx, buzz_data, SFX_BUZZ_LEN, 400, 50, 35);

    /* Cheer: crowd noise */
    cheer_data = (BYTE *)AllocMem(SFX_CHEER_LEN, MEMF_CHIP | MEMF_CLEAR);
    if (cheer_data) {
        for (i = 0; i < SFX_CHEER_LEN; i++) {
            WORD env;
            WORD noise;
            if (i < SFX_CHEER_LEN / 4)
                env = (i * 127) / (SFX_CHEER_LEN / 4);
            else
                env = 127 - ((i - SFX_CHEER_LEN / 4) * 127) / (SFX_CHEER_LEN * 3 / 4);
            noise = snd_rand() % 200 - 100;
            cheer_data[i] = (BYTE)(noise * env / 127);
        }
    }
    setup_sfx(&cheer_sfx, cheer_data, SFX_CHEER_LEN, 350, 55, 50);

    /* Pickup: quick ascending blip */
    pick_data = (BYTE *)AllocMem(SFX_PICK_LEN, MEMF_CHIP | MEMF_CLEAR);
    if (pick_data) {
        for (i = 0; i < SFX_PICK_LEN; i++) {
            WORD env = 127 - (i * 127) / SFX_PICK_LEN;
            WORD freq = 20 - (i * 15) / SFX_PICK_LEN;
            pick_data[i] = (BYTE)((((i * freq) & 0xFF) > 128 ? 64 : -64) * env / 127);
        }
    }
    setup_sfx(&pick_sfx, pick_data, SFX_PICK_LEN, 180, 50, 25);

    /* Crash: gift breaking */
    crash_data = (BYTE *)AllocMem(SFX_CRASH_LEN, MEMF_CHIP | MEMF_CLEAR);
    if (crash_data) {
        for (i = 0; i < SFX_CRASH_LEN; i++) {
            WORD env = 127 - (i * 127) / SFX_CRASH_LEN;
            WORD noise = snd_rand() % 256 - 128;
            crash_data[i] = (BYTE)(noise * env / 127);
        }
    }
    setup_sfx(&crash_sfx, crash_data, SFX_CRASH_LEN, 280, 60, 45);

    /* Party horn: ascending wail */
    party_data = (BYTE *)AllocMem(SFX_PARTY_LEN, MEMF_CHIP | MEMF_CLEAR);
    if (party_data) {
        for (i = 0; i < SFX_PARTY_LEN; i++) {
            WORD env;
            WORD freq, val;
            if (i < SFX_PARTY_LEN / 3)
                env = (i * 127) / (SFX_PARTY_LEN / 3);
            else if (i < SFX_PARTY_LEN * 2 / 3)
                env = 127;
            else
                env = 127 - ((i - SFX_PARTY_LEN * 2 / 3) * 127) / (SFX_PARTY_LEN / 3);
            freq = 5 + (i * 15) / SFX_PARTY_LEN;
            val = ((i * freq) & 0xFF) < 128 ? 60 : -60;
            val += (snd_rand() % 20) - 10;
            party_data[i] = (BYTE)(val * env / 127);
        }
    }
    setup_sfx(&party_sfx, party_data, SFX_PARTY_LEN, 300, 60, 55);

    /* Sinistar growl: deep menacing warble with formant harmonics
     * Evokes a threatening robotic voice - "BEWARE I LIVE" vibe */
    sinistar_data = (BYTE *)AllocMem(SFX_SINISTAR_LEN, MEMF_CHIP | MEMF_CLEAR);
    if (sinistar_data) {
        for (i = 0; i < SFX_SINISTAR_LEN; i++) {
            WORD env;
            WORD t = i;
            LONG val = 0;
            WORD wobble;

            /* Envelope: attack, sustain with slow decay */
            if (t < 200)
                env = (t * 127) / 200;            /* attack */
            else if (t < SFX_SINISTAR_LEN - 500)
                env = 127 - (t * 20) / SFX_SINISTAR_LEN;  /* slow decay */
            else
                env = (WORD)((SFX_SINISTAR_LEN - t) * 80L) / 500;  /* release */

            /* Deep fundamental with vibrato */
            wobble = 3 + ((t * 3) / SFX_SINISTAR_LEN);
            val += ((t * wobble) & 0xFF) < 128 ? 60 : -60;

            /* Growling harmonic (3x fundamental) */
            val += ((t * wobble * 3) & 0xFF) < 128 ? 25 : -25;

            /* High formant sweep (voice-like resonance) */
            {
                WORD ffreq = 12 + (t * 20) / SFX_SINISTAR_LEN;
                /* Reverse sweep in middle for "speech" cadence */
                if (t > SFX_SINISTAR_LEN / 3 && t < SFX_SINISTAR_LEN * 2 / 3)
                    ffreq = 32 - (t * 15) / SFX_SINISTAR_LEN;
                val += ((t * ffreq) & 0xFF) < 128 ? 20 : -20;
            }

            /* Rumbling noise for texture */
            val += (snd_rand() % 16) - 8;

            /* Apply envelope */
            val = (val * env) / 127;

            if (val > 127) val = 127;
            if (val < -127) val = -127;
            sinistar_data[i] = (BYTE)val;
        }
    }
    /* Low period = deep pitch, high volume, high priority */
    setup_sfx(&sinistar_sfx, sinistar_data, SFX_SINISTAR_LEN, 500, 64, 60);
}

void sound_cleanup(void)
{
    if (bell_data)  FreeMem(bell_data, SFX_BELL_LEN);
    if (slide_data) FreeMem(slide_data, SFX_SLIDE_LEN);
    if (chomp_data) FreeMem(chomp_data, SFX_CHOMP_LEN);
    if (ding_data)  FreeMem(ding_data, SFX_DING_LEN);
    if (buzz_data)  FreeMem(buzz_data, SFX_BUZZ_LEN);
    if (cheer_data) FreeMem(cheer_data, SFX_CHEER_LEN);
    if (pick_data)  FreeMem(pick_data, SFX_PICK_LEN);
    if (crash_data) FreeMem(crash_data, SFX_CRASH_LEN);
    if (party_data) FreeMem(party_data, SFX_PARTY_LEN);
    if (sinistar_data) FreeMem(sinistar_data, SFX_SINISTAR_LEN);
}

void sfx_doorbell(void)  { if (bell_data) mt_playfx(CUSTOM_BASE, &bell_sfx); }
void sfx_slide(void)     { if (slide_data) mt_playfx(CUSTOM_BASE, &slide_sfx); }
void sfx_chomp(void)     { if (chomp_data) mt_playfx(CUSTOM_BASE, &chomp_sfx); }
void sfx_ding(void)      { if (ding_data) mt_playfx(CUSTOM_BASE, &ding_sfx); }
void sfx_buzzer(void)    { if (buzz_data) mt_playfx(CUSTOM_BASE, &buzz_sfx); }
void sfx_cheer(void)     { if (cheer_data) mt_playfx(CUSTOM_BASE, &cheer_sfx); }
void sfx_pickup(void)    { if (pick_data) mt_playfx(CUSTOM_BASE, &pick_sfx); }
void sfx_crash(void)     { if (crash_data) mt_playfx(CUSTOM_BASE, &crash_sfx); }
void sfx_party(void)     { if (party_data) mt_playfx(CUSTOM_BASE, &party_sfx); }
void sfx_sinistar(void)  { if (sinistar_data) mt_playfx(CUSTOM_BASE, &sinistar_sfx); }

/* --- Sinistar voice samples loaded from disk --- */

#include <proto/dos.h>

#define SINI_VOICE_COUNT 4
#define SINI_PERIOD      354   /* 3546895 / 10026 Hz ≈ 354 */

static BYTE *voice_data[SINI_VOICE_COUNT] = { NULL, NULL, NULL, NULL };
static ULONG voice_size[SINI_VOICE_COUNT] = { 0, 0, 0, 0 };
static SfxStructure voice_sfx[SINI_VOICE_COUNT];
static WORD voice_active = -1;     /* which voice is currently playing, -1=none */
static WORD voice_frames_left = 0; /* frames until voice finishes */
static UBYTE music_was_enabled = 0;

static const char *voice_files[SINI_VOICE_COUNT] = {
    "DH2:Dev/snd_bewareco.raw",
    "DH2:Dev/snd_bewareil.raw",
    "DH2:Dev/snd_ihunger.raw",
    "DH2:Dev/snd_runrunru.raw"
};

static BYTE *load_raw_to_chip(const char *path, ULONG *out_size)
{
    BPTR fh;
    BYTE *buf = NULL;
    LONG len;
    fh = Open((CONST_STRPTR)path, MODE_OLDFILE);
    if (!fh) return NULL;
    Seek(fh, 0, OFFSET_END);
    len = Seek(fh, 0, OFFSET_BEGINNING);
    if (len <= 0) { Close(fh); return NULL; }
    buf = (BYTE *)AllocMem(len, MEMF_CHIP);
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

void sinistar_load_voices(void)
{
    WORD i;
    for (i = 0; i < SINI_VOICE_COUNT; i++) {
        voice_data[i] = load_raw_to_chip(voice_files[i], &voice_size[i]);
        if (voice_data[i]) {
            voice_sfx[i].sfx_ptr = voice_data[i];
            voice_sfx[i].sfx_len = (WORD)(voice_size[i] / 2);
            voice_sfx[i].sfx_per = SINI_PERIOD;
            voice_sfx[i].sfx_vol = 64;
            voice_sfx[i].sfx_cha = 0;   /* force channel 0 for voice */
            voice_sfx[i].sfx_pri = 127;  /* highest priority */
        }
    }
    voice_active = -1;
    voice_frames_left = 0;
}

void sinistar_cleanup_voices(void)
{
    WORD i;
    for (i = 0; i < SINI_VOICE_COUNT; i++) {
        if (voice_data[i]) {
            FreeMem(voice_data[i], voice_size[i]);
            voice_data[i] = NULL;
        }
    }
}

void sinistar_play_random(void)
{
    WORD pick;
    WORD attempts = 0;

    /* Find a loaded voice */
    do {
        pick = snd_rand() % SINI_VOICE_COUNT;
        attempts++;
    } while (!voice_data[pick] && attempts < 8);

    if (!voice_data[pick]) {
        /* Fallback to synth growl */
        sfx_sinistar();
        return;
    }

    /* Pause music and kill all channel audio */
    music_was_enabled = mt_Enable;
    mt_Enable = 0;
    mt_musicmask(CUSTOM_BASE, 0x0);

    /* Silence all 4 Paula channels immediately */
    {
        volatile struct Custom *hw = (volatile struct Custom *)0xdff000;
        hw->dmacon = 0x000F;  /* disable audio DMA for all 4 channels */
        hw->aud[0].ac_vol = 0;
        hw->aud[1].ac_vol = 0;
        hw->aud[2].ac_vol = 0;
        hw->aud[3].ac_vol = 0;
        hw->dmacon = 0x800F;  /* re-enable audio DMA */
    }

    /* Play the voice sample */
    mt_playfx(CUSTOM_BASE, &voice_sfx[pick]);
    voice_active = pick;

    /* Calculate duration in frames: samples / sample_rate * 50fps */
    /* 10026 Hz, 50fps → samples / 200.52 ≈ samples / 200 */
    voice_frames_left = (WORD)(voice_size[pick] / 200) + 10; /* +10 for safety */
}

WORD sinistar_is_playing(void)
{
    return voice_active >= 0;
}

void sinistar_check_done(void)
{
    if (voice_active < 0) return;
    voice_frames_left--;
    if (voice_frames_left <= 0) {
        /* Resume music */
        mt_musicmask(CUSTOM_BASE, 0xF);  /* unmask all music channels */
        mt_Enable = music_was_enabled;
        voice_active = -1;
    }
}
