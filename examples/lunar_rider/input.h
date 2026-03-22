/*
 * Lunar Rider - Input declarations
 */
#ifndef INPUT_H
#define INPUT_H

#include <intuition/intuition.h>
#include "game.h"

/* Initialize input system */
void input_init(struct Window *win);

/* Process IDCMP messages and read joystick - call once per frame */
void input_update(InputState *inp);

#endif /* INPUT_H */
