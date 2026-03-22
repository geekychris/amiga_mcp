/*
 * Gold Rush - Graphics layer (custom screen, double buffer)
 */
#ifndef GFX_H
#define GFX_H

#include <exec/types.h>
#include <graphics/rastport.h>
#include <intuition/screens.h>

/* Initialize custom screen + double buffer. Returns 0 on success. */
int gfx_init(void);

/* Cleanup screen */
void gfx_cleanup(void);

/* Get current draw rastport (back buffer) */
struct RastPort *gfx_backbuffer(void);

/* Swap buffers (display back, draw to new back) */
void gfx_swap(void);

/* Wait for vertical blank */
void gfx_vsync(void);

/* Get the screen pointer (for IDCMP window) */
struct Screen *gfx_screen(void);

/* Set palette from built-in Gold Rush palette */
void gfx_set_palette(void);

#endif /* GFX_H */
