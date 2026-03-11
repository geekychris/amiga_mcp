/*
 * Minimal MOD-style music player with heavy metal track generator.
 * Uses direct Paula hardware access. Call modplay_tick() each VBlank.
 * Channel 3 can be temporarily stolen for SFX.
 */

#include <exec/types.h>
#include <exec/memory.h>
#include <hardware/custom.h>
#include <hardware/dmabits.h>
#include <proto/exec.h>

#include "modplay.h"
#include "bridge_client.h"

extern struct Custom custom;

/* ---- Configuration ---- */
#define NUM_SAMPLES   6
#define NUM_PATTERNS  4
#define ROWS_PER_PAT  64
#define NUM_CHANNELS  4
#define SONG_LENGTH   8
#define SPEED_DEFAULT 3  /* ticks per row - fast for metal */

/* ---- Sample sizes (bytes, must be even) ---- */
#define SZ_GUITAR   128
#define SZ_PMUTE     64
#define SZ_BASS     256
#define SZ_KICK     256
#define SZ_SNARE    128
#define SZ_HIHAT     64
#define SZ_SILENCE    4

#define TOTAL_CHIP (SZ_GUITAR + SZ_PMUTE + SZ_BASS + SZ_KICK + SZ_SNARE + SZ_HIHAT + SZ_SILENCE)

/* ---- Note indices into period table ---- */
#define N__ 0xFF
#define C_1 0
#define D_1 2
#define E_1 4
#define F_1 5
#define Fs1 6
#define G_1 7
#define A_1 9
#define B_1 11
#define C_2 12
#define Cs2 13
#define D_2 14
#define E_2 16
#define F_2 17
#define Fs2 18
#define G_2 19
#define A_2 21
#define B_2 23
#define C_3 24

/* Period table (finetune 0) */
static const UWORD periods[37] = {
    856,808,762,720,678,640,604,570,538,508,480,453,
    428,404,381,360,339,320,302,285,269,254,240,226,
    214,202,190,180,170,160,151,143,135,127,120,113,
    0
};

/* ---- Sample definitions ---- */
enum { S_GUITAR=1, S_PMUTE, S_BASS, S_KICK, S_SNARE, S_HIHAT };

typedef struct {
    UWORD length;      /* in words */
    UBYTE volume;
    UBYTE loops;       /* 1 = loops entire sample, 0 = one-shot */
} SmpInfo;

/* ---- Pattern note (compact) ---- */
typedef struct {
    UBYTE smp;    /* sample 1-6, 0 = no change */
    UBYTE note;   /* period table index, N__ = none */
    UBYTE vol;    /* 0xFF = no change, else 0-64 */
} PNote;

/* ---- Player state ---- */
static struct {
    int playing;
    int speed;
    int tick;
    int row;
    int position;

    SmpInfo smp[NUM_SAMPLES];
    BYTE *smp_ptr[NUM_SAMPLES];   /* pointers into chip RAM */
    BYTE *silence;                 /* 4 bytes of silence */

    PNote pat[NUM_PATTERNS][ROWS_PER_PAT][NUM_CHANNELS];
    UBYTE order[SONG_LENGTH];

    struct {
        BYTE *ptr;
        UWORD length;
        UWORD period;
        UWORD volume;
        int loops;
        int need_loop;   /* set loop params on next tick */
    } ch[NUM_CHANNELS];

    int sfx_active;
    int sfx_frames;

    BYTE *chip_block;
} mp;

/* ---- PRNG for noise samples ---- */
static ULONG nrng = 0xDEADBEEF;
static BYTE nrand(void) {
    nrng ^= nrng << 13;
    nrng ^= nrng >> 17;
    nrng ^= nrng << 5;
    return (BYTE)(nrng & 0xFF);
}

/* ---- Sample generators ---- */

static void gen_guitar(BYTE *b, int len)
{
    int i, half = len / 2;
    for (i = 0; i < len; i++) {
        /* Sawtooth with hard clipping = heavy distortion */
        int saw = (i % half) * 255 / half - 128;
        saw = saw * 3;
        if (saw > 100) saw = 100;
        if (saw < -100) saw = -100;
        /* Grit */
        if ((i & 3) < 2) saw += 20; else saw -= 20;
        if (saw > 127) saw = 127;
        if (saw < -127) saw = -127;
        b[i] = (BYTE)saw;
    }
}

