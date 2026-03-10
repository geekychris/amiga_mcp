/*
 * snoop.c - SnoopDos-like system call monitoring
 *
 * Uses SetFunction() to patch exec.library and dos.library functions.
 * Intercepted calls are logged to a ring buffer, which the main loop
 * drains and sends over serial protocol.
 *
 * Patching approach: "unhook, call, re-hook" inside Disable()/Enable().
 * This is the classic safe AmigaOS pattern that avoids all register
 * calling convention issues with GCC on m68k.
 */

#include <exec/types.h>
#include <exec/memory.h>
#include <exec/execbase.h>
#include <exec/libraries.h>
#include <dos/dos.h>
#include <dos/dosextens.h>
#include <proto/exec.h>
#include <proto/dos.h>

#include <string.h>
#include <stdio.h>

#include "bridge_internal.h"

extern struct ExecBase *SysBase;
extern struct DosLibrary *DOSBase;

/* ---- Ring buffer ---- */

#define SNOOP_RING_SIZE 128

struct SnoopEntry {
    char  func[20];
    ULONG caller;
    char  arg1[60];
    char  arg2[40];
    char  result[40];
    ULONG tick;
};

static struct SnoopEntry g_ring[SNOOP_RING_SIZE];
static volatile ULONG g_head = 0;   /* write index */
static volatile ULONG g_tail = 0;   /* read index */
static BOOL g_active = FALSE;
static ULONG g_event_count = 0;
static ULONG g_drop_count = 0;

/* ---- Original function pointers (saved by SetFunction) ---- */

static APTR orig_OpenLibrary   = NULL;
static APTR orig_CloseLibrary  = NULL;
static APTR orig_OpenDevice    = NULL;
static APTR orig_CloseDevice   = NULL;
static APTR orig_dos_Open      = NULL;
static APTR orig_dos_Close     = NULL;
static APTR orig_dos_Lock      = NULL;
static APTR orig_dos_UnLock    = NULL;
static APTR orig_dos_LoadSeg   = NULL;
static APTR orig_dos_UnLoadSeg = NULL;

/* ---- LVO offsets ---- */

#define LVO_OpenLibrary   (-552)
#define LVO_CloseLibrary  (-414)
#define LVO_OpenDevice    (-444)
#define LVO_CloseDevice   (-450)

#define LVO_dos_Open      (-30)
#define LVO_dos_Close     (-36)
#define LVO_dos_Lock      (-84)
#define LVO_dos_UnLock    (-90)
#define LVO_dos_LoadSeg   (-150)
#define LVO_dos_UnLoadSeg (-156)

/* ---- Helpers ---- */

/*
 * Record an event in the ring buffer.
 * Called from patch functions. Uses Disable()/Enable() to protect
 * the head increment since multiple patches can nest.
 */
static void snoop_record(const char *func, ULONG caller,
                         const char *a1, const char *a2, const char *res)
{
    ULONG next;
    struct SnoopEntry *e;

    if (!g_active) return;

    Disable();
    next = (g_head + 1) % SNOOP_RING_SIZE;
    if (next == g_tail) {
        /* Buffer full - drop oldest */
        g_tail = (g_tail + 1) % SNOOP_RING_SIZE;
        g_drop_count++;
    }
    e = &g_ring[g_head];
    g_head = next;
    g_event_count++;
    Enable();

    /* Fill entry outside critical section - this slot is ours */
    strncpy(e->func, func, sizeof(e->func) - 1);
    e->func[sizeof(e->func) - 1] = '\0';
    e->caller = caller;

    if (a1) {
        strncpy(e->arg1, a1, sizeof(e->arg1) - 1);
        e->arg1[sizeof(e->arg1) - 1] = '\0';
    } else {
        e->arg1[0] = '\0';
    }

    if (a2) {
        strncpy(e->arg2, a2, sizeof(e->arg2) - 1);
        e->arg2[sizeof(e->arg2) - 1] = '\0';
    } else {
        e->arg2[0] = '\0';
    }

    if (res) {
        strncpy(e->result, res, sizeof(e->result) - 1);
        e->result[sizeof(e->result) - 1] = '\0';
    } else {
        e->result[0] = '\0';
    }

    e->tick = g_event_count;  /* Use event count as monotonic tick */
}

