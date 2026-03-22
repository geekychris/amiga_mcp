/*
 * Gold Rush - Input abstraction
 * Reads joystick port 2 + keyboard IDCMP.
 * Joystick directions are HELD state (not edge-detected).
 * Fire and keys are edge-detected.
 */

#include <exec/types.h>
#include <intuition/intuition.h>
#include <hardware/cia.h>
#include <hardware/custom.h>
#include <proto/exec.h>
#include <proto/intuition.h>

#include <string.h>

#include "input.h"

extern struct Custom custom;

/* Maximum raw keycode we track */
#define MAX_KEYS 128

static struct Window *input_win = NULL;

/* Current and previous frame state */
static int joy_dx_val = 0;
static int joy_dy_val = 0;
static int joy_fire_val = 0;
static int prev_fire = 0;

/* Key state arrays: current frame and previous frame */
static UBYTE keys_curr[MAX_KEYS];
static UBYTE keys_prev[MAX_KEYS];
static int any_key_pressed = 0;

void input_init(struct Window *win)
{
    input_win = win;
    joy_dx_val = 0;
    joy_dy_val = 0;
    joy_fire_val = 0;
    prev_fire = 0;
    any_key_pressed = 0;
    memset(keys_curr, 0, sizeof(keys_curr));
    memset(keys_prev, 0, sizeof(keys_prev));
}

void input_update(void)
{
    volatile UWORD joy;
    UBYTE ciaa_pra;

    /* Save previous frame state */
    prev_fire = joy_fire_val;
    memcpy(keys_prev, keys_curr, sizeof(keys_prev));
    memset(keys_curr, 0, sizeof(keys_curr));
    any_key_pressed = 0;

    /* Read joystick port 2 (JOY1DAT) - held state */
    joy = custom.joy1dat;

    joy_dx_val = 0;
    joy_dy_val = 0;

    /* Decode joystick directions */
    if ((joy >> 1) & 1)                  joy_dx_val = 1;   /* right */
    if ((joy >> 9) & 1)                  joy_dx_val = -1;  /* left */
    if (((joy >> 1) ^ joy) & 1)          joy_dy_val = 1;   /* down */
    if (((joy >> 9) ^ (joy >> 8)) & 1)   joy_dy_val = -1;  /* up */

    /* Fire button: CIA-A PRA bit 7, active low */
    ciaa_pra = *((volatile UBYTE *)0xBFE001);
    joy_fire_val = (ciaa_pra & 0x80) ? 0 : 1;

    /* Process keyboard IDCMP messages */
    if (input_win) {
        struct IntuiMessage *imsg;
        while ((imsg = (struct IntuiMessage *)GetMsg(input_win->UserPort))) {
            ULONG cl = imsg->Class;
            UWORD code = imsg->Code;
            ReplyMsg((struct Message *)imsg);

            if (cl == IDCMP_RAWKEY) {
                int keycode = code & 0x7F;
                int released = code & 0x80;

                if (!released && keycode < MAX_KEYS) {
                    keys_curr[keycode] = 1;
                    any_key_pressed = 1;
                }

                /* Map arrow keys to joystick directions (held) */
                if (!released) {
                    if (keycode == KEY_UP)    joy_dy_val = -1;
                    if (keycode == KEY_DOWN)  joy_dy_val = 1;
                    if (keycode == KEY_LEFT)  joy_dx_val = -1;
                    if (keycode == KEY_RIGHT) joy_dx_val = 1;
                    if (keycode == KEY_SPACE || keycode == KEY_RETURN)
                        joy_fire_val = 1;
                }
            }
        }
    }
}

int input_dx(void)
{
    return joy_dx_val;
}

int input_dy(void)
{
    return joy_dy_val;
}

int input_fire(void)
{
    /* Edge-detected: true only on the frame fire was pressed */
    return (joy_fire_val && !prev_fire);
}

int input_fire_held(void)
{
    return joy_fire_val;
}

int input_key(int rawkey)
{
    /* Edge-detected: true only on the frame the key was pressed */
    if (rawkey < 0 || rawkey >= MAX_KEYS) return 0;
    return (keys_curr[rawkey] && !keys_prev[rawkey]);
}

int input_any_key(void)
{
    return any_key_pressed;
}