static void gen_pmute(BYTE *b, int len)
{
    int i;
    for (i = 0; i < len - 2; i++) {
        int amp = 120 - i * 120 / len;
        b[i] = (BYTE)(((i & 7) < 4) ? amp : -amp);
    }
    b[len-2] = 0; b[len-1] = 0;
}

static void gen_bass(BYTE *b, int len)
{
    int i, half = len / 2, q = len / 8;
    for (i = 0; i < len; i++) {
        int p = i % half, v;
        if (p < q) v = p * 100 / q;
        else if (p < 3*q) v = 100 - (p - q) * 200 / (2*q);
        else v = -100 + (p - 3*q) * 100 / q;
        b[i] = (BYTE)v;
    }
}

static void gen_kick(BYTE *b, int len)
{
    int i;
    for (i = 0; i < len - 2; i++) {
        int per = 4 + i * 16 / len;
        int amp = 127 - i * 125 / len;
        if (amp < 0) amp = 0;
        b[i] = (BYTE)(((i % per) < per/2) ? amp : -amp);
    }
    b[len-2] = 0; b[len-1] = 0;
}

static void gen_snare(BYTE *b, int len)
{
    int i;
    for (i = 0; i < len - 2; i++) {
        int amp = 120 - i * 118 / len;
        if (amp < 0) amp = 0;
        int n = nrand() * amp / 127;
        int t = ((i % 6) < 3) ? amp/3 : -(amp/3);
        b[i] = (BYTE)((n + t) / 2);
    }
    b[len-2] = 0; b[len-1] = 0;
}

static void gen_hihat(BYTE *b, int len)
{
    int i;
    for (i = 0; i < len - 2; i++) {
        int amp = 90 - i * 88 / len;
        if (amp < 0) amp = 0;
        b[i] = (BYTE)(nrand() * amp / 127);
    }
    b[len-2] = 0; b[len-1] = 0;
}

/* ---- Pattern building ---- */

static void sn(int p, int r, int c, UBYTE smp, UBYTE note, UBYTE vol)
{
    if (p < NUM_PATTERNS && r < ROWS_PER_PAT && c < NUM_CHANNELS) {
        mp.pat[p][r][c].smp = smp;
        mp.pat[p][r][c].note = note;
        mp.pat[p][r][c].vol = vol;
    }
}

/*
 * Metal riff patterns at speed 3 (~16.7 rows/sec, 60ms per row).
 * Gallop rhythm: HIT . hit hit . hit hit . (8 rows = ~480ms)
 */

static void build_pattern0(void)
{
    /* Main E5 power chord gallop */
    int r;
    for (r = 0; r < 64; r += 8) {
        /* Lead guitar: distorted E power chord */
        sn(0, r+0, 0, S_GUITAR, E_2, 64);
        sn(0, r+2, 0, S_PMUTE,  E_2, 48);
        sn(0, r+3, 0, S_PMUTE,  E_2, 48);
        sn(0, r+5, 0, S_PMUTE,  E_2, 48);
        sn(0, r+6, 0, S_PMUTE,  E_2, 48);

        /* Harmony: fifth (B) for power chord thickness */
        sn(0, r+0, 1, S_GUITAR, B_2, 50);
        sn(0, r+2, 1, S_PMUTE,  B_2, 40);
        sn(0, r+3, 1, S_PMUTE,  B_2, 40);
        sn(0, r+5, 1, S_PMUTE,  B_2, 40);
        sn(0, r+6, 1, S_PMUTE,  B_2, 40);

        /* Bass follows root */
        sn(0, r+0, 2, S_BASS, E_1, 60);

        /* Drums: kick-hihat-snare-hihat */
        sn(0, r+0, 3, S_KICK,  C_2, 64);
        sn(0, r+2, 3, S_HIHAT, C_3, 40);
        sn(0, r+4, 3, S_SNARE, C_2, 56);
        sn(0, r+6, 3, S_HIHAT, C_3, 40);
    }
}

