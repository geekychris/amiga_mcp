/*
 * protocol_handler.c - Serial protocol parsing and formatting
 *
 * Handles the line-based pipe-delimited protocol between
 * the daemon and the MCP host.
 */

#include <exec/types.h>
#include <exec/memory.h>
#include <dos/dos.h>
#include <proto/exec.h>
#include <proto/dos.h>

#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#include "bridge_internal.h"

/* Version defines */
#define BRIDGE_VERSION_MAJOR 1
#define BRIDGE_VERSION_MINOR 0

ULONG g_tx_count = 0;
ULONG g_rx_count = 0;
BOOL g_shutdown_requested = FALSE;

static const char *level_chars = "DIWE";
static const char *type_names[] = {"i32", "u32", "str", "f32", "ptr"};

/* Forward declarations */
static void handle_ping(void);
static void handle_inspect(const char *args);
static void handle_getvar(const char *args);
static void handle_setvar(const char *args);
static void handle_exec(const char *args);
static void handle_listclients(void);
static void handle_listtasks(void);
static void handle_listlibs(void);
static void handle_listdevices(void);
static void handle_listvolumes(void);
static void handle_listdir(const char *args);
static void handle_readfile(const char *args);
static void handle_writefile(const char *args);
static void handle_fileinfo(const char *args);
static void handle_delete(const char *args);
static void handle_makedir(const char *args);
static void handle_launch(const char *args);
static void handle_run(const char *args);
static void handle_break(const char *args);
static void handle_listhooks(const char *args);
static void handle_callhook(const char *args);
static void handle_listmemregs(const char *args);
static void handle_readmemreg(const char *args);
static void handle_clientinfo(const char *args);
static void handle_stop(const char *args);
static void handle_script(const char *args);
static void handle_writemem(const char *args);
static void handle_listresources(const char *args);
static void handle_getperf(const char *args);
static void handle_lastcrash(void);
static void handle_capabilities(void);
static void handle_proclist(void);
static void handle_procstat(const char *args);
static void handle_signal(const char *args);
static void handle_tail(const char *args);
static void handle_stoptail(void);
static void handle_checksum(const char *args);
static void handle_assigns(void);
static void handle_assign(const char *args);
static void handle_protect(const char *args);
static void handle_rename(const char *args);
static void handle_setcomment(const char *args);
static void handle_copy(const char *args);
static void handle_append(const char *args);
static void handle_version(void);
static void handle_getenv(const char *args);
static void handle_setenv(const char *args);
static void handle_setdate(const char *args);
static void handle_volumes_ext(void);
static void handle_ports(void);
static void handle_sysinfo(void);
static void handle_uptime(void);
static void handle_reboot(void);

static void send_line(const char *line)
{
    /* Combine data + newline into a single write to avoid
     * FS-UAE serial buffer issues with split writes.
     * Static buffer: saves ~1KB stack per call (AmigaOS 4KB default stack). */
    static char sendbuf[BRIDGE_MAX_LINE + 2];
    int len = strlen(line);
    if (len > BRIDGE_MAX_LINE) len = BRIDGE_MAX_LINE;
    CopyMem((APTR)line, sendbuf, len);
    sendbuf[len] = '\n';
    serial_write(sendbuf, len + 1);
    g_tx_count++;
}

/* Send an error response, safely truncating the message */
static void send_err(const char *context, const char *detail)
{
    static char buf[BRIDGE_MAX_LINE];
    int clen = strlen(context);
    int dlen = detail ? strlen(detail) : 0;

    /* "ERR|context|detail" - truncate detail if needed */
    if (4 + clen + 1 + dlen >= BRIDGE_MAX_LINE) {
        dlen = BRIDGE_MAX_LINE - 4 - clen - 2;
        if (dlen < 0) dlen = 0;
    }

    strcpy(buf, "ERR|");
    strcat(buf, context);
    if (detail && dlen > 0) {
        strcat(buf, "|");
        strncat(buf, detail, dlen);
    }
    send_line(buf);
}

/* Send an OK response, safely truncating */
static void send_ok(const char *context, const char *detail)
{
    static char buf[BRIDGE_MAX_LINE];
    strcpy(buf, "OK|");
    strncat(buf, context, BRIDGE_MAX_LINE - 4);
    if (detail) {
        strncat(buf, "|", BRIDGE_MAX_LINE - strlen(buf) - 1);
        strncat(buf, detail, BRIDGE_MAX_LINE - strlen(buf) - 1);
    }
    buf[BRIDGE_MAX_LINE - 1] = '\0';
    send_line(buf);
}

/*
 * Parse a line received from host and dispatch to handler.
 */
