/*
 * Uranus Lander - Drawing function declarations
 */
#ifndef DRAW_H
#define DRAW_H

#include <graphics/rastport.h>
#include "game.h"

void planet_gfx_init(void);
void planet_gfx_cleanup(void);
void draw_clear(struct RastPort *rp);
void draw_stars(struct RastPort *rp, GameState *gs);
void draw_terrain(struct RastPort *rp, GameState *gs);
void draw_ship(struct RastPort *rp, GameState *gs);
void draw_particles(struct RastPort *rp, GameState *gs);
void draw_hud(struct RastPort *rp, GameState *gs);
void draw_title(struct RastPort *rp, GameState *gs);
void draw_landed(struct RastPort *rp, GameState *gs);
void draw_crash(struct RastPort *rp, GameState *gs);
void draw_gameover(struct RastPort *rp, GameState *gs);
void draw_enter_name(struct RastPort *rp, GameState *gs);

#endif /* DRAW_H */
