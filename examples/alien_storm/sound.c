#include <exec/types.h>
#include <exec/memory.h>
#include <hardware/custom.h>
#include <hardware/dmabits.h>
#include <proto/exec.h>
#include "sound.h"

/* Direct access to custom chip registers */
#define CUSTOM ((volatile struct Custom *)0xDFF000)

#define WAVE_LEN     32
#define NOISE_LEN    128

/* PAL audio clock */
#define PAL_CLOCK 3546895UL

/* Waveform buffers in chip RAM */
static BYTE *wave_square;
static BYTE *wave_sine;
static BYTE *wave_noise;
static BYTE *wave_shoot;

/* Channel state for timed one-shot sounds */
static WORD ch_timer[4];    /* frames remaining, 0 = idle */

/* UFO state */
static BOOL ufo_active;
static WORD ufo_phase;

/* March note periods (32-sample square wave) */
/* Notes: C3, Bb2, Ab2, G2 - descending pattern */
static const UWORD march_periods[4] = { 846, 950, 1066, 1130 };

/* Melody pattern - simple repeating arpeggio on channel 3 */
static const UWORD melody_periods[] = {
    504, 423, 336, 423, 504, 566, 504, 423,
    377, 336, 283, 336, 377, 423, 504, 566
};
#define MELODY_LEN 16
static WORD melody_pos;
static WORD melody_timer;
#define MELODY_SPEED 12  /* frames per note */

int sound_init(void)
{
    int i;

    /* Allocate chip RAM for waveforms */
    wave_square = (BYTE *)AllocMem(WAVE_LEN, MEMF_CHIP | MEMF_CLEAR);
    wave_sine   = (BYTE *)AllocMem(WAVE_LEN, MEMF_CHIP | MEMF_CLEAR);
    wave_noise  = (BYTE *)AllocMem(NOISE_LEN, MEMF_CHIP | MEMF_CLEAR);
    wave_shoot  = (BYTE *)AllocMem(WAVE_LEN, MEMF_CHIP | MEMF_CLEAR);

    if (!wave_square || !wave_sine || !wave_noise || !wave_shoot) {
        sound_cleanup();
        return -1;
    }

    /* Generate square wave */
    for (i = 0; i < WAVE_LEN / 2; i++) wave_square[i] = 100;
    for (i = WAVE_LEN / 2; i < WAVE_LEN; i++) wave_square[i] = -100;

    /* Generate sine-ish wave (32 samples) */
    {
        static const BYTE sine_tab[32] = {
            0, 25, 49, 71, 90, 106, 117, 125,
            127, 125, 117, 106, 90, 71, 49, 25,
            0, -25, -49, -71, -90, -106, -117, -125,
            -127, -125, -117, -106, -90, -71, -49, -25
        };
        for (i = 0; i < 32; i++) wave_sine[i] = sine_tab[i];
    }

    /* Generate noise */
    {
        ULONG seed = 7919;
        for (i = 0; i < NOISE_LEN; i++) {
            seed = seed * 1103515245 + 12345;
            wave_noise[i] = (BYTE)((seed >> 16) & 0xFF);
        }
    }

    /* Generate shoot waveform - sharp pulse */
    for (i = 0; i < 4; i++) wave_shoot[i] = 127;
    for (i = 4; i < WAVE_LEN; i++) wave_shoot[i] = -40;

    /* Disable all audio DMA initially */
    CUSTOM->dmacon = DMAF_AUD0 | DMAF_AUD1 | DMAF_AUD2 | DMAF_AUD3;

    for (i = 0; i < 4; i++) ch_timer[i] = 0;
    ufo_active = FALSE;
    ufo_phase = 0;
    melody_pos = 0;
    melody_timer = 0;

    return 0;
}

void sound_cleanup(void)
{
    /* Stop all audio DMA */
    CUSTOM->dmacon = DMAF_AUD0 | DMAF_AUD1 | DMAF_AUD2 | DMAF_AUD3;

    /* Zero out all channel registers to be safe */
    {
        int ch;
        for (ch = 0; ch < 4; ch++) {
            CUSTOM->aud[ch].ac_vol = 0;
            CUSTOM->aud[ch].ac_per = 1;
            CUSTOM->aud[ch].ac_len = 1;
        }
    }

    if (wave_square) { FreeMem(wave_square, WAVE_LEN); wave_square = NULL; }
    if (wave_sine)   { FreeMem(wave_sine, WAVE_LEN);   wave_sine = NULL; }
    if (wave_noise)  { FreeMem(wave_noise, NOISE_LEN);  wave_noise = NULL; }
    if (wave_shoot)  { FreeMem(wave_shoot, WAVE_LEN);   wave_shoot = NULL; }
}

