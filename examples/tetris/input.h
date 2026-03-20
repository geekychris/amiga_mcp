#ifndef INPUT_H
#define INPUT_H

#include <exec/types.h>

#define INPUT_LEFT   0x01
#define INPUT_RIGHT  0x02
#define INPUT_DOWN   0x04
#define INPUT_UP     0x08
#define INPUT_FIRE   0x10
#define INPUT_ESC    0x20
#define INPUT_START  0x40
#define INPUT_PAUSE  0x80

void input_init(void);
void input_cleanup(void);
UWORD input_read(void);
void input_handle_key(UWORD code, UWORD qualifier);
UWORD input_edge(UWORD current);

#endif
