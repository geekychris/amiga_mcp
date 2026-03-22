/*
 * Jump Quest - Sound System
 * Bouncy chiptune music via direct Paula + procedural SFX
 *
 * 4 channels: 0-1 for music, 2-3 for SFX
 */
#include "game.h"
#include <string.h>

/* Paula audio hardware registers */
#define CUSTOM_BASE     0xDFF000
#define AUD0            ((volatile UWORD *)(CUSTOM_BASE + 0x0A0))
#define AUD1            ((volatile UWORD *)(CUSTOM_BASE + 0x0B0))
#define AUD2            ((volatile UWORD *)(CUSTOM_BASE + 0x0C0))
#define AUD3            ((volatile UWORD *)(CUSTOM_BASE + 0x0D0))
#define DMACON          (*(volatile UWORD *)(CUSTOM_BASE + 0x096))
#define INTENA          (*(volatile UWORD *)(CUSTOM_BASE + 0x09A))

/* Per-channel register offsets (in UWORD units) */
#define AUDxLCH  0   /* pointer high */
#define AUDxLCL  1   /* pointer low */
#define AUDxLEN  2   /* length in words */
#define AUDxPER  3   /* period */
#define AUDxVOL  4   /* volume 0-64 */

/* Note periods for PAL Amiga (octave 2-3) */
static const UWORD note_periods[] = {
    0,      /* 0: rest */
    856,    /* 1: C-2 */
    808,    /* 2: C#2 */
    762,    /* 3: D-2 */
    720,    /* 4: D#2 */
    678,    /* 5: E-2 */
    640,    /* 6: F-2 */
    604,    /* 7: F#2 */
    570,    /* 8: G-2 */
    538,    /* 9: G#2 */
    508,    /* 10: A-2 */
    480,    /* 11: A#2 */
    453,    /* 12: B-2 */
    428,    /* 13: C-3 */
    404,    /* 14: C#3 */
    381,    /* 15: D-3 */
    360,    /* 16: D#3 */
    339,    /* 17: E-3 */
    320,    /* 18: F-3 */
    302,    /* 19: F#3 */
    285,    /* 20: G-3 */
    269,    /* 21: G#3 */
    254,    /* 22: A-3 */
    240,    /* 23: A#3 */
    226,    /* 24: B-3 */
    214,    /* 25: C-4 */
    202,    /* 26: C#4 */
    190,    /* 27: D-4 */
    180,    /* 28: D#4 */
    170,    /* 29: E-4 */
    160,    /* 30: F-4 */
    151,    /* 31: F#4 */
    143,    /* 32: G-4 */
    135,    /* 33: G#4 */
    127,    /* 34: A-4 */
};

/* Waveform samples in chip RAM */
static BYTE *wave_square = NULL;
static BYTE *wave_bass = NULL;
static BYTE *wave_noise = NULL;
static BYTE *sfx_buf = NULL;

#define WAVE_LEN    32   /* samples per waveform cycle */
#define SFX_LEN     512  /* SFX buffer in bytes */

/* Music data: note number per tick, 0=rest */
/* Pattern: bouncy platformer theme */
#define PATTERN_LEN  64
#define NUM_PATTERNS  4
#define SONG_LEN      8