/* Play the march bass note on channel 0 */
void sound_play_march(WORD note)
{
    if (note < 0 || note > 3) note = 0;

    CUSTOM->dmacon = DMAF_AUD0;  /* stop ch0 first */

    CUSTOM->aud[0].ac_ptr = (UWORD *)wave_square;
    CUSTOM->aud[0].ac_len = WAVE_LEN / 2;
    CUSTOM->aud[0].ac_per = march_periods[note];
    CUSTOM->aud[0].ac_vol = 50;

    CUSTOM->dmacon = DMAF_SETCLR | DMAF_AUD0;

    ch_timer[0] = 6;  /* play for 6 frames (~120ms) */
}

/* Shoot SFX on channel 1 */
void sound_play_shoot(void)
{
    CUSTOM->dmacon = DMAF_AUD1;

    CUSTOM->aud[1].ac_ptr = (UWORD *)wave_shoot;
    CUSTOM->aud[1].ac_len = WAVE_LEN / 2;
    CUSTOM->aud[1].ac_per = 200;
    CUSTOM->aud[1].ac_vol = 40;

    CUSTOM->dmacon = DMAF_SETCLR | DMAF_AUD1;

    ch_timer[1] = 4;
}

/* Alien explosion on channel 1 */
void sound_play_alien_explode(void)
{
    CUSTOM->dmacon = DMAF_AUD1;

    CUSTOM->aud[1].ac_ptr = (UWORD *)wave_noise;
    CUSTOM->aud[1].ac_len = NOISE_LEN / 2;
    CUSTOM->aud[1].ac_per = 180;
    CUSTOM->aud[1].ac_vol = 48;

    CUSTOM->dmacon = DMAF_SETCLR | DMAF_AUD1;

    ch_timer[1] = 8;
}

/* Player explosion on channel 1 - longer, deeper */
void sound_play_player_explode(void)
{
    CUSTOM->dmacon = DMAF_AUD1;

    CUSTOM->aud[1].ac_ptr = (UWORD *)wave_noise;
    CUSTOM->aud[1].ac_len = NOISE_LEN / 2;
    CUSTOM->aud[1].ac_per = 350;
    CUSTOM->aud[1].ac_vol = 60;

    CUSTOM->dmacon = DMAF_SETCLR | DMAF_AUD1;

    ch_timer[1] = 25;
}

/* UFO warble on channel 2 */
void sound_play_ufo(BOOL on)
{
    if (on && !ufo_active) {
        ufo_active = TRUE;
        ufo_phase = 0;

        CUSTOM->aud[2].ac_ptr = (UWORD *)wave_sine;
        CUSTOM->aud[2].ac_len = WAVE_LEN / 2;
        CUSTOM->aud[2].ac_per = 300;
        CUSTOM->aud[2].ac_vol = 32;

        CUSTOM->dmacon = DMAF_SETCLR | DMAF_AUD2;
    } else if (!on && ufo_active) {
        ufo_active = FALSE;
        CUSTOM->dmacon = DMAF_AUD2;
    }
}

void sound_update(void)
{
    int i;

    /* Handle one-shot channel timers */
    for (i = 0; i < 4; i++) {
        if (ch_timer[i] > 0) {
            ch_timer[i]--;
            if (ch_timer[i] == 0) {
                /* Stop this channel's DMA */
                CUSTOM->dmacon = (DMAF_AUD0 << i);
            }
        }
    }

    /* UFO warble modulation */
    if (ufo_active) {
        ufo_phase++;
        /* Oscillate period between 250-350 for warble effect */
        WORD per = 300 + ((ufo_phase & 8) ? 50 : -50);
        CUSTOM->aud[2].ac_per = per;
    }

    /* Background melody on channel 3 */
    melody_timer++;
    if (melody_timer >= MELODY_SPEED) {
        melody_timer = 0;

        CUSTOM->dmacon = DMAF_AUD3;

        CUSTOM->aud[3].ac_ptr = (UWORD *)wave_sine;
        CUSTOM->aud[3].ac_len = WAVE_LEN / 2;
        CUSTOM->aud[3].ac_per = melody_periods[melody_pos];
        CUSTOM->aud[3].ac_vol = 20;

        CUSTOM->dmacon = DMAF_SETCLR | DMAF_AUD3;

        ch_timer[3] = MELODY_SPEED - 2;
        melody_pos = (melody_pos + 1) % MELODY_LEN;
    }
}
