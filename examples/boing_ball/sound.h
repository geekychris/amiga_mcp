#ifndef SOUND_H
#define SOUND_H

#include <exec/types.h>

BOOL sound_init(void);
void sound_play_boing(void);
void sound_cleanup(void);

#endif
