/*
 * SKY KNIGHTS - Input function declarations
 */
#ifndef INPUT_H
#define INPUT_H

#include <exec/types.h>
#include "game.h"

/* RAWKEY handlers */
void input_key_down(UWORD code);
void input_key_up(UWORD code);
void input_reset(void);

/* Read combined keyboard + joystick into InputState */
void input_read(InputState *input);

#endif
