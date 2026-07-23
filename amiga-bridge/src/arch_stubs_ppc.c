/*
 * PPC AmigaOS 4.1 stubs for 68k-only bridge subsystems.
 *
 * On PowerPC the register-capture inline asm in snoop.c / debugger.c
 * and the 68k exception-frame parsing in crash_handler.c don't apply,
 * so those source files are excluded from the PPC build. Callers in
 * main.c / protocol_handler.c reference their APIs unconditionally,
 * though, so we provide no-op / not-implemented stubs here to keep
 * the link honest and give the host-side a clear signal when a
 * feature isn't available on this target.
 *
 * As each subsystem gets a real PPC port, delete its stub here and
 * add the real source file to DAEMON_SRCS_PORTABLE.
 */

#include <string.h>

#include "bridge_internal.h"

/* ---- crash_handler stubs ---------------------------------------- */

int  crash_init(void)     { return 0; }
void crash_cleanup(void)  {}
int  crash_get_last(char *buf, int max)
{
    if (!buf || max < 1) return -1;
    const char *msg = "not-implemented|arch=ppc";
    int n = (int)strlen(msg);
    if (n >= max) n = max - 1;
    memcpy(buf, msg, n);
    buf[n] = '\0';
    return 0;
}

/* ---- snoop stubs ------------------------------------------------- */

int  snoop_is_active(void)     { return 0; }
void snoop_drain(void)         {}
void snoop_start(void)         {}
void snoop_stop(void)          {}
void snoop_handle_status(void) {}

/* ---- debugger stubs --------------------------------------------- */

/* Kept intentionally empty for now — protocol_handler.c doesn't call
 * debugger_* symbols directly; the FS-UAE-integrated debugger path is
 * host-side. If future code paths link against debugger_* here, add
 * matching no-ops. */
