/*
 * sound.h - Sound effects for Ace Pilot
 */
#ifndef SOUND_H
#define SOUND_H

#include <exec/types.h>
#include "ptplayer.h"

#define CUSTOM_BASE ((void *)0xdff000)

void sound_init(void);
void sound_cleanup(void);

void sfx_gunfire(void);
void sfx_explosion(void);
void sfx_hit(void);
void sfx_die(void);

#endif