/* Melody (channel 0) - square wave */
static const UBYTE melody[NUM_PATTERNS][PATTERN_LEN] = {
    /* Pattern 0: Main theme - bouncy */
    { 13, 0,17, 0,20, 0,25, 0, 20, 0,17, 0,13, 0, 0, 0,
      15, 0,18, 0,22, 0,27, 0, 22, 0,18, 0,15, 0, 0, 0,
      17, 0,20, 0,25, 0,29, 0, 25, 0,20, 0,17, 0, 0, 0,
      13, 0,17, 0,20, 0,25, 0, 20, 0,17, 0,13, 0, 0, 0 },
    /* Pattern 1: Variation */
    { 25, 0,22, 0,20, 0,17, 0, 13, 0,17, 0,20, 0, 0, 0,
      27, 0,25, 0,22, 0,20, 0, 17, 0,20, 0,22, 0, 0, 0,
      29, 0,25, 0,22, 0,20, 0, 17, 0,13, 0,17, 0, 0, 0,
      25, 0,22, 0,20, 0,17, 0, 20, 0,22, 0,25, 0, 0, 0 },
    /* Pattern 2: Bridge */
    { 20, 0, 0,20, 0, 0,22, 0, 0,22, 0, 0,25, 0, 0, 0,
      20, 0, 0,20, 0, 0,22, 0, 0,22, 0, 0,27, 0, 0, 0,
      17, 0, 0,17, 0, 0,20, 0, 0,20, 0, 0,22, 0, 0, 0,
      17, 0, 0,17, 0, 0,20, 0, 0,22, 0, 0,25, 0, 0, 0 },
    /* Pattern 3: Repeat of main */
    { 13, 0,17, 0,20, 0,25, 0, 20, 0,17, 0,13, 0, 0, 0,
      15, 0,18, 0,22, 0,27, 0, 22, 0,18, 0,15, 0, 0, 0,
      13, 0,17, 0,20, 0,25, 0, 29, 0,25, 0,20, 0,17, 0,
      13, 0, 0, 0,17, 0, 0, 0, 20, 0, 0, 0,25, 0, 0, 0 },
};

/* Bass line (channel 1) */
static const UBYTE bassline[NUM_PATTERNS][PATTERN_LEN] = {
    { 1, 0, 0, 0, 1, 0, 0, 0, 5, 0, 0, 0, 5, 0, 0, 0,
      3, 0, 0, 0, 3, 0, 0, 0, 6, 0, 0, 0, 6, 0, 0, 0,
      5, 0, 0, 0, 5, 0, 0, 0, 8, 0, 0, 0, 8, 0, 0, 0,
      1, 0, 0, 0, 1, 0, 0, 0, 5, 0, 0, 0, 5, 0, 0, 0 },
    { 1, 0, 0, 0, 5, 0, 0, 0, 1, 0, 0, 0, 5, 0, 0, 0,
      3, 0, 0, 0, 6, 0, 0, 0, 3, 0, 0, 0, 6, 0, 0, 0,
      5, 0, 0, 0, 8, 0, 0, 0, 5, 0, 0, 0, 8, 0, 0, 0,
      1, 0, 0, 0, 5, 0, 0, 0, 8, 0, 0, 0, 5, 0, 0, 0 },
    { 8, 0, 0, 0, 8, 0, 0, 0, 10, 0, 0, 0, 10, 0, 0, 0,
      8, 0, 0, 0, 8, 0, 0, 0, 10, 0, 0, 0, 12, 0, 0, 0,
      5, 0, 0, 0, 5, 0, 0, 0, 8, 0, 0, 0, 8, 0, 0, 0,
      5, 0, 0, 0, 5, 0, 0, 0, 8, 0, 0, 0, 10, 0, 0, 0 },
    { 1, 0, 0, 0, 1, 0, 0, 0, 5, 0, 0, 0, 5, 0, 0, 0,
      3, 0, 0, 0, 3, 0, 0, 0, 6, 0, 0, 0, 6, 0, 0, 0,
      1, 0, 0, 0, 1, 0, 0, 0, 5, 0, 0, 0, 8, 0, 0, 0,
      1, 0, 0, 0, 5, 0, 0, 0, 8, 0, 0, 0,12, 0, 0, 0 },
};

/* Song order */
static const UBYTE song_order[SONG_LEN] = { 0, 0, 1, 2, 0, 1, 2, 3 };

/* Music state */
static int music_playing = 0;
static int song_pos = 0;
static int pattern_row = 0;
static int tick_count = 0;
static int speed = 3;  /* ticks per row */

