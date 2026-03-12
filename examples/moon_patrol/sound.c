/*
 * Moon Patrol - Sound system
 * Direct Paula hardware access for SFX and music.
 *
 * Channel 0: Shoot SFX
 * Channel 1: Explosion/death SFX
 * Channel 2: Jump/checkpoint SFX
 * Channel 3: Bouncy bass melody (Moon Patrol iconic music)
 */
#include <exec/types.h>
#include <exec/memory.h>
#include <hardware/custom.h>
#include <hardware/dmabits.h>
#include <proto/exec.h>

#include "sound.h"
#include "bridge_client.h"

extern struct Custom custom;

/* Waveform sizes (bytes, must be even) */
#define SZ_SHOOT     64
#define SZ_EXPLODE   256
#define SZ_JUMP      128
#define SZ_CHKPT     512
#define SZ_DEATH     256
#define SZ_BASS      64     /* square wave for bass melody */

#define TOTAL_SFX (SZ_SHOOT + SZ_EXPLODE + SZ_JUMP + SZ_CHKPT + SZ_DEATH + SZ_BASS)

static BYTE *sfx_block = NULL;
static BYTE *sfx_shoot = NULL;
static BYTE *sfx_explode = NULL;
static BYTE *sfx_jump = NULL;
static BYTE *sfx_chkpt = NULL;
static BYTE *sfx_death = NULL;
static BYTE *sfx_bass = NULL;

/* SFX active timers (frames remaining) */
static WORD sfx_timer[3] = { 0, 0, 0 };

/* ---- Music sequencer state ---- */

/* Moon Patrol bass line: C3, C3, G3, G3, A3, A3, F3, F3, E3, E3, G3, G3, C3, D3, E3, F3 */
/* PAL period table for bass notes (octave 3) */
#define PER_C3  428
#define PER_D3  381
#define PER_E3  339
#define PER_F3  320
#define PER_G3  285
#define PER_A3  254

static const UWORD bass_notes[] = {
    PER_C3, PER_C3, PER_G3, PER_G3,
    PER_A3, PER_A3, PER_F3, PER_F3,
    PER_E3, PER_E3, PER_G3, PER_G3,
    PER_C3, PER_D3, PER_E3, PER_F3
};
#define BASS_LEN 16
#define BASS_TEMPO 7    /* frames per note */
#define BASS_VOL   30

static WORD bass_pos = 0;       /* current note index */
static WORD bass_timer = 0;     /* frames until next note */
static WORD music_playing = 0;

/* ---- PRNG for noise ---- */
static ULONG nrng = 0xCAFEBABE;
static BYTE noise_byte(void)
{
    nrng ^= nrng << 13;
    nrng ^= nrng >> 17;
    nrng ^= nrng << 5;
    return (BYTE)(nrng & 0xFF);
}

/* ---- Waveform generators ---- */

static void build_shoot(BYTE *b)
{
    int i;
    /* Short square blip with fast decay */
    for (i = 0; i < SZ_SHOOT; i++) {
        int env = 127 - i * 127 / SZ_SHOOT;
        b[i] = (BYTE)(((i % 6) < 3 ? 80 : -80) * env / 127);
    }
}

static void build_explode(BYTE *b)
{
    int i;
    /* Noise with decay */
    for (i = 0; i < SZ_EXPLODE; i++) {
        int env = 127 - i * 127 / SZ_EXPLODE;
        b[i] = (BYTE)(noise_byte() * env / 127);
    }
}

static void build_jump(BYTE *b)
{
    int i;
    /* Ascending sine sweep: period decreases from 20 to 6 */
    int phase = 0;
    for (i = 0; i < SZ_JUMP; i++) {
        int per = 20 - i * 14 / SZ_JUMP;
        int half;
        int env = 100 - i * 80 / SZ_JUMP;
        if (per < 4) per = 4;
        half = per / 2;
        if (half == 0) half = 1;
        /* Square wave approximation */
        b[i] = (BYTE)((phase % per < half ? env : -env));
        phase++;
    }
}

static void build_checkpoint(BYTE *b)
{
    int i;
    /* Rising 4-note celebration arpeggio */
    int seg = SZ_CHKPT / 4;
    /* Note 1: period 20 */
    for (i = 0; i < seg; i++) {
        b[i] = (BYTE)((i % 20) < 10 ? 90 : -90);
    }
    /* Note 2: period 16 */
    for (i = seg; i < seg * 2; i++) {
        b[i] = (BYTE)((i % 16) < 8 ? 95 : -95);
    }
    /* Note 3: period 12 */
    for (i = seg * 2; i < seg * 3; i++) {
        b[i] = (BYTE)((i % 12) < 6 ? 100 : -100);
    }
    /* Note 4: period 10 with fade */
    for (i = seg * 3; i < SZ_CHKPT; i++) {
        int env = 110 - (i - seg * 3) * 80 / seg;
        if (env < 20) env = 20;
        b[i] = (BYTE)((i % 10) < 5 ? env : -env);
    }
}

