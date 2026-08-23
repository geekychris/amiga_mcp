/*
 * caps_util.c - Builds the per-build parts of the CAPABILITIES reply.
 * Pure C: no Amiga headers, no static state. See caps_util.h.
 *
 * The advertised command list must be honest per build: a verb is listed
 * only when the build can actually perform it. Verbs the PPC (OS4) build
 * cannot perform stay in the dispatch table but reply
 * ERR|Unknown command|<VERB>, and are dropped from the list here.
 */

#include <string.h>

#include "caps_util.h"

typedef struct {
    const char *name;
    int on_68k;
    int on_ppc;
} CapEntry;

/* Advertised command list. Order is the historical advertising order; the
 * debugger verbs are appended at the end. Aliases (LISTDEVICES/LISTDEVS,
 * DELETE/DELETEFILE) are separate dispatch entries and are both listed. */
static const CapEntry COMMANDS[] = {
    { "PING",            1, 1 },
    { "INSPECT",         1, 1 },
    { "GETVAR",          1, 1 },
    { "SETVAR",          1, 1 },
    { "EXEC",            1, 1 },
    { "LISTCLIENTS",     1, 1 },
    { "LISTTASKS",       1, 1 },
    { "LISTLIBS",        1, 1 },
    { "LISTDEVICES",     1, 1 },
    { "LISTDEVS",        1, 1 },
    { "LISTVOLUMES",     1, 1 },
    { "LISTDIR",         1, 1 },
    { "READFILE",        1, 1 },
    { "WRITEFILE",       1, 1 },
    { "FILEINFO",        1, 1 },
    { "DELETE",          1, 1 },
    { "DELETEFILE",      1, 1 },
    { "MAKEDIR",         1, 1 },
    { "LAUNCH",          1, 1 },
    { "DOSCOMMAND",      1, 1 },
    { "RUN",             1, 1 },
    { "BREAK",           1, 1 },
    { "LISTHOOKS",       1, 1 },
    { "CALLHOOK",        1, 1 },
    { "LISTMEMREGS",     1, 1 },
    { "READMEMREG",      1, 1 },
    { "CLIENTINFO",      1, 1 },
    { "STOP",            1, 1 },
    { "SCRIPT",          1, 1 },
    { "WRITEMEM",        1, 1 },
    { "SCREENSHOT",      1, 1 },
    { "PALETTE",         1, 1 },
    { "SETPALETTE",      1, 1 },
    { "COPPERLIST",      1, 0 },   /* classic chipset copper list */
    { "SPRITES",         1, 0 },   /* classic chipset sprite DMA */
    { "LISTRESOURCES",   1, 1 },
    { "GETPERF",         1, 1 },
    { "LASTCRASH",       1, 0 },   /* 68k exception-frame crash handler */
    { "CRASHINIT",       1, 0 },
    { "CRASHREMOVE",     1, 0 },
    { "CRASHTEST",       1, 0 },
    { "MEMMAP",          1, 1 },
    { "STACKINFO",       1, 1 },
    { "CHIPREGS",        1, 0 },   /* reads $DFF000 custom registers */
    { "READREGS",        1, 0 },   /* 68k register snapshot via inline asm */
    { "SEARCH",          1, 1 },
    { "LIBINFO",         1, 1 },
    { "DEVINFO",         1, 1 },
    { "LIBFUNCS",        1, 0 },   /* 68k jump table format */
    { "SNOOPSTART",      1, 0 },   /* SetFunction() patches on exec LVOs */
    { "SNOOPSTOP",       1, 0 },
    { "SNOOPSTATUS",     1, 0 },
    { "AUDIOCHANNELS",   1, 0 },   /* Paula registers */
    { "AUDIOSAMPLE",     1, 0 },
    { "LISTSCREENS",     1, 1 },
    { "LISTWINDOWS",     1, 1 },
    { "LISTWINDOWS2",    1, 1 },
    { "LISTGADGETS",     1, 1 },
    { "WINACTIVATE",     1, 1 },
    { "WINTOFRONT",      1, 1 },
    { "WINTOBACK",       1, 1 },
    { "WINZIP",          1, 1 },
    { "WINMOVE",         1, 1 },
    { "WINSIZE",         1, 1 },
    { "SCRTOFRONT",      1, 1 },
    { "SCRTOBACK",       1, 1 },
    { "INPUTKEY",        1, 1 },
    { "INPUTMOVE",       1, 1 },
    { "INPUTCLICK",      1, 1 },
    { "LISTFONTS",       1, 1 },
    { "FONTINFO",        1, 1 },
    { "CHIPLOGSTART",    1, 0 },   /* reads $DFF000 custom registers */
    { "CHIPLOGSTOP",     1, 0 },
    { "CHIPLOGSNAPSHOT", 1, 0 },
    { "POOLSTART",       1, 0 },   /* SetFunction() patches on exec LVOs */
    { "POOLSTOP",        1, 0 },
    { "POOLS",           1, 0 },
    { "CLIPGET",         1, 1 },
    { "CLIPSET",         1, 1 },
    { "AREXXPORTS",      1, 1 },
    { "AREXXSEND",       1, 1 },
    { "SHUTDOWN",        1, 1 },
    { "CAPABILITIES",    1, 1 },
    { "PROCLIST",        1, 1 },
    { "PROCSTAT",        1, 1 },
    { "SIGNAL",          1, 1 },
    { "TAIL",            1, 1 },
    { "STOPTAIL",        1, 1 },
    { "CHECKSUM",        1, 1 },
    { "ASSIGNS",         1, 1 },
    { "ASSIGN",          1, 1 },
    { "PROTECT",         1, 1 },
    { "RENAME",          1, 1 },
    { "SETCOMMENT",      1, 1 },
    { "COPY",            1, 1 },
    { "APPEND",          1, 1 },
    { "VERSION",         1, 1 },
    { "GETENV",          1, 1 },
    { "SETENV",          1, 1 },
    { "SETDATE",         1, 1 },
    { "VOLUMES",         1, 1 },
    { "PORTS",           1, 1 },
    { "SYSINFO",         1, 1 },
    { "UPTIME",          1, 1 },
    { "REBOOT",          1, 1 },
    /* Debugger (68k breakpoint / exception-frame implementation). */
    { "DBGATTACH",       1, 0 },
    { "DBGDETACH",       1, 0 },
    { "BPSET",           1, 0 },
    { "BPCLEAR",         1, 0 },
    { "BPLIST",          1, 0 },
    { "DBGSTEP",         1, 0 },
    { "DBGNEXT",         1, 0 },
    { "DBGCONT",         1, 0 },
    { "DBGREGS",         1, 0 },
    { "DBGSETREG",       1, 0 },
    { "DBGBT",           1, 0 },
    { "DBGBREAK",        1, 0 },
    { "DBGCLEARALLBP",   1, 0 },
    { "DBGSTATUS",       1, 0 },
    { "DBGLAUNCH",       1, 0 },
};

