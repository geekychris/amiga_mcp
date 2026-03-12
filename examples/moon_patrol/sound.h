/*
 * Moon Patrol - Sound declarations
 * Direct hardware Paula audio: SFX + bouncy bass melody
 */
#ifndef SOUND_H
#define SOUND_H

int  sound_init(void);
void sound_cleanup(void);
void sound_update(void);      /* call every frame for music sequencer */

void sound_shoot(void);       /* short blip on channel 0 */
void sound_explode(void);     /* noise burst on channel 1 */
void sound_jump(void);        /* ascending sweep on channel 2 */
void sound_checkpoint(void);  /* celebration jingle on channel 2 */
void sound_death(void);       /* descending buzz on channel 1 */

#endif /* SOUND_H */
