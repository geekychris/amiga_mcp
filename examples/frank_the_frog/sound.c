/*
 * Frank the Frog - Sound effects
 * Uses modplay for playback (direct Paula channel 3).
 * Generates complex SFX waveforms in chip RAM.
 */

#include <exec/types.h>
#include <exec/memory.h>
#include <proto/exec.h>

#include "bridge_client.h"
#include "modplay.h"
#include "sound.h"

#define SFX_LEN 2048  /* bytes per complex SFX */
#define HOP_LEN 64    /* bytes for short hop blip */

static BYTE *sfx_hop = NULL;
static BYTE *sfx_splat = NULL;
static BYTE *sfx_splash = NULL;
static BYTE *sfx_jingle = NULL;
static BYTE *sfx_levelup = NULL;
static BYTE *sfx_gameover = NULL;

#define TOTAL_SFX (HOP_LEN + SFX_LEN * 5)

static BYTE *sfx_block = NULL;

/* PRNG */
static ULONG srng = 0xCAFEBABE;
static BYTE srand_next(void) {
    srng ^= srng << 13;
    srng ^= srng >> 17;
    srng ^= srng << 5;
    return (BYTE)(srng & 0xFF);
}

/* Triangle helper */
static BYTE tri(int pos, int cycle, int amp)
{
    int q = cycle / 4, ph = pos % cycle;
    if (q == 0) return 0;
    if (ph < q) return (BYTE)(ph * amp / q);
    if (ph < 3*q) return (BYTE)(amp - (ph - q) * 2 * amp / (2*q));
    return (BYTE)(-amp + (ph - 3*q) * amp / q);
}

/* Sine approximation */
static BYTE sinapx(int pos, int cycle, int amp)
{
    int half = cycle / 2, ph = pos % cycle, x;
    if (half == 0) return 0;
    if (ph < half) {
        x = ph * 256 / half - 128;
        return (BYTE)(amp - x * x * amp / (128 * 128));
    }
    x = (ph - half) * 256 / half - 128;
    return (BYTE)(-(amp - x * x * amp / (128 * 128)));
}

static void build_hop(BYTE *b)
{
    int i;
    for (i = 0; i < HOP_LEN / 4; i++) b[i] = 120;
    for (i = HOP_LEN/4; i < HOP_LEN/2; i++)
        b[i] = (BYTE)(120 - (i - HOP_LEN/4) * 240 / (HOP_LEN/4));
    for (i = HOP_LEN/2; i < 3*HOP_LEN/4; i++) b[i] = -120;
    for (i = 3*HOP_LEN/4; i < HOP_LEN; i++)
        b[i] = (BYTE)(-120 + (i - 3*HOP_LEN/4) * 240 / (HOP_LEN/4));
}

static void build_splat(BYTE *b)
{
    int i;
    /* Loud noise impact that crunches and decays */
    for (i = 0; i < 512; i++) {
        int amp = 127 - i * 40 / 512;
        b[i] = (BYTE)(srand_next() * amp / 127);
    }
    for (i = 512; i < 1200; i++) {
        int amp = 87 - (i - 512) * 65 / 688;
        if ((i % 3) == 0) srand_next();
        b[i] = (BYTE)(srand_next() * amp / 127);
    }
    for (i = 1200; i < SFX_LEN; i++) {
        int amp = 22 - (i - 1200) * 22 / (SFX_LEN - 1200);
        if (amp < 0) amp = 0;
        b[i] = (BYTE)(srand_next() * amp / 127);
    }
}

static void build_splash(BYTE *b)
{
    int i;
    /* Splash noise burst into bubbly glug-glug-glug */
    for (i = 0; i < 400; i++) {
        int amp = 120 - i * 60 / 400;
        b[i] = (BYTE)(srand_next() * amp / 127);
    }
    /* Transition */
    for (i = 400; i < 600; i++) {
        int na = 60 - (i - 400) * 50 / 200;
        int ta = (i - 400) * 80 / 200;
        BYTE n = (BYTE)(srand_next() * na / 127);
        BYTE t = sinapx(i, 24, ta);
        b[i] = (BYTE)((n + t) / 2);
    }
    /* Glug 1 */
    for (i = 600; i < 1000; i++) {
        int w = 60 + sinapx(i, 80, 30);
        b[i] = sinapx(i, 20, w);
    }
    /* Glug 2 */
    for (i = 1000; i < 1400; i++) {
        int w = 50 + sinapx(i, 70, 25);
        b[i] = sinapx(i, 18, w);
    }
    /* Glug 3 fading */
    for (i = 1400; i < 1800; i++) {
        int a = 40 - (i - 1400) * 35 / 400;
        if (a < 5) a = 5;
        b[i] = sinapx(i, 16, a + sinapx(i, 60, a/2));
    }
    for (i = 1800; i < SFX_LEN; i++) b[i] = 0;
}

