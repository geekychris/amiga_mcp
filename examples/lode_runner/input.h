/*
 * Lode Runner - Input abstraction (joystick + keyboard)
 */
#ifndef INPUT_H
#define INPUT_H

#include <intuition/intuition.h>

/* Key constants (Amiga raw keycodes) */
#define KEY_ESC    0x45
#define KEY_SPACE  0x40
#define KEY_RETURN 0x44
#define KEY_Z      0x31
#define KEY_X      0x32
#define KEY_E      0x12
#define KEY_S      0x21
#define KEY_P      0x19
#define KEY_F1     0x50
#define KEY_F2     0x51
#define KEY_F3     0x52
#define KEY_UP     0x4C
#define KEY_DOWN   0x4D
#define KEY_LEFT   0x4F
#define KEY_RIGHT  0x4E

/* Initialize input system with the IDCMP window */
void input_init(struct Window *win);

/* Update input state - call once per frame */
void input_update(void);

/* Directional input: returns -1, 0, or 1 (held state) */
int input_dx(void);
int input_dy(void);

/* Fire button: edge-detected (true only on press frame) */
int input_fire(void);

/* Fire button: held state */
int input_fire_held(void);

/* Specific key pressed this frame (edge-detected) */
int input_key(int rawkey);

/* Any key was pressed this frame */
int input_any_key(void);

#endif /* INPUT_H */
