/*
 * Gold Rush - Level editor
 */
#ifndef EDITOR_H
#define EDITOR_H

#include <graphics/rastport.h>
#include "game.h"

/* Enter editor mode. level_num=0 for blank, >0 to load existing. */
void editor_init(GameState *gs, int level_num);

/* Process editor input and logic */
void editor_update(GameState *gs);

/* Draw editor view */
void editor_draw(struct RastPort *rp, GameState *gs);

#endif /* EDITOR_H */
