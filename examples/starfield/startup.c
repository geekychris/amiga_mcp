/*
 * Starfield - C startup stub
 *
 * Handles OS library opens, system takeover/restore, bridge integration,
 * music playback via ptplayer, and keyboard input reading.
 * The actual demo runs in starfield.asm which does all the hardware work.
 */

#include <exec/types.h>
#include <exec/memory.h>
#include <exec/execbase.h>
#include <graphics/gfxbase.h>
#include <hardware/custom.h>
#include <hardware/dmabits.h>
#include <hardware/cia.h>

#include <proto/exec.h>
#include <proto/graphics.h>
#include <proto/intuition.h>
#include <proto/dos.h>

#include <bridge_client.h>

#include "ptplayer.h"

/* Assembly entry point */
extern LONG sf_main(struct Custom *custom, APTR chipmem);
/* Assembly-side state */
extern long sf_frame_count;
extern long sf_star_count;
/* Keyboard state: written by C, read by asm */
extern BYTE sf_key_up;
extern BYTE sf_key_down;
extern BYTE sf_key_left;
extern BYTE sf_key_right;
extern BYTE sf_key_esc;

/* Chip memory layout:
 * 3 bitplanes (10240 each) = 30720
 * copper list workspace      = 2048
 * total                      = 32768
 */
#define CHIPMEM_SIZE (320*256/8*3 + 2048)

#define CUSTOM_BASE ((void *)0xdff000)

struct GfxBase *GfxBase = NULL;
struct IntuitionBase *IntuitionBase = NULL;
static struct View *old_view = NULL;
static UWORD old_dmacon;
static UWORD old_intena;
static UWORD old_intreq;
static UWORD old_adkcon;

static volatile struct Custom *hw = (volatile struct Custom *)0xdff000;
static volatile struct CIA *ciaa = (volatile struct CIA *)0xbfe001;

static long bridge_ok = 0;
static UBYTE *mod_data = NULL;
static ULONG mod_size = 0;
static int music_playing = 0;
static int kbd_initialized = 0;

/* --- Load MOD file from disk into chip RAM --- */

static UBYTE *load_mod_file(const char *filename, ULONG *out_size)
{
    BPTR fh;
    LONG size;
    UBYTE *buf;

    fh = Open((STRPTR)filename, MODE_OLDFILE);
    if (!fh) return NULL;

    Seek(fh, 0, OFFSET_END);
    size = Seek(fh, 0, OFFSET_BEGINNING);

    if (size <= 0) {
        Close(fh);
        return NULL;
    }

    buf = (UBYTE *)AllocMem(size, MEMF_CHIP);
    if (!buf) {
        Close(fh);
        return NULL;
    }

    if (Read(fh, buf, size) != size) {
        FreeMem(buf, size);
        Close(fh);
        return NULL;
    }

    Close(fh);
    *out_size = (ULONG)size;
    return buf;
}

/* --- Keyboard reading via CIAA --- */

/* Amiga raw keycodes */
#define KEY_ESC    0x45
#define KEY_W      0x11
#define KEY_A      0x20
#define KEY_S      0x21
#define KEY_D      0x22
#define KEY_UP     0x4C
#define KEY_DOWN   0x4D
#define KEY_LEFT   0x4F
#define KEY_RIGHT  0x4E

static void read_keyboard(void)
{
    UBYTE raw, code;
    UBYTE keyup;

    if (!kbd_initialized) {
        /* First call: flush stale keyboard state with a handshake
         * and clear any pending CIA-A interrupt flags */
        ciaa->ciacra |= 0x40;
        { volatile UBYTE tmp; tmp = ciaa->ciacra; tmp = ciaa->ciacra; (void)tmp; }
        ciaa->ciacra &= ~0x40;
        (void)ciaa->ciaicr;   /* clear pending flags */
        kbd_initialized = 1;
        return;
    }

    /* Check if a key event is pending (CIA-A ICR bit 3 = SP complete) */
    if (!(ciaa->ciaicr & 0x08))
        return;  /* No new key data */

    /* Read the key byte */
    raw = ciaa->ciasdr;

    /* The keyboard sends data inverted and bit-rotated */
    code = ~raw;
    code = (code >> 1) | (code << 7); /* rotate right 1 */

    keyup = code & 0x80;   /* bit 7 = key up flag */
    code  = code & 0x7F;   /* bits 0-6 = keycode */

    /* Handshake: pulse SP line to acknowledge */
    ciaa->ciacra |= 0x40;  /* set SP output mode */
    { volatile UBYTE tmp; tmp = ciaa->ciacra; tmp = ciaa->ciacra; tmp = ciaa->ciacra; (void)tmp; }
    ciaa->ciacra &= ~0x40; /* back to input mode */

    /* Update key states */
    if (keyup) {
        switch (code) {
            case KEY_W: case KEY_UP:    sf_key_up = 0; break;
            case KEY_S: case KEY_DOWN:  sf_key_down = 0; break;
            case KEY_A: case KEY_LEFT:  sf_key_left = 0; break;
            case KEY_D: case KEY_RIGHT: sf_key_right = 0; break;
            case KEY_ESC:               sf_key_esc = 0; break;
        }
    } else {
        switch (code) {
            case KEY_ESC:               sf_key_esc = 1; break;
            case KEY_W: case KEY_UP:    sf_key_up = 1; break;
            case KEY_S: case KEY_DOWN:  sf_key_down = 1; break;
            case KEY_A: case KEY_LEFT:  sf_key_left = 1; break;
            case KEY_D: case KEY_RIGHT: sf_key_right = 1; break;
        }
    }
}

