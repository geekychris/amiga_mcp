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

ULONG g_tx_count = 0;
ULONG g_rx_count = 0;

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
    } else if (strcmp(cmd, "SHUTDOWN") == 0) {
        send_ok("SHUTDOWN", NULL);
        /* The main loop will exit via CTRL-C or window close */
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
    static UBYTE filebuf[256];
    ULONG actual = 0;
    int result;

    if (size > 256) size = 256;

    result = fs_read_file(path, offset, size, filebuf, 256, &actual);
    if (result < 0) {
        send_err("READFILE failed", path);
    } else {
        static char buf[BRIDGE_MAX_LINE];
        static char hexbuf[514];
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
        Delay(5);
        retries--;
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
    static UBYTE databuf[256];
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
        if (datalen > 256) datalen = 256;

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

    PutMsg(ce->replyPort, (struct Message *)bm);

    /* Wait for reply with timeout */
    {
        struct Message *reply = NULL;
        int retries = 30; /* ~3 seconds */

        while (retries > 0) {
            reply = GetMsg(tempPort);
            if (reply) break;
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
    }

    FreeMem(bm, sizeof(struct BridgeMsg));
    DeleteMsgPort(tempPort);
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
    /* Format: client_name
     * Sends CTRL-C to the client's task, then waits briefly
     * and checks if it unregistered. */
    struct ClientEntry *ce;
    int result;

    if (!args || args[0] == '\0') {
        send_err("STOP", "needs client name");
        return;
    }

    ce = client_find_by_name(args);
    if (!ce) {
        /* Try as a raw task name for non-bridge processes */
        result = sys_break_task(args);
        if (result == 0) {
            send_ok("STOP", args);
        } else {
            send_err("Client/task not found", args);
        }
        return;
    }

    /* Send CTRL-C to the client's task */
    result = sys_break_task(ce->name);
    if (result == 0) {
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