void protocol_parse_line(const char *line)
{
    char cmd[32];
    const char *args = NULL;
    const char *sep;
    int cmdlen;

    if (!line || line[0] == '\0') return;

    g_rx_count++;
    g_host_connected = TRUE;

    /* Extract command (before first '|') */
    sep = strchr(line, '|');
    if (sep) {
        cmdlen = (int)(sep - line);
        if (cmdlen > 31) cmdlen = 31;
        strncpy(cmd, line, cmdlen);
        cmd[cmdlen] = '\0';
        args = sep + 1;
    } else {
        strncpy(cmd, line, 31);
        cmd[31] = '\0';
        args = "";
    }

    if (strcmp(cmd, "PING") == 0) {
        handle_ping();
    } else if (strcmp(cmd, "INSPECT") == 0) {
        handle_inspect(args);
    } else if (strcmp(cmd, "GETVAR") == 0) {
        handle_getvar(args);
    } else if (strcmp(cmd, "SETVAR") == 0) {
        handle_setvar(args);
    } else if (strcmp(cmd, "EXEC") == 0) {
        handle_exec(args);
    } else if (strcmp(cmd, "LISTCLIENTS") == 0) {
        handle_listclients();
    } else if (strcmp(cmd, "LISTTASKS") == 0) {
        handle_listtasks();
    } else if (strcmp(cmd, "LISTLIBS") == 0) {
        handle_listlibs();
    } else if (strcmp(cmd, "LISTDEVICES") == 0 || strcmp(cmd, "LISTDEVS") == 0) {
        handle_listdevices();
    } else if (strcmp(cmd, "LISTVOLUMES") == 0) {
        handle_listvolumes();
    } else if (strcmp(cmd, "LISTDIR") == 0) {
        handle_listdir(args);
    } else if (strcmp(cmd, "READFILE") == 0) {
        handle_readfile(args);
    } else if (strcmp(cmd, "WRITEFILE") == 0) {
        handle_writefile(args);
    } else if (strcmp(cmd, "FILEINFO") == 0) {
        handle_fileinfo(args);
    } else if (strcmp(cmd, "DELETE") == 0 || strcmp(cmd, "DELETEFILE") == 0) {
        handle_delete(args);
    } else if (strcmp(cmd, "MAKEDIR") == 0) {
        handle_makedir(args);
    } else if (strcmp(cmd, "LAUNCH") == 0) {
        handle_launch(args);
    } else if (strcmp(cmd, "DOSCOMMAND") == 0) {
        handle_launch(args);  /* Same as LAUNCH */
    } else if (strcmp(cmd, "RUN") == 0) {
        handle_run(args);
    } else if (strcmp(cmd, "BREAK") == 0) {
        handle_break(args);
    } else if (strcmp(cmd, "LISTHOOKS") == 0) {
        handle_listhooks(args);
    } else if (strcmp(cmd, "CALLHOOK") == 0) {
        handle_callhook(args);
    } else if (strcmp(cmd, "LISTMEMREGS") == 0) {
        handle_listmemregs(args);
    } else if (strcmp(cmd, "READMEMREG") == 0) {
        handle_readmemreg(args);
    } else if (strcmp(cmd, "CLIENTINFO") == 0) {
        handle_clientinfo(args);
    } else if (strcmp(cmd, "STOP") == 0) {
        handle_stop(args);
    } else if (strcmp(cmd, "SCRIPT") == 0) {
        handle_script(args);
    } else if (strcmp(cmd, "WRITEMEM") == 0) {
        handle_writemem(args);
    } else if (strcmp(cmd, "SCREENSHOT") == 0) {
        gfx_handle_screenshot(args);
    } else if (strcmp(cmd, "PALETTE") == 0) {
        gfx_handle_palette(args);
    } else if (strcmp(cmd, "SETPALETTE") == 0) {
        gfx_handle_setpalette(args);
    } else if (strcmp(cmd, "COPPERLIST") == 0) {
        gfx_handle_copperlist(args);
    } else if (strcmp(cmd, "SPRITES") == 0) {
        gfx_handle_sprites(args);
    } else if (strcmp(cmd, "LISTWINDOWS") == 0) {
        gfx_handle_listwindows(args);
    } else if (strcmp(cmd, "LISTRESOURCES") == 0) {
        handle_listresources(args);
    } else if (strcmp(cmd, "GETPERF") == 0) {
        handle_getperf(args);
    } else if (strcmp(cmd, "LASTCRASH") == 0) {
        handle_lastcrash();
    } else if (strcmp(cmd, "CRASHINIT") == 0) {
        crash_init();
        send_ok("CRASHINIT", "Crash handler installed");
    } else if (strcmp(cmd, "CRASHREMOVE") == 0) {
        crash_cleanup();
        send_ok("CRASHREMOVE", "Crash handler removed");
    } else if (strcmp(cmd, "MEMMAP") == 0) {
        sys_handle_memmap();
    } else if (strcmp(cmd, "STACKINFO") == 0) {
        sys_handle_stackinfo(args);
    } else if (strcmp(cmd, "CHIPREGS") == 0) {
        sys_handle_chipregs();
    } else if (strcmp(cmd, "READREGS") == 0) {
        sys_handle_readregs();
    } else if (strcmp(cmd, "SEARCH") == 0) {
        sys_handle_search(args);
    } else if (strcmp(cmd, "LIBINFO") == 0) {
        sys_handle_libinfo(args);
    } else if (strcmp(cmd, "DEVINFO") == 0) {
        sys_handle_devinfo(args);
    } else if (strcmp(cmd, "LIBFUNCS") == 0) {
        sys_handle_libfuncs(args);
    } else if (strcmp(cmd, "SNOOPSTART") == 0) {
        snoop_start();
        send_ok("SNOOPSTART", "Snoop monitoring started");
    } else if (strcmp(cmd, "SNOOPSTOP") == 0) {
        snoop_stop();
        send_ok("SNOOPSTOP", "Snoop monitoring stopped");
    } else if (strcmp(cmd, "SNOOPSTATUS") == 0) {
        snoop_handle_status();
    } else if (strcmp(cmd, "AUDIOCHANNELS") == 0) {
        audio_handle_channels();
    } else if (strcmp(cmd, "AUDIOSAMPLE") == 0) {
        audio_handle_sample(args);
    } else if (strcmp(cmd, "LISTSCREENS") == 0) {
        intui_handle_screens();
    } else if (strcmp(cmd, "LISTWINDOWS2") == 0) {
        intui_handle_windows(args);
    } else if (strcmp(cmd, "LISTGADGETS") == 0) {
        intui_handle_gadgets(args);
    } else if (strcmp(cmd, "WINACTIVATE") == 0) {
        intui_handle_activate(args);
    } else if (strcmp(cmd, "WINTOFRONT") == 0) {
        intui_handle_tofront(args);
    } else if (strcmp(cmd, "WINTOBACK") == 0) {
        intui_handle_toback(args);
    } else if (strcmp(cmd, "WINZIP") == 0) {
        intui_handle_zip(args);
    } else if (strcmp(cmd, "WINMOVE") == 0) {
        intui_handle_move(args);
    } else if (strcmp(cmd, "WINSIZE") == 0) {
        intui_handle_size(args);
    } else if (strcmp(cmd, "SCRTOFRONT") == 0) {
        intui_handle_scrtofront(args);
    } else if (strcmp(cmd, "SCRTOBACK") == 0) {
        intui_handle_scrtoback(args);
    } else if (strcmp(cmd, "INPUTKEY") == 0) {
        input_handle_key(args);
    } else if (strcmp(cmd, "INPUTMOVE") == 0) {
        input_handle_mouse_move(args);
    } else if (strcmp(cmd, "INPUTCLICK") == 0) {
        input_handle_mouse_button(args);
    } else if (strcmp(cmd, "CRASHTEST") == 0) {
        /* Trigger a non-fatal Alert to test the crash handler */
        send_ok("CRASHTEST", "Triggering test alert...");
        Alert(0x00010000); /* AG_NoMemory, recoverable */
    } else if (strcmp(cmd, "LISTFONTS") == 0) {
        font_handle_list();
    } else if (strcmp(cmd, "FONTINFO") == 0) {
        font_handle_info(args);
    } else if (strcmp(cmd, "CHIPLOGSTART") == 0) {
        chiplog_handle_start();
    } else if (strcmp(cmd, "CHIPLOGSTOP") == 0) {
        chiplog_handle_stop();
    } else if (strcmp(cmd, "CHIPLOGSNAPSHOT") == 0) {
        chiplog_handle_snapshot();
    } else if (strcmp(cmd, "POOLSTART") == 0) {
        pool_handle_start();
    } else if (strcmp(cmd, "POOLSTOP") == 0) {
        pool_handle_stop();
    } else if (strcmp(cmd, "POOLS") == 0) {
        pool_handle_list();
    } else if (strcmp(cmd, "CLIPGET") == 0) {
        clip_handle_get();
    } else if (strcmp(cmd, "CLIPSET") == 0) {
        clip_handle_set(args);
    } else if (strcmp(cmd, "AREXXPORTS") == 0) {
        arexx_handle_ports();
    } else if (strcmp(cmd, "AREXXSEND") == 0) {
        arexx_handle_send(args);
    } else if (strcmp(cmd, "CAPABILITIES") == 0) {
        handle_capabilities();
    } else if (strcmp(cmd, "PROCLIST") == 0) {
        handle_proclist();
    } else if (strcmp(cmd, "PROCSTAT") == 0) {
        handle_procstat(args);
    } else if (strcmp(cmd, "SIGNAL") == 0) {
        handle_signal(args);
    } else if (strcmp(cmd, "TAIL") == 0) {
        handle_tail(args);
    } else if (strcmp(cmd, "STOPTAIL") == 0) {
        handle_stoptail();
    } else if (strcmp(cmd, "CHECKSUM") == 0) {
        handle_checksum(args);
    } else if (strcmp(cmd, "ASSIGNS") == 0) {
        handle_assigns();
    } else if (strcmp(cmd, "ASSIGN") == 0) {
        handle_assign(args);
    } else if (strcmp(cmd, "PROTECT") == 0) {
        handle_protect(args);
    } else if (strcmp(cmd, "RENAME") == 0) {
        handle_rename(args);
    } else if (strcmp(cmd, "SETCOMMENT") == 0) {
        handle_setcomment(args);
    } else if (strcmp(cmd, "COPY") == 0) {
        handle_copy(args);
    } else if (strcmp(cmd, "APPEND") == 0) {
        handle_append(args);
    } else if (strcmp(cmd, "VERSION") == 0) {
        handle_version();
    } else if (strcmp(cmd, "GETENV") == 0) {
        handle_getenv(args);
    } else if (strcmp(cmd, "SETENV") == 0) {
        handle_setenv(args);
    } else if (strcmp(cmd, "SETDATE") == 0) {
        handle_setdate(args);
    } else if (strcmp(cmd, "VOLUMES") == 0) {
        handle_volumes_ext();
    } else if (strcmp(cmd, "PORTS") == 0) {
        handle_ports();
    } else if (strcmp(cmd, "SYSINFO") == 0) {
        handle_sysinfo();
    } else if (strcmp(cmd, "UPTIME") == 0) {
        handle_uptime();
    } else if (strcmp(cmd, "REBOOT") == 0) {
        handle_reboot();
    } else if (strcmp(cmd, "DBGATTACH") == 0) {
        dbg_handle_attach(args);
    } else if (strcmp(cmd, "DBGDETACH") == 0) {
        dbg_handle_detach();
    } else if (strcmp(cmd, "BPSET") == 0) {
        dbg_handle_bpset(args);
    } else if (strcmp(cmd, "BPCLEAR") == 0) {
        dbg_handle_bpclear(args);
    } else if (strcmp(cmd, "BPLIST") == 0) {
        dbg_handle_bplist();
    } else if (strcmp(cmd, "DBGSTEP") == 0) {
        dbg_handle_step();
    } else if (strcmp(cmd, "DBGNEXT") == 0) {
        dbg_handle_next();
    } else if (strcmp(cmd, "DBGCONT") == 0) {
        dbg_handle_continue();
    } else if (strcmp(cmd, "DBGREGS") == 0) {
        dbg_handle_regs();
    } else if (strcmp(cmd, "DBGSETREG") == 0) {
        dbg_handle_setreg(args);
    } else if (strcmp(cmd, "DBGBT") == 0) {
        dbg_handle_backtrace();
    } else if (strcmp(cmd, "DBGBREAK") == 0) {
        dbg_handle_break();
    } else if (strcmp(cmd, "DBGSTATUS") == 0) {
        dbg_handle_status();
    } else if (strcmp(cmd, "SHUTDOWN") == 0) {
        send_ok("SHUTDOWN", NULL);
        g_shutdown_requested = TRUE;
    } else {
        send_err("Unknown command", cmd);
    }
}

void protocol_send_log(const char *clientName, int level,
                       ULONG tick, const char *message)
{
    static char buf[BRIDGE_MAX_LINE];
    char lch;
    char lvl[2];

    if (level < 0 || level > 3) level = AB_INFO;
    lch = level_chars[level];
    lvl[0] = lch;
    lvl[1] = '\0';

    if (clientName && strcmp(clientName, "sys") != 0) {
        /* Client log - use CLOG format for proper client attribution.
         * Use %s instead of %c — amiga.lib RawDoFmt reads %c as 16-bit
         * WORD, causing stack misalignment and string truncation. */
        sprintf(buf, "CLOG|%s|%s|%lu|", clientName, lvl,
                (unsigned long)tick);
        /* Append message with truncation to avoid overflow */
        strncat(buf, message, BRIDGE_MAX_LINE - strlen(buf) - 1);
    } else {
        sprintf(buf, "LOG|%s|%lu|", lvl, (unsigned long)tick);
        strncat(buf, message, BRIDGE_MAX_LINE - strlen(buf) - 1);
    }
    buf[BRIDGE_MAX_LINE - 1] = '\0';
    send_line(buf);
}

void protocol_send_var(const char *clientName, const char *name,
                       int type, const char *value)
{
    static char buf[BRIDGE_MAX_LINE];
    const char *tname;

    if (type < 0 || type > 4) type = AB_TYPE_I32;
    tname = type_names[type];

    sprintf(buf, "VAR|%s.%s|%s|",
            clientName ? clientName : "sys",
            name, tname);
    strncat(buf, value, BRIDGE_MAX_LINE - strlen(buf) - 1);
    buf[BRIDGE_MAX_LINE - 1] = '\0';
    send_line(buf);
}

void protocol_send_heartbeat(ULONG tick, ULONG chipFree, ULONG fastFree)
{
    static char buf[BRIDGE_MAX_LINE];
    sprintf(buf, "HB|%lu|%lu|%lu",
            (unsigned long)tick,
            (unsigned long)chipFree,
            (unsigned long)fastFree);
    send_line(buf);
}

