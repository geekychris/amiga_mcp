/*
 * Frank the Frog - Playfield drawing
 */
#ifndef PLAYFIELD_H
#define PLAYFIELD_H

#include <graphics/rastport.h>

/* Draw the static playfield (grass, road, water, home bases) */
void playfield_draw(struct RastPort *rp);

#endif /* PLAYFIELD_H */