/* Called from assembly every frame */
void sf_read_keys(void)
{
    read_keyboard();
}

/* Called from assembly after DMA/interrupts are configured */
void sf_start_music(void)
{
    if (mod_data) {
        mt_install_cia(CUSTOM_BASE, NULL, 1); /* PAL */
        mt_init(CUSTOM_BASE, mod_data, NULL, 0);
        mt_Enable = 1;
        music_playing = 1;
    }
}

int main(void)
{
    APTR chipmem;

    /* Open required libraries */
    GfxBase = (struct GfxBase *)OpenLibrary("graphics.library", 33);
    if (!GfxBase) return 20;

    IntuitionBase = (struct IntuitionBase *)OpenLibrary("intuition.library", 33);
    if (!IntuitionBase) {
        CloseLibrary((struct Library *)GfxBase);
        return 20;
    }

    /* Try bridge connection (non-fatal) */
    if (ab_init("starfield") == 0) {
        bridge_ok = 1;
        ab_register_var("frames", AB_TYPE_I32, &sf_frame_count);
        ab_register_var("stars", AB_TYPE_I32, &sf_star_count);
        AB_I("Starfield starting");
    }

    /* Allocate chip RAM for bitplanes + copper */
    chipmem = AllocMem(CHIPMEM_SIZE, MEMF_CHIP | MEMF_CLEAR);
    if (!chipmem) {
        if (bridge_ok) AB_E("Failed to allocate %ld bytes chip memory", (long)CHIPMEM_SIZE);
        if (bridge_ok) ab_cleanup();
        CloseLibrary((struct Library *)IntuitionBase);
        CloseLibrary((struct Library *)GfxBase);
        return 20;
    }

    if (bridge_ok) {
        ab_register_memregion("chipmem", chipmem, CHIPMEM_SIZE, "Bitplane memory");
        AB_I("Chip mem at 0x%lx, %ld bytes", (unsigned long)chipmem, (long)CHIPMEM_SIZE);
    }

    /* Load Star Trek MOD from disk into chip RAM */
    mod_data = load_mod_file("DH2:Dev/amigatre.mod", &mod_size);
    if (mod_data) {
        if (bridge_ok) AB_I("Loaded amigatre.mod (%ld bytes)", (long)mod_size);
    } else {
        if (bridge_ok) AB_W("Failed to load amigatre.mod");
    }

    /* Save system state */
    old_view = GfxBase->ActiView;
    old_dmacon = hw->dmaconr | 0x8000;
    old_intena = hw->intenar | 0x8000;
    old_intreq = hw->intreqr | 0x8000;
    old_adkcon = hw->adkconr | 0x8000;

    /* Shut down the OS display */
    LoadView(NULL);
    WaitTOF();
    WaitTOF();

    /* Take over the machine */
    OwnBlitter();
    WaitBlit();

    Forbid();

    /* Disable all DMA and interrupts - asm will re-enable what's needed */
    hw->dmacon = 0x7FFF;
    hw->intena = 0x7FFF;
    hw->intreq = 0x7FFF;

    /* Clear keyboard state */
    kbd_initialized = 0;
    sf_key_up = 0;
    sf_key_down = 0;
    sf_key_left = 0;
    sf_key_right = 0;
    sf_key_esc = 0;

    /* ---- Run the demo (asm) ---- */
    sf_main((struct Custom *)0xdff000, chipmem);

    /* ---- Restore system ---- */

    /* Stop music first (needs CIA interrupt still active) */
    if (music_playing) {
        mt_end(CUSTOM_BASE);
        mt_remove_cia(CUSTOM_BASE);
        music_playing = 0;
    }

    /* Disable all DMA/interrupts, then restore saved state */
    hw->dmacon = 0x7FFF;
    hw->intena = 0x7FFF;
    hw->intreq = 0x7FFF;

    hw->dmacon = old_dmacon;
    hw->intena = old_intena;
    hw->intreq = old_intreq;
    hw->adkcon = old_adkcon;

    Permit();
    DisownBlitter();

    /* Restore OS view */
    LoadView(old_view);
    WaitTOF();
    WaitTOF();

    /* Rebuild copper lists (needs IntuitionBase) */
    RethinkDisplay();

    if (bridge_ok) {
        AB_I("Starfield finished, %ld frames", (long)sf_frame_count);
        ab_cleanup();
    }

    /* Free all allocated memory */
    FreeMem(chipmem, CHIPMEM_SIZE);
    if (mod_data) FreeMem(mod_data, mod_size);

    CloseLibrary((struct Library *)IntuitionBase);
    CloseLibrary((struct Library *)GfxBase);
    return 0;
}