void protocol_send_mem(APTR addr, ULONG size, const UBYTE *data)
{
    /* Static buffers: saves ~1.5KB stack (critical on AmigaOS 4KB stack) */
    static char buf[BRIDGE_MAX_LINE];
    static char hexbuf[514]; /* 256 bytes * 2 hex chars + nul */
    ULONG offset = 0;

    while (offset < size) {
        ULONG chunk = size - offset;
        ULONG i;
        if (chunk > 256) chunk = 256;

        for (i = 0; i < chunk; i++) {
            sprintf(hexbuf + i * 2, "%02lx", (unsigned long)data[offset + i]);
        }
        hexbuf[chunk * 2] = '\0';

        sprintf(buf, "MEM|%08lx|%lu|%s",
                (unsigned long)((ULONG)addr + offset),
                (unsigned long)chunk,
                hexbuf);
        send_line(buf);
        offset += chunk;
    }
}

void protocol_send_cmd_response(ULONG cmdId, const char *status,
                                const char *responseData)
{
    static char buf[BRIDGE_MAX_LINE];
    sprintf(buf, "CMD|%lu|%s|", (unsigned long)cmdId, status);
    if (responseData) {
        strncat(buf, responseData, BRIDGE_MAX_LINE - strlen(buf) - 1);
    }
    buf[BRIDGE_MAX_LINE - 1] = '\0';
    send_line(buf);
}

void protocol_send_clients(void)
{
    /* Build the response inline using client_find to iterate. */
    char cbuf[BRIDGE_MAX_LINE];
    int cc = client_count();
    int pos;
    int found = 0;
    ULONG id;

    sprintf(cbuf, "CLIENTS|%ld|", (long)cc);
    pos = strlen(cbuf);

    /* Iterate by ID - client_find works correctly */
    for (id = 1; id <= 100 && found < cc; id++) {
        struct ClientEntry *ce = client_find(id);
        if (ce) {
            if (found > 0 && pos < BRIDGE_MAX_LINE - 2) {
                cbuf[pos++] = ',';
            }
            sprintf(cbuf + pos, "%s(%lu)",
                    ce->name,
                    (unsigned long)ce->clientId);
            pos += strlen(cbuf + pos);
            found++;
            if (pos >= BRIDGE_MAX_LINE - 64) break;
        }
    }

    cbuf[pos] = '\0';
    send_line(cbuf);
}

void protocol_send_tasks(void)
{
    static char buf[BRIDGE_MAX_LINE];
    sys_list_tasks(buf, BRIDGE_MAX_LINE);
    send_line(buf);
}

void protocol_send_libs(void)
{
    static char buf[BRIDGE_MAX_LINE];
    sys_list_libs(buf, BRIDGE_MAX_LINE);
    send_line(buf);
}

void protocol_send_devices(void)
{
    static char buf[BRIDGE_MAX_LINE];
    sys_list_devices(buf, BRIDGE_MAX_LINE);
    send_line(buf);
}

void protocol_send_dir(const char *path)
{
    static char buf[BRIDGE_MAX_LINE];
    int result = fs_list_dir(path, buf, BRIDGE_MAX_LINE);
    if (result < 0) {
        send_err("LISTDIR failed", path);
    } else {
        send_line(buf);
    }
}

void protocol_send_file(const char *path, ULONG offset, ULONG size)
{
    static UBYTE filebuf[450];
    ULONG actual = 0;
    int result;

    if (size > 450) size = 450;

    result = fs_read_file(path, offset, size, filebuf, 450, &actual);
    if (result < 0) {
        send_err("READFILE failed", path);
    } else {
        static char buf[BRIDGE_MAX_LINE];
        static char hexbuf[902];
        ULONG i;
        for (i = 0; i < actual; i++) {
            sprintf(hexbuf + i * 2, "%02lx", (unsigned long)filebuf[i]);
        }
        hexbuf[actual * 2] = '\0';
        sprintf(buf, "FILE|%s|%lu|%lu|%s",
                path, (unsigned long)offset,
                (unsigned long)actual, hexbuf);
        send_line(buf);
    }
}

void protocol_send_fileinfo(const char *path)
{
    static char buf[BRIDGE_MAX_LINE];
    int result = fs_file_info(path, buf, BRIDGE_MAX_LINE);
    if (result < 0) {
        send_err("FILEINFO failed", path);
    } else {
        send_line(buf);
    }
}

void protocol_send_raw(const char *line)
{
    send_line(line);
}

/* ---- Command handlers ---- */

static void handle_ping(void)
{
    ULONG chip, fast;
    int cc;
    static char buf[128];

    sys_avail_mem(&chip, &fast);
    cc = client_count();
    sprintf(buf, "PONG|%lu|%lu|%lu",
            (unsigned long)cc,
            (unsigned long)chip,
            (unsigned long)fast);
    send_line(buf);
}

static void handle_inspect(const char *args)
{
    /* Format: addr_hex|size */
    ULONG addr = 0;
    ULONG size = 0;
    static UBYTE membuf[256];
    static char hexbuf[514];
    static char linebuf[BRIDGE_MAX_LINE];
    const char *sep;
    int actual;
    ULONG i;
    volatile UBYTE *src;

    sep = strchr(args, '|');
    if (sep) {
        addr = strtoul(args, NULL, 16);
        size = strtoul(sep + 1, NULL, 10);
    } else {
        addr = strtoul(args, NULL, 16);
        size = 16;
    }

    if (size == 0) {
        send_err("INSPECT", "size is 0");
        return;
    }
    if (size > 256) size = 256;

    /* Validate address via sys_inspect_mem (it does the safety checks) */
    actual = sys_inspect_mem((APTR)addr, size, membuf, 256);
    if (actual <= 0) {
        char detail[64];
        sprintf(detail, "address %08lx not accessible", (unsigned long)addr);
        send_err("INSPECT", detail);
        return;
    }

    /* Read again directly with volatile to avoid CopyMem/optimizer issues.
     * sys_inspect_mem already validated the address is safe. */
    src = (volatile UBYTE *)addr;
    for (i = 0; i < (ULONG)actual; i++) {
        membuf[i] = src[i];
    }

    /* Hex-encode: must use %02lx with (unsigned long) cast because
     * amiga.lib sprintf reads %x as 16-bit WORD, misaligning the stack */
    for (i = 0; i < (ULONG)actual; i++) {
        sprintf(hexbuf + i * 2, "%02lx", (unsigned long)membuf[i]);
    }
    hexbuf[actual * 2] = '\0';

    sprintf(linebuf, "MEM|%08lx|%lu|%s",
            (unsigned long)addr,
            (unsigned long)actual,
            hexbuf);
    send_line(linebuf);
}

/*
 * Send a var request to a client and wait for reply.
 * On success, sends VAR|name|type|value over serial.
 * msgType should be ABMSG_VAR_GET or ABMSG_VAR_SET.
 */
static void var_send_and_wait(struct ClientEntry *ce, UWORD msgType,
                               const char *data, ULONG dataLen)
{
    struct BridgeMsg *bm;
    struct MsgPort *tempPort;
    struct Message *reply = NULL;
    int retries = 30; /* ~3 seconds */

    bm = (struct BridgeMsg *)AllocMem(sizeof(struct BridgeMsg),
                                       MEMF_PUBLIC | MEMF_CLEAR);
    if (!bm) {
        send_err("VAR", "out of memory");
        return;
    }

    tempPort = CreateMsgPort();
    if (!tempPort) {
        FreeMem(bm, sizeof(struct BridgeMsg));
        send_err("VAR", "cannot create port");
        return;
    }

    bm->msg.mn_ReplyPort = tempPort;
    bm->msg.mn_Length = sizeof(struct BridgeMsg);
    bm->type = msgType;
    bm->clientId = ce->clientId;
    bm->cmdId = 0;
    bm->result = 0;
    if (data && dataLen > 0) {
        if (dataLen > AB_MAX_DATA) dataLen = AB_MAX_DATA;
        memcpy(bm->data, data, dataLen);
        bm->dataLen = dataLen;
    }

    PutMsg(ce->replyPort, (struct Message *)bm);

    while (retries > 0) {
        reply = GetMsg(tempPort);
        if (reply) break;
        /* Process IPC messages while waiting to avoid deadlock */
        ipc_process();
        Delay(5);
        retries--;
    }

    {
        char dbg[120];
        sprintf(dbg, "LOG|I|0|[VAR] send_and_wait: reply=%lx retries=%ld",
                (unsigned long)reply, (long)retries);
        send_line(dbg);
    }

    if (reply) {
        if (bm->result == 0 && bm->dataLen > 0) {
            /* Client put "name|type_int|value" in bm->data */
            static char buf[BRIDGE_MAX_LINE];
            char *p = bm->data;
            char *sep1 = strchr(p, '|');
            if (sep1) {
                char *sep2 = strchr(sep1 + 1, '|');
                if (sep2) {
                    int typeInt;
                    char varName[34];
                    int nlen = (int)(sep1 - p);
                    if (nlen > 33) nlen = 33;
                    strncpy(varName, p, nlen);
                    varName[nlen] = '\0';
                    typeInt = (int)strtol(sep1 + 1, NULL, 10);
                    sprintf(buf, "VAR|%s|%s|%s", varName,
                            (typeInt >= 0 && typeInt <= 4) ?
                                type_names[typeInt] : "?",
                            sep2 + 1);
                    send_line(buf);
                } else {
                    send_err("VAR", "bad reply format");
                }
            } else {
                send_err("VAR", "bad reply format");
            }
        } else {
            send_err("VAR", "not found in client");
        }
        FreeMem(bm, sizeof(struct BridgeMsg));
    } else {
        /* Leaked message - client didn't reply */
        send_err("VAR", "client timeout");
    }

    DeleteMsgPort(tempPort);
}

/*
 * Find a client that has a variable with the given name.
 * Returns the client entry, or NULL if not found.
 */