/* RNG for noise */
static ULONG rng_state = 12345;
static ULONG rng_next(void) {
    rng_state = rng_state * 1103515245 + 12345;
    return (rng_state >> 16) & 0x7FFF;
}

static void paula_set(volatile UWORD *base, APTR ptr, UWORD len, UWORD per, UWORD vol) {
    ULONG addr = (ULONG)ptr;
    base[AUDxLCH] = (UWORD)(addr >> 16);
    base[AUDxLCL] = (UWORD)(addr & 0xFFFF);
    base[AUDxLEN] = len;
    base[AUDxPER] = per;
    base[AUDxVOL] = vol;
}

int sound_init(void) {
    int i;

    /* Allocate waveforms in chip RAM */
    wave_square = (BYTE *)AllocMem(WAVE_LEN, MEMF_CHIP | MEMF_CLEAR);
    wave_bass   = (BYTE *)AllocMem(WAVE_LEN, MEMF_CHIP | MEMF_CLEAR);
    wave_noise  = (BYTE *)AllocMem(256, MEMF_CHIP | MEMF_CLEAR);
    sfx_buf     = (BYTE *)AllocMem(SFX_LEN, MEMF_CHIP | MEMF_CLEAR);

    if (!wave_square || !wave_bass || !wave_noise || !sfx_buf) {
        sound_cleanup();
        return 0;
    }

    /* Generate square wave */
    for (i = 0; i < WAVE_LEN; i++) {
        wave_square[i] = (i < WAVE_LEN / 2) ? 64 : -64;
    }

    /* Generate bass (triangle-ish) */
    for (i = 0; i < WAVE_LEN; i++) {
        if (i < WAVE_LEN / 4)
            wave_bass[i] = (BYTE)(i * 8);
        else if (i < 3 * WAVE_LEN / 4)
            wave_bass[i] = (BYTE)(127 - (i - WAVE_LEN / 4) * 8);
        else
            wave_bass[i] = (BYTE)(-127 + (i - 3 * WAVE_LEN / 4) * 8);
    }

    /* Generate noise */
    for (i = 0; i < 256; i++) {
        wave_noise[i] = (BYTE)((rng_next() & 0xFF) - 128);
    }

    /* Stop all audio DMA */
    DMACON = 0x000F;

    return 1;
}

void sound_cleanup(void) {
    /* Stop audio DMA */
    DMACON = 0x000F;

    if (wave_square) { FreeMem(wave_square, WAVE_LEN); wave_square = NULL; }
    if (wave_bass)   { FreeMem(wave_bass, WAVE_LEN); wave_bass = NULL; }
    if (wave_noise)  { FreeMem(wave_noise, 256); wave_noise = NULL; }
    if (sfx_buf)     { FreeMem(sfx_buf, SFX_LEN); sfx_buf = NULL; }
}

void sound_music_start(void) {
    song_pos = 0;
    pattern_row = 0;
    tick_count = 0;
    music_playing = 1;

    /* Enable audio DMA for channels 0 and 1 */
    DMACON = 0x8003;
}

void sound_music_stop(void) {
    music_playing = 0;
    DMACON = 0x000F;  /* Stop all channels */
}

void sound_music_tick(void) {
    int pat;
    UBYTE note;

    if (!music_playing) return;

    tick_count++;
    if (tick_count < speed) return;
    tick_count = 0;

    pat = song_order[song_pos];

    /* Channel 0: Melody (square wave) */
    note = melody[pat][pattern_row];
    if (note > 0 && note < 35) {
        paula_set(AUD0, wave_square, WAVE_LEN / 2, note_periods[note], 40);
        DMACON = 0x8001;
    } else if (note == 0) {
        AUD0[AUDxVOL] = 0;
    }

    /* Channel 1: Bass */
    note = bassline[pat][pattern_row];
    if (note > 0 && note < 35) {
        paula_set(AUD1, wave_bass, WAVE_LEN / 2, note_periods[note], 48);
        DMACON = 0x8002;
    } else if (note == 0) {
        AUD1[AUDxVOL] = 0;
    }

    /* Advance row */
    pattern_row++;
    if (pattern_row >= PATTERN_LEN) {
        pattern_row = 0;
        song_pos++;
        if (song_pos >= SONG_LEN) song_pos = 0;
    }
}

