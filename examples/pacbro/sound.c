/*
 * PACBRO - Sound
 * Authentic Pac-Man sounds via direct Paula chip access
 *
 * Channel 0: Chomp waka-waka
 * Channel 1: SFX (eat ghost, die, fruit, extra life)
 * Channel 2: Siren (rising/falling tone)
 * Channel 3: Power pellet drone / intermission
 */
#include <exec/types.h>
#include <exec/memory.h>
#include <hardware/custom.h>
#include <hardware/dmabits.h>
#include <proto/exec.h>
#include "sound.h"

#define CUSTOM ((volatile struct Custom *)0xDFF000)

#define WAVE_LEN     32
#define NOISE_LEN    128
#define PAL_CLOCK    3546895UL

/* Waveform buffers in chip RAM */
static BYTE *wave_square;
static BYTE *wave_sine;
static BYTE *wave_noise;
static BYTE *wave_chomp;

/* Channel timers (frames remaining) */
static WORD ch_timer[4];

/* Chomp alternation */
static WORD chomp_toggle;

/* Siren state */
static WORD siren_phase;
static WORD siren_on;
static WORD siren_speed_level; /* 0=slow, 1=med, 2=fast */

/* Power pellet drone */
static WORD power_on;
static WORD power_phase;

/* Death melody state */
static WORD death_active;
static WORD death_pos;
static WORD death_timer;

/* Death melody: descending notes */
static const UWORD death_notes[] = {
    400, 424, 450, 476, 504, 534, 566, 600,
    636, 674, 714, 756, 800, 848, 900, 952
};
#define DEATH_LEN 16
#define DEATH_SPEED 4

/* Eat ghost melody */
static WORD eat_active;
static WORD eat_pos;
static WORD eat_timer;
static const UWORD eat_notes[] = {
    600, 504, 424, 356, 300, 252, 212, 178
};
#define EAT_LEN 8
#define EAT_SPEED 3

int sound_init(void)
{
    int i;

    wave_square = (BYTE *)AllocMem(WAVE_LEN, MEMF_CHIP | MEMF_CLEAR);
    wave_sine   = (BYTE *)AllocMem(WAVE_LEN, MEMF_CHIP | MEMF_CLEAR);
    wave_noise  = (BYTE *)AllocMem(NOISE_LEN, MEMF_CHIP | MEMF_CLEAR);
    wave_chomp  = (BYTE *)AllocMem(WAVE_LEN, MEMF_CHIP | MEMF_CLEAR);

    if (!wave_square || !wave_sine || !wave_noise || !wave_chomp) {
        sound_cleanup();
        return -1;
    }

    /* Square wave */
    for (i = 0; i < WAVE_LEN / 2; i++) wave_square[i] = 100;
    for (i = WAVE_LEN / 2; i < WAVE_LEN; i++) wave_square[i] = -100;

    /* Sine wave */
    {
        static const BYTE sine_tab[32] = {
            0, 25, 49, 71, 90, 106, 117, 125,
            127, 125, 117, 106, 90, 71, 49, 25,
            0, -25, -49, -71, -90, -106, -117, -125,
            -127, -125, -117, -106, -90, -71, -49, -25
        };
        for (i = 0; i < 32; i++) wave_sine[i] = sine_tab[i];
    }

    /* Noise */
    {
        ULONG seed = 7919;
        for (i = 0; i < NOISE_LEN; i++) {
            seed = seed * 1103515245 + 12345;
            wave_noise[i] = (BYTE)((seed >> 16) & 0xFF);
        }
    }

    /* Chomp waveform: asymmetric pulse for "waka" sound */
    for (i = 0; i < 6; i++) wave_chomp[i] = 120;
    for (i = 6; i < 16; i++) wave_chomp[i] = -60;
    for (i = 16; i < 20; i++) wave_chomp[i] = 80;
    for (i = 20; i < WAVE_LEN; i++) wave_chomp[i] = -40;

    /* Disable all audio DMA */
    CUSTOM->dmacon = DMAF_AUD0 | DMAF_AUD1 | DMAF_AUD2 | DMAF_AUD3;

    for (i = 0; i < 4; i++) ch_timer[i] = 0;
    chomp_toggle = 0;
    siren_phase = 0;
    siren_on = 0;
    siren_speed_level = 0;
    power_on = 0;
    power_phase = 0;
    death_active = 0;
    eat_active = 0;

    return 0;
}

void sound_cleanup(void)
{
    int ch;

    CUSTOM->dmacon = DMAF_AUD0 | DMAF_AUD1 | DMAF_AUD2 | DMAF_AUD3;

    for (ch = 0; ch < 4; ch++) {
        CUSTOM->aud[ch].ac_vol = 0;
        CUSTOM->aud[ch].ac_per = 1;
        CUSTOM->aud[ch].ac_len = 1;
    }

    if (wave_square) { FreeMem(wave_square, WAVE_LEN); wave_square = NULL; }
    if (wave_sine)   { FreeMem(wave_sine, WAVE_LEN);   wave_sine = NULL; }
    if (wave_noise)  { FreeMem(wave_noise, NOISE_LEN);  wave_noise = NULL; }
    if (wave_chomp)  { FreeMem(wave_chomp, WAVE_LEN);   wave_chomp = NULL; }
}

void sound_chomp(void)
{
    /* Alternating waka-waka tone */
    UWORD period = chomp_toggle ? 500 : 600;
    chomp_toggle = !chomp_toggle;

    CUSTOM->dmacon = DMAF_AUD0;
    CUSTOM->aud[0].ac_ptr = (UWORD *)wave_chomp;
    CUSTOM->aud[0].ac_len = WAVE_LEN / 2;
    CUSTOM->aud[0].ac_per = period;
    CUSTOM->aud[0].ac_vol = 40;
    CUSTOM->dmacon = DMAF_SETCLR | DMAF_AUD0;

    ch_timer[0] = 4;
}