static struct ClientEntry *find_client_with_var(const char *varname)
{
    int i, cc = client_count();
    for (i = 0; i < cc; i++) {
        struct ClientEntry *ce = client_get_by_index(i);
        if (ce && ce->replyPort) {
            int vi;
            for (vi = 0; vi < ce->varCount; vi++) {
                if (strcmp(ce->vars[vi].name, varname) == 0) {
                    return ce;
                }
            }
        }
    }
    return NULL;
}

static void handle_getvar(const char *args)
{
    /* Format: client_name.var_name or just var_name */
    const char *dot;
    struct ClientEntry *ce;

    dot = strchr(args, '.');
    if (dot) {
        char cname[34];
        int nlen = (int)(dot - args);
        if (nlen > 33) nlen = 33;
        strncpy(cname, args, nlen);
        cname[nlen] = '\0';

        ce = client_find_by_name(cname);
        if (ce && ce->replyPort) {
            var_send_and_wait(ce, ABMSG_VAR_GET, dot + 1, strlen(dot + 1) + 1);
        } else {
            send_err("Client not found", cname);
        }
    } else {
        ce = find_client_with_var(args);
        if (ce) {
            var_send_and_wait(ce, ABMSG_VAR_GET, args, strlen(args) + 1);
        } else {
            send_err("GETVAR", "variable not found");
        }
    }
}

static void handle_setvar(const char *args)
{
    /* Format: client_name.var_name|value or var_name|value */
    const char *dot;
    const char *sep;
    struct ClientEntry *ce;

    dot = strchr(args, '.');
    sep = strchr(args, '|');

    if (dot && sep && sep > dot) {
        char cname[34];
        int nlen = (int)(dot - args);
        if (nlen > 33) nlen = 33;
        strncpy(cname, args, nlen);
        cname[nlen] = '\0';

        ce = client_find_by_name(cname);
        if (ce && ce->replyPort) {
            /* Send "varname|value" (skip client prefix) */
            var_send_and_wait(ce, ABMSG_VAR_SET, dot + 1, strlen(dot + 1) + 1);
        } else {
            send_err("Client not found", cname);
        }
    } else if (sep) {
        /* No dot - extract varname, find client */
        char varname[34];
        int vlen = (int)(sep - args);
        if (vlen > 33) vlen = 33;
        strncpy(varname, args, vlen);
        varname[vlen] = '\0';

        ce = find_client_with_var(varname);
        if (ce) {
            var_send_and_wait(ce, ABMSG_VAR_SET, args, strlen(args) + 1);
        } else {
            send_err("SETVAR", "variable not found");
        }
    } else {
        send_err("SETVAR", "needs varname|value format");
    }
}

static void handle_exec(const char *args)
{
    /* Format: id|expression */
    /* Forward to all clients or specific client */
    const char *sep;
    ULONG cmdId;
    int i;

    sep = strchr(args, '|');
    if (!sep) {
        send_err("EXEC", "needs id|expression format");
        return;
    }

    cmdId = strtoul(args, NULL, 10);

    /* Check if expression starts with "client_name:" */
    {
        const char *expr = sep + 1;
        const char *colon = strchr(expr, ':');

        if (colon) {
            char cname[34];
            int nlen = (int)(colon - expr);
            struct ClientEntry *ce;

            if (nlen > 33) nlen = 33;
            strncpy(cname, expr, nlen);
            cname[nlen] = '\0';

            ce = client_find_by_name(cname);
            if (ce && ce->replyPort) {
                ipc_send_to_client(ce->replyPort, ABMSG_CMD_FORWARD,
                                   ce->clientId, cmdId,
                                   colon + 1, strlen(colon + 1) + 1);
                return;
            }
        }

        /* Forward to first active client as fallback */
        for (i = 0; i < AB_MAX_CLIENTS; i++) {
            struct ClientEntry *c = client_find((ULONG)(i + 1));
            if (c && c->replyPort) {
                ipc_send_to_client(c->replyPort, ABMSG_CMD_FORWARD,
                                   c->clientId, cmdId,
                                   expr, strlen(expr) + 1);
                return;
            }
        }

        protocol_send_cmd_response(cmdId, "ERR", "No clients registered");
    }
}

static void handle_listclients(void)
{
    protocol_send_clients();
}

static void handle_listtasks(void)
{
    protocol_send_tasks();
}

static void handle_listlibs(void)
{
    protocol_send_libs();
}

static void handle_listdevices(void)
{
    protocol_send_devices();
}

static void handle_listvolumes(void)
{
    static char buf[BRIDGE_MAX_LINE];
    sys_list_volumes(buf, BRIDGE_MAX_LINE);
    send_line(buf);
}

static void handle_listdir(const char *args)
{
    protocol_send_dir(args);
}

static void handle_readfile(const char *args)
{
    /* Format: path|offset|size */
    static char path[256];
    ULONG offset = 0;
    ULONG size = 256;
    const char *sep1;
    const char *sep2;

    sep1 = strchr(args, '|');
    if (sep1) {
        int plen = (int)(sep1 - args);
        if (plen > 255) plen = 255;
        strncpy(path, args, plen);
        path[plen] = '\0';
        offset = strtoul(sep1 + 1, NULL, 10);
        sep2 = strchr(sep1 + 1, '|');
        if (sep2) {
            size = strtoul(sep2 + 1, NULL, 10);
        }
    } else {
        strncpy(path, args, 255);
        path[255] = '\0';
    }

    protocol_send_file(path, offset, size);
}

static void handle_writefile(const char *args)
{
    /* Format: path|offset|hexdata */
    static char path[256];
    ULONG offset = 0;
    static UBYTE databuf[450];
    ULONG datalen = 0;
    const char *sep1;
    const char *sep2;
    int result;

    sep1 = strchr(args, '|');
    if (!sep1) {
        send_err("WRITEFILE", "needs path|offset|hexdata");
        return;
    }

    {
        int plen = (int)(sep1 - args);
        if (plen > 255) plen = 255;
        strncpy(path, args, plen);
        path[plen] = '\0';
    }

    offset = strtoul(sep1 + 1, NULL, 10);
    sep2 = strchr(sep1 + 1, '|');
    if (sep2) {
        const char *hex = sep2 + 1;
        ULONG hexlen = strlen(hex);
        ULONG i;

        datalen = hexlen / 2;
        if (datalen > 450) datalen = 450;

        for (i = 0; i < datalen; i++) {
            char hb[3];
            hb[0] = hex[i * 2];
            hb[1] = hex[i * 2 + 1];
            hb[2] = '\0';
            databuf[i] = (UBYTE)strtoul(hb, NULL, 16);
        }
    }

    result = fs_write_file(path, offset, databuf, datalen);
    if (result < 0) {
        send_err("WRITEFILE failed", path);
    } else {
        char detail[32];
        sprintf(detail, "%lu", (unsigned long)datalen);
        send_ok("WRITEFILE", detail);
    }
}

static void handle_fileinfo(const char *args)
{
    protocol_send_fileinfo(args);
}

static void handle_delete(const char *args)
{
    int result = fs_delete(args);
    if (result < 0) {
        send_err("DELETE failed", args);
    } else {
        send_ok("DELETE", args);
    }
}

static void handle_makedir(const char *args)
{
    int result = fs_makedir(args);
    if (result < 0) {
        send_err("MAKEDIR failed", args);
    } else {
        send_ok("MAKEDIR", args);
    }
}

static void handle_launch(const char *args)
{
    /* Format: id|command
     * Uses async launch to avoid blocking the bridge.
     * Validates executable path before launching. */
    ULONG cmdId;
    const char *sep;
    static char resultBuf[256];
    int result;

    sep = strchr(args, '|');
    if (!sep) {
        send_err("LAUNCH", "needs id|command format");
        return;
    }

    cmdId = strtoul(args, NULL, 10);
    result = proc_run_async(cmdId, sep + 1, resultBuf, 256);
    if (result < 0) {
        protocol_send_cmd_response(cmdId, "ERR", resultBuf);
    } else {
        protocol_send_cmd_response(cmdId, "OK", resultBuf);
    }
}

static void handle_run(const char *args)
{
    /* Format: id|command - launches asynchronously, doesn't wait */
    ULONG cmdId;
    const char *sep;
    static char resultBuf[256];
    int result;

    sep = strchr(args, '|');
    if (!sep) {
        send_err("RUN", "needs id|command format");
        return;
    }

    cmdId = strtoul(args, NULL, 10);
    result = proc_run_async(cmdId, sep + 1, resultBuf, 256);
    if (result < 0) {
        protocol_send_cmd_response(cmdId, "ERR", resultBuf);
    } else {
        protocol_send_cmd_response(cmdId, "OK", resultBuf);
    }
}

static void handle_break(const char *args)
{
    /* Format: task_name - sends CTRL-C to named task */
    int result;

    if (!args || args[0] == '\0') {
        send_err("BREAK", "needs task name");
        return;
    }

    result = sys_break_task(args);
    if (result == 0) {
        send_ok("BREAK", args);
    } else {
        send_err("Task not found", args);
    }
}

