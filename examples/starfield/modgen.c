/*
 * modgen.c - Procedural ProTracker MOD generator
 *
 * Generates a recognizable Star Trek TOS-style theme melody
 * as a standard 4-channel ProTracker MOD in chip RAM.
 *
 * MOD format:
 *   20 bytes: title
 *   31 * 30 bytes: sample headers
 *   1 byte: song length (number of positions)
 *   1 byte: restart position
 *   128 bytes: pattern order table
 *   4 bytes: "M.K." tag
 *   N * 1024 bytes: pattern data (64 rows * 4 channels * 4 bytes)
 *   sample data follows
 */

#include <exec/types.h>
#include <exec/memory.h>
#include <proto/exec.h>
#include <string.h>

#include "modgen.h"

/* --- ProTracker period table (octaves 1-3) --- */
/* Tuning 0, notes C-1 to B-3 */
static const UWORD period_table[] = {
    /* C    C#   D    D#   E    F    F#   G    G#   A    A#   B   */
    856, 808, 762, 720, 678, 640, 604, 570, 538, 508, 480, 453, /* oct 1 */
    428, 404, 381, 360, 339, 320, 302, 285, 269, 254, 240, 226, /* oct 2 */
    214, 202, 190, 180, 170, 160, 151, 143, 135, 127, 120, 113, /* oct 3 */
};

/* Note indices */
#define C1  0
#define Cs1 1
#define D1  2
#define Ds1 3
#define E1  4
#define F1  5
#define Fs1 6
#define G1  7
#define Gs1 8
#define A1  9
#define As1 10
#define B1  11
#define C2  12
#define Cs2 13
#define D2  14
#define Ds2 15
#define E2  16
#define F2  17
#define Fs2 18
#define G2  19
#define Gs2 20
#define A2  21
#define As2 22
#define B2  23
#define C3  24
#define Cs3 25
#define D3  26
#define Ds3 27
#define E3  28
#define F3  29
#define Fs3 30
#define G3  31
#define Gs3 32
#define A3  33
#define As3 34
#define B3  35
#define REST 255

/* Write a 4-byte MOD note: sample+period high, period low, effect */
static void write_note(UBYTE *dst, UBYTE sample, UBYTE note_idx, UBYTE effect_cmd, UBYTE effect_val)
{
    UWORD period = 0;
    if (note_idx != REST && note_idx < 36) {
        period = period_table[note_idx];
    }
    /* Byte 0: upper 4 bits of sample | upper 4 bits of period */
    dst[0] = (sample & 0xF0) | ((period >> 8) & 0x0F);
    /* Byte 1: lower 8 bits of period */
    dst[1] = period & 0xFF;
    /* Byte 2: lower 4 bits of sample | upper 4 bits of effect */
    dst[2] = ((sample & 0x0F) << 4) | (effect_cmd & 0x0F);
    /* Byte 3: effect parameter */
    dst[3] = effect_val;
}

/* Generate a simple sine-ish waveform for a sample */
static void gen_sine_sample(BYTE *buf, ULONG len, BYTE vol)
{
    ULONG i;
    /* Simple triangle-ish wave with harmonics */
    for (i = 0; i < len; i++) {
        LONG phase = (i * 256) / len;
        LONG val;
        if (phase < 64)       val = phase * 2;       /* 0..127 */
        else if (phase < 192) val = 256 - phase * 2; /* 127..-127 */
        else                  val = phase * 2 - 512;  /* -127..0 */
        val = (val * vol) / 128;
        if (val > 127) val = 127;
        if (val < -128) val = -128;
        buf[i] = (BYTE)val;
    }
}

/* Generate a bass-like sample */
static void gen_bass_sample(BYTE *buf, ULONG len, BYTE vol)
{
    ULONG i;
    for (i = 0; i < len; i++) {
        LONG phase = (i * 256) / len;
        LONG val;
        /* Square-ish with soft edges */
        if (phase < 32)       val = phase * 4;
        else if (phase < 128) val = 127;
        else if (phase < 160) val = 127 - (phase - 128) * 4;
        else                  val = -128;
        val = (val * vol) / 128;
        buf[i] = (BYTE)val;
    }
}

