/*
 * Lode Runner - MOD-style music player
 * Direct Paula hardware access. Fast-paced adventurous chiptune.
 * Call modplay_tick() each VBlank. Channel 3 stolen for SFX.
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
#define NUM_SAMPLES   7
#define NUM_PATTERNS  6
#define ROWS_PER_PAT  16
#define NUM_CHANNELS  4
#define SONG_LENGTH   8
#define SPEED_DEFAULT 6  /* ticks per row: 50/6 = ~8.3 rows/sec */

/* ---- Sample sizes (bytes, must be even) ---- */
#define SZ_BASS     128
#define SZ_LEAD      64
#define SZ_ARPEG     32
#define SZ_KICK     256
#define SZ_SNARE    128
#define SZ_HIHAT     64
#define SZ_SILENCE    2

#define TOTAL_CHIP (SZ_BASS + SZ_LEAD + SZ_ARPEG + SZ_KICK + SZ_SNARE + SZ_HIHAT + SZ_SILENCE)

/* ---- Note periods (PAL) ---- */
/* N__ = no note (continue previous) */
#define N__ 0xFF

/* Octave 3 */
#define C_3  0
#define Cs3  1
#define D_3  2
#define Ds3  3
#define E_3  4
#define F_3  5
#define Fs3  6
#define G_3  7
#define Gs3  8
#define A_3  9
#define As3 10
#define B_3 11

/* Octave 4 */
#define C_4 12
#define Cs4 13
#define D_4 14
#define Ds4 15
#define E_4 16
#define F_4 17
#define Fs4 18
#define G_4 19
#define Gs4 20
#define A_4 21
#define As4 22
#define B_4 23

/* Octave 5 */
#define C_5 24
#define D_5 25
#define E_5 26
#define F_5 27
#define G_5 28
#define A_5 29
#define B_5 30

static const UWORD periods[] = {
    /* Octave 3 */
    428, 404, 381, 360, 339, 320, 302, 285, 269, 254, 240, 226,
    /* Octave 4 */
    214, 202, 190, 180, 170, 160, 151, 143, 135, 127, 120, 113,
    /* Octave 5 */
    107, 95, 85, 80, 71, 64, 57,
    0
};