/* Safe pointer-to-hex helper */
static void ptr_to_hex(char *buf, int buflen, ULONG val)
{
    sprintf(buf, "%08lx", (unsigned long)val);
}

/* ---- exec.library patches ---- */

/*
 * OpenLibrary patch (LVO -552)
 * Register args: A1=name, D0=version, A6=SysBase
 */
static struct Library * patch_OpenLibrary(void)
{
    register UBYTE *a1_name __asm("a1");
    register ULONG d0_ver  __asm("d0");
    UBYTE *name = a1_name;
    ULONG ver = d0_ver;
    struct Library *result;
    char verbuf[16];

    /* Unhook, call original, re-hook */
    Disable();
    SetFunction((struct Library *)SysBase, LVO_OpenLibrary,
                (ULONG (*)())orig_OpenLibrary);
    Enable();

    result = OpenLibrary(name, ver);

    Disable();
    orig_OpenLibrary = (APTR)SetFunction((struct Library *)SysBase,
                        LVO_OpenLibrary, (ULONG (*)())patch_OpenLibrary);
    Enable();

    sprintf(verbuf, "v%ld", (long)ver);
    snoop_record("OpenLibrary", 0, (const char *)name, verbuf,
                 result ? "OK" : "FAIL");
    return result;
}

/*
 * CloseLibrary patch (LVO -414)
 * Register args: A1=library, A6=SysBase
 */
static void patch_CloseLibrary(void)
{
    register struct Library *a1_lib __asm("a1");
    struct Library *lib = a1_lib;
    char namebuf[60];
    char addrbuf[16];

    /* Save lib name before closing (it may be expunged) */
    if (lib && lib->lib_Node.ln_Name) {
        strncpy(namebuf, lib->lib_Node.ln_Name, sizeof(namebuf) - 1);
        namebuf[sizeof(namebuf) - 1] = '\0';
    } else {
        strcpy(namebuf, "NULL");
    }
    ptr_to_hex(addrbuf, sizeof(addrbuf), (ULONG)lib);

    Disable();
    SetFunction((struct Library *)SysBase, LVO_CloseLibrary,
                (ULONG (*)())orig_CloseLibrary);
    Enable();

    CloseLibrary(lib);

    Disable();
    orig_CloseLibrary = (APTR)SetFunction((struct Library *)SysBase,
                         LVO_CloseLibrary, (ULONG (*)())patch_CloseLibrary);
    Enable();

    snoop_record("CloseLibrary", 0, namebuf, addrbuf, NULL);
}

/*
 * OpenDevice patch (LVO -444)
 * Register args: A0=devName, D0=unitNumber, A1=ioRequest, D1=flags, A6=SysBase
 */
static BYTE patch_OpenDevice(void)
{
    register UBYTE *a0_name   __asm("a0");
    register ULONG  d0_unit   __asm("d0");
    register struct IORequest *a1_io __asm("a1");
    register ULONG  d1_flags  __asm("d1");
    UBYTE *devname = a0_name;
    ULONG unit = d0_unit;
    struct IORequest *io = a1_io;
    ULONG flags = d1_flags;
    BYTE result;
    char unitbuf[16];

    Disable();
    SetFunction((struct Library *)SysBase, LVO_OpenDevice,
                (ULONG (*)())orig_OpenDevice);
    Enable();

    result = OpenDevice(devname, unit, io, flags);

    Disable();
    orig_OpenDevice = (APTR)SetFunction((struct Library *)SysBase,
                       LVO_OpenDevice, (ULONG (*)())patch_OpenDevice);
    Enable();

    sprintf(unitbuf, "unit %ld", (long)unit);
    snoop_record("OpenDevice", 0, (const char *)devname, unitbuf,
                 result == 0 ? "OK" : "FAIL");
    return result;
}

/*
 * CloseDevice patch (LVO -450)
 * Register args: A1=ioRequest, A6=SysBase
 */
