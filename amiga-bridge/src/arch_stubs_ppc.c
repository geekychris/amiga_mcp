// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Chris Collins <chris@hitorro.com>

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

void crash_init(void)     {}
void crash_cleanup(void)  {}
int  crash_get_last(char *buf, int max)
{
    /* No crash data is ever captured on OS4; LASTCRASH itself answers
     * ERR|Unknown command|LASTCRASH in protocol_handler.c. */
    (void)buf; (void)max;
    return -1;
}

/* ---- snoop stubs ------------------------------------------------- */

BOOL snoop_is_active(void)     { return FALSE; }
void snoop_drain(void)         {}
void snoop_start(void)         {}
void snoop_stop(void)          {}
void snoop_handle_status(void) { protocol_send_raw("ERR|Unknown command|SNOOPSTATUS"); }

/* ---- debugger stubs --------------------------------------------- */

/* protocol_handler.c dispatches the DBG* / BP* commands into these entry
 * points unconditionally. On PPC OS4 the classic breakpoint /
 * exception-frame implementation does not apply; the verbs are not
 * advertised in CAPABILITIES and answer exactly what an unknown verb
 * answers, ERR|Unknown command|<VERB>, so hosts treat them as unsupported.
 * Replace with a real PPC debugger backend when one exists. */
static void dbg_not_impl(const char *verb)
{
    static char buf[64];
    int n = 0;
    const char *prefix = "ERR|Unknown command|";
    while (*prefix && n < (int)sizeof(buf) - 1) buf[n++] = *prefix++;
    while (*verb   && n < (int)sizeof(buf) - 1) buf[n++] = *verb++;
    buf[n] = '\0';
    protocol_send_raw(buf);
}

void dbg_handle_attach(const char *args)   { (void)args; dbg_not_impl("DBGATTACH"); }
void dbg_handle_detach(void)               { dbg_not_impl("DBGDETACH"); }
void dbg_handle_bpset(const char *args)    { (void)args; dbg_not_impl("BPSET"); }
void dbg_handle_bpclear(const char *args)  { (void)args; dbg_not_impl("BPCLEAR"); }
void dbg_handle_bplist(void)               { dbg_not_impl("BPLIST"); }
void dbg_handle_step(void)                 { dbg_not_impl("DBGSTEP"); }
void dbg_handle_next(void)                 { dbg_not_impl("DBGNEXT"); }
void dbg_handle_continue(void)             { dbg_not_impl("DBGCONT"); }
void dbg_handle_regs(void)                 { dbg_not_impl("DBGREGS"); }
void dbg_handle_setreg(const char *args)   { (void)args; dbg_not_impl("DBGSETREG"); }
void dbg_handle_backtrace(void)            { dbg_not_impl("DBGBT"); }
void dbg_handle_clearall(void)             { dbg_not_impl("DBGCLEARALLBP"); }
void dbg_poll(void)                        {}
void dbg_handle_break(void)                { dbg_not_impl("DBGBREAK"); }
void dbg_handle_status(void)               { dbg_not_impl("DBGSTATUS"); }
void dbg_handle_launch(const char *args)   { (void)args; dbg_not_impl("DBGLAUNCH"); }
void dbg_cleanup(void)                     {}
BOOL dbg_should_pause_on_launch(void)      { return FALSE; }
