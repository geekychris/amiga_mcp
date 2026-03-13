/*
 * RJ and Dale's Super Bros - Input Handler
 * Joystick port 2 + Keyboard
 */
#include "game.h"

#define JOY1DAT   (*(volatile UWORD *)0xDFF00C)
#define CIAAPRA   (*(volatile UBYTE *)0xBFE001)

static UWORD key_state = 0;
static BOOL  esc_pressed = FALSE;

void input_init(void) {
    key_state = 0;
    esc_pressed = FALSE;
}

/* Process keyboard events from screen's IDCMP */
static void process_keys(void) {
    struct IntuiMessage *msg;
    ULONG code;

    if (!gameScreen || !gameScreen->FirstWindow) return;

    while ((msg = (struct IntuiMessage *)GetMsg(gameScreen->FirstWindow->UserPort))) {
        if (msg->Class == IDCMP_RAWKEY) {
            code = msg->Code;
            if (code & 0x80) {
                /* key up */
                code &= 0x7F;
                switch (code) {
                case 0x4F: key_state &= ~INP_LEFT;  break; /* Left arrow */
                case 0x4E: key_state &= ~INP_RIGHT; break; /* Right arrow */
                case 0x4C: key_state &= ~INP_UP;    break; /* Up arrow */
                case 0x4D: key_state &= ~INP_DOWN;  break; /* Down arrow */
                case 0x20: key_state &= ~INP_LEFT;  break; /* A */
                case 0x22: key_state &= ~INP_RIGHT; break; /* D */
                case 0x11: key_state &= ~INP_UP;    break; /* W */
                case 0x21: key_state &= ~INP_DOWN;  break; /* S */
                case 0x40: key_state &= ~INP_JUMP;  break; /* Space */
                case 0x64: key_state &= ~INP_JUMP;  break; /* Left Alt */
                case 0x31: key_state &= ~INP_JUMP;  break; /* Z */
                case 0x33: key_state &= ~INP_JUMP;  break; /* X */
                case 0x44: key_state &= ~INP_START; break; /* Return */
                }
            } else {
                /* key down */
                switch (code) {
                case 0x4F: key_state |= INP_LEFT;  break; /* Left arrow */
                case 0x4E: key_state |= INP_RIGHT; break; /* Right arrow */
                case 0x4C: key_state |= INP_UP;    break; /* Up arrow */
                case 0x4D: key_state |= INP_DOWN;  break; /* Down arrow */
                case 0x20: key_state |= INP_LEFT;  break; /* A */
                case 0x22: key_state |= INP_RIGHT; break; /* D */
                case 0x11: key_state |= INP_UP;    break; /* W */
                case 0x21: key_state |= INP_DOWN;  break; /* S */
                case 0x40: key_state |= INP_JUMP;  break; /* Space */
                case 0x64: key_state |= INP_JUMP;  break; /* Left Alt */
                case 0x31: key_state |= INP_JUMP;  break; /* Z */
                case 0x33: key_state |= INP_JUMP;  break; /* X */
                case 0x44: key_state |= INP_START; break; /* Return */
                case 0x45: esc_pressed = TRUE;      break; /* Escape */
                }
            }
        }
        ReplyMsg((struct Message *)msg);
    }
}

UWORD input_read(void) {
    UWORD result = 0;
    UWORD joy;

    /* Process keyboard - single place for all IDCMP */
    process_keys();
    result |= key_state;

    /* Read joystick port 2 */
    joy = JOY1DAT;

    /* Horizontal: XOR of bits 1 and 9 */
    if (joy & 0x0002) {
        if (joy & 0x0001) result |= INP_LEFT;
        else result |= INP_RIGHT;
    } else if (joy & 0x0001) {
        result |= INP_RIGHT;
    }

    /* Vertical: XOR of bits 9 and 8 */
    if (joy & 0x0200) {
        if (joy & 0x0100) result |= INP_UP;
        else result |= INP_DOWN;
    } else if (joy & 0x0100) {
        result |= INP_DOWN;
    }

    /* Fire button = jump (active low) */
    if (!(CIAAPRA & 0x80)) {
        result |= INP_JUMP;
    }

    return result;
}

BOOL input_check_esc(void) {
    /* Just check the flag set by process_keys() - no separate GetMsg loop */
    BOOL result = esc_pressed;
    esc_pressed = FALSE;
    return result;
}