static void build_death(BYTE *b)
{
    int i;
    /* Descending buzz + noise */
    for (i = 0; i < SZ_DEATH; i++) {
        int per = 6 + i * 20 / SZ_DEATH;
        int env = 120 - i * 100 / SZ_DEATH;
        int tone;
        int n;
        if (env < 10) env = 10;
        tone = (i % per) < (per / 2) ? 60 : -60;
        n = noise_byte() / 4;
        b[i] = (BYTE)(((tone + n) * env) / 127);
    }
}

static void build_bass(BYTE *b)
{
    int i;
    /* Square wave for punchy bass */
    for (i = 0; i < SZ_BASS; i++) {
        b[i] = (BYTE)(i < SZ_BASS / 2 ? 100 : -100);
    }
}

/* ---- Paula helpers ---- */

static void paula_play(int ch, BYTE *data, UWORD len_words, UWORD period, UWORD volume)
{
    volatile UBYTE dummy;

    custom.dmacon = (UWORD)(1 << ch);  /* disable channel */

    dummy = *(volatile UBYTE *)0xBFE001;
    dummy = *(volatile UBYTE *)0xBFE001;
    (void)dummy;

    custom.aud[ch].ac_ptr = (UWORD *)data;
    custom.aud[ch].ac_len = len_words;
    custom.aud[ch].ac_per = period;
    custom.aud[ch].ac_vol = volume;

    custom.dmacon = (UWORD)(0x8200 | (1 << ch));  /* enable */
}

/* ---- Public API ---- */

int sound_init(void)
{
    BYTE *p;

    sfx_block = (BYTE *)AllocMem(TOTAL_SFX, MEMF_CHIP | MEMF_CLEAR);
    if (!sfx_block) {
        AB_E("sound: chip RAM alloc failed (%ld bytes)", (long)TOTAL_SFX);
        return 1;
    }

    p = sfx_block;
    sfx_shoot   = p; p += SZ_SHOOT;
    sfx_explode = p; p += SZ_EXPLODE;
    sfx_jump    = p; p += SZ_JUMP;
    sfx_chkpt   = p; p += SZ_CHKPT;
    sfx_death   = p; p += SZ_DEATH;
    sfx_bass    = p;

    build_shoot(sfx_shoot);
    build_explode(sfx_explode);
    build_jump(sfx_jump);
    build_checkpoint(sfx_chkpt);
    build_death(sfx_death);
    build_bass(sfx_bass);

    bass_pos = 0;
    bass_timer = 0;
    music_playing = 0;

    /* Enable audio DMA for all channels */
    custom.dmacon = (UWORD)0x820F;

    music_playing = 1;

    AB_I("sound: init OK, %ld bytes chip RAM", (long)TOTAL_SFX);
    return 0;
}

void sound_cleanup(void)
{
    /* Disable all audio DMA */
    custom.dmacon = (UWORD)0x000F;

    /* Silence all channels */
    custom.aud[0].ac_vol = 0;
    custom.aud[1].ac_vol = 0;
    custom.aud[2].ac_vol = 0;
    custom.aud[3].ac_vol = 0;

    if (sfx_block) {
        FreeMem(sfx_block, TOTAL_SFX);
        sfx_block = NULL;
    }
}

void sound_update(void)
{
    WORD i;

    /* Tick down SFX timers */
    for (i = 0; i < 3; i++) {
        if (sfx_timer[i] > 0) {
            sfx_timer[i]--;
            if (sfx_timer[i] == 0) {
                /* Silence the channel */
                custom.aud[i].ac_vol = 0;
            }
        }
    }

    /* Music sequencer: bouncy bass on channel 3 */
    if (music_playing && sfx_bass) {
        bass_timer--;
        if (bass_timer <= 0) {
            bass_timer = BASS_TEMPO;

            /* Play current bass note */
            paula_play(3, sfx_bass, SZ_BASS / 2, bass_notes[bass_pos], BASS_VOL);

            bass_pos++;
            if (bass_pos >= BASS_LEN) {
                bass_pos = 0;
            }
        }
    }
}

void sound_shoot(void)
{
    if (sfx_shoot) {
        paula_play(0, sfx_shoot, SZ_SHOOT / 2, 180, 40);
        sfx_timer[0] = 3;
    }
}

void sound_explode(void)
{
    if (sfx_explode) {
        paula_play(1, sfx_explode, SZ_EXPLODE / 2, 150, 55);
        sfx_timer[1] = 10;
    }
}

void sound_jump(void)
{
    if (sfx_jump) {
        paula_play(2, sfx_jump, SZ_JUMP / 2, 300, 45);
        sfx_timer[2] = 8;
    }
}

void sound_checkpoint(void)
{
    if (sfx_chkpt) {
        paula_play(2, sfx_chkpt, SZ_CHKPT / 2, 200, 50);
        sfx_timer[2] = 20;
    }
}

void sound_death(void)
{
    if (sfx_death) {
        paula_play(1, sfx_death, SZ_DEATH / 2, 200, 55);
        sfx_timer[1] = 15;
    }
}
