/*
 * JOUST - Draw function declarations
 */
#ifndef DRAW_H
#define DRAW_H

#include <graphics/rastport.h>
#include "game.h"

void draw_title(struct RastPort *rp, GameState *gs);
void draw_playing(struct RastPort *rp, GameState *gs);
void draw_wave_intro(struct RastPort *rp, GameState *gs);
void draw_gameover(struct RastPort *rp, GameState *gs);
void draw_text(struct RastPort *rp, WORD x, WORD y, const char *str, WORD color);

#endif
