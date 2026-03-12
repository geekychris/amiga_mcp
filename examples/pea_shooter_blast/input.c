/*
 * Pea Shooter Blast - Input: Joystick port 2 + keyboard
 */
#include <hardware/custom.h>
#include <hardware/cia.h>
#include "input.h"

extern volatile struct Custom custom;
extern volatile struct CIA ciaa;

static UWORD key_state = 0;

void input_key_down(UWORD code)
{
    switch (code) {
        case 0x4F: key_state |= INPUT_LEFT;  break;  /* cursor left */
        case 0x4E: key_state |= INPUT_RIGHT; break;  /* cursor right */
        case 0x4C: key_state |= INPUT_JUMP;  break;  /* cursor up */
        case 0x4D: key_state |= INPUT_DOWN;  break;  /* cursor down */
        case 0x20: key_state |= INPUT_LEFT;  break;  /* A */
        case 0x22: key_state |= INPUT_RIGHT; break;  /* D */
        case 0x11: key_state |= INPUT_JUMP;  break;  /* W */
        case 0x31: key_state |= INPUT_DOWN;  break;  /* S... actually Z */
        case 0x60: key_state |= INPUT_FIRE;  break;  /* left alt */
        case 0x64: key_state |= INPUT_FIRE;  break;  /* right alt */
        case 0x40: key_state |= INPUT_FIRE;  break;  /* space */
        case 0x45: key_state |= INPUT_ESC;   break;  /* escape */
    }
}

void input_key_up(UWORD code)
{
    switch (code) {
        case 0x4F: key_state &= ~INPUT_LEFT;  break;
        case 0x4E: key_state &= ~INPUT_RIGHT; break;
        case 0x4C: key_state &= ~INPUT_JUMP;  break;
        case 0x4D: key_state &= ~INPUT_DOWN;  break;
        case 0x20: key_state &= ~INPUT_LEFT;  break;
        case 0x22: key_state &= ~INPUT_RIGHT; break;
        case 0x11: key_state &= ~INPUT_JUMP;  break;
        case 0x31: key_state &= ~INPUT_DOWN;  break;
        case 0x60: key_state &= ~INPUT_FIRE;  break;
        case 0x64: key_state &= ~INPUT_FIRE;  break;
        case 0x40: key_state &= ~INPUT_FIRE;  break;
        case 0x45: key_state &= ~INPUT_ESC;   break;
    }
}

void input_reset(void)
{
    key_state = 0;
}

UWORD input_read(void)
{
    UWORD result = key_state;
    UWORD joy;

    /* Read joystick port 2 (JOY1DAT) */
    joy = custom.joy1dat;

    {
        UWORD h = joy & 3;
        UWORD v = (joy >> 8) & 3;

        if (h == 2) result |= INPUT_RIGHT;
        if (h == 1) result |= INPUT_LEFT;
        if (v == 1) result |= INPUT_JUMP;
        if (v == 2) result |= INPUT_DOWN;
    }

    /* Fire button: CIA-A PRA bit 7, active low (port 2) */
    if (!(ciaa.ciapra & 0x80))
        result |= INPUT_FIRE;

    return result;
}