static void patch_CloseDevice(void)
{
    register struct IORequest *a1_io __asm("a1");
    struct IORequest *io = a1_io;
    char devbuf[60];
    char addrbuf[16];

    /* Try to get device name from IO request */
    if (io && io->io_Device && io->io_Device->dd_Library.lib_Node.ln_Name) {
        strncpy(devbuf, io->io_Device->dd_Library.lib_Node.ln_Name,
                sizeof(devbuf) - 1);
        devbuf[sizeof(devbuf) - 1] = '\0';
    } else {
        strcpy(devbuf, "?");
    }
    ptr_to_hex(addrbuf, sizeof(addrbuf), (ULONG)io);

    Disable();
    SetFunction((struct Library *)SysBase, LVO_CloseDevice,
                (ULONG (*)())orig_CloseDevice);
    Enable();

    CloseDevice(io);

    Disable();
    orig_CloseDevice = (APTR)SetFunction((struct Library *)SysBase,
                        LVO_CloseDevice, (ULONG (*)())patch_CloseDevice);
    Enable();

    snoop_record("CloseDevice", 0, devbuf, addrbuf, NULL);
}

/* ---- dos.library patches ---- */

/*
 * Open patch (LVO -30)
 * Register args: D1=name(STRPTR), D2=accessMode, A6=DOSBase
 */
static BPTR patch_dos_Open(void)
{
    register char *d1_name __asm("d1");
    register LONG  d2_mode __asm("d2");
    char *name = d1_name;
    LONG mode = d2_mode;
    BPTR result;
    const char *modestr;

    Disable();
    SetFunction((struct Library *)DOSBase, LVO_dos_Open,
                (ULONG (*)())orig_dos_Open);
    Enable();

    result = Open((CONST_STRPTR)name, mode);

    Disable();
    orig_dos_Open = (APTR)SetFunction((struct Library *)DOSBase,
                     LVO_dos_Open, (ULONG (*)())patch_dos_Open);
    Enable();

    if (mode == MODE_OLDFILE)       modestr = "OLD";
    else if (mode == MODE_NEWFILE)  modestr = "NEW";
    else if (mode == MODE_READWRITE) modestr = "RW";
    else                             modestr = "?";

    snoop_record("Open", 0, name, modestr,
                 result ? "OK" : "FAIL");
    return result;
}

/*
 * Close patch (LVO -36)
 * Register args: D1=file(BPTR), A6=DOSBase
 */
static LONG patch_dos_Close(void)
{
    register BPTR d1_fh __asm("d1");
    BPTR fh = d1_fh;
    LONG result;
    char addrbuf[16];

    ptr_to_hex(addrbuf, sizeof(addrbuf), (ULONG)fh);

    Disable();
    SetFunction((struct Library *)DOSBase, LVO_dos_Close,
                (ULONG (*)())orig_dos_Close);
    Enable();

    result = Close(fh);

    Disable();
    orig_dos_Close = (APTR)SetFunction((struct Library *)DOSBase,
                      LVO_dos_Close, (ULONG (*)())patch_dos_Close);
    Enable();

    snoop_record("Close", 0, addrbuf, NULL,
                 result ? "OK" : "FAIL");
    return result;
}

/*
 * Lock patch (LVO -84)
 * Register args: D1=name(STRPTR), D2=accessMode, A6=DOSBase
 */
static BPTR patch_dos_Lock(void)
{
    register char *d1_name __asm("d1");
    register LONG  d2_mode __asm("d2");
    char *name = d1_name;
    LONG mode = d2_mode;
    BPTR result;
    const char *modestr;

    Disable();
    SetFunction((struct Library *)DOSBase, LVO_dos_Lock,
                (ULONG (*)())orig_dos_Lock);
    Enable();

    result = Lock((CONST_STRPTR)name, mode);

    Disable();
    orig_dos_Lock = (APTR)SetFunction((struct Library *)DOSBase,
                     LVO_dos_Lock, (ULONG (*)())patch_dos_Lock);
    Enable();

    modestr = (mode == ACCESS_READ) ? "SHARED" : "EXCL";

    snoop_record("Lock", 0, name, modestr,
                 result ? "OK" : "FAIL");
    return result;
}

/*
 * UnLock patch (LVO -90)
 * Register args: D1=lock(BPTR), A6=DOSBase
 */
static void patch_dos_UnLock(void)
{
    register BPTR d1_lock __asm("d1");
    BPTR lock = d1_lock;
    char addrbuf[16];

    ptr_to_hex(addrbuf, sizeof(addrbuf), (ULONG)lock);

    Disable();
    SetFunction((struct Library *)DOSBase, LVO_dos_UnLock,
                (ULONG (*)())orig_dos_UnLock);
    Enable();

    UnLock(lock);

    Disable();
    orig_dos_UnLock = (APTR)SetFunction((struct Library *)DOSBase,
                       LVO_dos_UnLock, (ULONG (*)())patch_dos_UnLock);
    Enable();

    snoop_record("UnLock", 0, addrbuf, NULL, NULL);
}