static void handle_listhooks(const char *args)
{
    /* Format: client_name (optional - if empty, list all)
     * Response: HOOKS|client|count|name1:desc1,name2:desc2,... */
    static char buf[BRIDGE_MAX_LINE];
    int pos;
    ULONG id;
    int cc = client_count();
    int found = 0;

    if (args && args[0] != '\0') {
        /* Specific client */
        struct ClientEntry *ce = client_find_by_name(args);
        if (!ce) {
            send_err("Client not found", args);
            return;
        }
        sprintf(buf, "HOOKS|%s|%ld|", ce->name, (long)ce->hookCount);
        pos = strlen(buf);
        {
            int i;
            for (i = 0; i < ce->hookCount && pos < BRIDGE_MAX_LINE - 80; i++) {
                if (i > 0) buf[pos++] = ',';
                sprintf(buf + pos, "%s:%s",
                        ce->hooks[i].name, ce->hooks[i].description);
                pos += strlen(buf + pos);
            }
        }
        buf[pos] = '\0';
        send_line(buf);
    } else {
        /* All clients */
        for (id = 1; id <= 100 && found < cc; id++) {
            struct ClientEntry *ce = client_find(id);
            if (ce) {
                int i;
                found++;
                sprintf(buf, "HOOKS|%s|%ld|", ce->name, (long)ce->hookCount);
                pos = strlen(buf);
                for (i = 0; i < ce->hookCount && pos < BRIDGE_MAX_LINE - 80; i++) {
                    if (i > 0) buf[pos++] = ',';
                    sprintf(buf + pos, "%s:%s",
                            ce->hooks[i].name, ce->hooks[i].description);
                    pos += strlen(buf + pos);
                }
                buf[pos] = '\0';
                send_line(buf);
            }
        }
        if (found == 0) {
            send_line("HOOKS||0|");
        }
    }
}

static void handle_callhook(const char *args)
{
    /* Format: id|client_name|hook_name|args_string
     * Forwards to client via IPC, client calls hook fn and replies.
     * Uses a dedicated send-and-wait to capture the reply data. */
    const char *sep1;
    const char *sep2;
    ULONG cmdId;
    char cname[34];
    char payload[AB_MAX_DATA];
    struct ClientEntry *ce;
    struct BridgeMsg *bm;
    struct MsgPort *tempPort;

    if (!args || args[0] == '\0') {
        send_err("CALLHOOK", "needs id|client|hook|args");
        return;
    }

    cmdId = strtoul(args, NULL, 10);
    sep1 = strchr(args, '|');
    if (!sep1) {
        send_err("CALLHOOK", "needs id|client|hook|args");
        return;
    }

    sep2 = strchr(sep1 + 1, '|');
    {
        const char *cstart = sep1 + 1;
        int nlen = sep2 ? (int)(sep2 - cstart) : (int)strlen(cstart);
        if (nlen > 33) nlen = 33;
        strncpy(cname, cstart, nlen);
        cname[nlen] = '\0';
    }

    ce = client_find_by_name(cname);
    if (!ce || !ce->replyPort) {
        protocol_send_cmd_response(cmdId, "ERR", "Client not found");
        return;
    }

    /* payload = "hookname|args" */
    if (sep2) {
        strncpy(payload, sep2 + 1, AB_MAX_DATA - 1);
    } else {
        payload[0] = '\0';
    }
    payload[AB_MAX_DATA - 1] = '\0';

    /* Allocate temp port and message for direct send/wait */
    tempPort = CreateMsgPort();
    if (!tempPort) {
        protocol_send_cmd_response(cmdId, "ERR", "No resources");
        return;
    }

    bm = (struct BridgeMsg *)AllocMem(sizeof(struct BridgeMsg), MEMF_PUBLIC | MEMF_CLEAR);
    if (!bm) {
        DeleteMsgPort(tempPort);
        protocol_send_cmd_response(cmdId, "ERR", "No memory");
        return;
    }

    bm->msg.mn_ReplyPort = tempPort;
    bm->msg.mn_Length = sizeof(struct BridgeMsg);
    bm->version = 1;
    bm->type = ABMSG_HOOK_CALL;
    bm->clientId = ce->clientId;
    bm->cmdId = cmdId;
    bm->result = 0;
    strncpy(bm->data, payload, AB_MAX_DATA - 1);
    bm->data[AB_MAX_DATA - 1] = '\0';
    bm->dataLen = strlen(bm->data) + 1;

    {
        char dbg[200];
        sprintf(dbg, "LOG|I|0|[HOOK] Sending to port %lx payload='%s'",
                (unsigned long)ce->replyPort, payload);
        send_line(dbg);
    }
    PutMsg(ce->replyPort, (struct Message *)bm);

    /* Wait for reply with timeout.
     * IMPORTANT: Process IPC messages while waiting! The hook function
     * may call ab_log/ab_push_var which sends messages to the daemon.
     * If we don't process those, we deadlock: client waits for daemon
     * to reply to LOG, daemon waits for client to reply to HOOK. */
    {
        struct Message *reply = NULL;
        int retries = 150; /* ~15 seconds (for slow hooks like disk I/O) */

        while (retries > 0) {
            reply = GetMsg(tempPort);
            if (reply) break;
            /* Process any pending IPC messages (LOG, VAR_PUSH, etc)
             * to avoid deadlock with hook functions that send messages */
            ipc_process();
            Delay(5);
            retries--;
        }

        if (reply) {
            /* Client replied with result in bm->data: "ok|result" or "err|msg" */
            char *rsep = strchr(bm->data, '|');
            if (rsep) {
                char status[8];
                int slen = (int)(rsep - bm->data);
                if (slen > 7) slen = 7;
                strncpy(status, bm->data, slen);
                status[slen] = '\0';
                protocol_send_cmd_response(cmdId, status, rsep + 1);
            } else {
                protocol_send_cmd_response(cmdId, "OK", bm->data);
            }
        } else {
            protocol_send_cmd_response(cmdId, "ERR", "Hook call timed out");
        }

        if (reply) {
            /* Safe to free — message was replied and is back in our possession */
            FreeMem(bm, sizeof(struct BridgeMsg));
            DeleteMsgPort(tempPort);
        }
        /* On timeout: bm AND tempPort may still be referenced by the client.
         * Leak both to avoid use-after-free crash when client eventually
         * calls ReplyMsg(). Small leak is better than a guru meditation. */
    }
}

static void handle_listmemregs(const char *args)
{
    /* Format: client_name (optional)
     * Response: MEMREGS|client|count|name1:addr:size:desc,...  */
    static char buf[BRIDGE_MAX_LINE];
    int pos;
    ULONG id;
    int cc = client_count();
    int found = 0;

    if (args && args[0] != '\0') {
        struct ClientEntry *ce = client_find_by_name(args);
        if (!ce) {
            send_err("Client not found", args);
            return;
        }
        sprintf(buf, "MEMREGS|%s|%ld|", ce->name, (long)ce->memregCount);
        pos = strlen(buf);
        {
            int i;
            for (i = 0; i < ce->memregCount && pos < BRIDGE_MAX_LINE - 120; i++) {
                if (i > 0) buf[pos++] = ',';
                sprintf(buf + pos, "%s:%08lx:%lu:%s",
                        ce->memregs[i].name,
                        (unsigned long)ce->memregs[i].addr,
                        (unsigned long)ce->memregs[i].size,
                        ce->memregs[i].description);
                pos += strlen(buf + pos);
            }
        }
        buf[pos] = '\0';
        send_line(buf);
    } else {
        for (id = 1; id <= 100 && found < cc; id++) {
            struct ClientEntry *ce = client_find(id);
            if (ce) {
                int i;
                found++;
                sprintf(buf, "MEMREGS|%s|%ld|", ce->name, (long)ce->memregCount);
                pos = strlen(buf);
                for (i = 0; i < ce->memregCount && pos < BRIDGE_MAX_LINE - 120; i++) {
                    if (i > 0) buf[pos++] = ',';
                    sprintf(buf + pos, "%s:%08lx:%lu:%s",
                            ce->memregs[i].name,
                            (unsigned long)ce->memregs[i].addr,
                            (unsigned long)ce->memregs[i].size,
                            ce->memregs[i].description);
                    pos += strlen(buf + pos);
                }
                buf[pos] = '\0';
                send_line(buf);
            }
        }
        if (found == 0) {
            send_line("MEMREGS||0|");
        }
    }
}

static void handle_readmemreg(const char *args)
{
    /* Format: client_name|region_name
     * Reads memory at the registered region's address. */
    const char *sep;
    char cname[34];
    const char *regname;
    struct ClientEntry *ce;
    int i;

    if (!args || args[0] == '\0') {
        send_err("READMEMREG", "needs client|region");
        return;
    }

    sep = strchr(args, '|');
    if (!sep) {
        send_err("READMEMREG", "needs client|region");
        return;
    }

    {
        int nlen = (int)(sep - args);
        if (nlen > 33) nlen = 33;
        strncpy(cname, args, nlen);
        cname[nlen] = '\0';
    }
    regname = sep + 1;

    ce = client_find_by_name(cname);
    if (!ce) {
        send_err("Client not found", cname);
        return;
    }

    /* Find the memory region in client's registry */
    for (i = 0; i < ce->memregCount; i++) {
        if (strcmp(ce->memregs[i].name, regname) == 0) {
            /* Read memory at this region */
            static UBYTE membuf[256];
            ULONG readSize = ce->memregs[i].size;
            if (readSize > 256) readSize = 256;

            {
                int actual = sys_inspect_mem((APTR)ce->memregs[i].addr,
                                             readSize, membuf, 256);
                if (actual > 0) {
                    protocol_send_mem((APTR)ce->memregs[i].addr,
                                      (ULONG)actual, membuf);
                } else {
                    send_err("READMEMREG", "memory not accessible");
                }
            }
            return;
        }
    }

    send_err("Region not found", regname);
}

