/*
 * Moon Patrol - Input handling
 * Keyboard + Joystick port 2, simultaneous support
 */
#include <exec/types.h>
#include <intuition/intuition.h>
#include <hardware/cia.h>
#include <hardware/custom.h>
#include <proto/exec.h>
#include <proto/intuition.h>
#include <string.h>

#include "game.h"
#include "input.h"

extern struct Custom custom;

/* Amiga raw keycodes */
#define KEY_ESC     0x45
#define KEY_SPACE   0x40
#define KEY_UP      0x4C
#define KEY_LEFT    0x4F
#define KEY_RIGHT   0x4E
#define KEY_A       0x20
#define KEY_D       0x22
#define KEY_W       0x11

#define MAX_KEYS    128

static struct Window *inp_win = NULL;

/* Key held state */
static UBYTE keys_held[MAX_KEYS];

void input_init(struct Window *win)
{
    inp_win = win;
    memset(keys_held, 0, sizeof(keys_held));
}

void input_update(InputState *inp)
{
    volatile UWORD joy;
    UBYTE ciaa_pra;

    /* Reset input state */
    inp->left = 0;
    inp->right = 0;
    inp->jump = 0;
    inp->fire = 0;
    inp->quit = 0;

    /* Process IDCMP keyboard messages */
    if (inp_win) {
        struct IntuiMessage *imsg;
        while ((imsg = (struct IntuiMessage *)GetMsg(inp_win->UserPort))) {
            ULONG cl = imsg->Class;
            UWORD code = imsg->Code;
            ReplyMsg((struct Message *)imsg);

            if (cl == IDCMP_RAWKEY) {
                WORD keycode = code & 0x7F;
                WORD released = code & 0x80;

                if (keycode < MAX_KEYS) {
                    keys_held[keycode] = released ? 0 : 1;
                }
            }
        }
    }

    /* Map keyboard to input */
    if (keys_held[KEY_LEFT])  inp->left = 1;
    if (keys_held[KEY_RIGHT]) inp->right = 1;
    if (keys_held[KEY_UP])    inp->jump = 1;
    if (keys_held[KEY_SPACE]) inp->jump = 1;
    if (keys_held[KEY_A])     inp->fire = 1;
    if (keys_held[KEY_W])     inp->jump = 1;
    if (keys_held[KEY_D])     inp->right = 1;
    if (keys_held[KEY_ESC])   inp->quit = 1;

    /* Read joystick port 2 (JOY1DAT) */
    joy = custom.joy1dat;

    /* Decode joystick directions */
    if ((joy >> 1) & 1)                    inp->right = 1;  /* right */
    if ((joy >> 9) & 1)                    inp->left = 1;   /* left */
    if (((joy >> 9) ^ (joy >> 8)) & 1)    inp->jump = 1;   /* up */

    /* Fire button: CIA-A PRA bit 7, active low */
    ciaa_pra = *((volatile UBYTE *)0xBFE001);
    if (!(ciaa_pra & 0x80)) inp->fire = 1;
}
