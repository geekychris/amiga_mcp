/*
 * Planet Patrol - Keyboard + Joystick input
 *
 * Uses input.device handler to catch keyboard events directly,
 * bypassing IDCMP screen/window focus issues.
 * Also reads joystick port 2 hardware registers.
 */
#include <proto/exec.h>
#include <exec/interrupts.h>
#include <devices/input.h>
#include <devices/inputevent.h>
#include <hardware/custom.h>
#include <hardware/cia.h>
#include "game.h"
#include "input.h"

/* Hardware registers - MUST be volatile */
extern volatile struct Custom custom;
extern volatile struct CIA ciaa;

/* Raw key codes (Amiga US layout) */
#define KEY_ESC     0x45
#define KEY_SPACE   0x40
#define KEY_RETURN  0x44
#define KEY_UP      0x4C
#define KEY_DOWN    0x4D
#define KEY_LEFT    0x4F
#define KEY_RIGHT   0x4E
#define KEY_LSHIFT  0x60
#define KEY_LALT    0x64
#define KEY_RALT    0x65
#define KEY_A       0x20
#define KEY_D       0x22
#define KEY_W       0x11
#define KEY_S       0x21
#define KEY_H       0x23
#define KEY_Z       0x31
#define KEY_X       0x32

/* Input handler state - shared with interrupt, exported for debug */
volatile UBYTE input_keys[128]; /* 1 = held, 0 = released */

/* Input.device resources */
static struct MsgPort *input_port = NULL;
static struct IOStdReq *input_req = NULL;
static struct Interrupt input_handler;
static WORD handler_installed = 0;

/* Edge detection for one-shot actions */
static WORD bomb_edge = 0;
static WORD hyper_edge = 0;

/* Input handler function - called at interrupt level */
static struct InputEvent * __attribute__((used))
handler_func(void)
{
    register struct InputEvent *events __asm("a0");
    struct InputEvent *ev;

    for (ev = events; ev; ev = ev->ie_NextEvent) {
        if (ev->ie_Class == IECLASS_RAWKEY) {
            UWORD code = ev->ie_Code;
            UWORD key = code & 0x7F;
            if (key < 128) {
                input_keys[key] = (code & 0x80) ? 0 : 1;
            }
        }
    }
    return events;
}

void input_init(InputData *id)
{
    WORD i;
    (void)id;

    for (i = 0; i < 128; i++) input_keys[i] = 0;
    bomb_edge = 0;
    hyper_edge = 0;

    /* Open input.device and install handler */
    input_port = CreateMsgPort();
    if (!input_port) return;

    input_req = (struct IOStdReq *)CreateIORequest(input_port, sizeof(struct IOStdReq));
    if (!input_req) return;

    if (OpenDevice((STRPTR)"input.device", 0, (struct IORequest *)input_req, 0) != 0) {
        DeleteIORequest((struct IORequest *)input_req);
        input_req = NULL;
        return;
    }

    /* Set up handler */
    input_handler.is_Node.ln_Type = NT_INTERRUPT;
    input_handler.is_Node.ln_Pri = 100; /* High priority, before Intuition */
    input_handler.is_Node.ln_Name = (char *)"Planet PatrolKeys";
    input_handler.is_Data = NULL;
    input_handler.is_Code = (void (*)())handler_func;

    /* Install handler */
    input_req->io_Command = IND_ADDHANDLER;
    input_req->io_Data = (APTR)&input_handler;
    DoIO((struct IORequest *)input_req);
    handler_installed = 1;
}

void input_key_event(InputData *id, UWORD code)
{
    /* Still process IDCMP events as backup */
    UWORD key = code & 0x7F;
    (void)id;
    if (key < 128)
        input_keys[key] = (code & 0x80) ? 0 : 1;
}

void input_cleanup(void)
{
    if (handler_installed && input_req) {
        input_req->io_Command = IND_REMHANDLER;
        input_req->io_Data = (APTR)&input_handler;
        DoIO((struct IORequest *)input_req);
        handler_installed = 0;
    }
    if (input_req) {
        CloseDevice((struct IORequest *)input_req);
        DeleteIORequest((struct IORequest *)input_req);
        input_req = NULL;
    }
    if (input_port) {
        DeleteMsgPort(input_port);
        input_port = NULL;
    }
}

WORD input_read(InputData *id)
{
    WORD result = 0;
    UWORD joy;
    (void)id;

    /* Keyboard - read from handler state */
    if (input_keys[KEY_LEFT]  || input_keys[KEY_A])  result |= INPUT_LEFT;
    if (input_keys[KEY_RIGHT] || input_keys[KEY_D])  result |= INPUT_RIGHT;
    if (input_keys[KEY_UP]    || input_keys[KEY_W])  result |= INPUT_UP;
    if (input_keys[KEY_DOWN]  || input_keys[KEY_S])  result |= INPUT_DOWN;
    if (input_keys[KEY_SPACE] || input_keys[KEY_LALT] || input_keys[KEY_RALT] || input_keys[KEY_LSHIFT])
        result |= INPUT_FIRE;
    if (input_keys[KEY_ESC])                   result |= INPUT_QUIT;
    if (input_keys[KEY_RETURN] || input_keys[KEY_SPACE])
        result |= INPUT_START;

    /* Smart bomb: edge-triggered */
    {
        WORD bomb_held = input_keys[KEY_Z];
        if (bomb_held && !bomb_edge) result |= INPUT_BOMB;
        bomb_edge = bomb_held;
    }

    /* Hyperspace: edge-triggered */
    {
        WORD hyper_held = input_keys[KEY_H] || input_keys[KEY_X];
        if (hyper_held && !hyper_edge) result |= INPUT_HYPER;
        hyper_edge = hyper_held;
    }

    /* Joystick port 2 (JOY1DAT) */
    joy = custom.joy1dat;
    {
        UWORD h = joy & 3;
        UWORD v = (joy >> 8) & 3;

        if (h == 2) result |= INPUT_RIGHT;
        if (h == 1) result |= INPUT_LEFT;
        if (v == 1) result |= INPUT_UP;
        if (v == 2) result |= INPUT_DOWN;
    }

    /* Fire button: CIA-A PRA bit 7 (port 2), active low */
    if (!(ciaa.ciapra & 0x80)) {
        result |= INPUT_FIRE;
        result |= INPUT_START;
    }

    return result;
}