static void handle_clientinfo(const char *args)
{
    /* Format: client_name
     * Response: CINFO|client|id|msgs|vars:v1,v2,...|hooks:h1,h2,...|memregs:m1,m2,...
     */
    struct ClientEntry *ce;
    static char buf[BRIDGE_MAX_LINE];
    int pos;
    int i;

    if (!args || args[0] == '\0') {
        send_err("CLIENTINFO", "needs client name");
        return;
    }

    ce = client_find_by_name(args);
    if (!ce) {
        send_err("Client not found", args);
        return;
    }

    sprintf(buf, "CINFO|%s|%lu|%lu|vars:",
            ce->name,
            (unsigned long)ce->clientId,
            (unsigned long)ce->msgCount);
    pos = strlen(buf);

    /* Append var names */
    for (i = 0; i < ce->varCount && pos < BRIDGE_MAX_LINE - 100; i++) {
        if (i > 0) buf[pos++] = ',';
        sprintf(buf + pos, "%s(%s)",
                ce->vars[i].name,
                (ce->vars[i].type >= 0 && ce->vars[i].type <= 4)
                    ? type_names[ce->vars[i].type] : "?");
        pos += strlen(buf + pos);
    }

    /* Append hooks */
    if (pos < BRIDGE_MAX_LINE - 20) {
        sprintf(buf + pos, "|hooks:");
        pos += strlen(buf + pos);
    }
    for (i = 0; i < ce->hookCount && pos < BRIDGE_MAX_LINE - 80; i++) {
        if (i > 0) buf[pos++] = ',';
        sprintf(buf + pos, "%s", ce->hooks[i].name);
        pos += strlen(buf + pos);
    }

    /* Append memregs */
    if (pos < BRIDGE_MAX_LINE - 20) {
        sprintf(buf + pos, "|memregs:");
        pos += strlen(buf + pos);
    }
    for (i = 0; i < ce->memregCount && pos < BRIDGE_MAX_LINE - 80; i++) {
        if (i > 0) buf[pos++] = ',';
        sprintf(buf + pos, "%s(%08lx,%lu)",
                ce->memregs[i].name,
                (unsigned long)ce->memregs[i].addr,
                (unsigned long)ce->memregs[i].size);
        pos += strlen(buf + pos);
    }

    buf[pos] = '\0';
    send_line(buf);
}

static void handle_stop(const char *args)
{
    /* Format: name_or_addr[|CTRLC|CTRLD|CTRLE|CTRLF]
     * If name starts with "0x", treat as hex task address.
     * Sends specified signal (default CTRL-C) to the task. */
    struct ClientEntry *ce;
    int result;
    static char namebuf[256];
    const char *sigArg = NULL;
    const char *sep;
    ULONG sigMask = SIGBREAKF_CTRL_C;

    if (!args || args[0] == '\0') {
        send_err("STOP", "needs client name or address");
        return;
    }

    /* Parse optional signal type */
    sep = strchr(args, '|');
    if (sep) {
        int nlen = (int)(sep - args);
        if (nlen > 255) nlen = 255;
        strncpy(namebuf, args, nlen);
        namebuf[nlen] = '\0';
        sigArg = sep + 1;
    } else {
        strncpy(namebuf, args, 255);
        namebuf[255] = '\0';
    }

    /* Determine signal mask from argument */
    if (sigArg) {
        if (strcmp(sigArg, "CTRLD") == 0) {
            sigMask = SIGBREAKF_CTRL_D;
        } else if (strcmp(sigArg, "CTRLE") == 0) {
            sigMask = SIGBREAKF_CTRL_E;
        } else if (strcmp(sigArg, "CTRLF") == 0) {
            sigMask = SIGBREAKF_CTRL_F;
        }
        /* CTRLC or anything else stays as default */
    }

    /* Check if name is a hex address (starts with "0x") */
    if (namebuf[0] == '0' && (namebuf[1] == 'x' || namebuf[1] == 'X')) {
        ULONG addr = strtoul(namebuf, NULL, 16);
        result = sys_signal_task_by_addr(addr, sigMask);
        if (result == 0) {
            send_ok("STOP", namebuf);
        } else {
            send_err("STOP", "address not found in task lists");
        }
        return;
    }

    ce = client_find_by_name(namebuf);
    if (!ce) {
        /* Try as a raw task name for non-bridge processes */
        /* For non-default signals, use FindTask + Signal directly */
        {
            struct Task *task;
            Forbid();
            task = FindTask((CONST_STRPTR)namebuf);
            if (task) {
                Signal(task, sigMask);
            }
            Permit();
            if (task) {
                send_ok("STOP", namebuf);
            } else {
                send_err("Client/task not found", namebuf);
            }
        }
        return;
    }

    /* Send signal to the client's task */
    {
        struct Task *task;
        Forbid();
        task = FindTask((CONST_STRPTR)ce->name);
        if (task) {
            Signal(task, sigMask);
        }
        Permit();

        if (task) {
            send_ok("STOP", ce->name);
        } else {
            /* Task not found by name - try sending SHUTDOWN via IPC */
            if (ce->replyPort) {
                ipc_send_to_client(ce->replyPort, ABMSG_SHUTDOWN,
                                   ce->clientId, 0, NULL, 0);
                send_ok("STOP", "shutdown sent");
            } else {
                send_err("STOP", "cannot reach client");
            }
        }
    }
}

static void handle_script(const char *args)
{
    /* Format: id|script_text
     * Writes script to T:ab_script_<id>, makes it executable,
     * runs it via proc_launch, captures output. */
    ULONG cmdId;
    const char *sep;
    char scriptPath[64];
    static char resultBuf[512];
    int result;
    BPTR fh;

    if (!args || args[0] == '\0') {
        send_err("SCRIPT", "needs id|script_text");
        return;
    }

    sep = strchr(args, '|');
    if (!sep) {
        send_err("SCRIPT", "needs id|script_text");
        return;
    }

    cmdId = strtoul(args, NULL, 10);

    /* Write script to temp file */
    sprintf(scriptPath, "T:ab_script_%lu", (unsigned long)cmdId);
    fh = Open((CONST_STRPTR)scriptPath, MODE_NEWFILE);
    if (!fh) {
        protocol_send_cmd_response(cmdId, "ERR", "Cannot create script file");
        return;
    }

    /* Write script content - convert semicolons back to newlines */
    {
        const char *src = sep + 1;
        int len = strlen(src);
        /* Use stack buffer for small scripts, or just write directly */
        if (len < 480) {
            static char tmpBuf[480];
            int i;
            memcpy(tmpBuf, src, len);
            for (i = 0; i < len; i++) {
                if (tmpBuf[i] == ';') tmpBuf[i] = '\n';
            }
            /* Ensure trailing newline */
            if (len > 0 && tmpBuf[len - 1] != '\n') {
                tmpBuf[len] = '\n';
                len++;
            }
            Write(fh, (APTR)tmpBuf, (LONG)len);
        } else {
            Write(fh, (APTR)src, (LONG)len);
        }
    }
    Close(fh);

    /* Run the script via Execute or proc_launch */
    {
        char runCmd[80];
        sprintf(runCmd, "Execute %s", scriptPath);
        result = proc_launch(cmdId, runCmd, resultBuf, 512);
    }

    /* Clean up script file */
    DeleteFile((CONST_STRPTR)scriptPath);

    if (result < 0) {
        protocol_send_cmd_response(cmdId, "ERR", "Script execution failed");
    } else {
        protocol_send_cmd_response(cmdId, "OK", resultBuf);
    }
}

static void handle_writemem(const char *args)
{
    /* Format: addr_hex|hexdata
     * Writes binary data to the specified memory address.
     * WARNING: No protection - can crash if writing to wrong address. */
    const char *sep;
    ULONG addr;
    static UBYTE databuf[256];
    ULONG datalen;

    if (!args || args[0] == '\0') {
        send_err("WRITEMEM", "needs addr|hexdata");
        return;
    }

    sep = strchr(args, '|');
    if (!sep) {
        send_err("WRITEMEM", "needs addr|hexdata");
        return;
    }

    addr = strtoul(args, NULL, 16);

    /* Reject only NULL and I/O/ROM ranges */
    if (addr < 4) {
        send_err("WRITEMEM", "address too low");
        return;
    }
    if (addr >= 0xBF0000 && addr < 0xC00000) {
        send_err("WRITEMEM", "CIA registers - use caution");
        return;
    }
    if (addr >= 0xDFF000 && addr < 0xE00000) {
        send_err("WRITEMEM", "custom chip registers - use caution");
        return;
    }
    if (addr >= 0xF80000) {
        send_err("WRITEMEM", "ROM is read-only");
        return;
    }

    /* Decode hex data */
    {
        const char *hex = sep + 1;
        ULONG hexlen = strlen(hex);
        ULONG i;

        datalen = hexlen / 2;
        if (datalen > 256) datalen = 256;

        for (i = 0; i < datalen; i++) {
            char hb[3];
            hb[0] = hex[i * 2];
            hb[1] = hex[i * 2 + 1];
            hb[2] = '\0';
            databuf[i] = (UBYTE)strtoul(hb, NULL, 16);
        }
    }

    /* Write to memory */
    CopyMem(databuf, (APTR)addr, datalen);

    {
        char detail[32];
        sprintf(detail, "%08lx|%lu", (unsigned long)addr,
                (unsigned long)datalen);
        send_ok("WRITEMEM", detail);
    }
}

/*
 * LISTRESOURCES|clientname
 * Request resource tracking data from a client.
 * Response: RESOURCES|client|count|type:tag:ptr:size:state,...
 */