static void build_pattern1(void)
{
    /* G5 - A5 variation */
    int r;
    for (r = 0; r < 32; r += 8) {
        /* G power chord */
        sn(1, r+0, 0, S_GUITAR, G_2, 64);
        sn(1, r+2, 0, S_PMUTE,  G_2, 48);
        sn(1, r+3, 0, S_PMUTE,  G_2, 48);
        sn(1, r+5, 0, S_PMUTE,  G_2, 48);
        sn(1, r+6, 0, S_PMUTE,  G_2, 48);

        sn(1, r+0, 1, S_GUITAR, D_2, 50);
        sn(1, r+2, 1, S_PMUTE,  D_2, 40);
        sn(1, r+3, 1, S_PMUTE,  D_2, 40);
        sn(1, r+5, 1, S_PMUTE,  D_2, 40);
        sn(1, r+6, 1, S_PMUTE,  D_2, 40);

        sn(1, r+0, 2, S_BASS, G_1, 60);

        sn(1, r+0, 3, S_KICK,  C_2, 64);
        sn(1, r+2, 3, S_HIHAT, C_3, 40);
        sn(1, r+4, 3, S_SNARE, C_2, 56);
        sn(1, r+6, 3, S_HIHAT, C_3, 40);
    }
    for (r = 32; r < 64; r += 8) {
        /* A power chord */
        sn(1, r+0, 0, S_GUITAR, A_2, 64);
        sn(1, r+2, 0, S_PMUTE,  A_2, 48);
        sn(1, r+3, 0, S_PMUTE,  A_2, 48);
        sn(1, r+5, 0, S_PMUTE,  A_2, 48);
        sn(1, r+6, 0, S_PMUTE,  A_2, 48);

        sn(1, r+0, 1, S_GUITAR, E_2, 50);
        sn(1, r+2, 1, S_PMUTE,  E_2, 40);
        sn(1, r+3, 1, S_PMUTE,  E_2, 40);
        sn(1, r+5, 1, S_PMUTE,  E_2, 40);
        sn(1, r+6, 1, S_PMUTE,  E_2, 40);

        sn(1, r+0, 2, S_BASS, A_1, 60);

        sn(1, r+0, 3, S_KICK,  C_2, 64);
        sn(1, r+2, 3, S_HIHAT, C_3, 40);
        sn(1, r+4, 3, S_SNARE, C_2, 56);
        sn(1, r+6, 3, S_HIHAT, C_3, 40);
    }
}

static void build_pattern2(void)
{
    /* Breakdown: slower half-time feel with double kicks */
    int r;
    for (r = 0; r < 64; r += 16) {
        /* Heavy downtuned E hits */
        sn(2, r+0,  0, S_GUITAR, E_1, 64);
        sn(2, r+8,  0, S_GUITAR, E_1, 64);
        sn(2, r+12, 0, S_GUITAR, Fs1, 64);

        sn(2, r+0,  1, S_GUITAR, B_1, 50);
        sn(2, r+8,  1, S_GUITAR, B_1, 50);
        sn(2, r+12, 1, S_GUITAR, Cs2, 50);

        sn(2, r+0, 2, S_BASS, E_1, 64);
        sn(2, r+8, 2, S_BASS, E_1, 64);

        /* Double kick */
        sn(2, r+0,  3, S_KICK, C_2, 64);
        sn(2, r+2,  3, S_KICK, C_2, 58);
        sn(2, r+4,  3, S_SNARE, C_2, 60);
        sn(2, r+6,  3, S_KICK, C_2, 58);
        sn(2, r+8,  3, S_KICK, C_2, 64);
        sn(2, r+10, 3, S_KICK, C_2, 58);
        sn(2, r+12, 3, S_SNARE, C_2, 60);
        sn(2, r+14, 3, S_HIHAT, C_3, 45);
    }
}

static void build_pattern3(void)
{
    /* Fast E-F-E-F chromatic riff */
    int r;
    for (r = 0; r < 64; r += 4) {
        UBYTE note = ((r / 4) & 1) ? F_2 : E_2;
        UBYTE bnote = ((r / 4) & 1) ? C_3 : B_2;
        UBYTE bass = ((r / 4) & 1) ? F_1 : E_1;

        sn(3, r+0, 0, S_PMUTE, note, 56);
        sn(3, r+1, 0, S_PMUTE, note, 48);
        sn(3, r+2, 0, S_PMUTE, note, 56);

        sn(3, r+0, 1, S_PMUTE, bnote, 45);
        sn(3, r+2, 1, S_PMUTE, bnote, 45);

        if ((r & 7) == 0) sn(3, r, 2, S_BASS, bass, 58);

        sn(3, r+0, 3, S_KICK,  C_2, 64);
        sn(3, r+2, 3, S_HIHAT, C_3, 38);
    }
}