void sound_eat_ghost(void)
{
    eat_active = 1;
    eat_pos = 0;
    eat_timer = 0;
}

void sound_die(void)
{
    death_active = 1;
    death_pos = 0;
    death_timer = 0;
    /* Stop siren and power */
    siren_on = 0;
    power_on = 0;
    CUSTOM->dmacon = DMAF_AUD2 | DMAF_AUD3;
}

void sound_fruit(void)
{
    /* Ascending blip on channel 1 */
    CUSTOM->dmacon = DMAF_AUD1;
    CUSTOM->aud[1].ac_ptr = (UWORD *)wave_sine;
    CUSTOM->aud[1].ac_len = WAVE_LEN / 2;
    CUSTOM->aud[1].ac_per = 250;
    CUSTOM->aud[1].ac_vol = 45;
    CUSTOM->dmacon = DMAF_SETCLR | DMAF_AUD1;
    ch_timer[1] = 10;
}

void sound_extra_life(void)
{
    /* Short bright jingle on channel 1 */
    CUSTOM->dmacon = DMAF_AUD1;
    CUSTOM->aud[1].ac_ptr = (UWORD *)wave_sine;
    CUSTOM->aud[1].ac_len = WAVE_LEN / 2;
    CUSTOM->aud[1].ac_per = 200;
    CUSTOM->aud[1].ac_vol = 50;
    CUSTOM->dmacon = DMAF_SETCLR | DMAF_AUD1;
    ch_timer[1] = 20;
}

void sound_power(void)
{
    power_on = 1;
    power_phase = 0;
}

void sound_set_siren(int speed)
{
    siren_on = 1;
    siren_speed_level = speed;
}

void sound_siren_off(void)
{
    siren_on = 0;
    CUSTOM->dmacon = DMAF_AUD2;
}

void sound_update(void)
{
    int i;

    /* Handle channel timers */
    for (i = 0; i < 4; i++) {
        if (ch_timer[i] > 0) {
            ch_timer[i]--;
            if (ch_timer[i] == 0) {
                CUSTOM->dmacon = (DMAF_AUD0 << i);
            }
        }
    }

    /* Death melody on channel 1 */
    if (death_active) {
        death_timer++;
        if (death_timer >= DEATH_SPEED) {
            death_timer = 0;

            CUSTOM->dmacon = DMAF_AUD1;
            CUSTOM->aud[1].ac_ptr = (UWORD *)wave_sine;
            CUSTOM->aud[1].ac_len = WAVE_LEN / 2;
            CUSTOM->aud[1].ac_per = death_notes[death_pos];
            CUSTOM->aud[1].ac_vol = 45;
            CUSTOM->dmacon = DMAF_SETCLR | DMAF_AUD1;

            ch_timer[1] = DEATH_SPEED - 1;
            death_pos++;
            if (death_pos >= DEATH_LEN) {
                death_active = 0;
            }
        }
    }

    /* Eat ghost ascending tone on channel 1 */
    if (eat_active) {
        eat_timer++;
        if (eat_timer >= EAT_SPEED) {
            eat_timer = 0;

            CUSTOM->dmacon = DMAF_AUD1;
            CUSTOM->aud[1].ac_ptr = (UWORD *)wave_square;
            CUSTOM->aud[1].ac_len = WAVE_LEN / 2;
            CUSTOM->aud[1].ac_per = eat_notes[eat_pos];
            CUSTOM->aud[1].ac_vol = 40;
            CUSTOM->dmacon = DMAF_SETCLR | DMAF_AUD1;

            ch_timer[1] = EAT_SPEED - 1;
            eat_pos++;
            if (eat_pos >= EAT_LEN) {
                eat_active = 0;
            }
        }
    }

    /* Siren on channel 2 */
    if (siren_on && !power_on) {
        WORD base_period;
        WORD range;
        WORD step;

        switch (siren_speed_level) {
        case 0: base_period = 500; range = 100; step = 2; break;
        case 1: base_period = 420; range = 80;  step = 3; break;
        default: base_period = 350; range = 60;  step = 4; break;
        }

        siren_phase += step;
        if (siren_phase >= 128) siren_phase -= 128;

        {
            /* Triangle wave oscillation */
            WORD pos = siren_phase;
            WORD offset;
            if (pos < 64) {
                offset = (pos * range) / 64;
            } else {
                offset = ((128 - pos) * range) / 64;
            }

            CUSTOM->aud[2].ac_ptr = (UWORD *)wave_sine;
            CUSTOM->aud[2].ac_len = WAVE_LEN / 2;
            CUSTOM->aud[2].ac_per = base_period - offset;
            CUSTOM->aud[2].ac_vol = 25;
            CUSTOM->dmacon = DMAF_SETCLR | DMAF_AUD2;
        }
    }

    /* Power pellet drone on channel 3 (replaces siren) */
    if (power_on) {
        power_phase++;
        {
            WORD per = 280 + ((power_phase & 4) ? 40 : -40);
            CUSTOM->aud[3].ac_ptr = (UWORD *)wave_square;
            CUSTOM->aud[3].ac_len = WAVE_LEN / 2;
            CUSTOM->aud[3].ac_per = per;
            CUSTOM->aud[3].ac_vol = 20;
            CUSTOM->dmacon = DMAF_SETCLR | DMAF_AUD3;
        }
    }
}
