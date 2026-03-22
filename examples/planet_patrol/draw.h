/*
 * Planet Patrol - Drawing declarations
 */
#ifndef DRAW_H
#define DRAW_H

#include <graphics/rastport.h>
#include "game.h"

void draw_all(struct RastPort *rp, GameState *gs);
void draw_title(struct RastPort *rp, GameState *gs);
void draw_gameover(struct RastPort *rp, GameState *gs);
void draw_hiscore_table(struct RastPort *rp, ScoreTable *st);
void draw_hiscore_entry(struct RastPort *rp, GameState *gs, char *name, WORD cursor);
void draw_level_message(struct RastPort *rp, WORD level, const char *msg);

/* 5x7 bitmap font */
void draw_string_at(struct RastPort *rp, WORD x, WORD y, const char *s, WORD scale);
WORD string_pixel_width(const char *s, WORD scale);

#endif