/*
 * LoadSeg patch (LVO -150)
 * Register args: D1=name(STRPTR), A6=DOSBase
 */
static BPTR patch_dos_LoadSeg(void)
{
    register char *d1_name __asm("d1");
    char *name = d1_name;
    BPTR result;

    Disable();
    SetFunction((struct Library *)DOSBase, LVO_dos_LoadSeg,
                (ULONG (*)())orig_dos_LoadSeg);
    Enable();

    result = LoadSeg((CONST_STRPTR)name);

    Disable();
    orig_dos_LoadSeg = (APTR)SetFunction((struct Library *)DOSBase,
                        LVO_dos_LoadSeg, (ULONG (*)())patch_dos_LoadSeg);
    Enable();

    snoop_record("LoadSeg", 0, name, NULL,
                 result ? "OK" : "FAIL");
    return result;
}

/*
 * UnLoadSeg patch (LVO -156)
 * Register args: D1=segList(BPTR), A6=DOSBase
 */
static void patch_dos_UnLoadSeg(void)
{
    register BPTR d1_seg __asm("d1");
    BPTR seg = d1_seg;
    char addrbuf[16];

    ptr_to_hex(addrbuf, sizeof(addrbuf), (ULONG)seg);

    Disable();
    SetFunction((struct Library *)DOSBase, LVO_dos_UnLoadSeg,
                (ULONG (*)())orig_dos_UnLoadSeg);
    Enable();

    UnLoadSeg(seg);

    Disable();
    orig_dos_UnLoadSeg = (APTR)SetFunction((struct Library *)DOSBase,
                          LVO_dos_UnLoadSeg, (ULONG (*)())patch_dos_UnLoadSeg);
    Enable();

    snoop_record("UnLoadSeg", 0, addrbuf, NULL, NULL);
}

/* ---- Public API ---- */

/*
 * Install all patches and start monitoring.
 */
void snoop_start(void)
{
    if (g_active) return;

    /* Reset ring buffer */
    g_head = 0;
    g_tail = 0;
    g_event_count = 0;
    g_drop_count = 0;

    /* Install exec.library patches */
    Disable();
    orig_OpenLibrary = (APTR)SetFunction((struct Library *)SysBase,
                        LVO_OpenLibrary, (ULONG (*)())patch_OpenLibrary);
    orig_CloseLibrary = (APTR)SetFunction((struct Library *)SysBase,
                         LVO_CloseLibrary, (ULONG (*)())patch_CloseLibrary);
    orig_OpenDevice = (APTR)SetFunction((struct Library *)SysBase,
                       LVO_OpenDevice, (ULONG (*)())patch_OpenDevice);
    orig_CloseDevice = (APTR)SetFunction((struct Library *)SysBase,
                        LVO_CloseDevice, (ULONG (*)())patch_CloseDevice);

    /* Install dos.library patches */
    orig_dos_Open = (APTR)SetFunction((struct Library *)DOSBase,
                     LVO_dos_Open, (ULONG (*)())patch_dos_Open);
    orig_dos_Close = (APTR)SetFunction((struct Library *)DOSBase,
                      LVO_dos_Close, (ULONG (*)())patch_dos_Close);
    orig_dos_Lock = (APTR)SetFunction((struct Library *)DOSBase,
                     LVO_dos_Lock, (ULONG (*)())patch_dos_Lock);
    orig_dos_UnLock = (APTR)SetFunction((struct Library *)DOSBase,
                       LVO_dos_UnLock, (ULONG (*)())patch_dos_UnLock);
    orig_dos_LoadSeg = (APTR)SetFunction((struct Library *)DOSBase,
                        LVO_dos_LoadSeg, (ULONG (*)())patch_dos_LoadSeg);
    orig_dos_UnLoadSeg = (APTR)SetFunction((struct Library *)DOSBase,
                          LVO_dos_UnLoadSeg, (ULONG (*)())patch_dos_UnLoadSeg);
    Enable();

    g_active = TRUE;
    ui_add_log("Snoop: monitoring started");
    printf("Snoop: 10 functions patched\n");
}

