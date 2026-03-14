/*
 * ptplayer.h - C interface to Frank Wille's ptplayer V5.1
 * All functions expect a6 = $dff000 (CUSTOM), passed via inline asm.
 */
#ifndef PTPLAYER_H
#define PTPLAYER_H

#include <exec/types.h>

/* SFX structure for mt_playfx */
typedef struct {
    APTR  sfx_ptr;   /* Pointer to sample start in Chip RAM */
    WORD  sfx_len;   /* Sample length in words */
    WORD  sfx_per;   /* Hardware replay period */
    WORD  sfx_vol;   /* Volume 0..64 */
    BYTE  sfx_cha;   /* Channel 0..3, or -1 for auto */
    BYTE  sfx_pri;   /* Priority (non-zero) */
} SfxStructure;

/* Assembly functions */
extern void mt_install_cia(void *custom asm("a6"),
                           void *autovec asm("a0"),
                           UBYTE pal asm("d0"));
extern void mt_remove_cia(void *custom asm("a6"));
extern void mt_init(void *custom asm("a6"),
                    APTR module asm("a0"),
                    APTR samples asm("a1"),
                    UBYTE songpos asm("d0"));
extern void mt_end(void *custom asm("a6"));
extern void mt_playfx(void *custom asm("a6"),
                      SfxStructure *sfx asm("a0"));
extern void mt_musicmask(void *custom asm("a6"),
                         UBYTE mask asm("d0"));
extern void mt_mastervol(void *custom asm("a6"),
                         UWORD vol asm("d0"));
extern void mt_music(void *custom asm("a6"));

/* Variables */
extern UBYTE mt_Enable;
extern UBYTE mt_E8Trigger;
extern UBYTE mt_MusicChannels;

#endif
