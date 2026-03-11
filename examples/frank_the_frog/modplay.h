/*
 * Minimal MOD-style music player - direct Paula hardware access
 */
#ifndef MODPLAY_H
#define MODPLAY_H

#include <exec/types.h>

int  modplay_init(void);     /* allocate samples, build music */
void modplay_start(void);    /* begin playback */
void modplay_stop(void);     /* stop playback, silence Paula */
void modplay_cleanup(void);  /* free chip RAM */
void modplay_tick(void);     /* call once per frame (VBlank) */

/* Play a one-shot SFX on channel 3, temporarily overriding music */
void modplay_sfx(BYTE *data, UWORD len_words, UWORD period, UWORD volume);

#endif