/* SFX: play a quick sound on channel 2 or 3 */
static void play_sfx(int channel, BYTE *data, int len, int period, int vol) {
    volatile UWORD *base = (channel == 2) ? AUD2 : AUD3;
    UWORD dma_bit = (channel == 2) ? 0x0004 : 0x0008;

    /* Stop channel, set new data, restart */
    DMACON = dma_bit;
    paula_set(base, data, (UWORD)(len / 2), (UWORD)period, (UWORD)vol);
    DMACON = 0x8000 | dma_bit;
}

void sound_jump(void) {
    int i;
    /* Rising pitch blip */
    for (i = 0; i < 128; i++) {
        int pitch = 8 - (i * 6 / 128);
        if (pitch < 2) pitch = 2;
        sfx_buf[i] = ((i / pitch) & 1) ? (BYTE)(60 * (128 - i) / 128) : (BYTE)(-60 * (128 - i) / 128);
    }
    play_sfx(2, sfx_buf, 128, 200, 50);
}

void sound_stomp(void) {
    int i;
    /* Impact noise + pitch down */
    for (i = 0; i < 256; i++) {
        if (i < 32) {
            sfx_buf[i] = (BYTE)((rng_next() & 0x7F) - 64);
        } else {
            int pitch = 4 + i / 32;
            sfx_buf[i] = ((i / pitch) & 1) ? 48 : -48;
            sfx_buf[i] = (BYTE)(sfx_buf[i] * (256 - i) / 256);
        }
    }
    play_sfx(2, sfx_buf, 256, 180, 56);
}

void sound_collect(void) {
    int i;
    /* Quick sparkle - rising arpeggio */
    for (i = 0; i < 96; i++) {
        int pitch = 6 - (i / 32);
        if (pitch < 2) pitch = 2;
        sfx_buf[i] = ((i / pitch) & 1) ? 48 : -48;
    }
    play_sfx(3, sfx_buf, 96, 160, 44);
}

void sound_powerup(void) {
    int i;
    /* Rising fanfare */
    for (i = 0; i < 256; i++) {
        int phase = i / 64;
        int pitch = 8 - phase * 2;
        if (pitch < 2) pitch = 2;
        sfx_buf[i] = ((i / pitch) & 1) ? 56 : -56;
    }
    play_sfx(2, sfx_buf, 256, 150, 52);
}

void sound_hurt(void) {
    int i;
    /* Descending buzz */
    for (i = 0; i < 200; i++) {
        int pitch = 2 + i / 25;
        sfx_buf[i] = ((i / pitch) & 1) ? 50 : -50;
        sfx_buf[i] = (BYTE)(sfx_buf[i] * (200 - i) / 200);
    }
    play_sfx(2, sfx_buf, 200, 220, 56);
}

void sound_die(void) {
    int i;
    /* Sad descending tone */
    for (i = 0; i < 512; i++) {
        int pitch = 3 + i / 64;
        sfx_buf[i] = ((i / pitch) & 1) ? 48 : -48;
        sfx_buf[i] = (BYTE)(sfx_buf[i] * (512 - i) / 512);
    }
    play_sfx(2, sfx_buf, 512, 250, 60);
}

void sound_levelwin(void) {
    int i;
    /* Victory fanfare - ascending notes */
    for (i = 0; i < 512; i++) {
        int phase = i / 128;
        int pitch = 8 - phase * 2;
        if (pitch < 2) pitch = 2;
        sfx_buf[i] = ((i / pitch) & 1) ? 60 : -60;
    }
    play_sfx(2, sfx_buf, 512, 140, 56);
}
