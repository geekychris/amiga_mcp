/*
 * Planet Patrol - Sound declarations
 */
#ifndef SOUND_H
#define SOUND_H

#include "game.h"

void sound_init(void);
void sound_cleanup(void);
WORD sound_load_mod(const char *filename);
void sound_start_music(void);
void sound_stop_music(void);
void sound_update(GameState *gs);

void sfx_laser(void);
void sfx_explode(void);
void sfx_explode_big(void);
void sfx_pickup(void);
void sfx_human_die(void);
void sfx_bomb(void);
void sfx_hyper(void);
void sfx_die(void);
void sfx_level(void);

#endif
