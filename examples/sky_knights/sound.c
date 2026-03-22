/*
 * SKY KNIGHTS - Sound system
 * Direct hardware audio via custom chip registers
 * Channel 0: Flap SFX (loaded from flap.raw)
 * Channel 1: Kill/collision SFX (loaded from smash.raw)
 * Channel 2: Egg collect / misc SFX
 * Channel 3: Background melody
 */
#include <proto/exec.h>
#include <proto/dos.h>
#include <exec/memory.h>
#include <hardware/custom.h>
#include <hardware/dmabits.h>
#include "sound.h"

#define CUSTOM ((volatile struct Custom *)0xDFF000)

/* Synthesized waveform sizes */
#define SINE_LEN     32
#define NOISE_LEN    64
#define SWEEP_LEN    48

/* Chip RAM buffers - synthesized */
static BYTE *sine_wave = NULL;
static BYTE *noise_wave = NULL;
static BYTE *sweep_wave = NULL;

/* Chip RAM buffers - loaded from files */
static BYTE *flap_sample = NULL;
static LONG  flap_len = 0;
static BYTE *smash_sample = NULL;
static LONG  smash_len = 0;

/* Playback period for 11025 Hz samples on PAL Amiga */
#define PERIOD_11KHZ  322   /* 3546895 / 11025 */

/* SFX active timers (frames remaining) */
static WORD sfx_timer[3] = { 0, 0, 0 };

/* Music state */
static WORD music_enabled = 0;
static WORD music_note_idx = 0;
static WORD music_timer = 0;

/* Medieval/fanfare melody - Amiga periods for notes */
#define NOTE_C3  856
#define NOTE_G3  570
#define NOTE_A3  508
#define NOTE_C4  428
#define NOTE_D4  381
#define NOTE_E4  339
#define NOTE_F4  320
#define NOTE_G4  285
#define NOTE_A4  254
#define NOTE_C5  214

static const UWORD melody[] = {
    NOTE_C4, NOTE_E4, NOTE_G4, NOTE_C5,
    NOTE_G4, NOTE_E4, NOTE_C4, NOTE_G3,
    NOTE_A3, NOTE_C4, NOTE_E4, NOTE_G4,
    NOTE_F4, NOTE_E4, NOTE_D4, NOTE_C4,
    NOTE_C4, NOTE_D4, NOTE_E4, NOTE_G4,
    NOTE_A4, NOTE_G4, NOTE_E4, NOTE_C4,
    NOTE_G3, NOTE_A3, NOTE_C4, NOTE_E4,
    NOTE_D4, NOTE_C4, NOTE_G3, NOTE_C4
};
#define MELODY_LEN 32
#define MELODY_TEMPO 9  /* frames per note */

/* Simple RNG for noise */
static ULONG snd_rng = 54321;
static BYTE snd_noise(void)
{
    snd_rng = snd_rng * 1103515245UL + 12345UL;
    return (BYTE)((snd_rng >> 16) & 0xFF);
}

/* Load a raw 8-bit signed PCM file into chip RAM */
static BYTE *load_sample(const char *filename, LONG *out_len)
{
    BPTR fh;
    BYTE *buf = NULL;
    LONG len;

    fh = Open((STRPTR)filename, MODE_OLDFILE);
    if (!fh) return NULL;

    Seek(fh, 0, OFFSET_END);
    len = Seek(fh, 0, OFFSET_BEGINNING);

    /* Paula needs even-length buffers */
    if (len & 1) len--;
    if (len < 2) { Close(fh); return NULL; }

    buf = (BYTE *)AllocMem(len, MEMF_CHIP);
    if (buf) {
        Read(fh, buf, len);
        *out_len = len;
    }

    Close(fh);
    return buf;
}

void sound_init(void)
{
    WORD i;

    /* Disable all audio DMA */
    CUSTOM->dmacon = DMAF_AUD0 | DMAF_AUD1 | DMAF_AUD2 | DMAF_AUD3;

    /* Load sampled sound effects from files */
    flap_sample = load_sample("DH2:Dev/flap.raw", &flap_len);
    smash_sample = load_sample("DH2:Dev/smash.raw", &smash_len);

    /* Allocate synthesized waveforms in chip RAM */
    sine_wave = (BYTE *)AllocMem(SINE_LEN, MEMF_CHIP | MEMF_CLEAR);
    noise_wave = (BYTE *)AllocMem(NOISE_LEN, MEMF_CHIP | MEMF_CLEAR);
    sweep_wave = (BYTE *)AllocMem(SWEEP_LEN, MEMF_CHIP | MEMF_CLEAR);

    if (!sine_wave || !noise_wave || !sweep_wave) return;

    /* Generate sine wave */
    {
        static const BYTE sine_tab[32] = {
            0, 25, 49, 71, 90, 106, 117, 125,
            127, 125, 117, 106, 90, 71, 49, 25,
            0, -25, -49, -71, -90, -106, -117, -125,
            -127, -125, -117, -106, -90, -71, -49, -25
        };
        for (i = 0; i < SINE_LEN; i++) {
            sine_wave[i] = sine_tab[i];
        }
    }

    /* Generate noise */
    for (i = 0; i < NOISE_LEN; i++) {
        noise_wave[i] = snd_noise();
    }

    /* Generate sweep (descending tone for egg pickup) */
    for (i = 0; i < SWEEP_LEN; i++) {
        WORD phase = (i * (SWEEP_LEN - i)) / 4;
        sweep_wave[i] = (BYTE)((phase & 1) ? 60 : -60);
    }

    music_enabled = 1;
    music_note_idx = 0;
    music_timer = 0;
}

