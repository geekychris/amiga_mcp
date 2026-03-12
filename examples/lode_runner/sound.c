/*
 * Lode Runner - Sound effects
 * Uses modplay for playback (direct Paula channel 3).
 * Generates SFX waveforms procedurally in chip RAM.
 */

#include <exec/types.h>
#include <exec/memory.h>
#include <proto/exec.h>

#include "bridge_client.h"
#include "modplay.h"
#include "sound.h"

/* SFX buffer sizes (bytes, must be even) */
#define SZ_DIG       256
#define SZ_FALL      128
#define SZ_GOLD      512
#define SZ_DEATH    1024
#define SZ_LEVELUP  1024
#define SZ_TRAP      256
#define SZ_STEP       32

#define TOTAL_SFX (SZ_DIG + SZ_FALL + SZ_GOLD + SZ_DEATH + SZ_LEVELUP + SZ_TRAP + SZ_STEP)

static BYTE *sfx_dig = NULL;
static BYTE *sfx_fall = NULL;
static BYTE *sfx_gold = NULL;
static BYTE *sfx_death = NULL;
static BYTE *sfx_levelup = NULL;
static BYTE *sfx_trap = NULL;
static BYTE *sfx_step_data = NULL;

static BYTE *sfx_block = NULL;

/* PRNG */
static ULONG srng = 0xDEADBEEF;
static BYTE noise(void) {
    srng ^= srng << 13;
    srng ^= srng >> 17;
    srng ^= srng << 5;
    return (BYTE)(srng & 0xFF);
}

/* Triangle helper */
static BYTE tri(int pos, int period, int amp)
{
    int q = period / 4, ph = pos % period;
    if (q == 0) return 0;
    if (ph < q) return (BYTE)(ph * amp / q);
    if (ph < 3*q) return (BYTE)(amp - (ph - q) * 2 * amp / (2*q));
    return (BYTE)(-amp + (ph - 3*q) * amp / q);
}

/* ---- SFX waveform generators ---- */

/* Dig: crunch/scrape - noise with descending tone */
static void build_dig(BYTE *b)
{
    int i;
    for (i = 0; i < SZ_DIG; i++) {
        int decay = (255 - i) * 100 / 255;  /* noise amplitude decay */
        int saw_per = 8 + i / 16;
        int saw_val;
        int n;
        if (saw_per == 0) saw_per = 1;
        saw_val = (i % saw_per) * 160 / saw_per - 80;
        n = noise();
        b[i] = (BYTE)((n * decay / 127 + saw_val) / 2);
    }
}

/* Fall: quick descending whistle - sine sweep with increasing period */
static void build_fall(BYTE *b)
{
    int i;
    int phase = 0;
    for (i = 0; i < SZ_FALL; i++) {
        /* Period increases from 8 to 32 over the sample */
        int per = 8 + i * 24 / SZ_FALL;
        int half = per / 2;
        int amp = 100 - i * 60 / SZ_FALL;
        int ph;
        if (half == 0) half = 1;
        ph = phase % per;
        /* Simple sine approximation */
        if (ph < half) {
            int x = ph * 256 / half - 128;
            b[i] = (BYTE)(amp - x * x * amp / (128 * 128));
        } else {
            int x = (ph - half) * 256 / half - 128;
            b[i] = (BYTE)(-(amp - x * x * amp / (128 * 128)));
        }
        phase++;
    }
}

/* Gold: bright pickup chime - rising 3-note triangle arpeggio */
static void build_gold(BYTE *b)
{
    int i;
    /* Note 1: triangle period 16, amp 100 */
    for (i = 0; i < 170; i++) {
        b[i] = tri(i, 16, 100);
    }
    /* Note 2: triangle period 12, amp 110 */
    for (i = 170; i < 340; i++) {
        b[i] = tri(i, 12, 110);
    }
    /* Note 3: triangle period 10, amp 120 with fade */
    for (i = 340; i < SZ_GOLD; i++) {
        int a = 120 - (i - 340) * 80 / (SZ_GOLD - 340);
        if (a < 20) a = 20;
        b[i] = tri(i, 10, a);
    }
}

