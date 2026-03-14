/*
 * Starfield - C startup stub
 *
 * Handles OS library opens, system takeover/restore, bridge integration.
 * The actual demo runs in starfield.asm which does all the hardware work.
 */

#include <exec/types.h>
#include <exec/memory.h>
#include <exec/execbase.h>
#include <graphics/gfxbase.h>
#include <hardware/custom.h>
#include <hardware/dmabits.h>

#include <proto/exec.h>
#include <proto/graphics.h>
#include <proto/intuition.h>

#include <bridge_client.h>

/* Assembly entry point */
extern void sf_main(struct Custom *custom, APTR chipmem);
/* Assembly-side frame counter (updated by asm) */
extern long sf_frame_count;
extern long sf_star_count;

/* We allocate chip memory here and pass it to asm */
#define CHIPMEM_SIZE (320*256/8*3 + 320*256/8 + 4096)
/* 3 bitplanes + 1 clear plane + workspace */

struct GfxBase *GfxBase = NULL;
static struct View *old_view = NULL;
static UWORD old_dmacon;
static UWORD old_intena;
static UWORD old_intreq;
static UWORD old_adkcon;

static volatile struct Custom *hw = (volatile struct Custom *)0xdff000;

static long bridge_ok = 0;

int main(void)
{
    APTR chipmem;

    /* Open graphics library - we need it to save/restore the view */
    GfxBase = (struct GfxBase *)OpenLibrary("graphics.library", 33);
    if (!GfxBase) return 20;

    /* Try bridge connection (non-fatal) */
    if (ab_init("starfield") == 0) {
        bridge_ok = 1;
        ab_register_var("frames", AB_TYPE_I32, &sf_frame_count);
        ab_register_var("stars", AB_TYPE_I32, &sf_star_count);
        AB_I("Starfield starting");
    }

    /* Allocate chip RAM for bitplanes */
    chipmem = AllocMem(CHIPMEM_SIZE, MEMF_CHIP | MEMF_CLEAR);
    if (!chipmem) {
        if (bridge_ok) AB_E("Failed to allocate chip memory");
        CloseLibrary((struct Library *)GfxBase);
        return 20;
    }

    if (bridge_ok) {
        ab_register_memregion("chipmem", chipmem, CHIPMEM_SIZE, "Bitplane memory");
        AB_I("Chip mem at 0x%lx, %ld bytes", (unsigned long)chipmem, (long)CHIPMEM_SIZE);
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

    /* Disable all DMA and interrupts, we'll enable what we need */
    hw->dmacon = 0x7FFF;
    hw->intena = 0x7FFF;
    hw->intreq = 0x7FFF;

    /* ---- Run the demo (asm) ---- */
    sf_main((struct Custom *)0xdff000, chipmem);

    /* ---- Restore system ---- */
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

    /* Rebuild copper lists */
    RethinkDisplay();

    if (bridge_ok) {
        AB_I("Starfield finished, %ld frames", (long)sf_frame_count);
        ab_cleanup();
    }

    FreeMem(chipmem, CHIPMEM_SIZE);
    CloseLibrary((struct Library *)GfxBase);
    return 0;
}
