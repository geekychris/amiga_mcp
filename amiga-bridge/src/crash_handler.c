/*
 * crash_handler.c - Crash/Guru Meditation catcher for AmigaBridge
 *
 * Intercepts exec.library Alert() via SetFunction() to catch all
 * guru meditations. Captures alert number, register state, and
 * stack snapshot, then sends CRASH message over serial before
 * calling the original Alert.
 *
 * This approach works on all CPU types (68000-68060) because it
 * hooks at the library level rather than patching exception vectors
 * (which require VBR handling on 68010+).
 */

#include <exec/types.h>
#include <exec/memory.h>
#include <exec/execbase.h>
#include <exec/alerts.h>
#include <proto/exec.h>

#include <string.h>
#include <stdio.h>

#include "bridge_internal.h"

extern struct ExecBase *SysBase;

/* Original Alert function pointer */
static APTR orig_alert = NULL;
static BOOL crash_installed = FALSE;

/* Static buffer for crash message - must not be on stack since
 * the stack may be corrupt when we're called */
static char crash_buf[BRIDGE_MAX_LINE];
static char hex_buf[256];

/* Saved register state */
static ULONG saved_regs[16]; /* D0-D7, A0-A7 */
static ULONG saved_pc = 0;
static UWORD saved_sr = 0;
static UBYTE stack_snapshot[64];
static int stack_snapshot_len = 0;

/*
 * Convert a byte buffer to hex string.
 */
static void bytes_to_hex(const UBYTE *data, int len, char *out)
{
    static const char hexchars[] = "0123456789ABCDEF";
    int i;
    for (i = 0; i < len; i++) {
        out[i * 2] = hexchars[(data[i] >> 4) & 0x0F];
        out[i * 2 + 1] = hexchars[data[i] & 0x0F];
    }
    out[len * 2] = '\0';
}

/*
 * Identify an alert number and return a human-readable string.
 */
static const char *alert_name(ULONG alertNum)
{
    /* Check for common alert types */
    ULONG subsys = alertNum & 0x7F000000;
    ULONG general = alertNum & 0x00FF0000;

    if (alertNum & AT_DeadEnd) {
        /* Deadend alert - system cannot recover */
        switch (general) {
        case AG_NoMemory:  return "NoMemory(dead)";
        case AG_OpenLib:   return "OpenLib(dead)";
        case AG_OpenDev:   return "OpenDev(dead)";
        case AG_OpenRes:   return "OpenRes(dead)";
        case AG_IOError:   return "IOError(dead)";
        case AG_NoSignal:  return "NoSignal(dead)";
        default:           return "DeadEnd";
        }
    } else {
        switch (general) {
        case AG_NoMemory:  return "NoMemory";
        case AG_OpenLib:   return "OpenLib";
        case AG_OpenDev:   return "OpenDev";
        case AG_OpenRes:   return "OpenRes";
        case AG_IOError:   return "IOError";
        case AG_NoSignal:  return "NoSignal";
        default:           return "Alert";
        }
    }
}

/*
 * Our replacement Alert function.
 *
 * Alert() is called with the alert number in D7.
 * We capture register state, format a crash message,
 * send it over serial, then call the original Alert.
 *
 * Note: We use a naked function with inline asm to
 * capture registers before the compiler touches them.
 */
