/*
 * Uranus Lander - Input definitions
 */
#ifndef INPUT_H
#define INPUT_H

#include <exec/types.h>

/* Input bits */
#define INPUT_LEFT   0x01
#define INPUT_RIGHT  0x02
#define INPUT_THRUST 0x04
#define INPUT_ESC    0x08
#define INPUT_UP     0x10
#define INPUT_DOWN   0x20
#define INPUT_MUSIC  0x40

void input_key_down(UWORD code);
void input_key_up(UWORD code);
void input_reset(void);
UWORD input_read(void);

#endif /* INPUT_H */
