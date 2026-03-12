#ifndef INPUT_H
#define INPUT_H

#include <exec/types.h>
#include "game.h"

void input_key_down(UWORD code);
void input_key_up(UWORD code);
void input_reset(void);
void input_read(InputState *input);

#endif
