#ifndef SOUND_H
#define SOUND_H

#include <exec/types.h>

int  sound_init(void);
void sound_cleanup(void);
void sound_play_shoot(void);
void sound_play_alien_explode(void);
void sound_play_player_explode(void);
void sound_play_ufo(BOOL on);
void sound_play_march(WORD note);   /* 0-3 for the 4-note cycle */
void sound_update(void);

#endif
