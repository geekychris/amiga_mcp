/*
 * SKY KNIGHTS - Sound function declarations
 */
#ifndef SOUND_H
#define SOUND_H

#include <exec/types.h>

/* Initialize sound system (allocate chip RAM waveforms) */
void sound_init(void);

/* Cleanup sound system (disable DMA, free chip RAM) */
void sound_cleanup(void);

/* Trigger SFX */
void sound_flap(void);
void sound_kill(void);
void sound_egg(void);
void sound_die(void);
void sound_wave(void);

/* Update music + SFX timers - call every frame */
void sound_update(void);

#endif