static void build_song(void)
{
    /* Clear all patterns */
    int p, r, c;
    for (p = 0; p < NUM_PATTERNS; p++)
        for (r = 0; r < ROWS_PER_PAT; r++)
            for (c = 0; c < NUM_CHANNELS; c++) {
                mp.pat[p][r][c].smp = 0;
                mp.pat[p][r][c].note = N__;
                mp.pat[p][r][c].vol = 0xFF;
            }

    build_pattern0();  /* E5 gallop */
    build_pattern1();  /* G5/A5 variation */
    build_pattern2();  /* Breakdown */
    build_pattern3();  /* Chromatic riff */

    /* Song order: verse-verse-chorus-verse-verse-chorus-breakdown-riff */
    mp.order[0] = 0;
    mp.order[1] = 0;
    mp.order[2] = 1;
    mp.order[3] = 0;
    mp.order[4] = 0;
    mp.order[5] = 1;
    mp.order[6] = 2;
    mp.order[7] = 3;
}

/* ---- Paula interface ---- */

static void paula_trigger(int ch)
{
    custom.dmacon = (UWORD)(1 << ch);  /* disable channel */

    custom.aud[ch].ac_ptr = (UWORD *)mp.ch[ch].ptr;
    custom.aud[ch].ac_len = mp.ch[ch].length;
    custom.aud[ch].ac_per = mp.ch[ch].period;
    custom.aud[ch].ac_vol = mp.ch[ch].volume;

    custom.dmacon = (UWORD)(0x8200 | (1 << ch));  /* enable */

    mp.ch[ch].need_loop = 1;
}

static void paula_set_loop(int ch)
{
    if (mp.ch[ch].loops) {
        /* Loop entire sample - already set, nothing to change */
    } else {
        /* One-shot: point DMA at silence */
        custom.aud[ch].ac_ptr = (UWORD *)mp.silence;
        custom.aud[ch].ac_len = 2;  /* 2 words = 4 bytes */
    }
    mp.ch[ch].need_loop = 0;
}

/* ---- Tick processing ---- */

static void process_row(void)
{
    int c, pat;

    pat = mp.order[mp.position];

    for (c = 0; c < NUM_CHANNELS; c++) {
        PNote *n = &mp.pat[pat][mp.row][c];

        /* Skip ch3 if SFX active */
        if (c == 3 && mp.sfx_active) continue;

        /* Load sample info */
        if (n->smp > 0 && n->smp <= NUM_SAMPLES) {
            int si = n->smp - 1;
            mp.ch[c].ptr = mp.smp_ptr[si];
            mp.ch[c].length = mp.smp[si].length;
            mp.ch[c].volume = mp.smp[si].volume;
            mp.ch[c].loops = mp.smp[si].loops;
        }

        /* Volume override */
        if (n->vol != 0xFF) {
            mp.ch[c].volume = n->vol;
        }

        /* Trigger note */
        if (n->note != N__ && n->note < 36 && mp.ch[c].ptr) {
            mp.ch[c].period = periods[n->note];
            paula_trigger(c);
        }
    }
}

void modplay_tick(void)
{
    int c;

    if (!mp.playing) return;

    /* SFX timeout */
    if (mp.sfx_active) {
        mp.sfx_frames--;
        if (mp.sfx_frames <= 0) {
            mp.sfx_active = 0;
        }
    }

    /* Set loop params for channels triggered last tick */
    for (c = 0; c < NUM_CHANNELS; c++) {
        if (mp.ch[c].need_loop) {
            paula_set_loop(c);
        }
    }

    /* Process on tick 0 */
    if (mp.tick == 0) {
        process_row();
    }

    /* Advance */
    mp.tick++;
    if (mp.tick >= mp.speed) {
        mp.tick = 0;
        mp.row++;
        if (mp.row >= ROWS_PER_PAT) {
            mp.row = 0;
            mp.position++;
            if (mp.position >= SONG_LENGTH) {
                mp.position = 0;
            }
        }
    }
}

/* ---- Public API ---- */

