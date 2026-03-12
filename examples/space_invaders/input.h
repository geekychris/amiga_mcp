#ifndef INPUT_H
#define INPUT_H

#include "game.h"

void input_init(void);
void input_read(InputState *input, struct IntuiMessage *imsg);
void input_read_mouse(InputState *input);

#endif
