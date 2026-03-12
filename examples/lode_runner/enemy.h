/*
 * Lode Runner - Enemy AI
 */
#ifndef ENEMY_H
#define ENEMY_H

#include "game.h"

/* Update all enemies */
void enemy_update_all(GameState *gs);

/* Check if any enemy overlaps the player - returns 1 if caught */
int enemy_check_collision(GameState *gs);

/* Check if enemy at grid pos should be trapped (brick was dug) */
void enemy_trap_check(GameState *gs, int x, int y);

#endif /* ENEMY_H */
