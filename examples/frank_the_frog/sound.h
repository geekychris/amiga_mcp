/*
 * Frank the Frog - Sound effects via Paula
 */
#ifndef SOUND_H
#define SOUND_H

/* Initialize audio (allocate channel) */
int sound_init(void);

/* Cleanup audio */
void sound_cleanup(void);

/* Play sound effects */
void sound_hop(void);
void sound_splat(void);
void sound_splash(void);
void sound_home(void);
void sound_levelup(void);
void sound_gameover(void);

#endif /* SOUND_H */
