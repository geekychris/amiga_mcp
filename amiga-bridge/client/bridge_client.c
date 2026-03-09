/*
 * bridge_client.c - Client library for AmigaBridge
 *
 * Links into Amiga applications. Communicates with the AmigaBridge
 * daemon via IPC (MsgPort message passing).
 */

#include <exec/types.h>
#include <exec/memory.h>
#include <exec/ports.h>
#include <proto/exec.h>

#include <string.h>
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>

#include "bridge_ipc.h"
#include "bridge_client.h"

/* Pool of pre-allocated messages to avoid per-call AllocMem */
#define MSG_POOL_SIZE 4
static struct BridgeMsg msg_pool[MSG_POOL_SIZE];
static BOOL msg_pool_used[MSG_POOL_SIZE];

/* Local variable registry */
struct VarEntry {
    BOOL  active;
    char  name[34];
    int   type;
    void *ptr;
};
static struct VarEntry var_table[AB_MAX_VARS];

/* Client state */
static struct MsgPort *reply_port = NULL;
static struct MsgPort *daemon_port = NULL;
static ULONG my_client_id = 0;
static BOOL connected = FALSE;
static ab_cmd_handler_t cmd_handler = NULL;
static char my_app_name[34]; /* Persistent copy for task name */

static struct BridgeMsg *pool_alloc(void)
{
    int i;
    for (i = 0; i < MSG_POOL_SIZE; i++) {
        if (!msg_pool_used[i]) {
            msg_pool_used[i] = TRUE;
            memset(&msg_pool[i], 0, sizeof(struct BridgeMsg));
            msg_pool[i].msg.mn_Length = sizeof(struct BridgeMsg);
            msg_pool[i].version = 1;
            return &msg_pool[i];
        }
    }
    return NULL;
}

static void pool_free(struct BridgeMsg *m)
{
    int i;
    for (i = 0; i < MSG_POOL_SIZE; i++) {
        if (&msg_pool[i] == m) {
            msg_pool_used[i] = FALSE;
            return;
        }
    }
}

/*
 * Send a message to daemon and wait for reply.
 * Returns the replied message (same pointer, now replied).
 * Caller must pool_free() after use.
 */
static struct BridgeMsg *send_and_wait(UWORD type, ULONG cmdId,
                                       const char *data, ULONG dataLen)
{
    struct BridgeMsg *bm;

    if (!daemon_port || !reply_port) return NULL;

    bm = pool_alloc();
    if (!bm) return NULL;

    bm->msg.mn_ReplyPort = reply_port;
    bm->type = type;
    bm->clientId = my_client_id;
    bm->cmdId = cmdId;
    bm->result = 0;
    bm->extData = NULL;
    bm->extDataLen = 0;

    if (data && dataLen > 0) {
        if (dataLen > AB_MAX_DATA) dataLen = AB_MAX_DATA;
        memcpy(bm->data, data, dataLen);
        bm->dataLen = dataLen;
    } else {
        bm->dataLen = 0;
    }

    PutMsg(daemon_port, (struct Message *)bm);
    WaitPort(reply_port);
    GetMsg(reply_port); /* Remove the reply */

    return bm;
}

/*
 * Send a message to daemon, wait for reply, then free the message.
 * Returns the result code from the reply.
 */
static LONG send_simple(UWORD type, ULONG cmdId,
                        const char *data, ULONG dataLen)
{
    struct BridgeMsg *bm;
    LONG result;

    bm = send_and_wait(type, cmdId, data, dataLen);
    if (!bm) return -1;

    result = bm->result;
    pool_free(bm);
    return result;
}

/*
 * Find a variable in the local registry by name.
 */
static struct VarEntry *find_var(const char *name)
{
    int i;
    for (i = 0; i < AB_MAX_VARS; i++) {
        if (var_table[i].active && strcmp(var_table[i].name, name) == 0) {
            return &var_table[i];
        }
    }
    return NULL;
}

/*
 * Format a variable value as string based on type.
 */
static int format_var_value(const struct VarEntry *ve, char *buf, int bufSize)
{
    switch (ve->type) {
    case AB_TYPE_I32:
        sprintf(buf, "%ld", (long)(*(LONG *)ve->ptr));
        return strlen(buf);
    case AB_TYPE_U32:
        sprintf(buf, "%lu", (unsigned long)(*(ULONG *)ve->ptr));
        return strlen(buf);
    case AB_TYPE_STR:
        {
            const char *s = (const char *)ve->ptr;
            int len = strlen(s);
            if (len >= bufSize) len = bufSize - 1;
            strncpy(buf, s, len);
            buf[len] = '\0';
            return len;
        }
    case AB_TYPE_F32:
        sprintf(buf, "0x%08lx", *(unsigned long *)ve->ptr);
        return strlen(buf);
    case AB_TYPE_PTR:
        sprintf(buf, "%08lx", (unsigned long)(*(ULONG *)ve->ptr));
        return strlen(buf);
    default:
        buf[0] = '?';
        buf[1] = '\0';
        return 1;
    }
}