/* Feature flags: dotted names for behaviours that are not a single verb.
 *   exec.async          RUN returns before the program does
 *   exec.signals        SIGNAL
 *   env.vars            GETENV / SETENV
 *   fs.tail             TAIL / STOPTAIL
 *   fs.attrs            PROTECT / SETCOMMENT / SETDATE
 *   gfx.planar          SCREENSHOT emits planar bitmaps + palette
 *   gfx.chunky          SCREENSHOT emits chunky pixels
 *   gfx.truecolor       SCREENSHOT emits 24-bit pixels (68k: when the front
 *                       screen is an RTG screen deeper than 8 bits)
 *   gfx.palette.read    PALETTE
 *   gfx.palette.write   SETPALETTE
 *   gfx.window          SCREENSHOT window=
 *   mem.regs            READREGS
 *   dbg.*               debugger verbs; dbg.crash = LASTCRASH
 *   client.lib          bridge client library IPC (GETVAR, CALLHOOK, ...)
 *   amiga.intuition     LISTSCREENS / LISTWINDOWS / WIN* / SCR*
 *   amiga.arexx         AREXXPORTS / AREXXSEND
 *   amiga.libs          LIBINFO / LISTLIBS
 *   amiga.libs.jumptable LIBFUNCS
 *   amiga.copper        COPPERLIST
 *   amiga.paula         AUDIOCHANNELS / AUDIOSAMPLE
 *   amiga.chipset       CHIPREGS / CHIPLOG* / SPRITES
 *   amiga.snoop         SNOOP*
 *   amiga.assigns       ASSIGNS / ASSIGN
 *   amiga.pools         POOL*
 *   amiga.clipboard     CLIPGET / CLIPSET
 *   amiga.fonts         LISTFONTS / FONTINFO
 */
