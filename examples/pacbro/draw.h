#ifndef DRAW_H
#define DRAW_H

#include <exec/types.h>
#include <graphics/rastport.h>
#include "game.h"

/* Draw the full static maze (walls, dots, all tiles). Call once per buffer. */
void draw_maze_full(struct RastPort *rp, GameState *gs);

/* Per-frame update: erase old sprites, restore tiles, draw new sprites */
void draw_game(struct RastPort *rp, GameState *gs);

/* Full-screen draws */
void draw_title(struct RastPort *rp, GameState *gs);
void draw_gameover(struct RastPort *rp, GameState *gs);

/* Signal that maze needs full redraw (level change, etc) */
void draw_set_dirty(void);

#endif
