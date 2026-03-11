/*
 * chipwrite_logger.c - Custom chip register change monitor
 *
 * Periodically reads readable custom chip registers at $DFF000
 * and reports changes over the serial protocol.
 */

#include <exec/types.h>
#include <exec/memory.h>
#include <proto/exec.h>

#include <string.h>
#include <stdio.h>

#include "bridge_internal.h"

extern struct ExecBase *SysBase;

/* Register definitions: offset from $DFF000 and name */
struct ChipReg {
    UWORD offset;
    const char *name;
};

static const struct ChipReg g_regs[] = {
    { 0x002, "DMACONR"  },
    { 0x004, "VPOSR"    },
    { 0x006, "VHPOSR"   },
    /* DSKDATR ($008) skipped - DMA only */
    { 0x00A, "JOY0DAT"  },
    { 0x00C, "JOY1DAT"  },
    { 0x00E, "CLXDAT"   },
    { 0x010, "ADKCONR"  },
    { 0x012, "POT0DAT"  },
    { 0x014, "POT1DAT"  },
    { 0x016, "POTGOR"   },
    { 0x018, "SERDATR"  },
    { 0x01C, "INTENAR"  },
    { 0x01E, "INTREQR"  },
};

#define NUM_REGS (sizeof(g_regs) / sizeof(g_regs[0]))

static BOOL g_active = FALSE;
static UWORD g_prev[NUM_REGS];
static ULONG g_tick = 0;

/*
 * Read the current value of all monitored registers.
 */
static void read_all_regs(UWORD *values)
{
    volatile UWORD *custom = (volatile UWORD *)0xDFF000;
    int i;

    for (i = 0; i < (int)NUM_REGS; i++) {
        values[i] = custom[g_regs[i].offset / 2];
    }
}

/*
 * Initialize the chip register logger.
 */
void chiplog_init(void)
{
    g_active = FALSE;
    g_tick = 0;
    memset(g_prev, 0, sizeof(g_prev));
    printf("ChipLog: initialized\n");
}

/*
 * Cleanup the chip register logger.
 */
void chiplog_cleanup(void)
{
    g_active = FALSE;
    printf("ChipLog: cleaned up\n");
}

/*
 * Start monitoring register changes.
 * Command: CHIPLOGSTART
 *
 * Takes an initial snapshot so only subsequent changes are reported.
 *
 * Response: OK|CHIPLOG|started
 */
void chiplog_handle_start(void)
{
    /* Take initial snapshot */
    read_all_regs(g_prev);
    g_tick = 0;
    g_active = TRUE;

    protocol_send_raw("OK|CHIPLOG|started");
    ui_add_log("ChipLog: monitoring started");
}

/*
 * Stop monitoring register changes.
 * Command: CHIPLOGSTOP
 *
 * Response: OK|CHIPLOG|stopped
 */
void chiplog_handle_stop(void)
{
    g_active = FALSE;

    protocol_send_raw("OK|CHIPLOG|stopped");
    ui_add_log("ChipLog: monitoring stopped");
}

/*
 * Read current register state (one-shot).
 * Command: CHIPLOGSNAPSHOT
 *
 * Response: CHIPLOG|reg1:val1|reg2:val2|...
 */
void chiplog_handle_snapshot(void)
{
    static char linebuf[BRIDGE_MAX_LINE];
    UWORD values[NUM_REGS];
    int pos, i;

    read_all_regs(values);

    strcpy(linebuf, "CHIPLOG");
    pos = 7;

    for (i = 0; i < (int)NUM_REGS && pos < BRIDGE_MAX_LINE - 30; i++) {
        char entry[24];
        int elen;

        sprintf(entry, "%s:%04lx", g_regs[i].name, (unsigned long)values[i]);
        elen = strlen(entry);

        if (pos + 1 + elen >= BRIDGE_MAX_LINE - 2) break;
        linebuf[pos++] = '|';
        memcpy(&linebuf[pos], entry, elen);
        pos += elen;
    }
    linebuf[pos] = '\0';

    protocol_send_raw(linebuf);
}

/*
 * Poll for register changes.
 * Called from the bridge main loop. When monitoring is active,
 * reads all registers and sends a CHIPLOGCHANGE message if any
 * values differ from the previous snapshot.
 *
 * Format: CHIPLOGCHANGE|tick|reg:oldval:newval|...
 */
void chiplog_poll(void)
{
    static char linebuf[BRIDGE_MAX_LINE];
    UWORD current[NUM_REGS];
    int pos, i;
    BOOL changed;

    if (!g_active) return;

    read_all_regs(current);
    g_tick++;

    /* Check for any changes */
    changed = FALSE;
    for (i = 0; i < (int)NUM_REGS; i++) {
        if (current[i] != g_prev[i]) {
            changed = TRUE;
            break;
        }
    }

    if (!changed) return;

    /* Build change report */
    sprintf(linebuf, "CHIPLOGCHANGE|%lu", (unsigned long)g_tick);
    pos = strlen(linebuf);

    for (i = 0; i < (int)NUM_REGS && pos < BRIDGE_MAX_LINE - 40; i++) {
        if (current[i] != g_prev[i]) {
            char entry[32];
            int elen;

            sprintf(entry, "%s:%04lx:%04lx",
                    g_regs[i].name,
                    (unsigned long)g_prev[i],
                    (unsigned long)current[i]);
            elen = strlen(entry);

            if (pos + 1 + elen >= BRIDGE_MAX_LINE - 2) break;
            linebuf[pos++] = '|';
            memcpy(&linebuf[pos], entry, elen);
            pos += elen;
        }
    }
    linebuf[pos] = '\0';

    protocol_send_raw(linebuf);

    /* Update snapshot */
    memcpy(g_prev, current, sizeof(g_prev));
}

/*
 * Return whether chip register monitoring is currently active.
 * Can be used by the main loop to decide polling frequency.
 */
BOOL chiplog_is_active(void)
{
    return g_active;
}