int ab_init(const char *appName)
{
    struct BridgeMsg *bm;
    int i;

    if (connected) return 0; /* Already initialized */

    /* Initialize message pool */
    for (i = 0; i < MSG_POOL_SIZE; i++) {
        msg_pool_used[i] = FALSE;
    }

    /* Initialize var table */
    for (i = 0; i < AB_MAX_VARS; i++) {
        var_table[i].active = FALSE;
    }

    /* Find the daemon's public port */
    Forbid();
    daemon_port = FindPort((CONST_STRPTR)BRIDGE_PORT_NAME);
    Permit();

    if (!daemon_port) return -1; /* Daemon not running */

    /* Create our reply port */
    reply_port = CreateMsgPort();
    if (!reply_port) {
        daemon_port = NULL;
        return -1;
    }

    /* Send REGISTER message */
    bm = send_and_wait(ABMSG_REGISTER, 0,
                       appName, strlen(appName) + 1);
    if (!bm || bm->result != 0) {
        if (bm) pool_free(bm);
        DeleteMsgPort(reply_port);
        reply_port = NULL;
        daemon_port = NULL;
        return -1;
    }

    my_client_id = bm->clientId;
    pool_free(bm);
    connected = TRUE;

    /* Set task name so BREAK/FindTask can find us.
     * Copy to static buffer since appName may be on stack. */
    strncpy(my_app_name, appName, 33);
    my_app_name[33] = '\0';
    {
        struct Task *me = FindTask(NULL);
        if (me) {
            me->tc_Node.ln_Name = my_app_name;
        }
    }

    return 0;
}

void ab_cleanup(void)
{
    int i;

    if (!connected) return;

    /* Send UNREGISTER */
    send_simple(ABMSG_UNREGISTER, 0, NULL, 0);

    /* Drain any pending messages on reply port */
    if (reply_port) {
        struct Message *msg;
        while ((msg = GetMsg(reply_port)) != NULL) {
            /* These are daemon-initiated messages; reply to them */
            ReplyMsg(msg);
        }
        DeleteMsgPort(reply_port);
        reply_port = NULL;
    }

    daemon_port = NULL;
    my_client_id = 0;
    connected = FALSE;
    cmd_handler = NULL;

    /* Clear var table */
    for (i = 0; i < AB_MAX_VARS; i++) {
        var_table[i].active = FALSE;
    }
}

BOOL ab_is_connected(void)
{
    return connected;
}

void ab_log(int level, const char *fmt, ...)
{
    char logdata[AB_MAX_DATA];
    va_list args;
    int pos;

    if (!connected) return;

    /* Format: "level|message" */
    logdata[0] = '0' + (char)level;
    logdata[1] = '|';

    va_start(args, fmt);
    vsprintf(logdata + 2, fmt, args);
    va_end(args);

    /* Safety: ensure null termination within buffer */
    logdata[AB_MAX_DATA - 1] = '\0';

    pos = strlen(logdata);
    send_simple(ABMSG_LOG, 0, logdata, pos + 1);
}

void ab_register_var(const char *name, int type, void *ptr)
{
    char data[AB_MAX_DATA];
    int len;
    int i;
    int slot = -1;

    if (!connected || !name || !ptr) return;

    /* Store in local registry */
    for (i = 0; i < AB_MAX_VARS; i++) {
        if (var_table[i].active && strcmp(var_table[i].name, name) == 0) {
            /* Update existing */
            var_table[i].type = type;
            var_table[i].ptr = ptr;
            slot = i;
            break;
        }
    }
    if (slot < 0) {
        for (i = 0; i < AB_MAX_VARS; i++) {
            if (!var_table[i].active) {
                slot = i;
                break;
            }
        }
    }
    if (slot >= 0) {
        var_table[slot].active = TRUE;
        strncpy(var_table[slot].name, name, 33);
        var_table[slot].name[33] = '\0';
        var_table[slot].type = type;
        var_table[slot].ptr = ptr;
    }

    /* Notify daemon: "name|type" */
    sprintf(data, "%s|%ld", name, (long)type);
    len = strlen(data);
    send_simple(ABMSG_VAR_REGISTER, 0, data, len + 1);
}