static void crash_alert_handler(void)
{
    ULONG alertNum;
    ULONG sp_val;
    int i;
    int pos;
    const char *name;

    /* Read D7 (alert number) and SP before compiler uses regs */
    __asm volatile (
        "move.l %%d7, %0\n\t"
        "move.l %%d0, %1\n\t"
        : "=m"(alertNum), "=m"(saved_regs[0])
    );

    /* Save remaining data registers */
    __asm volatile ("move.l %%d1, %0" : "=m"(saved_regs[1]));
    __asm volatile ("move.l %%d2, %0" : "=m"(saved_regs[2]));
    __asm volatile ("move.l %%d3, %0" : "=m"(saved_regs[3]));
    __asm volatile ("move.l %%d4, %0" : "=m"(saved_regs[4]));
    __asm volatile ("move.l %%d5, %0" : "=m"(saved_regs[5]));
    __asm volatile ("move.l %%d6, %0" : "=m"(saved_regs[6]));
    __asm volatile ("move.l %%d7, %0" : "=m"(saved_regs[7]));

    /* Save address registers */
    __asm volatile ("move.l %%a0, %0" : "=m"(saved_regs[8]));
    __asm volatile ("move.l %%a1, %0" : "=m"(saved_regs[9]));
    __asm volatile ("move.l %%a2, %0" : "=m"(saved_regs[10]));
    __asm volatile ("move.l %%a3, %0" : "=m"(saved_regs[11]));
    __asm volatile ("move.l %%a4, %0" : "=m"(saved_regs[12]));
    __asm volatile ("move.l %%a5, %0" : "=m"(saved_regs[13]));
    __asm volatile ("move.l %%a6, %0" : "=m"(saved_regs[14]));
    __asm volatile ("move.l %%a7, %0" : "=m"(saved_regs[15]));

    /* Get current SP for stack snapshot */
    __asm volatile ("move.l %%sp, %0" : "=r"(sp_val));

    /* Capture stack snapshot (top 64 bytes) */
    stack_snapshot_len = 64;
    if (TypeOfMem((APTR)sp_val)) {
        for (i = 0; i < stack_snapshot_len; i++) {
            stack_snapshot[i] = ((UBYTE *)sp_val)[i];
        }
    } else {
        stack_snapshot_len = 0;
    }

    /* Format crash message:
     * CRASH|alert_hex|alert_name|D0:D1:...:D7|A0:A1:...:A7|SP|stack_hex
     */
    name = alert_name(alertNum);

    /* Build register hex strings */
    pos = sprintf(crash_buf,
        "CRASH|%08lx|%s|",
        (unsigned long)alertNum, name);

    /* Data registers D0-D7 */
    for (i = 0; i < 8; i++) {
        if (i > 0) crash_buf[pos++] = ':';
        sprintf(crash_buf + pos, "%08lx", (unsigned long)saved_regs[i]);
        pos += 8;
    }
    crash_buf[pos++] = '|';

    /* Address registers A0-A7 */
    for (i = 0; i < 8; i++) {
        if (i > 0) crash_buf[pos++] = ':';
        sprintf(crash_buf + pos, "%08lx", (unsigned long)saved_regs[8 + i]);
        pos += 8;
    }
    crash_buf[pos++] = '|';

    /* Stack pointer */
    sprintf(crash_buf + pos, "%08lx", (unsigned long)sp_val);
    pos += 8;
    crash_buf[pos++] = '|';

    /* Stack snapshot as hex */
    if (stack_snapshot_len > 0) {
        bytes_to_hex(stack_snapshot, stack_snapshot_len,
                     crash_buf + pos);
        pos += stack_snapshot_len * 2;
    }
    crash_buf[pos] = '\0';

    /* Send directly over serial - bypass normal IPC since
     * the system may be in an unstable state */
    if (serial_is_open()) {
        protocol_send_raw(crash_buf);
    }

    /* Also log to daemon UI */
    ui_add_log("*** CRASH CAUGHT ***");

    /* Call original Alert function */
    if (orig_alert) {
        /* Restore D7 with alert number and jump to original */
        __asm volatile (
            "move.l %0, %%d7\n\t"
            "move.l %1, %%a0\n\t"
            "jsr (%%a0)\n\t"
            :
            : "m"(alertNum), "m"(orig_alert)
            : "d7", "a0"
        );
    }
}

/*
 * Initialize crash handler - patches exec.library Alert().
 */
void crash_init(void)
{
    if (crash_installed) return;

    /* Alert() is at LVO offset -0x6C (-108) in exec.library */
    Disable();
    orig_alert = SetFunction((struct Library *)SysBase,
                              -108,
                              (APTR)crash_alert_handler);
    Enable();

    crash_installed = TRUE;
    printf("  Crash handler: OK (Alert patched)\n");
    ui_add_log("Crash handler active");
}

/*
 * Cleanup crash handler - restore original Alert().
 */
void crash_cleanup(void)
{
    if (!crash_installed) return;

    if (orig_alert) {
        Disable();
        SetFunction((struct Library *)SysBase, -108, orig_alert);
        Enable();
        orig_alert = NULL;
    }

    crash_installed = FALSE;
    printf("  Crash handler: removed\n");
}

/*
 * Get last crash info formatted as a string.
 * Returns 0 if crash data is available, -1 if none.
 */
int crash_get_last(char *buf, int bufSize)
{
    if (crash_buf[0] == '\0') {
        return -1;
    }

    strncpy(buf, crash_buf, bufSize - 1);
    buf[bufSize - 1] = '\0';
    return 0;
}
