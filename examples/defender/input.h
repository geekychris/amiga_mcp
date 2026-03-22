/*
 * Defender - Input handling declarations
 */
#ifndef INPUT_H
#define INPUT_H

#include "game.h"

void input_init(InputData *id);
void input_key_event(InputData *id, UWORD code);
WORD input_read(InputData *id);
void input_cleanup(void);

#endif