static const CapEntry FEATURES[] = {
    { "exec.async",           1, 1 },
    { "exec.signals",         1, 1 },
    { "env.vars",             1, 1 },
    { "fs.tail",              1, 1 },
    { "fs.attrs",             1, 1 },
    { "gfx.planar",           1, 0 },
    { "gfx.chunky",           0, 1 },
    { "gfx.truecolor",        1, 1 },
    { "gfx.palette.read",     1, 1 },
    { "gfx.palette.write",    1, 1 },
    { "gfx.window",           1, 1 },
    { "mem.regs",             1, 0 },
    { "dbg.attach",           1, 0 },
    { "dbg.breakpoints",      1, 0 },
    { "dbg.step",             1, 0 },
    { "dbg.backtrace",        1, 0 },
    { "dbg.registers.write",  1, 0 },
    { "dbg.crash",            1, 0 },
    { "client.lib",           1, 1 },
    { "amiga.intuition",      1, 1 },
    { "amiga.arexx",          1, 1 },
    { "amiga.libs",           1, 1 },
    { "amiga.libs.jumptable", 1, 0 },
    { "amiga.copper",         1, 0 },
    { "amiga.paula",          1, 0 },
    { "amiga.chipset",        1, 0 },
    { "amiga.snoop",          1, 0 },
    { "amiga.assigns",        1, 1 },
    { "amiga.pools",          1, 0 },
    { "amiga.clipboard",      1, 1 },
    { "amiga.fonts",          1, 1 },
};

/* Profiles: named command sets the build implements in full.
 *   core   PING,VERSION,CAPABILITIES,SYSINFO,UPTIME,REBOOT,SHUTDOWN
 *   mem    INSPECT,WRITEMEM,SEARCH,MEMMAP
 *   fs     LISTDIR,READFILE,WRITEFILE,DELETE,MAKEDIR,RENAME,FILEINFO,COPY,
 *          APPEND,CHECKSUM,LISTVOLUMES,SETDATE,PROTECT,SETCOMMENT
 *   exec   LAUNCH,DOSCOMMAND,PROCLIST,PROCSTAT,STOP,SCRIPT
 *   gfx    SCREENSHOT,PALETTE,SETPALETTE
 *   input  INPUTKEY,INPUTMOVE,INPUTCLICK
 *   debug  DBGATTACH,DBGDETACH,BPSET,BPCLEAR,BPLIST,DBGSTEP,DBGNEXT,DBGCONT,
 *          DBGREGS,DBGBT,DBGSTATUS
 *   client LISTCLIENTS,CLIENTINFO,GETVAR,SETVAR,LISTHOOKS,CALLHOOK,
 *          LISTMEMREGS,READMEMREG,LISTRESOURCES,GETPERF,EXEC
 */
static const CapEntry PROFILES[] = {
    { "core",   1, 1 },
    { "mem",    1, 1 },
    { "fs",     1, 1 },
    { "exec",   1, 1 },
    { "gfx",    1, 1 },
    { "input",  1, 1 },
    { "debug",  1, 0 },
    { "client", 1, 1 },
};

static size_t build_list(char *out, size_t cap,
                         const CapEntry *tab, size_t n, int arch)
{
    size_t need = 0;   /* full length required */
    size_t pos = 0;    /* bytes written so far */
    int first = 1;
    size_t i;

    if (out && cap > 0) out[0] = '\0';

    for (i = 0; i < n; i++) {
        size_t len;
        int enabled = (arch == CAPS_ARCH_PPC) ? tab[i].on_ppc : tab[i].on_68k;
        if (!enabled) continue;

        len = strlen(tab[i].name);
        need += (first ? 0 : 1) + len;

        if (out && need < cap) {
            if (!first) out[pos++] = ',';
            memcpy(out + pos, tab[i].name, len);
            pos += len;
            out[pos] = '\0';
        }
        first = 0;
    }
    return need;
}

size_t caps_build_commands(char *out, size_t cap, int arch)
{
    return build_list(out, cap, COMMANDS,
                      sizeof(COMMANDS) / sizeof(COMMANDS[0]), arch);
}

size_t caps_build_features(char *out, size_t cap, int arch)
{
    return build_list(out, cap, FEATURES,
                      sizeof(FEATURES) / sizeof(FEATURES[0]), arch);
}

size_t caps_build_profiles(char *out, size_t cap, int arch)
{
    return build_list(out, cap, PROFILES,
                      sizeof(PROFILES) / sizeof(PROFILES[0]), arch);
}

const char *caps_platform(int arch)
{
    return (arch == CAPS_ARCH_PPC) ? "amiga/aos4/unknown" : "amiga/aos3/unknown";
}
