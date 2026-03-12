/*
 * Pea Shooter Blast - Input handling
 */
#ifndef INPUT_H
#define INPUT_H

#include <exec/types.h>

/* Input state bits */
#define INPUT_LEFT   1
#define INPUT_RIGHT  2
#define INPUT_JUMP   4    /* up / jump button */
#define INPUT_FIRE   8
#define INPUT_DOWN   16
#define INPUT_ESC    32

/* Read joystick port 2 + keyboard state */
UWORD input_read(void);

/* RAWKEY handlers */
void input_key_down(UWORD code);
void input_key_up(UWORD code);
void input_reset(void);

#endif
