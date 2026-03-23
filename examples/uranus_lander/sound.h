/*
 * Uranus Lander - Sound effect definitions
 */
#ifndef SOUND_H
#define SOUND_H

#include <exec/types.h>

void sound_init(void);
void sound_cleanup(void);

/* SFX trigger functions */
void sfx_thrust_play(void);
void sfx_thrust_stop(void);
void sfx_crash_play(void);
void sfx_land_play(void);
void sfx_beep_play(void);

#endif /* SOUND_H */
