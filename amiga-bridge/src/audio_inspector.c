/*
 * audio_inspector.c - Paula audio chip inspection
 *
 * Provides audio channel status and sample data reading
 * for the AmigaBridge daemon.
 */

#include <exec/types.h>
#include <exec/memory.h>
#include <exec/execbase.h>
#include <proto/exec.h>

#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#include "bridge_internal.h"

extern struct ExecBase *SysBase;

/*
 * Read Paula audio channel status.
 * Command: AUDIOCHANNELS
 *
 * Paula's audio registers (AUDxLCH/LCL/LEN/PER/VOL) are WRITE-ONLY,
 * so we read the audio state from readable custom chip registers:
 *   DMACONR ($DFF002) - bits 0-3: audio DMA enable for channels 0-3
 *   INTREQR ($DFF01E) - bits 7-10: audio interrupt request for channels 0-3
 *   INTENAR ($DFF01C) - bits 7-10: audio interrupt enable for channels 0-3
 *
 * Response: AUDIOCHANNELS|dmaEnabled|intReq|intEna
 *   dmaEnabled: hex bitmask of DMACON audio bits (0-3)
 *   intReq: hex bitmask of INTREQ audio bits (7-10), shifted down
 *   intEna: hex bitmask of INTEN audio bits (7-10), shifted down
 */
void audio_handle_channels(void)
{
    volatile UWORD *custom = (volatile UWORD *)0xDFF000;
    static char linebuf[BRIDGE_MAX_LINE];
    UWORD dmaconr = custom[0x002 / 2];  /* DMACONR */
    UWORD intreqr = custom[0x01E / 2];  /* INTREQR */
    UWORD intenar = custom[0x01C / 2];  /* INTENAR */

    /* Extract audio-relevant bits */
    UWORD audioDma = dmaconr & 0x000F;         /* bits 0-3: AUD0-AUD3 DMA */
    UWORD audioIntReq = (intreqr >> 7) & 0x0F; /* bits 7-10: AUD0-AUD3 int req */
    UWORD audioIntEna = (intenar >> 7) & 0x0F; /* bits 7-10: AUD0-AUD3 int ena */

    sprintf(linebuf, "AUDIOCHANNELS|%lx|%lx|%lx",
        (unsigned long)audioDma,
        (unsigned long)audioIntReq,
        (unsigned long)audioIntEna);
    protocol_send_raw(linebuf);
}

/*
 * Read sample data from chip RAM.
 * Command: AUDIOSAMPLE|addr_hex|size
 *
 * Reads raw bytes from a chip RAM address so the host can
 * visualize waveform data. Size is in bytes, max 512.
 *
 * Validates that the address is in chip RAM via TypeOfMem().
 *
 * Response: AUDIOSAMPLE|addr|size|hexdata
 */
void audio_handle_sample(const char *args)
{
    static char linebuf[BRIDGE_MAX_LINE];
    static char hexbuf[1026]; /* 512 bytes * 2 + nul */
    ULONG addr, size, i;
    const char *p;
    UBYTE *mem;

    if (!args || !args[0]) {
        protocol_send_raw("ERR|AUDIOSAMPLE|Missing arguments");
        return;
    }

    addr = strtoul(args, NULL, 16);
    p = strchr(args, '|');
    if (!p) {
        protocol_send_raw("ERR|AUDIOSAMPLE|Missing size");
        return;
    }
    size = strtoul(p + 1, NULL, 10);
    if (size > 512) size = 512;
    if (size == 0) {
        protocol_send_raw("ERR|AUDIOSAMPLE|Size must be > 0");
        return;
    }

    /* Validate chip RAM */
    if (!(TypeOfMem((APTR)addr) & MEMF_CHIP)) {
        protocol_send_raw("ERR|AUDIOSAMPLE|Address not in chip RAM");
        return;
    }

    mem = (UBYTE *)addr;
    for (i = 0; i < size; i++) {
        sprintf(hexbuf + i * 2, "%02x", (unsigned int)mem[i]);
    }
    hexbuf[size * 2] = '\0';

    sprintf(linebuf, "AUDIOSAMPLE|%lx|%lu|%s",
        (unsigned long)addr,
        (unsigned long)size,
        hexbuf);
    protocol_send_raw(linebuf);
}
