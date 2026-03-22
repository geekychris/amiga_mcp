/*
 * Gold Rush - Player logic
 */
#ifndef PLAYER_H
#define PLAYER_H

#include "game.h"

/* Update player movement and actions.
 * dx/dy: directional input (-1/0/1)
 * dig_left/dig_right: dig input flags */
void player_update(GameState *gs, int dx, int dy, int dig_left, int dig_right);

#endif /* PLAYER_H */
