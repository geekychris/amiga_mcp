/*
 * protocol_handler.c - Serial protocol parsing and formatting
 *
 * Handles the line-based pipe-delimited protocol between
 * the daemon and the MCP host.
 */

#include <exec/types.h>
#include <exec/memory.h>
#include <proto/exec.h>

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

static void send_line(const char *line)
{
    /* Combine data + newline into a single write to avoid
     * FS-UAE serial buffer issues with split writes */
    char sendbuf[BRIDGE_MAX_LINE + 2];
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
    char buf[BRIDGE_MAX_LINE];
    int clen = strlen(context);
    int dlen = detail ? strlen(detail) : 0;

    /* "ERR|context: detail" - truncate detail if needed */
    if (4 + clen + 2 + dlen >= BRIDGE_MAX_LINE) {
        dlen = BRIDGE_MAX_LINE - 4 - clen - 3;
        if (dlen < 0) dlen = 0;
    }

    strcpy(buf, "ERR|");
    strcat(buf, context);
    if (detail && dlen > 0) {
        strcat(buf, ": ");
        strncat(buf, detail, dlen);
    }
    send_line(buf);
}

/* Send an OK response, safely truncating */
static void send_ok(const char *context, const char *detail)
{
    char buf[BRIDGE_MAX_LINE];
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
    char buf[BRIDGE_MAX_LINE];
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
    char buf[BRIDGE_MAX_LINE];
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
    char buf[BRIDGE_MAX_LINE];
    sprintf(buf, "HB|%lu|%lu|%lu",
            (unsigned long)tick,
            (unsigned long)chipFree,
            (unsigned long)fastFree);
    send_line(buf);
}

void protocol_send_mem(APTR addr, ULONG size, const UBYTE *data)
{
    char buf[BRIDGE_MAX_LINE];
    char hexbuf[514]; /* 256 bytes * 2 hex chars + nul */
    ULONG offset = 0;

    while (offset < size) {
        ULONG chunk = size - offset;
        ULONG i;
        if (chunk > 256) chunk = 256;

        for (i = 0; i < chunk; i++) {
            sprintf(hexbuf + i * 2, "%02x", data[offset + i]);
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
    char buf[BRIDGE_MAX_LINE];
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
    char buf[BRIDGE_MAX_LINE];
    sys_list_tasks(buf, BRIDGE_MAX_LINE);
    send_line(buf);
}

void protocol_send_libs(void)
{
    char buf[BRIDGE_MAX_LINE];
    sys_list_libs(buf, BRIDGE_MAX_LINE);
    send_line(buf);
}

void protocol_send_devices(void)
{
    char buf[BRIDGE_MAX_LINE];
    sys_list_devices(buf, BRIDGE_MAX_LINE);
    send_line(buf);
}

void protocol_send_dir(const char *path)
{
    char buf[BRIDGE_MAX_LINE];
    int result = fs_list_dir(path, buf, BRIDGE_MAX_LINE);
    if (result < 0) {
        send_err("LISTDIR failed", path);
    } else {
        send_line(buf);
    }
}

void protocol_send_file(const char *path, ULONG offset, ULONG size)
{
    UBYTE filebuf[256];
    ULONG actual = 0;
    int result;

    if (size > 256) size = 256;

    result = fs_read_file(path, offset, size, filebuf, 256, &actual);
    if (result < 0) {
        send_err("READFILE failed", path);
    } else {
        char buf[BRIDGE_MAX_LINE];
        char hexbuf[514];
        ULONG i;
        for (i = 0; i < actual; i++) {
            sprintf(hexbuf + i * 2, "%02x", filebuf[i]);
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
    char buf[BRIDGE_MAX_LINE];
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
    char buf[128];

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
    UBYTE membuf[256];
    const char *sep;
    int actual;

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

    actual = sys_inspect_mem((APTR)addr, size, membuf, 256);
    if (actual > 0) {
        protocol_send_mem((APTR)addr, (ULONG)actual, membuf);
    } else {
        char detail[64];
        sprintf(detail, "address %08lx not accessible", (unsigned long)addr);
        send_err("INSPECT", detail);
    }
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
            ipc_send_to_client(ce->replyPort, ABMSG_VAR_GET,
                               ce->clientId, 0,
                               dot + 1, strlen(dot + 1) + 1);
        } else {
            send_err("Client not found", cname);
        }
    } else {
        send_err("GETVAR", "needs client.varname format");
    }
}

static void handle_setvar(const char *args)
{
    /* Format: client_name.var_name|value */
    const char *dot;
    const char *sep;
    struct ClientEntry *ce;

    dot = strchr(args, '.');
    sep = strchr(args, '|');

    if (dot && sep && sep > dot) {
        char cname[34];
        char payload[AB_MAX_DATA];
        int nlen = (int)(dot - args);
        if (nlen > 33) nlen = 33;
        strncpy(cname, args, nlen);
        cname[nlen] = '\0';

        /* payload = "varname|value" */
        strncpy(payload, dot + 1, AB_MAX_DATA - 1);
        payload[AB_MAX_DATA - 1] = '\0';

        ce = client_find_by_name(cname);
        if (ce && ce->replyPort) {
            ipc_send_to_client(ce->replyPort, ABMSG_VAR_SET,
                               ce->clientId, 0,
                               payload, strlen(payload) + 1);
        } else {
            send_err("Client not found", cname);
        }
    } else {
        send_err("SETVAR", "needs client.varname|value format");
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
    char buf[BRIDGE_MAX_LINE];
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
    char path[256];
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
    char path[256];
    ULONG offset = 0;
    UBYTE databuf[256];
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
    /* Format: id|command */
    ULONG cmdId;
    const char *sep;
    char resultBuf[512];
    int result;

    sep = strchr(args, '|');
    if (!sep) {
        send_err("LAUNCH", "needs id|command format");
        return;
    }

    cmdId = strtoul(args, NULL, 10);
    result = proc_launch(cmdId, sep + 1, resultBuf, 512);
    if (result < 0) {
        protocol_send_cmd_response(cmdId, "ERR", "Launch failed");
    } else {
        protocol_send_cmd_response(cmdId, "OK", resultBuf);
    }
}

static void handle_run(const char *args)
{
    /* Format: id|command - launches asynchronously, doesn't wait */
    ULONG cmdId;
    const char *sep;
    char resultBuf[256];
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