/* Death: sad descending melody - 4 descending notes */
static void build_death(BYTE *b)
{
    int i;
    int note_len = SZ_DEATH / 4;

    /* Note 1: period 16, amp 100 */
    for (i = 0; i < note_len; i++) {
        b[i] = tri(i, 16, 100);
    }
    /* Note 2: period 20, amp 90 */
    for (i = note_len; i < note_len * 2; i++) {
        b[i] = tri(i, 20, 90);
    }
    /* Note 3: period 24, amp 75 */
    for (i = note_len * 2; i < note_len * 3; i++) {
        b[i] = tri(i, 24, 75);
    }
    /* Note 4: period 32, amp fading to 0 */
    for (i = note_len * 3; i < SZ_DEATH; i++) {
        int a = 65 - (i - note_len * 3) * 60 / note_len;
        if (a < 5) a = 5;
        b[i] = tri(i, 32, a);
    }
}

/* Level complete: triumphant ascending fanfare - 5 ascending notes */
static void build_levelup(BYTE *b)
{
    int i;
    int seg = SZ_LEVELUP / 5;  /* ~200 samples each */

    /* Note 1: period 28, amp 80 */
    for (i = 0; i < seg; i++) {
        b[i] = tri(i, 28, 80);
    }
    /* Note 2: period 22, amp 90 */
    for (i = seg; i < seg * 2; i++) {
        b[i] = tri(i, 22, 90);
    }
    /* Note 3: period 18, amp 100 */
    for (i = seg * 2; i < seg * 3; i++) {
        b[i] = tri(i, 18, 100);
    }
    /* Note 4: period 14, amp 110 */
    for (i = seg * 3; i < seg * 4; i++) {
        b[i] = tri(i, 14, 110);
    }
    /* Note 5: period 11, amp 120 with sustain */
    for (i = seg * 4; i < SZ_LEVELUP; i++) {
        int a = 120 - (i - seg * 4) * 40 / (SZ_LEVELUP - seg * 4);
        if (a < 60) a = 60;
        b[i] = tri(i, 11, a);
    }
}

/* Enemy trap: thud/crunch - short noise burst with fast decay */
static void build_trap(BYTE *b)
{
    int i;
    for (i = 0; i < SZ_TRAP; i++) {
        int amp = 127 - i * 2;
        if (amp < 0) amp = 0;
        b[i] = (BYTE)(noise() * amp / 127);
    }
}

/* Step: subtle click - very short square pulse */
static void build_step(BYTE *b)
{
    int i;
    for (i = 0; i < 16; i++) {
        b[i] = 60;
    }
    for (i = 16; i < SZ_STEP; i++) {
        b[i] = -60;
    }
}

/* ---- Public API ---- */

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
        AB_E("sound: SFX chip RAM alloc failed (%ld bytes)", (long)TOTAL_SFX);
        return 1;
    }

    /* Carve up the block */
    p = sfx_block;
    sfx_dig       = p; p += SZ_DIG;
    sfx_fall      = p; p += SZ_FALL;
    sfx_gold      = p; p += SZ_GOLD;
    sfx_death     = p; p += SZ_DEATH;
    sfx_levelup   = p; p += SZ_LEVELUP;
    sfx_trap      = p; p += SZ_TRAP;
    sfx_step_data = p;

    /* Generate waveforms */
    build_dig(sfx_dig);
    build_fall(sfx_fall);
    build_gold(sfx_gold);
    build_death(sfx_death);
    build_levelup(sfx_levelup);
    build_trap(sfx_trap);
    build_step(sfx_step_data);

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

void sound_dig(void)
{
    modplay_sfx(sfx_dig, SZ_DIG / 2, 200, 50);
}

void sound_fall(void)
{
    modplay_sfx(sfx_fall, SZ_FALL / 2, 160, 48);
}

void sound_gold(void)
{
    modplay_sfx(sfx_gold, SZ_GOLD / 2, 180, 56);
}

void sound_death(void)
{
    modplay_sfx(sfx_death, SZ_DEATH / 2, 220, 52);
}

void sound_level_complete(void)
{
    modplay_sfx(sfx_levelup, SZ_LEVELUP / 2, 160, 60);
}

void sound_enemy_trap(void)
{
    modplay_sfx(sfx_trap, SZ_TRAP / 2, 180, 48);
}

void sound_step(void)
{
    modplay_sfx(sfx_step_data, SZ_STEP / 2, 300, 32);
}