static void handle_listresources(const char *args)
{
    struct ClientEntry *ce;
    const char *clientName = args;

    if (!clientName || clientName[0] == '\0') {
        send_err("LISTRESOURCES", "client name required");
        return;
    }

    ce = client_find_by_name(clientName);
    if (!ce) {
        send_err("LISTRESOURCES", "client not found");
        return;
    }

    /* Send request to client */
    ipc_send_to_client(ce->replyPort, ABMSG_GET_RESOURCES,
                       ce->clientId, 0, NULL, 0);

    /* The client will reply with resource data in the BridgeMsg.
     * ipc_send_to_client waits for reply, so after it returns
     * we need to read the reply data. However, the current
     * ipc_send_to_client doesn't return the reply data.
     * Use a different approach: send msg and get reply. */

    /* For now, use a direct approach with a temp port */
    {
        struct MsgPort *tempPort;
        struct BridgeMsg sendMsg;
        struct Message *reply;
        int retries;

        tempPort = CreateMsgPort();
        if (!tempPort) {
            send_err("LISTRESOURCES", "cannot create port");
            return;
        }

        memset(&sendMsg, 0, sizeof(sendMsg));
        sendMsg.msg.mn_Length = sizeof(struct BridgeMsg);
        sendMsg.msg.mn_ReplyPort = tempPort;
        sendMsg.version = 1;
        sendMsg.type = ABMSG_GET_RESOURCES;
        sendMsg.clientId = ce->clientId;

        PutMsg(ce->replyPort, (struct Message *)&sendMsg);

        /* Wait for reply with timeout */
        reply = NULL;
        retries = 20;
        while (retries > 0) {
            reply = GetMsg(tempPort);
            if (reply) break;
            if (SetSignal(0L, 0L) & (1UL << tempPort->mp_SigBit)) {
                reply = GetMsg(tempPort);
                if (reply) break;
            }
            Delay(5);
            retries--;
        }

        if (reply) {
            struct BridgeMsg *rbm = (struct BridgeMsg *)reply;
            static char resbuf[BRIDGE_MAX_LINE];
            int pos;

            /* Format: RESOURCES|client|data_from_client */
            pos = sprintf(resbuf, "RESOURCES|%s|%s",
                          ce->name, rbm->data);
            resbuf[BRIDGE_MAX_LINE - 1] = '\0';
            send_line(resbuf);
        } else {
            send_err("LISTRESOURCES", "client timeout");
        }

        DeleteMsgPort(tempPort);
    }
}

/*
 * GETPERF|clientname
 * Request performance profiling data from a client.
 * Response: PERF|client|frame_avg|frame_min|frame_max|frame_count|section1:avg:min:max:count,...
 */
static void handle_getperf(const char *args)
{
    struct ClientEntry *ce;
    const char *clientName = args;

    if (!clientName || clientName[0] == '\0') {
        send_err("GETPERF", "client name required");
        return;
    }

    ce = client_find_by_name(clientName);
    if (!ce) {
        send_err("GETPERF", "client not found");
        return;
    }

    {
        struct MsgPort *tempPort;
        struct BridgeMsg sendMsg;
        struct Message *reply;
        int retries;

        tempPort = CreateMsgPort();
        if (!tempPort) {
            send_err("GETPERF", "cannot create port");
            return;
        }

        memset(&sendMsg, 0, sizeof(sendMsg));
        sendMsg.msg.mn_Length = sizeof(struct BridgeMsg);
        sendMsg.msg.mn_ReplyPort = tempPort;
        sendMsg.version = 1;
        sendMsg.type = ABMSG_GET_PERF;
        sendMsg.clientId = ce->clientId;

        PutMsg(ce->replyPort, (struct Message *)&sendMsg);

        reply = NULL;
        retries = 20;
        while (retries > 0) {
            reply = GetMsg(tempPort);
            if (reply) break;
            if (SetSignal(0L, 0L) & (1UL << tempPort->mp_SigBit)) {
                reply = GetMsg(tempPort);
                if (reply) break;
            }
            Delay(5);
            retries--;
        }

        if (reply) {
            struct BridgeMsg *rbm = (struct BridgeMsg *)reply;
            static char perfbuf[BRIDGE_MAX_LINE];

            /* Format: PERF|client|data_from_client
             * Client data format: frame_avg|frame_min|frame_max|frame_count|sections... */
            sprintf(perfbuf, "PERF|%s|%s", ce->name, rbm->data);
            perfbuf[BRIDGE_MAX_LINE - 1] = '\0';
            send_line(perfbuf);
        } else {
            send_err("GETPERF", "client timeout");
        }

        DeleteMsgPort(tempPort);
    }
}

/*
 * LASTCRASH
 * Returns the last crash/guru meditation info captured by the crash handler.
 * Response: CRASH|alert_hex|alert_name|D0:...:D7|A0:...:A7|SP|stack_hex
 *       or: ERR|LASTCRASH|no crash data
 */
static void handle_lastcrash(void)
{
    static char buf[BRIDGE_MAX_LINE];

    if (crash_get_last(buf, BRIDGE_MAX_LINE) == 0) {
        send_line(buf);
    } else {
        send_err("LASTCRASH", "no crash data");
    }
}

/* ---- New command handlers ---- */

static void handle_capabilities(void)
{
    sys_handle_capabilities();
}

static void handle_proclist(void)
{
    static char buf[BRIDGE_MAX_LINE];
    proc_list(buf, BRIDGE_MAX_LINE);
    send_line(buf);
}

static void handle_procstat(const char *args)
{
    static char buf[BRIDGE_MAX_LINE];
    int procId;

    if (!args || args[0] == '\0') {
        send_err("PROCSTAT", "needs process id");
        return;
    }
    procId = (int)strtol(args, NULL, 10);
    if (proc_stat(procId, buf, BRIDGE_MAX_LINE) < 0) {
        send_line(buf); /* buf already has ERR message */
    } else {
        send_line(buf);
    }
}

static void handle_signal(const char *args)
{
    /* Format: procId|sigType (0=CTRL_C, 1=CTRL_D, 2=CTRL_E, 3=CTRL_F) */
    const char *sep;
    int procId;
    ULONG sigType;

    if (!args || args[0] == '\0') {
        send_err("SIGNAL", "needs procId|sigType");
        return;
    }
    sep = strchr(args, '|');
    if (!sep) {
        send_err("SIGNAL", "needs procId|sigType");
        return;
    }
    procId = (int)strtol(args, NULL, 10);
    sigType = strtoul(sep + 1, NULL, 10);

    if (proc_signal(procId, sigType) == 0) {
        static char detail[64];
        sprintf(detail, "Signal %lu sent to proc %ld", (unsigned long)sigType, (long)procId);
        send_ok("SIGNAL", detail);
    } else {
        send_err("SIGNAL", "process not found or signal failed");
    }
}

/* Tail file streaming state */
BOOL g_tail_active = FALSE;
char g_tail_path[256] = "";
ULONG g_tail_pos = 0;

static void handle_tail(const char *args)
{
    /* Format: path
     * Start streaming file appends. Uses polling from main loop. */
    BPTR fh;
    struct Process *pr;
    APTR oldWinPtr;

    if (!args || args[0] == '\0') {
        send_err("TAIL", "needs file path");
        return;
    }
    if (g_tail_active) {
        send_err("TAIL", "already tailing a file - send STOPTAIL first");
        return;
    }

    pr = (struct Process *)FindTask(NULL);
    oldWinPtr = pr->pr_WindowPtr;
    pr->pr_WindowPtr = (APTR)-1;

    /* Get initial file size */
    fh = Open((CONST_STRPTR)args, MODE_OLDFILE);
    pr->pr_WindowPtr = oldWinPtr;

    if (!fh) {
        send_err("TAIL", "cannot open file");
        return;
    }
    Seek(fh, 0, OFFSET_END);
    g_tail_pos = (ULONG)Seek(fh, 0, OFFSET_CURRENT);
    Close(fh);

    strncpy(g_tail_path, args, 255);
    g_tail_path[255] = '\0';
    g_tail_active = TRUE;

    send_ok("TAIL", args);
}

static void handle_stoptail(void)
{
    if (!g_tail_active) {
        send_ok("STOPTAIL", "not active");
        return;
    }
    g_tail_active = FALSE;
    g_tail_path[0] = '\0';
    send_ok("STOPTAIL", "stopped");
}

/*
 * Poll for tail data. Called from main loop.
 * Reads new data appended since last check, sends as TAILDATA|path|hexdata.
 */
void tail_poll(void)
{
    BPTR fh;
    static UBYTE tailBuf[256];
    static char hexBuf[520];
    static char lineBuf[BRIDGE_MAX_LINE];
    LONG bytesRead;
    ULONG curSize;
    struct Process *pr;
    APTR oldWinPtr;
    ULONG i;

    if (!g_tail_active) return;

    pr = (struct Process *)FindTask(NULL);
    oldWinPtr = pr->pr_WindowPtr;
    pr->pr_WindowPtr = (APTR)-1;

    fh = Open((CONST_STRPTR)g_tail_path, MODE_OLDFILE);
    pr->pr_WindowPtr = oldWinPtr;

    if (!fh) {
        g_tail_active = FALSE;
        return;
    }

    /* Check file size */
    Seek(fh, 0, OFFSET_END);
    curSize = (ULONG)Seek(fh, 0, OFFSET_CURRENT);

    if (curSize < g_tail_pos) {
        /* File was truncated - reset to beginning */
        g_tail_pos = 0;
        sprintf(lineBuf, "TAILDATA|%s|TRUNCATED", g_tail_path);
        send_line(lineBuf);
    }

    if (curSize > g_tail_pos) {
        ULONG toRead = curSize - g_tail_pos;
        if (toRead > 256) toRead = 256;

        Seek(fh, (LONG)g_tail_pos, OFFSET_BEGINNING);
        bytesRead = Read(fh, tailBuf, (LONG)toRead);

        if (bytesRead > 0) {
            for (i = 0; i < (ULONG)bytesRead; i++) {
                sprintf(hexBuf + i * 2, "%02lx", (unsigned long)tailBuf[i]);
            }
            hexBuf[bytesRead * 2] = '\0';

            sprintf(lineBuf, "TAILDATA|%s|%s", g_tail_path, hexBuf);
            send_line(lineBuf);

            g_tail_pos += (ULONG)bytesRead;
        }
    }

    Close(fh);
}

static void handle_checksum(const char *args)
{
    ULONG crc32, fileSize;

    if (!args || args[0] == '\0') {
        send_err("CHECKSUM", "needs file path");
        return;
    }

    if (fs_checksum(args, &crc32, &fileSize) == 0) {
        static char buf[256];
        sprintf(buf, "CHECKSUM|%s|%08lx|%lu",
                args, (unsigned long)crc32, (unsigned long)fileSize);
        send_line(buf);
    } else {
        send_err("CHECKSUM", args);
    }
}