void sound_cleanup(void)
{
    /* Disable all audio DMA */
    CUSTOM->dmacon = DMAF_AUD0 | DMAF_AUD1 | DMAF_AUD2 | DMAF_AUD3;

    /* Zero volumes */
    CUSTOM->aud[0].ac_vol = 0;
    CUSTOM->aud[1].ac_vol = 0;
    CUSTOM->aud[2].ac_vol = 0;
    CUSTOM->aud[3].ac_vol = 0;

    /* Free chip RAM */
    if (sine_wave)    FreeMem(sine_wave, SINE_LEN);
    if (noise_wave)   FreeMem(noise_wave, NOISE_LEN);
    if (sweep_wave)   FreeMem(sweep_wave, SWEEP_LEN);
    if (flap_sample)  FreeMem(flap_sample, flap_len);
    if (smash_sample) FreeMem(smash_sample, smash_len);

    sine_wave = NULL;
    noise_wave = NULL;
    sweep_wave = NULL;
    flap_sample = NULL;
    smash_sample = NULL;
}

void sound_flap(void)
{
    if (!flap_sample) return;

    /* Channel 0: sampled wing flap at 11025 Hz */
    CUSTOM->aud[0].ac_ptr = (UWORD *)flap_sample;
    CUSTOM->aud[0].ac_len = flap_len / 2;    /* length in words */
    CUSTOM->aud[0].ac_per = PERIOD_11KHZ;
    CUSTOM->aud[0].ac_vol = 50;
    CUSTOM->dmacon = DMAF_SETCLR | DMAF_AUD0;

    /* Play through once: frames = sample_bytes / (11025/50) */
    sfx_timer[0] = (WORD)(flap_len / 220) + 1;
}

void sound_kill(void)
{
    if (!smash_sample) return;

    /* Channel 1: sampled crunch/splat at 11025 Hz */
    CUSTOM->aud[1].ac_ptr = (UWORD *)smash_sample;
    CUSTOM->aud[1].ac_len = smash_len / 2;
    CUSTOM->aud[1].ac_per = PERIOD_11KHZ;
    CUSTOM->aud[1].ac_vol = 60;
    CUSTOM->dmacon = DMAF_SETCLR | DMAF_AUD1;

    sfx_timer[1] = (WORD)(smash_len / 220) + 1;
}

void sound_egg(void)
{
    if (!sweep_wave) return;

    /* Channel 2: ascending tone */
    CUSTOM->aud[2].ac_ptr = (UWORD *)sweep_wave;
    CUSTOM->aud[2].ac_len = SWEEP_LEN / 2;
    CUSTOM->aud[2].ac_per = 300;
    CUSTOM->aud[2].ac_vol = 40;
    CUSTOM->dmacon = DMAF_SETCLR | DMAF_AUD2;

    sfx_timer[2] = 5;
}

void sound_die(void)
{
    if (!noise_wave) return;

    /* Channel 1: longer noise burst */
    CUSTOM->aud[1].ac_ptr = (UWORD *)noise_wave;
    CUSTOM->aud[1].ac_len = NOISE_LEN / 2;
    CUSTOM->aud[1].ac_per = 350;
    CUSTOM->aud[1].ac_vol = 55;
    CUSTOM->dmacon = DMAF_SETCLR | DMAF_AUD1;

    sfx_timer[1] = 10;
}

void sound_wave(void)
{
    if (!sine_wave) return;

    /* Channel 2: fanfare tone */
    CUSTOM->aud[2].ac_ptr = (UWORD *)sine_wave;
    CUSTOM->aud[2].ac_len = SINE_LEN / 2;
    CUSTOM->aud[2].ac_per = NOTE_C4;
    CUSTOM->aud[2].ac_vol = 45;
    CUSTOM->dmacon = DMAF_SETCLR | DMAF_AUD2;

    sfx_timer[2] = 15;
}

void sound_update(void)
{
    WORD i;

    /* Update SFX timers - disable channel DMA when done */
    for (i = 0; i < 3; i++) {
        if (sfx_timer[i] > 0) {
            sfx_timer[i]--;
            if (sfx_timer[i] == 0) {
                CUSTOM->dmacon = (DMAF_AUD0 << i); /* disable without SETCLR */
                CUSTOM->aud[i].ac_vol = 0;
            }
        }
    }

    /* Update background melody on channel 3 */
    if (music_enabled && sine_wave) {
        music_timer--;
        if (music_timer <= 0) {
            music_timer = MELODY_TEMPO;

            CUSTOM->aud[3].ac_ptr = (UWORD *)sine_wave;
            CUSTOM->aud[3].ac_len = SINE_LEN / 2;
            CUSTOM->aud[3].ac_per = melody[music_note_idx];
            CUSTOM->aud[3].ac_vol = 22;
            CUSTOM->dmacon = DMAF_SETCLR | DMAF_AUD3;

            music_note_idx++;
            if (music_note_idx >= MELODY_LEN) {
                music_note_idx = 0;
            }
        }
    }
}
