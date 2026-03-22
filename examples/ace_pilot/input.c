/*
 * input.c - Joystick + keyboard input for Ace Pilot
 * Player 1: Joystick port 2 + keyboard arrows/WASD/space
 * Player 2: Joystick port 1
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
        case 0x4F: key_state |= INPUT_LEFT;     break;  /* cursor left */
        case 0x4E: key_state |= INPUT_RIGHT;    break;  /* cursor right */
        case 0x4C: key_state |= INPUT_UP;       break;  /* cursor up */
        case 0x4D: key_state |= INPUT_DOWN;     break;  /* cursor down */
        case 0x20: key_state |= INPUT_LEFT;     break;  /* A */
        case 0x22: key_state |= INPUT_RIGHT;    break;  /* D */
        case 0x11: key_state |= INPUT_UP;       break;  /* W */
        case 0x21: key_state |= INPUT_DOWN;     break;  /* S */
        case 0x40: key_state |= INPUT_FIRE;     break;  /* space */
        case 0x60: key_state |= INPUT_FIRE;     break;  /* left alt */
        case 0x45: key_state |= INPUT_ESC;      break;  /* escape */
        case 0x10: key_state |= INPUT_THROT_UP; break;  /* Q = throttle up */
        case 0x12: key_state |= INPUT_THROT_DN; break;  /* E = throttle down */
        case 0x19: key_state |= INPUT_MODE;     break;  /* P = toggle mode */
        case 0x50: key_state |= INPUT_START;    break;  /* F1 = start */
        case 0x51: key_state |= INPUT_TWO_P;    break;  /* F2 = 2 player */
    }
}

void input_key_up(UWORD code)
{
    switch (code) {
        case 0x4F: key_state &= ~INPUT_LEFT;     break;
        case 0x4E: key_state &= ~INPUT_RIGHT;    break;
        case 0x4C: key_state &= ~INPUT_UP;       break;
        case 0x4D: key_state &= ~INPUT_DOWN;     break;
        case 0x20: key_state &= ~INPUT_LEFT;     break;
        case 0x22: key_state &= ~INPUT_RIGHT;    break;
        case 0x11: key_state &= ~INPUT_UP;       break;
        case 0x21: key_state &= ~INPUT_DOWN;     break;
        case 0x40: key_state &= ~INPUT_FIRE;     break;
        case 0x60: key_state &= ~INPUT_FIRE;     break;
        case 0x45: key_state &= ~INPUT_ESC;      break;
        case 0x10: key_state &= ~INPUT_THROT_UP; break;
        case 0x12: key_state &= ~INPUT_THROT_DN; break;
        case 0x19: key_state &= ~INPUT_MODE;     break;
        case 0x50: key_state &= ~INPUT_START;    break;
        case 0x51: key_state &= ~INPUT_TWO_P;    break;
    }
}

void input_reset(void)
{
    key_state = 0;
}

/*
 * Proper Amiga joystick decoding from JOYxDAT registers.
 * The register uses a counter/Gray-code encoding:
 *   Right: bit1=1
 *   Left:  bit1=0, (bit0 XOR bit1)=1  => bit0=1
 *   Down:  bit9=1
 *   Up:    bit9=0, (bit8 XOR bit9)=1  => bit8=1
 */
static void decode_joy(UWORD joy, UWORD *result)
{
    UWORD right = (joy >> 1) & 1;
    UWORD left  = (joy & 1) ^ right;
    UWORD down  = (joy >> 9) & 1;
    UWORD up    = ((joy >> 8) & 1) ^ down;

    if (right) *result |= INPUT_RIGHT;
    if (left)  *result |= INPUT_LEFT;
    if (up)    *result |= INPUT_UP;
    if (down)  *result |= INPUT_DOWN;
}

UWORD input_read_p1(void)
{
    UWORD result = key_state;

    /* Joystick port 2 (JOY1DAT) */
    decode_joy(custom.joy1dat, &result);

    /* Fire: CIA-A PRA bit 7, active low (port 2) */
    if (!(ciaa.ciapra & 0x80))
        result |= INPUT_FIRE;

    return result;
}

UWORD input_read_p2(void)
{
    UWORD result = 0;

    /* Joystick port 1 (JOY0DAT) */
    decode_joy(custom.joy0dat, &result);

    /* Fire: CIA-A PRA bit 6, active low (port 1) */
    if (!(ciaa.ciapra & 0x40))
        result |= INPUT_FIRE;

    return result;
}
