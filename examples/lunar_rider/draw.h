/*
 * Lunar Rider - Drawing declarations
 */
#ifndef DRAW_H
#define DRAW_H

#include <graphics/rastport.h>
#include "game.h"

void draw_title(struct RastPort *rp, WORD frame);
void draw_game(struct RastPort *rp, GameState *gs);
void draw_hud(struct RastPort *rp, GameState *gs);
void draw_gameover(struct RastPort *rp, LONG score);
void draw_checkpoint(struct RastPort *rp, WORD checkpoint);
void draw_string_at(struct RastPort *rp, WORD x, WORD y, const char *s, WORD scale);
WORD string_pixel_width(const char *s, WORD scale);

#endif /* DRAW_H */