int modplay_init(void)
{
    BYTE *p;
    int i;

    /* Clear state */
    for (i = 0; i < NUM_CHANNELS; i++) {
        mp.ch[i].ptr = NULL;
        mp.ch[i].length = 0;
        mp.ch[i].period = 428;
        mp.ch[i].volume = 0;
        mp.ch[i].loops = 0;
        mp.ch[i].need_loop = 0;
    }
    mp.playing = 0;
    mp.speed = SPEED_DEFAULT;
    mp.tick = 0;
    mp.row = 0;
    mp.position = 0;
    mp.sfx_active = 0;
    mp.sfx_frames = 0;

    /* Allocate chip RAM for all samples */
    mp.chip_block = (BYTE *)AllocMem(TOTAL_CHIP, MEMF_CHIP | MEMF_CLEAR);
    if (!mp.chip_block) {
        AB_E("modplay: chip RAM alloc failed (%ld bytes)", (long)TOTAL_CHIP);
        return 1;
    }

    /* Carve up the block */
    p = mp.chip_block;

    mp.smp_ptr[0] = p; p += SZ_GUITAR;
    mp.smp_ptr[1] = p; p += SZ_PMUTE;
    mp.smp_ptr[2] = p; p += SZ_BASS;
    mp.smp_ptr[3] = p; p += SZ_KICK;
    mp.smp_ptr[4] = p; p += SZ_SNARE;
    mp.smp_ptr[5] = p; p += SZ_HIHAT;
    mp.silence = p;  /* 4 bytes of zeros (already cleared) */

    /* Sample info */
    mp.smp[0] = (SmpInfo){ SZ_GUITAR/2, 64, 1 };  /* guitar: loops */
    mp.smp[1] = (SmpInfo){ SZ_PMUTE/2,  50, 0 };  /* palm mute: one-shot */
    mp.smp[2] = (SmpInfo){ SZ_BASS/2,   60, 1 };  /* bass: loops */
    mp.smp[3] = (SmpInfo){ SZ_KICK/2,   64, 0 };  /* kick: one-shot */
    mp.smp[4] = (SmpInfo){ SZ_SNARE/2,  56, 0 };  /* snare: one-shot */
    mp.smp[5] = (SmpInfo){ SZ_HIHAT/2,  42, 0 };  /* hihat: one-shot */

    /* Generate waveforms */
    gen_guitar(mp.smp_ptr[0], SZ_GUITAR);
    gen_pmute(mp.smp_ptr[1], SZ_PMUTE);
    gen_bass(mp.smp_ptr[2], SZ_BASS);
    gen_kick(mp.smp_ptr[3], SZ_KICK);
    gen_snare(mp.smp_ptr[4], SZ_SNARE);
    gen_hihat(mp.smp_ptr[5], SZ_HIHAT);

    /* Build the metal track */
    build_song();

    AB_I("modplay: init OK, %ld bytes chip RAM", (long)TOTAL_CHIP);
    return 0;
}

void modplay_start(void)
{
    mp.playing = 1;
    mp.tick = 0;
    mp.row = 0;
    mp.position = 0;

    /* Enable audio DMA master */
    custom.dmacon = (UWORD)0x820F;

    AB_I("modplay: started");
}

void modplay_stop(void)
{
    mp.playing = 0;

    /* Disable all audio DMA */
    custom.dmacon = (UWORD)0x000F;

    /* Silence all channels */
    custom.aud[0].ac_vol = 0;
    custom.aud[1].ac_vol = 0;
    custom.aud[2].ac_vol = 0;
    custom.aud[3].ac_vol = 0;
}

void modplay_cleanup(void)
{
    modplay_stop();
    if (mp.chip_block) {
        FreeMem(mp.chip_block, TOTAL_CHIP);
        mp.chip_block = NULL;
    }
}

void modplay_sfx(BYTE *data, UWORD len_words, UWORD period, UWORD volume)
{
    if (!data || len_words == 0) return;

    /* Take over channel 3 */
    custom.dmacon = (UWORD)0x0008;  /* disable ch3 */

    custom.aud[3].ac_ptr = (UWORD *)data;
    custom.aud[3].ac_len = len_words;
    custom.aud[3].ac_per = period;
    custom.aud[3].ac_vol = volume;

    custom.dmacon = (UWORD)0x8208;  /* enable ch3 */

    /* Point loop at silence */
    /* (will be set on next modplay_tick via need_loop) */

    mp.sfx_active = 1;
    /* Calculate duration in frames: samples / sample_rate * 50 */
    mp.sfx_frames = (int)((ULONG)len_words * 2 * (ULONG)period / 35469UL) + 3;
    if (mp.sfx_frames < 3) mp.sfx_frames = 3;
    if (mp.sfx_frames > 25) mp.sfx_frames = 25;
}