void ab_unregister_var(const char *name)
{
    int i;

    if (!connected || !name) return;

    /* Remove from local registry */
    for (i = 0; i < AB_MAX_VARS; i++) {
        if (var_table[i].active && strcmp(var_table[i].name, name) == 0) {
            var_table[i].active = FALSE;
            break;
        }
    }

    send_simple(ABMSG_VAR_UNREGISTER, 0, name, strlen(name) + 1);
}

void ab_push_var(const char *name)
{
    struct VarEntry *ve;
    char data[AB_MAX_DATA];
    char valbuf[128];
    int len;

    if (!connected || !name) return;

    ve = find_var(name);
    if (!ve) return;

    /* Format: "name|type|value" */
    format_var_value(ve, valbuf, 128);
    sprintf(data, "%s|%ld|%s", ve->name, (long)ve->type, valbuf);
    len = strlen(data);
    send_simple(ABMSG_VAR_PUSH, 0, data, len + 1);
}

void ab_heartbeat(void)
{
    if (!connected) return;
    send_simple(ABMSG_HEARTBEAT, 0, NULL, 0);
}

void ab_send_mem(APTR addr, ULONG size)
{
    struct BridgeMsg *bm;

    if (!connected || !addr || size == 0) return;

    bm = pool_alloc();
    if (!bm) return;

    bm->msg.mn_ReplyPort = reply_port;
    bm->type = ABMSG_MEM_DUMP;
    bm->clientId = my_client_id;
    bm->cmdId = 0;
    bm->dataLen = 0;
    bm->extData = addr;
    bm->extDataLen = size;

    PutMsg(daemon_port, (struct Message *)bm);
    WaitPort(reply_port);
    GetMsg(reply_port);

    pool_free(bm);
}

void ab_poll(void)
{
    struct Message *msg;

    if (!connected || !reply_port) return;

    /* Check for daemon-initiated messages */
    while ((msg = GetMsg(reply_port)) != NULL) {
        struct BridgeMsg *bm = (struct BridgeMsg *)msg;

        switch (bm->type) {
        case ABMSG_CMD_FORWARD:
            if (cmd_handler) {
                cmd_handler(bm->cmdId, bm->data);
            }
            ReplyMsg(msg);
            break;

        case ABMSG_VAR_GET:
            /* Daemon is asking for a variable value */
            {
                struct VarEntry *ve = find_var(bm->data);
                if (ve) {
                    char valbuf[128];
                    format_var_value(ve, valbuf, 128);
                    /* Put value in reply data */
                    sprintf(bm->data, "%s|%d|%s",
                            ve->name, ve->type, valbuf);
                    bm->dataLen = strlen(bm->data) + 1;
                    bm->result = 0;
                } else {
                    bm->result = -1;
                }
            }
            ReplyMsg(msg);
            break;

        case ABMSG_VAR_SET:
            /* Daemon is setting a variable value.
             * Format in data: "varname|value" */
            {
                char *sep = strchr(bm->data, '|');
                if (sep) {
                    char vname[34];
                    int nlen = (int)(sep - bm->data);
                    struct VarEntry *ve;
                    if (nlen > 33) nlen = 33;
                    strncpy(vname, bm->data, nlen);
                    vname[nlen] = '\0';
                    ve = find_var(vname);
                    if (ve) {
                        const char *val = sep + 1;
                        switch (ve->type) {
                        case AB_TYPE_I32:
                            *(LONG *)ve->ptr = (LONG)strtol(val, NULL, 10);
                            break;
                        case AB_TYPE_U32:
                            *(ULONG *)ve->ptr = strtoul(val, NULL, 10);
                            break;
                        case AB_TYPE_STR:
                            strncpy((char *)ve->ptr, val, 127);
                            ((char *)ve->ptr)[127] = '\0';
                            break;
                        case AB_TYPE_PTR:
                            *(ULONG *)ve->ptr = strtoul(val, NULL, 16);
                            break;
                        default:
                            break;
                        }
                        bm->result = 0;
                    } else {
                        bm->result = -1;
                    }
                } else {
                    bm->result = -1;
                }
            }
            ReplyMsg(msg);
            break;

        case ABMSG_SHUTDOWN:
            /* Daemon is shutting down */
            connected = FALSE;
            daemon_port = NULL;
            ReplyMsg(msg);
            return;

        default:
            ReplyMsg(msg);
            break;
        }
    }
}

void ab_set_cmd_handler(ab_cmd_handler_t handler)
{
    cmd_handler = handler;
}

void ab_cmd_respond(ULONG id, const char *status, const char *data)
{
    char respdata[AB_MAX_DATA];
    int len;

    if (!connected) return;

    sprintf(respdata, "%s|%s",
            status ? status : "OK",
            data ? data : "");
    len = strlen(respdata);

    send_simple(ABMSG_CMD_RESPONSE, id, respdata, len + 1);
}