/* ---- Sample definitions ---- */
enum { S_BASS=1, S_LEAD, S_ARPEG, S_KICK, S_SNARE, S_HIHAT };

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
    BYTE *silence;                 /* 2 bytes of silence */

    PNote pat[NUM_PATTERNS][ROWS_PER_PAT][NUM_CHANNELS];
    UBYTE order[SONG_LENGTH];

    struct {
        BYTE *ptr;
        UWORD length;
        UWORD period;
        UWORD volume;
        int loops;
        int need_loop;
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

/* Bass: triangle wave, warm foundation */
static void gen_bass(BYTE *b, int len)
{
    int i, half = len / 2, q = len / 8;
    for (i = 0; i < len; i++) {
        int p = i % half, v;
        if (q == 0) { b[i] = 0; continue; }
        if (p < q) v = p * 100 / q;
        else if (p < 3*q) v = 100 - (p - q) * 200 / (2*q);
        else v = -100 + (p - 3*q) * 100 / q;
        b[i] = (BYTE)v;
    }
}

/* Lead: square/pulse wave, bright melody sound */
static void gen_lead(BYTE *b, int len)
{
    int i;
    /* 75% duty cycle pulse for brighter tone */
    for (i = 0; i < len; i++) {
        int phase = i % (len / 2);
        int duty = (len / 2) * 3 / 4;
        b[i] = (BYTE)(phase < duty ? 100 : -100);
    }
}

/* Arpeggio: sawtooth wave */
static void gen_arpeg(BYTE *b, int len)
{
    int i;
    for (i = 0; i < len; i++) {
        b[i] = (BYTE)(i * 200 / len - 100);
    }
}

/* Kick: frequency-sweep noise, percussive impact */
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

/* Snare: noise+tone mix */
static void gen_snare(BYTE *b, int len)
{
    int i;
    for (i = 0; i < len - 2; i++) {
        int amp = 120 - i * 118 / len;
        if (amp < 0) amp = 0;
        {
            int n = nrand() * amp / 127;
            int t = ((i % 6) < 3) ? amp/3 : -(amp/3);
            b[i] = (BYTE)((n + t) / 2);
        }
    }
    b[len-2] = 0; b[len-1] = 0;
}

/* HiHat: filtered noise, short tick */
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
 * Adventurous puzzle-platformer music in C minor.
 * Speed 6 at 50Hz PAL = ~8.3 rows/sec. 16 rows per pattern = ~1.9 sec/pat.
 */

static void build_pattern0(void)
{
    /* Intro/Verse: C minor, ascending bass, playful lead */
    /* Bass: C minor root movement */
    sn(0,  0, 0, S_BASS, C_3,  56);
    sn(0,  4, 0, S_BASS, Ds3,  56);
    sn(0,  8, 0, S_BASS, G_3,  56);
    sn(0, 12, 0, S_BASS, F_3,  52);

    /* Lead: playful ascending melody */
    sn(0,  0, 1, S_LEAD, G_4,  58);
    sn(0,  2, 1, S_LEAD, As4,  58);
    sn(0,  4, 1, S_LEAD, C_5,  60);
    sn(0,  6, 1, S_LEAD, Ds4,  54);
    sn(0,  8, 1, S_LEAD, D_4,  58);
    sn(0, 10, 1, S_LEAD, E_4,  60);
    sn(0, 12, 1, S_LEAD, F_4,  62);
    sn(0, 14, 1, S_LEAD, G_4,  58);

    /* Arpeggio: C minor chord arp */
    sn(0,  0, 2, S_ARPEG, C_4,  40);
    sn(0,  1, 2, S_ARPEG, Ds4,  38);
    sn(0,  2, 2, S_ARPEG, G_4,  40);
    sn(0,  3, 2, S_ARPEG, C_5,  38);
    sn(0,  4, 2, S_ARPEG, Ds4,  40);
    sn(0,  5, 2, S_ARPEG, G_4,  38);
    sn(0,  6, 2, S_ARPEG, C_5,  40);
    sn(0,  7, 2, S_ARPEG, G_4,  38);
    sn(0,  8, 2, S_ARPEG, G_3,  40);
    sn(0,  9, 2, S_ARPEG, B_3,  38);
    sn(0, 10, 2, S_ARPEG, D_4,  40);
    sn(0, 11, 2, S_ARPEG, G_4,  38);
    sn(0, 12, 2, S_ARPEG, F_3,  40);
    sn(0, 13, 2, S_ARPEG, A_3,  38);
    sn(0, 14, 2, S_ARPEG, C_4,  40);
    sn(0, 15, 2, S_ARPEG, F_4,  38);

    /* Drums: driving beat */
    sn(0,  0, 3, S_KICK,  C_4, 60);
    sn(0,  2, 3, S_HIHAT, C_5, 36);
    sn(0,  4, 3, S_SNARE, C_4, 52);
    sn(0,  6, 3, S_HIHAT, C_5, 36);
    sn(0,  8, 3, S_KICK,  C_4, 60);
    sn(0, 10, 3, S_HIHAT, C_5, 36);
    sn(0, 12, 3, S_SNARE, C_4, 52);
    sn(0, 14, 3, S_HIHAT, C_5, 36);
}

static void build_pattern1(void)
{
    /* Chorus: brighter Eb major feel, faster lead movement */
    /* Bass: Eb major root */
    sn(1,  0, 0, S_BASS, Ds3,  58);
    sn(1,  4, 0, S_BASS, F_3,  58);
    sn(1,  8, 0, S_BASS, G_3,  58);
    sn(1, 12, 0, S_BASS, As3,  54);

    /* Lead: uplifting melody, quick runs */
    sn(1,  0, 1, S_LEAD, Ds4,  60);
    sn(1,  1, 1, S_LEAD, F_4,  58);
    sn(1,  2, 1, S_LEAD, G_4,  62);
    sn(1,  4, 1, S_LEAD, As4,  64);
    sn(1,  5, 1, S_LEAD, G_4,  58);
    sn(1,  6, 1, S_LEAD, F_4,  60);
    sn(1,  8, 1, S_LEAD, G_4,  62);
    sn(1,  9, 1, S_LEAD, As4,  60);
    sn(1, 10, 1, S_LEAD, C_5,  64);
    sn(1, 12, 1, S_LEAD, As4,  58);
    sn(1, 13, 1, S_LEAD, G_4,  56);
    sn(1, 14, 1, S_LEAD, F_4,  54);
    sn(1, 15, 1, S_LEAD, Ds4,  52);

    /* Arpeggio: Eb major chord arp */
    sn(1,  0, 2, S_ARPEG, Ds4,  42);
    sn(1,  1, 2, S_ARPEG, G_4,  40);
    sn(1,  2, 2, S_ARPEG, As4,  42);
    sn(1,  3, 2, S_ARPEG, Ds4,  40);
    sn(1,  4, 2, S_ARPEG, F_4,  42);
    sn(1,  5, 2, S_ARPEG, A_4,  40);
    sn(1,  6, 2, S_ARPEG, C_5,  42);
    sn(1,  7, 2, S_ARPEG, F_4,  40);
    sn(1,  8, 2, S_ARPEG, G_4,  42);
    sn(1,  9, 2, S_ARPEG, B_4,  40);
    sn(1, 10, 2, S_ARPEG, D_5,  42);
    sn(1, 11, 2, S_ARPEG, G_4,  40);
    sn(1, 12, 2, S_ARPEG, As3,  42);
    sn(1, 13, 2, S_ARPEG, D_4,  40);
    sn(1, 14, 2, S_ARPEG, F_4,  42);
    sn(1, 15, 2, S_ARPEG, As4,  40);

    /* Drums: same driving beat with extra kick */
    sn(1,  0, 3, S_KICK,  C_4, 62);
    sn(1,  2, 3, S_HIHAT, C_5, 38);
    sn(1,  3, 3, S_KICK,  C_4, 50);
    sn(1,  4, 3, S_SNARE, C_4, 54);
    sn(1,  6, 3, S_HIHAT, C_5, 38);
    sn(1,  8, 3, S_KICK,  C_4, 62);
    sn(1, 10, 3, S_HIHAT, C_5, 38);
    sn(1, 12, 3, S_SNARE, C_4, 54);
    sn(1, 14, 3, S_HIHAT, C_5, 38);
    sn(1, 15, 3, S_KICK,  C_4, 48);
}

static void build_pattern2(void)
{
    /* Bridge: tension building, chromatic bass movement */
    /* Bass: chromatic ascending */
    sn(2,  0, 0, S_BASS, C_3,  58);
    sn(2,  4, 0, S_BASS, Cs3,  58);
    sn(2,  8, 0, S_BASS, D_3,  58);
    sn(2, 12, 0, S_BASS, Ds3,  60);

    /* Lead: tense, held notes with chromatic tension */
    sn(2,  0, 1, S_LEAD, Ds4,  56);
    sn(2,  2, 1, S_LEAD, D_4,  54);
    sn(2,  4, 1, S_LEAD, E_4,  58);
    sn(2,  6, 1, S_LEAD, Ds4,  54);
    sn(2,  8, 1, S_LEAD, F_4,  58);
    sn(2, 10, 1, S_LEAD, Fs4,  60);
    sn(2, 12, 1, S_LEAD, G_4,  62);
    sn(2, 14, 1, S_LEAD, Gs4,  64);

    /* Arpeggio: diminished feel */
    sn(2,  0, 2, S_ARPEG, C_4,  38);
    sn(2,  1, 2, S_ARPEG, Ds4,  36);
    sn(2,  2, 2, S_ARPEG, Fs4,  38);
    sn(2,  3, 2, S_ARPEG, C_4,  36);
    sn(2,  4, 2, S_ARPEG, Cs4,  38);
    sn(2,  5, 2, S_ARPEG, E_4,  36);
    sn(2,  6, 2, S_ARPEG, G_4,  38);
    sn(2,  7, 2, S_ARPEG, Cs4,  36);
    sn(2,  8, 2, S_ARPEG, D_4,  38);
    sn(2,  9, 2, S_ARPEG, F_4,  36);
    sn(2, 10, 2, S_ARPEG, A_4,  38);
    sn(2, 11, 2, S_ARPEG, D_4,  36);
    sn(2, 12, 2, S_ARPEG, Ds4,  40);
    sn(2, 13, 2, S_ARPEG, G_4,  38);
    sn(2, 14, 2, S_ARPEG, As4,  40);
    sn(2, 15, 2, S_ARPEG, Ds4,  38);

    /* Drums: more intense, double kick */
    sn(2,  0, 3, S_KICK,  C_4, 62);
    sn(2,  1, 3, S_KICK,  C_4, 50);
    sn(2,  2, 3, S_HIHAT, C_5, 36);
    sn(2,  4, 3, S_SNARE, C_4, 56);
    sn(2,  6, 3, S_HIHAT, C_5, 36);
    sn(2,  8, 3, S_KICK,  C_4, 62);
    sn(2,  9, 3, S_KICK,  C_4, 50);
    sn(2, 10, 3, S_HIHAT, C_5, 36);
    sn(2, 12, 3, S_SNARE, C_4, 58);
    sn(2, 13, 3, S_KICK,  C_4, 48);
    sn(2, 14, 3, S_HIHAT, C_5, 38);
}

static void build_pattern3(void)
{
    /* Breakdown: stripped back, just bass and drums */
    /* Bass: heavy low octave hits */
    sn(3,  0, 0, S_BASS, C_3,  62);
    sn(3,  8, 0, S_BASS, G_3,  58);
    sn(3, 12, 0, S_BASS, Ds3,  60);

    /* No lead - silence for tension */

    /* No arpeggio - sparse */

    /* Drums: half-time heavy feel */
    sn(3,  0, 3, S_KICK,  C_4, 64);
    sn(3,  4, 3, S_SNARE, C_4, 58);
    sn(3,  6, 3, S_HIHAT, C_5, 32);
    sn(3,  8, 3, S_KICK,  C_4, 64);
    sn(3, 10, 3, S_HIHAT, C_5, 32);
    sn(3, 12, 3, S_SNARE, C_4, 58);
    sn(3, 14, 3, S_KICK,  C_4, 52);
}

static void build_pattern4(void)
{
    /* Variation of pattern 0: same bass, different lead melody */
    /* Bass: same as pattern 0 */
    sn(4,  0, 0, S_BASS, C_3,  56);
    sn(4,  4, 0, S_BASS, Ds3,  56);
    sn(4,  8, 0, S_BASS, G_3,  56);
    sn(4, 12, 0, S_BASS, F_3,  52);

    /* Lead: variation - descending then resolving */
    sn(4,  0, 1, S_LEAD, C_5,  60);
    sn(4,  2, 1, S_LEAD, As4,  58);
    sn(4,  4, 1, S_LEAD, G_4,  56);
    sn(4,  6, 1, S_LEAD, F_4,  58);
    sn(4,  8, 1, S_LEAD, Ds4,  56);
    sn(4, 10, 1, S_LEAD, F_4,  58);
    sn(4, 12, 1, S_LEAD, G_4,  62);
    sn(4, 14, 1, S_LEAD, As4,  60);

    /* Arpeggio: same as pattern 0 */
    sn(4,  0, 2, S_ARPEG, C_4,  40);
    sn(4,  1, 2, S_ARPEG, Ds4,  38);
    sn(4,  2, 2, S_ARPEG, G_4,  40);
    sn(4,  3, 2, S_ARPEG, C_5,  38);
    sn(4,  4, 2, S_ARPEG, Ds4,  40);
    sn(4,  5, 2, S_ARPEG, G_4,  38);
    sn(4,  6, 2, S_ARPEG, C_5,  40);
    sn(4,  7, 2, S_ARPEG, G_4,  38);
    sn(4,  8, 2, S_ARPEG, G_3,  40);
    sn(4,  9, 2, S_ARPEG, B_3,  38);
    sn(4, 10, 2, S_ARPEG, D_4,  40);
    sn(4, 11, 2, S_ARPEG, G_4,  38);
    sn(4, 12, 2, S_ARPEG, F_3,  40);
    sn(4, 13, 2, S_ARPEG, A_3,  38);
    sn(4, 14, 2, S_ARPEG, C_4,  40);
    sn(4, 15, 2, S_ARPEG, F_4,  38);

    /* Drums */
    sn(4,  0, 3, S_KICK,  C_4, 60);
    sn(4,  2, 3, S_HIHAT, C_5, 36);
    sn(4,  4, 3, S_SNARE, C_4, 52);
    sn(4,  6, 3, S_HIHAT, C_5, 36);
    sn(4,  8, 3, S_KICK,  C_4, 60);
    sn(4, 10, 3, S_HIHAT, C_5, 36);
    sn(4, 12, 3, S_SNARE, C_4, 52);
    sn(4, 14, 3, S_HIHAT, C_5, 36);
}

static void build_pattern5(void)
{
    /* Variation of pattern 1: same structure, octave jump ending */
    /* Bass */
    sn(5,  0, 0, S_BASS, Ds3,  58);
    sn(5,  4, 0, S_BASS, F_3,  58);
    sn(5,  8, 0, S_BASS, G_3,  58);
    sn(5, 12, 0, S_BASS, C_3,  60);

    /* Lead: ascending run to climax */
    sn(5,  0, 1, S_LEAD, Ds4,  58);
    sn(5,  1, 1, S_LEAD, F_4,  58);
    sn(5,  2, 1, S_LEAD, G_4,  60);
    sn(5,  3, 1, S_LEAD, As4,  60);
    sn(5,  4, 1, S_LEAD, C_5,  64);
    sn(5,  6, 1, S_LEAD, As4,  58);
    sn(5,  8, 1, S_LEAD, G_4,  60);
    sn(5, 10, 1, S_LEAD, As4,  62);
    sn(5, 12, 1, S_LEAD, C_5,  64);
    sn(5, 14, 1, S_LEAD, G_4,  56);

    /* Arpeggio: Eb then C minor */
    sn(5,  0, 2, S_ARPEG, Ds4,  42);
    sn(5,  1, 2, S_ARPEG, G_4,  40);
    sn(5,  2, 2, S_ARPEG, As4,  42);
    sn(5,  3, 2, S_ARPEG, Ds4,  40);
    sn(5,  4, 2, S_ARPEG, F_4,  42);
    sn(5,  5, 2, S_ARPEG, A_4,  40);
    sn(5,  6, 2, S_ARPEG, C_5,  42);
    sn(5,  7, 2, S_ARPEG, F_4,  40);
    sn(5,  8, 2, S_ARPEG, C_4,  42);
    sn(5,  9, 2, S_ARPEG, Ds4,  40);
    sn(5, 10, 2, S_ARPEG, G_4,  42);
    sn(5, 11, 2, S_ARPEG, C_5,  40);
    sn(5, 12, 2, S_ARPEG, C_4,  42);
    sn(5, 13, 2, S_ARPEG, G_4,  40);
    sn(5, 14, 2, S_ARPEG, Ds4,  42);
    sn(5, 15, 2, S_ARPEG, C_4,  40);

    /* Drums: energetic fill at end */
    sn(5,  0, 3, S_KICK,  C_4, 62);
    sn(5,  2, 3, S_HIHAT, C_5, 38);
    sn(5,  4, 3, S_SNARE, C_4, 54);
    sn(5,  6, 3, S_HIHAT, C_5, 38);
    sn(5,  8, 3, S_KICK,  C_4, 62);
    sn(5, 10, 3, S_SNARE, C_4, 48);
    sn(5, 11, 3, S_SNARE, C_4, 44);
    sn(5, 12, 3, S_SNARE, C_4, 50);
    sn(5, 13, 3, S_SNARE, C_4, 52);
    sn(5, 14, 3, S_KICK,  C_4, 60);
    sn(5, 15, 3, S_KICK,  C_4, 56);
}

static void build_song(void)
{
    int p, r, c;

    /* Clear all patterns */
    for (p = 0; p < NUM_PATTERNS; p++)
        for (r = 0; r < ROWS_PER_PAT; r++)
            for (c = 0; c < NUM_CHANNELS; c++) {
                mp.pat[p][r][c].smp = 0;
                mp.pat[p][r][c].note = N__;
                mp.pat[p][r][c].vol = 0xFF;
            }

    build_pattern0();  /* Verse: C minor */
    build_pattern1();  /* Chorus: Eb major */
    build_pattern2();  /* Bridge: chromatic tension */
    build_pattern3();  /* Breakdown: sparse */
    build_pattern4();  /* Verse variation */
    build_pattern5();  /* Chorus variation */

    /* Song order: verse-chorus-verse-chorus-bridge-breakdown-verse-chorus */
    mp.order[0] = 0;
    mp.order[1] = 1;
    mp.order[2] = 4;
    mp.order[3] = 5;
    mp.order[4] = 2;
    mp.order[5] = 3;
    mp.order[6] = 0;
    mp.order[7] = 1;
}

/* ---- Paula interface ---- */

static void paula_trigger(int ch)
{
    volatile UBYTE dummy;

    custom.dmacon = (UWORD)(1 << ch);  /* disable channel */

    /* Brief delay for Paula to settle */
    dummy = *(volatile UBYTE *)0xBFE001;
    dummy = *(volatile UBYTE *)0xBFE001;
    (void)dummy;

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
        /* Loop entire sample - already set */
    } else {
        /* One-shot: point DMA at silence */
        custom.aud[ch].ac_ptr = (UWORD *)mp.silence;
        custom.aud[ch].ac_len = 1;  /* 1 word = 2 bytes */
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
        if (n->note != N__ && n->note < 31 && mp.ch[c].ptr) {
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

    mp.smp_ptr[0] = p; p += SZ_BASS;
    mp.smp_ptr[1] = p; p += SZ_LEAD;
    mp.smp_ptr[2] = p; p += SZ_ARPEG;
    mp.smp_ptr[3] = p; p += SZ_KICK;
    mp.smp_ptr[4] = p; p += SZ_SNARE;
    mp.smp_ptr[5] = p; p += SZ_HIHAT;
    mp.silence = p;  /* 2 bytes of zeros (already cleared) */

    /* Sample info: {length_words, volume, loops} */
    mp.smp[0] = (SmpInfo){ SZ_BASS/2,   56, 1 };  /* bass: loops */
    mp.smp[1] = (SmpInfo){ SZ_LEAD/2,   58, 1 };  /* lead: loops */
    mp.smp[2] = (SmpInfo){ SZ_ARPEG/2,  40, 1 };  /* arpeg: loops */
    mp.smp[3] = (SmpInfo){ SZ_KICK/2,   64, 0 };  /* kick: one-shot */
    mp.smp[4] = (SmpInfo){ SZ_SNARE/2,  54, 0 };  /* snare: one-shot */
    mp.smp[5] = (SmpInfo){ SZ_HIHAT/2,  38, 0 };  /* hihat: one-shot */

    /* Generate waveforms */
    gen_bass(mp.smp_ptr[0], SZ_BASS);
    gen_lead(mp.smp_ptr[1], SZ_LEAD);
    gen_arpeg(mp.smp_ptr[2], SZ_ARPEG);
    gen_kick(mp.smp_ptr[3], SZ_KICK);
    gen_snare(mp.smp_ptr[4], SZ_SNARE);
    gen_hihat(mp.smp_ptr[5], SZ_HIHAT);

    /* Build the adventure track */
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

    /* Enable audio DMA master + all 4 channels */
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

    mp.sfx_active = 1;
    /* Calculate duration in frames: samples / sample_rate * 50 */
    mp.sfx_frames = (int)((ULONG)len_words * 2 * (ULONG)period / 35469UL) + 3;
    if (mp.sfx_frames < 3) mp.sfx_frames = 3;
    if (mp.sfx_frames > 25) mp.sfx_frames = 25;
}
