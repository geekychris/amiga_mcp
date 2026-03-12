#ifndef DRAW_H
#define DRAW_H

#include <graphics/rastport.h>
#include "game.h"

void draw_game(struct RastPort *rp, GameState *gs);
void draw_title(struct RastPort *rp, GameState *gs);
void draw_gameover(struct RastPort *rp, GameState *gs);
void draw_text_scaled(struct RastPort *rp, WORD x, WORD y, UBYTE color,
                      const char *text, WORD scale);

#endif
