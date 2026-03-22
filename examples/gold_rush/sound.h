/*
 * Gold Rush - Sound effects via Paula
 */
#ifndef SOUND_H
#define SOUND_H

int  sound_init(void);
void sound_cleanup(void);

void sound_dig(void);            /* brick being dug */
void sound_fall(void);           /* player/enemy falling */
void sound_gold(void);           /* gold collected */
void sound_death(void);          /* player caught/killed */
void sound_level_complete(void); /* all gold + escaped */
void sound_enemy_trap(void);     /* enemy falls in hole */
void sound_step(void);           /* footstep */

#endif /* SOUND_H */
