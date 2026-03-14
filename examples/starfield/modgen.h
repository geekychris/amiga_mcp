/*
 * modgen.h - Procedural ProTracker MOD generator
 * Builds a Star Trek TOS-inspired theme tune in chip RAM.
 */
#ifndef MODGEN_H
#define MODGEN_H

#include <exec/types.h>

/* Generate a MOD file in chip RAM.
 * Returns pointer to chip RAM buffer (caller must FreeMem).
 * Sets *out_size to the total size of the MOD data.
 * Returns NULL on failure.
 */
UBYTE *generate_startrek_mod(ULONG *out_size);

#endif