#define MOD_HEADER_SIZE (20 + 31*30 + 1 + 1 + 128 + 4)
#define PATTERN_SIZE    (64 * 4 * 4)  /* 64 rows, 4 channels, 4 bytes */
#define NUM_PATTERNS    4
#define SAMPLE1_LEN     512   /* melody - must be even */
#define SAMPLE2_LEN     256   /* bass - must be even */
#define SAMPLE3_LEN     128   /* pad/string */
#define TOTAL_SAMPLE_LEN (SAMPLE1_LEN + SAMPLE2_LEN + SAMPLE3_LEN)

/*
 * Star Trek TOS theme - simplified melody:
 *
 * Pattern 0 (intro/main theme):
 *   The famous opening: Bb... F... (held notes with rising feel)
 *   Then the melody: Bb-C-D-Eb-F (heroic ascending)
 *
 * Pattern 1 (continuation):
 *   G-Ab-Bb (continuing upward)
 *   Then descending resolution
 *
 * Pattern 2 (bridge section):
 *   Different melodic movement
 *
 * Pattern 3 (loop back):
 *   Resolution back to tonic
 *
 * Using Bb major / related modes for that Trek feel.
 */

UBYTE *generate_startrek_mod(ULONG *out_size)
{
    ULONG mod_size;
    UBYTE *mod;
    UBYTE *p;
    UBYTE *pat;
    BYTE *samp;
    int i, row;

    mod_size = MOD_HEADER_SIZE + NUM_PATTERNS * PATTERN_SIZE + TOTAL_SAMPLE_LEN;
    mod = (UBYTE *)AllocMem(mod_size, MEMF_CHIP | MEMF_CLEAR);
    if (!mod) return NULL;

    p = mod;

    /* --- Title (20 bytes) --- */
    memcpy(p, "Star Trek Theme\0\0\0\0\0", 20);
    p += 20;

    /* --- Sample 1: melody instrument (30 bytes) --- */
    memcpy(p, "melody\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0", 22); p += 22;
    *p++ = (UBYTE)((SAMPLE1_LEN / 2) >> 8);  /* length in words (high) */
    *p++ = (UBYTE)((SAMPLE1_LEN / 2) & 0xFF); /* length in words (low) */
    *p++ = 0;   /* finetune */
    *p++ = 50;  /* volume */
    *p++ = 0; *p++ = 0; /* repeat offset */
    *p++ = (UBYTE)((SAMPLE1_LEN / 2) >> 8);  /* repeat length (high) - loop whole sample */
    *p++ = (UBYTE)((SAMPLE1_LEN / 2) & 0xFF);

    /* --- Sample 2: bass instrument (30 bytes) --- */
    memcpy(p, "bass\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0", 22); p += 22;
    *p++ = (UBYTE)((SAMPLE2_LEN / 2) >> 8);
    *p++ = (UBYTE)((SAMPLE2_LEN / 2) & 0xFF);
    *p++ = 0;   /* finetune */
    *p++ = 45;  /* volume */
    *p++ = 0; *p++ = 0;
    *p++ = (UBYTE)((SAMPLE2_LEN / 2) >> 8);
    *p++ = (UBYTE)((SAMPLE2_LEN / 2) & 0xFF);

    /* --- Sample 3: string pad (30 bytes) --- */
    memcpy(p, "strings\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0", 22); p += 22;
    *p++ = (UBYTE)((SAMPLE3_LEN / 2) >> 8);
    *p++ = (UBYTE)((SAMPLE3_LEN / 2) & 0xFF);
    *p++ = 0;   /* finetune */
    *p++ = 40;  /* volume */
    *p++ = 0; *p++ = 0;
    *p++ = (UBYTE)((SAMPLE3_LEN / 2) >> 8);
    *p++ = (UBYTE)((SAMPLE3_LEN / 2) & 0xFF);

    /* --- Samples 4-31: empty (28 * 30 bytes) --- */
    for (i = 3; i < 31; i++) {
        memset(p, 0, 30); p += 30;
    }

    /* --- Song length and restart --- */
    *p++ = NUM_PATTERNS; /* song length */
    *p++ = 0;            /* restart position */

    /* --- Pattern order table (128 bytes) --- */
    p[0] = 0; p[1] = 1; p[2] = 2; p[3] = 3;
    /* Rest are already 0 from MEMF_CLEAR */
    p += 128;

    /* --- M.K. tag --- */
    memcpy(p, "M.K.", 4);
    p += 4;

    /* === Pattern data === */
    /* Channel 0: melody (sample 1)
     * Channel 1: bass (sample 2)
     * Channel 2: harmony/pad (sample 3)
     * Channel 3: counter-melody (sample 1)
     *
     * Set tempo: effect F, value $80+ = BPM, value <$20 = speed
     */

    /* --- Pattern 0: Opening theme --- */
    pat = p;
    /* Row 0: Set tempo to 100 BPM, speed 6 */
    write_note(pat + 0*4*4 + 0, 1, As1, 0xF, 100); /* ch0: Bb2, tempo 100 */
    write_note(pat + 0*4*4 + 4, 2, As1, 0xF, 6);   /* ch1: Bb bass, speed 6 */
    write_note(pat + 0*4*4 + 8, 3, F1, 0, 0);       /* ch2: F pad */
    write_note(pat + 0*4*4 + 12, 0, REST, 0, 0);

    /* Rows 1-7: held Bb (no new notes - sustain via loop) */
    for (row = 1; row < 8; row++) {
        write_note(pat + row*4*4 + 0, 0, REST, 0, 0);
        write_note(pat + row*4*4 + 4, 0, REST, 0, 0);
        write_note(pat + row*4*4 + 8, 0, REST, 0, 0);
        write_note(pat + row*4*4 + 12, 0, REST, 0, 0);
    }

    /* Row 8: F note (the answering phrase) */
    write_note(pat + 8*4*4 + 0, 1, F2, 0, 0);       /* ch0: F */
    write_note(pat + 8*4*4 + 4, 2, F1, 0, 0);        /* ch1: F bass */
    write_note(pat + 8*4*4 + 8, 3, C2, 0, 0);        /* ch2: C pad */
    write_note(pat + 8*4*4 + 12, 0, REST, 0, 0);

    /* Rows 9-15: sustained */
    for (row = 9; row < 16; row++) {
        write_note(pat + row*4*4 + 0, 0, REST, 0, 0);
        write_note(pat + row*4*4 + 4, 0, REST, 0, 0);
        write_note(pat + row*4*4 + 8, 0, REST, 0, 0);
        write_note(pat + row*4*4 + 12, 0, REST, 0, 0);
    }

    /* Row 16: Start the ascending melody Bb-C-D */
    write_note(pat + 16*4*4 + 0, 1, As1, 0, 0);
    write_note(pat + 16*4*4 + 4, 2, As1, 0, 0);
    write_note(pat + 16*4*4 + 8, 3, F1, 0, 0);
    write_note(pat + 16*4*4 + 12, 1, D2, 0, 0);      /* counter-melody */

    for (row = 17; row < 20; row++) {
        write_note(pat + row*4*4 + 0, 0, REST, 0, 0);
        write_note(pat + row*4*4 + 4, 0, REST, 0, 0);
        write_note(pat + row*4*4 + 8, 0, REST, 0, 0);
        write_note(pat + row*4*4 + 12, 0, REST, 0, 0);
    }

    /* Row 20: C */
    write_note(pat + 20*4*4 + 0, 1, C2, 0, 0);
    write_note(pat + 20*4*4 + 4, 0, REST, 0, 0);
    write_note(pat + 20*4*4 + 8, 0, REST, 0, 0);
    write_note(pat + 20*4*4 + 12, 0, REST, 0, 0);

    for (row = 21; row < 24; row++) {
        write_note(pat + row*4*4 + 0, 0, REST, 0, 0);
        write_note(pat + row*4*4 + 4, 0, REST, 0, 0);
        write_note(pat + row*4*4 + 8, 0, REST, 0, 0);
        write_note(pat + row*4*4 + 12, 0, REST, 0, 0);
    }

    /* Row 24: D */
    write_note(pat + 24*4*4 + 0, 1, D2, 0, 0);
    write_note(pat + 24*4*4 + 4, 2, As1, 0, 0);
    write_note(pat + 24*4*4 + 8, 3, As1, 0, 0);
    write_note(pat + 24*4*4 + 12, 1, F2, 0, 0);

    for (row = 25; row < 28; row++) {
        write_note(pat + row*4*4 + 0, 0, REST, 0, 0);
        write_note(pat + row*4*4 + 4, 0, REST, 0, 0);
        write_note(pat + row*4*4 + 8, 0, REST, 0, 0);
        write_note(pat + row*4*4 + 12, 0, REST, 0, 0);
    }

    /* Row 28: Eb */
    write_note(pat + 28*4*4 + 0, 1, Ds2, 0, 0);
    write_note(pat + 28*4*4 + 4, 2, Ds1, 0, 0);
    write_note(pat + 28*4*4 + 8, 3, G1, 0, 0);
    write_note(pat + 28*4*4 + 12, 0, REST, 0, 0);

    for (row = 29; row < 32; row++) {
        write_note(pat + row*4*4 + 0, 0, REST, 0, 0);
        write_note(pat + row*4*4 + 4, 0, REST, 0, 0);
        write_note(pat + row*4*4 + 8, 0, REST, 0, 0);
        write_note(pat + row*4*4 + 12, 0, REST, 0, 0);
    }

    /* Row 32: F (climax of first phrase) */
    write_note(pat + 32*4*4 + 0, 1, F2, 0, 0);
    write_note(pat + 32*4*4 + 4, 2, F1, 0, 0);
    write_note(pat + 32*4*4 + 8, 3, A1, 0, 0);
    write_note(pat + 32*4*4 + 12, 1, C2, 0, 0);

    /* Rows 33-63: sustained + silence */
    for (row = 33; row < 64; row++) {
        write_note(pat + row*4*4 + 0, 0, REST, 0, 0);
        write_note(pat + row*4*4 + 4, 0, REST, 0, 0);
        write_note(pat + row*4*4 + 8, 0, REST, 0, 0);
        write_note(pat + row*4*4 + 12, 0, REST, 0, 0);
    }

    p += PATTERN_SIZE;

    /* --- Pattern 1: Continuation --- */
    pat = p;
    /* Row 0: G */
    write_note(pat + 0*4*4 + 0, 1, G2, 0, 0);
    write_note(pat + 0*4*4 + 4, 2, G1, 0, 0);
    write_note(pat + 0*4*4 + 8, 3, D2, 0, 0);
    write_note(pat + 0*4*4 + 12, 1, B1, 0, 0);

    for (row = 1; row < 8; row++) {
        write_note(pat + row*4*4 + 0, 0, REST, 0, 0);
        write_note(pat + row*4*4 + 4, 0, REST, 0, 0);
        write_note(pat + row*4*4 + 8, 0, REST, 0, 0);
        write_note(pat + row*4*4 + 12, 0, REST, 0, 0);
    }

    /* Row 8: Ab */
    write_note(pat + 8*4*4 + 0, 1, Gs2, 0, 0);
    write_note(pat + 8*4*4 + 4, 2, Gs1, 0, 0);
    write_note(pat + 8*4*4 + 8, 3, Ds2, 0, 0);
    write_note(pat + 8*4*4 + 12, 0, REST, 0, 0);

    for (row = 9; row < 16; row++) {
        write_note(pat + row*4*4 + 0, 0, REST, 0, 0);
        write_note(pat + row*4*4 + 4, 0, REST, 0, 0);
        write_note(pat + row*4*4 + 8, 0, REST, 0, 0);
        write_note(pat + row*4*4 + 12, 0, REST, 0, 0);
    }

    /* Row 16: Bb high (climax) */
    write_note(pat + 16*4*4 + 0, 1, As2, 0, 0);
    write_note(pat + 16*4*4 + 4, 2, As1, 0, 0);
    write_note(pat + 16*4*4 + 8, 3, F2, 0, 0);
    write_note(pat + 16*4*4 + 12, 1, D2, 0, 0);

    for (row = 17; row < 32; row++) {
        write_note(pat + row*4*4 + 0, 0, REST, 0, 0);
        write_note(pat + row*4*4 + 4, 0, REST, 0, 0);
        write_note(pat + row*4*4 + 8, 0, REST, 0, 0);
        write_note(pat + row*4*4 + 12, 0, REST, 0, 0);
    }

    /* Row 32: Descending - F */
    write_note(pat + 32*4*4 + 0, 1, F2, 0, 0);
    write_note(pat + 32*4*4 + 4, 2, F1, 0, 0);
    write_note(pat + 32*4*4 + 8, 3, C2, 0, 0);
    write_note(pat + 32*4*4 + 12, 0, REST, 0, 0);

    for (row = 33; row < 40; row++) {
        write_note(pat + row*4*4 + 0, 0, REST, 0, 0);
        write_note(pat + row*4*4 + 4, 0, REST, 0, 0);
        write_note(pat + row*4*4 + 8, 0, REST, 0, 0);
        write_note(pat + row*4*4 + 12, 0, REST, 0, 0);
    }

    /* Row 40: Eb */
    write_note(pat + 40*4*4 + 0, 1, Ds2, 0, 0);
    write_note(pat + 40*4*4 + 4, 2, Ds1, 0, 0);
    write_note(pat + 40*4*4 + 8, 3, As1, 0, 0);
    write_note(pat + 40*4*4 + 12, 1, G1, 0, 0);

    for (row = 41; row < 48; row++) {
        write_note(pat + row*4*4 + 0, 0, REST, 0, 0);
        write_note(pat + row*4*4 + 4, 0, REST, 0, 0);
        write_note(pat + row*4*4 + 8, 0, REST, 0, 0);
        write_note(pat + row*4*4 + 12, 0, REST, 0, 0);
    }

    /* Row 48: D resolving */
    write_note(pat + 48*4*4 + 0, 1, D2, 0, 0);
    write_note(pat + 48*4*4 + 4, 2, As1, 0, 0);
    write_note(pat + 48*4*4 + 8, 3, F1, 0, 0);
    write_note(pat + 48*4*4 + 12, 0, REST, 0, 0);

    for (row = 49; row < 64; row++) {
        write_note(pat + row*4*4 + 0, 0, REST, 0, 0);
        write_note(pat + row*4*4 + 4, 0, REST, 0, 0);
        write_note(pat + row*4*4 + 8, 0, REST, 0, 0);
        write_note(pat + row*4*4 + 12, 0, REST, 0, 0);
    }

    p += PATTERN_SIZE;

    /* --- Pattern 2: Bridge section --- */
    pat = p;
    write_note(pat + 0*4*4 + 0, 1, Ds2, 0, 0);
    write_note(pat + 0*4*4 + 4, 2, Ds1, 0, 0);
    write_note(pat + 0*4*4 + 8, 3, G1, 0, 0);
    write_note(pat + 0*4*4 + 12, 1, As1, 0, 0);

    for (row = 1; row < 8; row++) {
        write_note(pat + row*4*4 + 0, 0, REST, 0, 0);
        write_note(pat + row*4*4 + 4, 0, REST, 0, 0);
        write_note(pat + row*4*4 + 8, 0, REST, 0, 0);
        write_note(pat + row*4*4 + 12, 0, REST, 0, 0);
    }

    write_note(pat + 8*4*4 + 0, 1, D2, 0, 0);
    write_note(pat + 8*4*4 + 4, 0, REST, 0, 0);
    write_note(pat + 8*4*4 + 8, 3, F1, 0, 0);
    write_note(pat + 8*4*4 + 12, 0, REST, 0, 0);

    for (row = 9; row < 16; row++) {
        write_note(pat + row*4*4 + 0, 0, REST, 0, 0);
        write_note(pat + row*4*4 + 4, 0, REST, 0, 0);
        write_note(pat + row*4*4 + 8, 0, REST, 0, 0);
        write_note(pat + row*4*4 + 12, 0, REST, 0, 0);
    }

    write_note(pat + 16*4*4 + 0, 1, C2, 0, 0);
    write_note(pat + 16*4*4 + 4, 2, C1, 0, 0);
    write_note(pat + 16*4*4 + 8, 3, E1, 0, 0);
    write_note(pat + 16*4*4 + 12, 1, G1, 0, 0);

    for (row = 17; row < 24; row++) {
        write_note(pat + row*4*4 + 0, 0, REST, 0, 0);
        write_note(pat + row*4*4 + 4, 0, REST, 0, 0);
        write_note(pat + row*4*4 + 8, 0, REST, 0, 0);
        write_note(pat + row*4*4 + 12, 0, REST, 0, 0);
    }

    write_note(pat + 24*4*4 + 0, 1, As1, 0, 0);
    write_note(pat + 24*4*4 + 4, 2, F1, 0, 0);
    write_note(pat + 24*4*4 + 8, 3, D1, 0, 0);
    write_note(pat + 24*4*4 + 12, 0, REST, 0, 0);

    for (row = 25; row < 48; row++) {
        write_note(pat + row*4*4 + 0, 0, REST, 0, 0);
        write_note(pat + row*4*4 + 4, 0, REST, 0, 0);
        write_note(pat + row*4*4 + 8, 0, REST, 0, 0);
        write_note(pat + row*4*4 + 12, 0, REST, 0, 0);
    }

    write_note(pat + 48*4*4 + 0, 1, F2, 0, 0);
    write_note(pat + 48*4*4 + 4, 2, F1, 0, 0);
    write_note(pat + 48*4*4 + 8, 3, A1, 0, 0);
    write_note(pat + 48*4*4 + 12, 1, C2, 0, 0);

    for (row = 49; row < 64; row++) {
        write_note(pat + row*4*4 + 0, 0, REST, 0, 0);
        write_note(pat + row*4*4 + 4, 0, REST, 0, 0);
        write_note(pat + row*4*4 + 8, 0, REST, 0, 0);
        write_note(pat + row*4*4 + 12, 0, REST, 0, 0);
    }

    p += PATTERN_SIZE;

    /* --- Pattern 3: Resolution / loop --- */
    pat = p;
    write_note(pat + 0*4*4 + 0, 1, As2, 0, 0);   /* Bb high - triumphant */
    write_note(pat + 0*4*4 + 4, 2, As1, 0, 0);
    write_note(pat + 0*4*4 + 8, 3, D2, 0, 0);
    write_note(pat + 0*4*4 + 12, 1, F2, 0, 0);

    for (row = 1; row < 16; row++) {
        write_note(pat + row*4*4 + 0, 0, REST, 0, 0);
        write_note(pat + row*4*4 + 4, 0, REST, 0, 0);
        write_note(pat + row*4*4 + 8, 0, REST, 0, 0);
        write_note(pat + row*4*4 + 12, 0, REST, 0, 0);
    }

    write_note(pat + 16*4*4 + 0, 1, F2, 0, 0);
    write_note(pat + 16*4*4 + 4, 2, F1, 0, 0);
    write_note(pat + 16*4*4 + 8, 3, A1, 0, 0);
    write_note(pat + 16*4*4 + 12, 0, REST, 0, 0);

    for (row = 17; row < 32; row++) {
        write_note(pat + row*4*4 + 0, 0, REST, 0, 0);
        write_note(pat + row*4*4 + 4, 0, REST, 0, 0);
        write_note(pat + row*4*4 + 8, 0, REST, 0, 0);
        write_note(pat + row*4*4 + 12, 0, REST, 0, 0);
    }

    write_note(pat + 32*4*4 + 0, 1, As1, 0, 0);  /* Final Bb resolution */
    write_note(pat + 32*4*4 + 4, 2, As1, 0, 0);
    write_note(pat + 32*4*4 + 8, 3, F1, 0, 0);
    write_note(pat + 32*4*4 + 12, 1, D2, 0, 0);

    for (row = 33; row < 64; row++) {
        write_note(pat + row*4*4 + 0, 0, REST, 0, 0);
        write_note(pat + row*4*4 + 4, 0, REST, 0, 0);
        write_note(pat + row*4*4 + 8, 0, REST, 0, 0);
        write_note(pat + row*4*4 + 12, 0, REST, 0, 0);
    }

    p += PATTERN_SIZE;

    /* === Sample data === */
    samp = (BYTE *)p;
    gen_sine_sample(samp, SAMPLE1_LEN, 100);
    samp += SAMPLE1_LEN;
    gen_bass_sample(samp, SAMPLE2_LEN, 90);
    samp += SAMPLE2_LEN;
    gen_sine_sample(samp, SAMPLE3_LEN, 70);

    *out_size = mod_size;
    return mod;
}
