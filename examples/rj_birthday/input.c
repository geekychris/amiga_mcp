/*
 * input.c - Joystick + keyboard input
 */
#include <proto/exec.h>
#include <proto/intuition.h>
#include <hardware/cia.h>
#include <hardware/custom.h>
#include <intuition/intuition.h>
#include "game.h"

extern volatile struct Custom custom;
extern volatile struct CIA ciaa;

/* Key state tracking */
static UBYTE keys[128];

void input_init(void)
{
    WORD i;
    for (i = 0; i < 128; i++) keys[i] = 0;
}

/* Raw key to ASCII map for basic keys (unshifted) */
static UBYTE rawkey_to_ascii(UWORD code)
{
    /* Letters a-z: rawkeys 0x20-0x39 roughly, but Amiga layout varies */
    /* Using standard US layout mapping */
    static const char row0[] = "`1234567890-=";   /* 0x00-0x0C */
    static const char row1[] = "qwertyuiop[]";     /* 0x10-0x1B */
    static const char row2[] = "asdfghjkl;'";      /* 0x20-0x2A */
    static const char row3[] = "zxcvbnm,./";       /* 0x31-0x3A */

    if (code <= 0x0C) return row0[code];
    if (code >= 0x10 && code <= 0x1B) return row1[code - 0x10];
    if (code >= 0x20 && code <= 0x2A) return row2[code - 0x20];
    if (code >= 0x31 && code <= 0x3A) return row3[code - 0x31];
    if (code == 0x40) return ' ';  /* space */
    return 0;
}

void input_read(InputState *inp, struct Window *win)
{
    struct IntuiMessage *msg;
    UWORD joy;
    UWORD prev = inp->prev_fire;

    inp->last_char = 0;
    inp->key_up = 0;
    inp->key_down = 0;
    inp->key_return = 0;
    inp->key_backspace = 0;
    inp->key_delete = 0;

    /* Process IDCMP messages */
    while ((msg = (struct IntuiMessage *)GetMsg(win->UserPort))) {
        ULONG cl = msg->Class;
        UWORD code = msg->Code;
        ReplyMsg((struct Message *)msg);

        if (cl == IDCMP_RAWKEY) {
            if (code & 0x80) {
                keys[code & 0x7F] = 0;
            } else {
                keys[code & 0x7F] = 1;
                /* Track edges for name entry */
                if (code == 0x4C) inp->key_up = 1;
                if (code == 0x4D) inp->key_down = 1;
                if (code == 0x44) inp->key_return = 1;  /* Return */
                if (code == 0x41) inp->key_backspace = 1; /* Backspace */
                if (code == 0x46) inp->key_delete = 1;    /* Delete */
                /* Map to character */
                {
                    UBYTE ch = rawkey_to_ascii(code);
                    if (ch) inp->last_char = ch;
                }
            }
        }
    }

    inp->bits = 0;

    /* Keyboard: arrows + space/alt */
    if (keys[0x4F]) inp->bits |= INP_LEFT;   /* cursor left */
    if (keys[0x4E]) inp->bits |= INP_RIGHT;  /* cursor right */
    if (keys[0x4C]) inp->bits |= INP_UP;     /* cursor up */
    if (keys[0x4D]) inp->bits |= INP_DOWN;   /* cursor down */
    if (keys[0x40]) inp->bits |= INP_FIRE;   /* space */
    if (keys[0x64]) inp->bits |= INP_FIRE;   /* left alt */
    if (keys[0x45]) inp->bits |= INP_ESC;    /* ESC */

    /* Joystick port 2 */
    joy = custom.joy1dat;

    if (joy & 0x0200) inp->bits |= INP_LEFT;
    if (joy & 0x0002) inp->bits |= INP_RIGHT;
    if ((joy & 0x0100) ^ ((joy & 0x0200) >> 1)) inp->bits |= INP_UP;
    if ((joy & 0x0001) ^ ((joy & 0x0002) >> 1)) inp->bits |= INP_DOWN;

    /* Fire button (active low) */
    if (!(ciaa.ciapra & 0x0080)) inp->bits |= INP_FIRE;

    /* Edge detect fire */
    inp->fire_edge = (inp->bits & INP_FIRE) && !prev;
    inp->prev_fire = (inp->bits & INP_FIRE) ? 1 : 0;
}
