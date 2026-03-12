/*
 * PACBRO - Input: Keyboard + Joystick port 2
 */
#include <hardware/custom.h>
#include <hardware/cia.h>
#include "input.h"

extern volatile struct Custom custom;
extern volatile struct CIA ciaa;

/* Keyboard state bits */
#define KEY_UP     1
#define KEY_DOWN   2
#define KEY_LEFT   4
#define KEY_RIGHT  8
#define KEY_FIRE   16
#define KEY_ESC    32

static UWORD key_state = 0;

void input_key_down(UWORD code)
{
    switch (code) {
        case 0x4C: key_state |= KEY_UP;    break;  /* cursor up */
        case 0x4D: key_state |= KEY_DOWN;  break;  /* cursor down */
        case 0x4F: key_state |= KEY_LEFT;  break;  /* cursor left */
        case 0x4E: key_state |= KEY_RIGHT; break;  /* cursor right */
        case 0x11: key_state |= KEY_UP;    break;  /* W */
        case 0x21: key_state |= KEY_DOWN;  break;  /* S */
        case 0x20: key_state |= KEY_LEFT;  break;  /* A */
        case 0x22: key_state |= KEY_RIGHT; break;  /* D */
        case 0x40: key_state |= KEY_FIRE;  break;  /* space */
        case 0x44: key_state |= KEY_FIRE;  break;  /* return */
        case 0x50: key_state |= KEY_FIRE;  break;  /* F1 */
        case 0x45: key_state |= KEY_ESC;   break;  /* escape */
    }
}

void input_key_up(UWORD code)
{
    switch (code) {
        case 0x4C: key_state &= ~KEY_UP;    break;
        case 0x4D: key_state &= ~KEY_DOWN;  break;
        case 0x4F: key_state &= ~KEY_LEFT;  break;
        case 0x4E: key_state &= ~KEY_RIGHT; break;
        case 0x11: key_state &= ~KEY_UP;    break;
        case 0x21: key_state &= ~KEY_DOWN;  break;
        case 0x20: key_state &= ~KEY_LEFT;  break;
        case 0x22: key_state &= ~KEY_RIGHT; break;
        case 0x40: key_state &= ~KEY_FIRE;  break;
        case 0x44: key_state &= ~KEY_FIRE;  break;
        case 0x50: key_state &= ~KEY_FIRE;  break;
        case 0x45: key_state &= ~KEY_ESC;   break;
    }
}

void input_reset(void)
{
    key_state = 0;
}

void input_read(InputState *input)
{
    UWORD joy;
    UWORD combined = key_state;

    /* Joystick port 2 (JOY1DAT) */
    joy = custom.joy1dat;
    {
        UWORD h = joy & 3;
        UWORD v = (joy >> 8) & 3;
        if (h == 2) combined |= KEY_RIGHT;
        if (h == 1) combined |= KEY_LEFT;
        if (v == 2) combined |= KEY_DOWN;
        if (v == 1) combined |= KEY_UP;
    }

    /* Fire button: CIA-A PRA bit 7, active low (port 2) */
    if (!(ciaa.ciapra & 0x80))
        combined |= KEY_FIRE;

    /* Map to game input */
    input->dir = DIR_NONE;
    if (combined & KEY_UP)    input->dir = DIR_UP;
    if (combined & KEY_DOWN)  input->dir = DIR_DOWN;
    if (combined & KEY_LEFT)  input->dir = DIR_LEFT;
    if (combined & KEY_RIGHT) input->dir = DIR_RIGHT;

    input->start = (combined & KEY_FIRE) ? 1 : 0;
    input->quit = (combined & KEY_ESC) ? 1 : 0;
}