/*
 * Remove all patches and stop monitoring.
 */
void snoop_stop(void)
{
    if (!g_active) return;

    g_active = FALSE;

    /* Restore all original function vectors */
    Disable();

    if (orig_OpenLibrary)
        SetFunction((struct Library *)SysBase, LVO_OpenLibrary,
                    (ULONG (*)())orig_OpenLibrary);
    if (orig_CloseLibrary)
        SetFunction((struct Library *)SysBase, LVO_CloseLibrary,
                    (ULONG (*)())orig_CloseLibrary);
    if (orig_OpenDevice)
        SetFunction((struct Library *)SysBase, LVO_OpenDevice,
                    (ULONG (*)())orig_OpenDevice);
    if (orig_CloseDevice)
        SetFunction((struct Library *)SysBase, LVO_CloseDevice,
                    (ULONG (*)())orig_CloseDevice);

    if (orig_dos_Open)
        SetFunction((struct Library *)DOSBase, LVO_dos_Open,
                    (ULONG (*)())orig_dos_Open);
    if (orig_dos_Close)
        SetFunction((struct Library *)DOSBase, LVO_dos_Close,
                    (ULONG (*)())orig_dos_Close);
    if (orig_dos_Lock)
        SetFunction((struct Library *)DOSBase, LVO_dos_Lock,
                    (ULONG (*)())orig_dos_Lock);
    if (orig_dos_UnLock)
        SetFunction((struct Library *)DOSBase, LVO_dos_UnLock,
                    (ULONG (*)())orig_dos_UnLock);
    if (orig_dos_LoadSeg)
        SetFunction((struct Library *)DOSBase, LVO_dos_LoadSeg,
                    (ULONG (*)())orig_dos_LoadSeg);
    if (orig_dos_UnLoadSeg)
        SetFunction((struct Library *)DOSBase, LVO_dos_UnLoadSeg,
                    (ULONG (*)())orig_dos_UnLoadSeg);

    Enable();

    /* Clear saved pointers */
    orig_OpenLibrary   = NULL;
    orig_CloseLibrary  = NULL;
    orig_OpenDevice    = NULL;
    orig_CloseDevice   = NULL;
    orig_dos_Open      = NULL;
    orig_dos_Close     = NULL;
    orig_dos_Lock      = NULL;
    orig_dos_UnLock    = NULL;
    orig_dos_LoadSeg   = NULL;
    orig_dos_UnLoadSeg = NULL;

    ui_add_log("Snoop: monitoring stopped");
    printf("Snoop: all patches removed\n");
}

/*
 * Drain buffered events and send over serial.
 * Called from the main loop each iteration.
 * Sends at most 8 events per call to avoid flooding serial.
 */
void snoop_drain(void)
{
    static char buf[BRIDGE_MAX_LINE];
    struct SnoopEntry *e;
    int count = 0;

    while (g_tail != g_head && count < 8) {
        e = &g_ring[g_tail];

        /* Format: SNOOP|func|caller_hex|arg1|arg2|result|tick */
        sprintf(buf, "SNOOP|%s|%08lx|%s|%s|%s|%lu",
                e->func,
                (unsigned long)e->caller,
                e->arg1,
                e->arg2,
                e->result,
                (unsigned long)e->tick);
        protocol_send_raw(buf);

        g_tail = (g_tail + 1) % SNOOP_RING_SIZE;
        count++;
    }
}

/*
 * Return whether snoop monitoring is currently active.
 */
BOOL snoop_is_active(void)
{
    return g_active;
}

/*
 * Send snoop status over serial.
 * Format: SNOOPSTATE|active|events|drops|buffered
 */
void snoop_handle_status(void)
{
    static char buf[BRIDGE_MAX_LINE];
    ULONG buffered;

    if (g_head >= g_tail)
        buffered = g_head - g_tail;
    else
        buffered = SNOOP_RING_SIZE - g_tail + g_head;

    sprintf(buf, "SNOOPSTATE|%s|%lu|%lu|%lu",
            g_active ? "ON" : "OFF",
            (unsigned long)g_event_count,
            (unsigned long)g_drop_count,
            (unsigned long)buffered);
    protocol_send_raw(buf);
}