static void handle_assigns(void)
{
    static char buf[BRIDGE_MAX_LINE];
    sys_list_assigns(buf, BRIDGE_MAX_LINE);
    send_line(buf);
}

static void handle_assign(const char *args)
{
    sys_handle_assign(args);
}

static void handle_protect(const char *args)
{
    /* Format: path[|bits_hex]
     * If bits_hex present: SET mode. Otherwise: GET mode.
     * Response: PROTECT|path|bits_hex */
    const char *sep;
    static char path[256];
    ULONG bits;

    if (!args || args[0] == '\0') {
        send_err("PROTECT", "needs path[|bits_hex]");
        return;
    }

    sep = strchr(args, '|');
    if (sep) {
        /* SET mode */
        int plen = (int)(sep - args);
        if (plen > 255) plen = 255;
        strncpy(path, args, plen);
        path[plen] = '\0';
        bits = strtoul(sep + 1, NULL, 16);

        if (fs_protect(path, &bits, 1) == 0) {
            static char buf[300];
            sprintf(buf, "PROTECT|%s|%08lx", path, (unsigned long)bits);
            send_line(buf);
        } else {
            send_err("PROTECT", "failed to set");
        }
    } else {
        /* GET mode */
        strncpy(path, args, 255);
        path[255] = '\0';

        if (fs_protect(path, &bits, 0) == 0) {
            static char buf[300];
            sprintf(buf, "PROTECT|%s|%08lx", path, (unsigned long)bits);
            send_line(buf);
        } else {
            send_err("PROTECT", "failed to read");
        }
    }
}

static void handle_rename(const char *args)
{
    /* Format: oldpath|newpath */
    const char *sep;
    static char oldPath[256], newPath[256];

    if (!args || args[0] == '\0') {
        send_err("RENAME", "needs oldpath|newpath");
        return;
    }
    sep = strchr(args, '|');
    if (!sep) {
        send_err("RENAME", "needs oldpath|newpath");
        return;
    }
    {
        int olen = (int)(sep - args);
        if (olen > 255) olen = 255;
        strncpy(oldPath, args, olen);
        oldPath[olen] = '\0';
    }
    strncpy(newPath, sep + 1, 255);
    newPath[255] = '\0';

    if (fs_rename(oldPath, newPath) == 0) {
        send_ok("RENAME", newPath);
    } else {
        send_err("RENAME", "failed");
    }
}

static void handle_setcomment(const char *args)
{
    /* Format: path|comment */
    const char *sep;
    static char path[256];

    if (!args || args[0] == '\0') {
        send_err("SETCOMMENT", "needs path|comment");
        return;
    }
    sep = strchr(args, '|');
    if (!sep) {
        send_err("SETCOMMENT", "needs path|comment");
        return;
    }
    {
        int plen = (int)(sep - args);
        if (plen > 255) plen = 255;
        strncpy(path, args, plen);
        path[plen] = '\0';
    }

    if (fs_set_comment(path, sep + 1) == 0) {
        send_ok("SETCOMMENT", path);
    } else {
        send_err("SETCOMMENT", "failed");
    }
}

static void handle_copy(const char *args)
{
    /* Format: srcpath|dstpath */
    const char *sep;
    static char srcPath[256], dstPath[256];

    if (!args || args[0] == '\0') {
        send_err("COPY", "needs srcpath|dstpath");
        return;
    }
    sep = strchr(args, '|');
    if (!sep) {
        send_err("COPY", "needs srcpath|dstpath");
        return;
    }
    {
        int slen = (int)(sep - args);
        if (slen > 255) slen = 255;
        strncpy(srcPath, args, slen);
        srcPath[slen] = '\0';
    }
    strncpy(dstPath, sep + 1, 255);
    dstPath[255] = '\0';

    if (fs_copy(srcPath, dstPath) == 0) {
        send_ok("COPY", dstPath);
    } else {
        send_err("COPY", "failed");
    }
}

static void handle_append(const char *args)
{
    /* Format: path|hexdata */
    const char *sep;
    static char path[256];
    static UBYTE databuf[450];
    ULONG datalen = 0;

    if (!args || args[0] == '\0') {
        send_err("APPEND", "needs path|hexdata");
        return;
    }
    sep = strchr(args, '|');
    if (!sep) {
        send_err("APPEND", "needs path|hexdata");
        return;
    }
    {
        int plen = (int)(sep - args);
        if (plen > 255) plen = 255;
        strncpy(path, args, plen);
        path[plen] = '\0';
    }
    {
        const char *hex = sep + 1;
        ULONG hexlen = strlen(hex);
        ULONG i;

        datalen = hexlen / 2;
        if (datalen > 450) datalen = 450;

        for (i = 0; i < datalen; i++) {
            char hb[3];
            hb[0] = hex[i * 2];
            hb[1] = hex[i * 2 + 1];
            hb[2] = '\0';
            databuf[i] = (UBYTE)strtoul(hb, NULL, 16);
        }
    }

    if (fs_append(path, databuf, datalen) == 0) {
        static char detail[32];
        sprintf(detail, "%lu", (unsigned long)datalen);
        send_ok("APPEND", detail);
    } else {
        send_err("APPEND", "failed");
    }
}

static void handle_version(void)
{
    static char buf[128];
    sprintf(buf, "VERSION|AmigaBridge|%ld|%ld|%s",
        (long)BRIDGE_VERSION_MAJOR,
        (long)BRIDGE_VERSION_MINOR,
        __DATE__);
    send_line(buf);
}

static void handle_getenv(const char *args)
{
    /* Format: name[|1] - if |1, read from ENVARC: */
    static char namebuf[256];
    static char valbuf[512];
    const char *sep;
    int archive = 0;

    if (!args || args[0] == '\0') {
        send_err("GETENV", "needs variable name");
        return;
    }

    sep = strchr(args, '|');
    if (sep) {
        int nlen = (int)(sep - args);
        if (nlen > 255) nlen = 255;
        strncpy(namebuf, args, nlen);
        namebuf[nlen] = '\0';
        if (*(sep + 1) == '1') archive = 1;
    } else {
        strncpy(namebuf, args, 255);
        namebuf[255] = '\0';
    }

    if (fs_get_env(namebuf, archive, valbuf, sizeof(valbuf)) == 0) {
        static char linebuf[BRIDGE_MAX_LINE];
        sprintf(linebuf, "ENV|%s|", namebuf);
        strncat(linebuf, valbuf, BRIDGE_MAX_LINE - strlen(linebuf) - 1);
        linebuf[BRIDGE_MAX_LINE - 1] = '\0';
        send_line(linebuf);
    } else {
        send_err("GETENV", namebuf);
    }
}

static void handle_setenv(const char *args)
{
    /* Format: name|value[|1] - if |1, also archive to ENVARC: */
    static char namebuf[256];
    static char valbuf[512];
    const char *sep1;
    const char *sep2;
    int archive = 0;

    if (!args || args[0] == '\0') {
        send_err("SETENV", "needs name|value");
        return;
    }

    sep1 = strchr(args, '|');
    if (!sep1) {
        send_err("SETENV", "needs name|value");
        return;
    }

    {
        int nlen = (int)(sep1 - args);
        if (nlen > 255) nlen = 255;
        strncpy(namebuf, args, nlen);
        namebuf[nlen] = '\0';
    }

    sep2 = strchr(sep1 + 1, '|');
    if (sep2) {
        int vlen = (int)(sep2 - (sep1 + 1));
        if (vlen > 511) vlen = 511;
        strncpy(valbuf, sep1 + 1, vlen);
        valbuf[vlen] = '\0';
        if (*(sep2 + 1) == '1') archive = 1;
    } else {
        strncpy(valbuf, sep1 + 1, 511);
        valbuf[511] = '\0';
    }

    if (fs_set_env(namebuf, valbuf, archive) == 0) {
        send_ok("SETENV", namebuf);
    } else {
        send_err("SETENV", namebuf);
    }
}

static void handle_setdate(const char *args)
{
    /* Format: path|days|mins|ticks */
    static char path[256];
    LONG days, mins, ticks;
    const char *sep1, *sep2, *sep3;

    if (!args || args[0] == '\0') {
        send_err("SETDATE", "needs path|days|mins|ticks");
        return;
    }

    sep1 = strchr(args, '|');
    if (!sep1) {
        send_err("SETDATE", "needs path|days|mins|ticks");
        return;
    }

    {
        int plen = (int)(sep1 - args);
        if (plen > 255) plen = 255;
        strncpy(path, args, plen);
        path[plen] = '\0';
    }

    days = strtol(sep1 + 1, NULL, 10);
    sep2 = strchr(sep1 + 1, '|');
    if (!sep2) {
        send_err("SETDATE", "needs days|mins|ticks");
        return;
    }
    mins = strtol(sep2 + 1, NULL, 10);
    sep3 = strchr(sep2 + 1, '|');
    if (!sep3) {
        send_err("SETDATE", "needs mins|ticks");
        return;
    }
    ticks = strtol(sep3 + 1, NULL, 10);

    if (fs_set_date(path, days, mins, ticks) == 0) {
        send_ok("SETDATE", path);
    } else {
        send_err("SETDATE", path);
    }
}

static void handle_volumes_ext(void)
{
    sys_handle_volumes_ext();
}

static void handle_ports(void)
{
    sys_handle_ports();
}

static void handle_sysinfo(void)
{
    sys_handle_sysinfo();
}

static void handle_uptime(void)
{
    sys_handle_uptime();
}

static void handle_reboot(void)
{
    send_ok("REBOOT", NULL);
    /* Small delay to ensure the response is transmitted */
    Delay(10);
    ColdReboot();
}
