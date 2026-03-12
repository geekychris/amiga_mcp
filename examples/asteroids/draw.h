/*
 * Asteroids - Drawing routines
 */
#ifndef DRAW_H
#define DRAW_H

#include <graphics/rastport.h>
#include "game.h"

void draw_clear(struct RastPort *rp);
void draw_ship(struct RastPort *rp, Ship *ship, WORD frame);
void draw_rocks(struct RastPort *rp, Rock *rocks);
void draw_bullets(struct RastPort *rp, Bullet *bullets);
void draw_particles(struct RastPort *rp, Particle *particles);
void draw_hud(struct RastPort *rp, GameState *gs);
void draw_title(struct RastPort *rp);
void draw_gameover(struct RastPort *rp, LONG score);

#endif
