/*
 * input.h - Joystick and keyboard input for Red Baron
 * Player 1: Joystick port 2 + keyboard
 * Player 2: Joystick port 1
 */
#ifndef INPUT_H
#define INPUT_H

#include <exec/types.h>

/* Input flags */
#define INPUT_LEFT      0x0001
#define INPUT_RIGHT     0x0002
#define INPUT_UP        0x0004
#define INPUT_DOWN      0x0008
#define INPUT_FIRE      0x0010
#define INPUT_ESC       0x0020
#define INPUT_THROT_UP  0x0040
#define INPUT_THROT_DN  0x0080
#define INPUT_START     0x0100
#define INPUT_MODE      0x0200
#define INPUT_TWO_P     0x0400

/* Key-down / key-up from IDCMP_RAWKEY */
void input_key_down(UWORD code);
void input_key_up(UWORD code);
void input_reset(void);

/* Read combined joystick + keyboard for player 1 (port 2) */
UWORD input_read_p1(void);

/* Read joystick for player 2 (port 1) */
UWORD input_read_p2(void);

#endif