static void build_jingle(BYTE *b)
{
    int i;
    /* Rising 4-note arpeggio: celebratory jingle */
    for (i = 0; i < 512; i++)
        b[i] = tri(i, 32, 100);
    for (i = 512; i < 1024; i++)
        b[i] = tri(i, 25, 110);
    for (i = 1024; i < 1536; i++)
        b[i] = tri(i, 20, 120);
    for (i = 1536; i < SFX_LEN; i++) {
        int a = 127 - (i - 1536) * 80 / 512;
        if (a < 20) a = 20;
        b[i] = tri(i, 16, a);
    }
}

static void build_levelup(BYTE *b)
{
    int i;
    /* 5-note ascending fanfare */
    for (i = 0; i < 350; i++) b[i] = tri(i, 28, 110);
    for (i = 350; i < 700; i++) b[i] = tri(i, 22, 115);
    for (i = 700; i < 1050; i++) b[i] = tri(i, 18, 120);
    for (i = 1050; i < 1400; i++) b[i] = tri(i, 14, 125);
    for (i = 1400; i < SFX_LEN; i++) {
        int vib = 11 + sinapx(i, 40, 2);
        int a = 127 - (i - 1400) * 60 / (SFX_LEN - 1400);
        if (a < 30) a = 30;
        b[i] = tri(i, vib, a);
    }
}

static void build_gameover(BYTE *b)
{
    int i;
    /* Sad descending melody */
    for (i = 0; i < 500; i++) b[i] = tri(i, 16, 100);
    for (i = 500; i < 1000; i++) b[i] = tri(i, 22, 90);
    for (i = 1000; i < 1500; i++) b[i] = tri(i, 30, 80);
    for (i = 1500; i < SFX_LEN; i++) {
        int a = 70 - (i - 1500) * 60 / (SFX_LEN - 1500);
        if (a < 10) a = 10;
        b[i] = tri(i, 40, a);
    }
}

int sound_init(void)
{
    BYTE *p;

    /* Init the MOD player (generates music samples + patterns) */
    if (modplay_init() != 0) {
        AB_W("sound: modplay init failed, continuing without music");
    }

    /* Allocate chip RAM for SFX */
    sfx_block = (BYTE *)AllocMem(TOTAL_SFX, MEMF_CHIP | MEMF_CLEAR);
    if (!sfx_block) {
        AB_E("sound: SFX chip RAM alloc failed");
        return 1;
    }

    p = sfx_block;
    sfx_hop     = p; p += HOP_LEN;
    sfx_splat   = p; p += SFX_LEN;
    sfx_splash  = p; p += SFX_LEN;
    sfx_jingle  = p; p += SFX_LEN;
    sfx_levelup = p; p += SFX_LEN;
    sfx_gameover = p;

    build_hop(sfx_hop);
    build_splat(sfx_splat);
    build_splash(sfx_splash);
    build_jingle(sfx_jingle);
    build_levelup(sfx_levelup);
    build_gameover(sfx_gameover);

    AB_I("sound: SFX init OK, %ld bytes chip RAM", (long)TOTAL_SFX);
    return 0;
}

void sound_cleanup(void)
{
    modplay_cleanup();
    if (sfx_block) {
        FreeMem(sfx_block, TOTAL_SFX);
        sfx_block = NULL;
    }
}

void sound_hop(void)
{
    modplay_sfx(sfx_hop, HOP_LEN / 2, 200, 48);
}

void sound_splat(void)
{
    modplay_sfx(sfx_splat, SFX_LEN / 2, 140, 64);
}

void sound_splash(void)
{
    modplay_sfx(sfx_splash, SFX_LEN / 2, 160, 60);
}

void sound_home(void)
{
    modplay_sfx(sfx_jingle, SFX_LEN / 2, 180, 64);
}

void sound_levelup(void)
{
    modplay_sfx(sfx_levelup, SFX_LEN / 2, 160, 64);
}

void sound_gameover(void)
{
    modplay_sfx(sfx_gameover, SFX_LEN / 2, 200, 60);
}
