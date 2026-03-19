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

/* Local hook registry */
struct HookEntry {
    BOOL           active;
    char           name[34];
    char           description[64];
    ab_hook_fn_t   fn;
};
static struct HookEntry hook_table[AB_MAX_HOOKS];

/* Local memory region registry */
struct MemRegEntry {
    BOOL  active;
    char  name[34];
    char  description[64];
    APTR  addr;
    ULONG size;
};
static struct MemRegEntry memreg_table[AB_MAX_MEMREGIONS];

/* ---- Resource Tracker ---- */
#define RES_TYPE_MEM    0
#define RES_TYPE_HANDLE 1
#define RES_STATE_OPEN   0
#define RES_STATE_CLOSED 1

struct ResourceEntry {
    BOOL  active;
    UBYTE resType;   /* RES_TYPE_MEM or RES_TYPE_HANDLE */
    UBYTE state;     /* RES_STATE_OPEN or RES_STATE_CLOSED */
    char  tag[32];
    APTR  ptr;
    ULONG size;      /* Only for MEM type */
};
static struct ResourceEntry res_table[AB_MAX_RESOURCES];

/* ---- Performance Profiler ---- */
struct PerfSection {
    BOOL  active;
    char  label[24];
    ULONG start_vhpos;
    ULONG total;     /* Accumulated ticks */
    ULONG min_val;
    ULONG max_val;
    ULONG count;
};
static struct PerfSection perf_sections[AB_MAX_PERF_SECTIONS];

static ULONG perf_frame_start_vhpos = 0;
static ULONG perf_frame_times[AB_PERF_FRAME_HISTORY];
static int perf_frame_head = 0;
static int perf_frame_count = 0;
static ULONG perf_frame_min = 0xFFFFFFFF;
static ULONG perf_frame_max = 0;
static ULONG perf_frame_total = 0;
static ULONG perf_frame_num = 0;

/*
 * Read VHPOSR ($DFF006) for sub-frame timing.
 * Returns vertical position in high byte, horizontal in low byte.
 * On PAL: V ranges 0-312, H ranges 0-226.
 * Each line is ~63.5us, giving roughly 64us resolution.
 */
static ULONG read_vhpos(void)
{
    volatile ULONG *vposr = (volatile ULONG *)0xDFF004;
    return *vposr;  /* Read VPOSR+VHPOSR as 32-bit for full V position */
}

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

/* Forward declaration - processes a single daemon-initiated message */
static void process_daemon_msg(struct Message *msg);

/*
 * Send a message to daemon and wait for reply.
 * Returns the replied message (same pointer, now replied).
 * Caller must pool_free() after use.
 *
 * NOTE: While waiting, daemon-initiated messages (hook calls, var
 * get/set) may arrive on reply_port before our reply does.
 * We must process those inline to avoid deadlock/lost messages.
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

    /* Wait for OUR reply. Any daemon-initiated messages that arrive
     * first are processed inline (hook calls, var get/set, etc). */
    for (;;) {
        struct Message *got;
        WaitPort(reply_port);
        got = GetMsg(reply_port);
        if (!got) continue;

        if (got == (struct Message *)bm) {
            /* Got our own reply back */
            break;
        }

        /* Daemon-initiated message arrived while we were waiting.
         * Process it now so hooks/vars work even during push bursts. */
        process_daemon_msg(got);

        /* If daemon shut down while we waited, abort */
        if (!connected) return NULL;
    }

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

    /* Initialize hook table */
    for (i = 0; i < AB_MAX_HOOKS; i++) {
        hook_table[i].active = FALSE;
    }

    /* Initialize memreg table */
    for (i = 0; i < AB_MAX_MEMREGIONS; i++) {
        memreg_table[i].active = FALSE;
    }

    /* Initialize resource tracker */
    for (i = 0; i < AB_MAX_RESOURCES; i++) {
        res_table[i].active = FALSE;
    }

    /* Initialize perf sections */
    for (i = 0; i < AB_MAX_PERF_SECTIONS; i++) {
        perf_sections[i].active = FALSE;
    }
    perf_frame_head = 0;
    perf_frame_count = 0;
    perf_frame_min = 0xFFFFFFFF;
    perf_frame_max = 0;
    perf_frame_total = 0;
    perf_frame_num = 0;

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

    /* Clear hook table */
    for (i = 0; i < AB_MAX_HOOKS; i++) {
        hook_table[i].active = FALSE;
    }

    /* Clear memreg table */
    for (i = 0; i < AB_MAX_MEMREGIONS; i++) {
        memreg_table[i].active = FALSE;
    }

    /* Clear resource tracker */
    for (i = 0; i < AB_MAX_RESOURCES; i++) {
        res_table[i].active = FALSE;
    }

    /* Clear perf sections */
    for (i = 0; i < AB_MAX_PERF_SECTIONS; i++) {
        perf_sections[i].active = FALSE;
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

/*
 * Process a single daemon-initiated message on reply_port.
 * Used by both ab_poll() and send_and_wait() to handle
 * hook calls, variable get/set, etc.
 */
static void process_daemon_msg(struct Message *msg)
{
    struct BridgeMsg *bm = (struct BridgeMsg *)msg;

    printf("[CLIENT] process_daemon_msg type=%ld data='%s'\n",
           (long)bm->type, bm->data);

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
                sprintf(bm->data, "%s|%ld|%s",
                        ve->name, (long)ve->type, valbuf);
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

    case ABMSG_HOOK_CALL:
        /* Daemon is calling a registered hook.
         * data format: "hookname|args" */
        {
            char *sep = strchr(bm->data, '|');
            char hname[34];
            const char *hargs = "";
            int found = 0;
            int hi;

            if (sep) {
                int nlen = (int)(sep - bm->data);
                if (nlen > 33) nlen = 33;
                strncpy(hname, bm->data, nlen);
                hname[nlen] = '\0';
                hargs = sep + 1;
            } else {
                strncpy(hname, bm->data, 33);
                hname[33] = '\0';
            }

            for (hi = 0; hi < AB_MAX_HOOKS; hi++) {
                if (hook_table[hi].active &&
                    strcmp(hook_table[hi].name, hname) == 0) {
                    char resultBuf[AB_MAX_DATA - 4];
                    int rc;
                    resultBuf[0] = '\0';
                    rc = hook_table[hi].fn(hargs, resultBuf,
                                           sizeof(resultBuf));
                    resultBuf[sizeof(resultBuf) - 1] = '\0';
                    if (rc == 0) {
                        sprintf(bm->data, "ok|%s", resultBuf);
                    } else {
                        sprintf(bm->data, "err|%s", resultBuf);
                    }
                    bm->data[AB_MAX_DATA - 1] = '\0';
                    bm->dataLen = strlen(bm->data) + 1;
                    bm->result = 0;
                    found = 1;
                    break;
                }
            }
            if (!found) {
                sprintf(bm->data, "err|hook not found: %s", hname);
                bm->data[AB_MAX_DATA - 1] = '\0';
                bm->dataLen = strlen(bm->data) + 1;
                bm->result = -1;
            }
        }
        ReplyMsg(msg);
        break;

    case ABMSG_GET_RESOURCES:
        /* Daemon is requesting resource list */
        {
            int ri, pos = 0, cnt = 0;
            /* Format: "count|type:tag:ptr:size:state,..." */
            for (ri = 0; ri < AB_MAX_RESOURCES; ri++) {
                if (res_table[ri].active) cnt++;
            }
            pos = sprintf(bm->data, "%ld|", (long)cnt);
            for (ri = 0; ri < AB_MAX_RESOURCES; ri++) {
                if (!res_table[ri].active) continue;
                if (pos > 0 && bm->data[pos - 1] != '|') {
                    if (pos < AB_MAX_DATA - 1)
                        bm->data[pos++] = ',';
                }
                pos += sprintf(bm->data + pos, "%s:%s:%08lx:%lu:%s",
                    res_table[ri].resType == RES_TYPE_MEM ? "MEM" : "HANDLE",
                    res_table[ri].tag,
                    (unsigned long)res_table[ri].ptr,
                    (unsigned long)res_table[ri].size,
                    res_table[ri].state == RES_STATE_OPEN ? "OPEN" : "CLOSED");
                if (pos >= AB_MAX_DATA - 2) break;
            }
            bm->data[pos] = '\0';
            bm->dataLen = pos + 1;
            bm->result = 0;
        }
        ReplyMsg(msg);
        break;

    case ABMSG_GET_PERF:
        /* Daemon is requesting performance data */
        {
            int pi, pos;
            ULONG frame_avg = 0;
            if (perf_frame_num > 0) {
                frame_avg = perf_frame_total / perf_frame_num;
            }
            pos = sprintf(bm->data, "%lu|%lu|%lu|%lu|",
                (unsigned long)frame_avg,
                (unsigned long)(perf_frame_min == 0xFFFFFFFF ? 0 : perf_frame_min),
                (unsigned long)perf_frame_max,
                (unsigned long)perf_frame_num);

            /* Append sections: label:avg:min:max:count,... */
            for (pi = 0; pi < AB_MAX_PERF_SECTIONS; pi++) {
                if (!perf_sections[pi].active) continue;
                if (pos > 0 && bm->data[pos - 1] != '|') {
                    if (pos < AB_MAX_DATA - 1)
                        bm->data[pos++] = ',';
                }
                {
                    ULONG savg = 0;
                    if (perf_sections[pi].count > 0) {
                        savg = perf_sections[pi].total / perf_sections[pi].count;
                    }
                    pos += sprintf(bm->data + pos, "%s:%lu:%lu:%lu:%lu",
                        perf_sections[pi].label,
                        (unsigned long)savg,
                        (unsigned long)(perf_sections[pi].min_val == 0xFFFFFFFF ? 0 : perf_sections[pi].min_val),
                        (unsigned long)perf_sections[pi].max_val,
                        (unsigned long)perf_sections[pi].count);
                }
                if (pos >= AB_MAX_DATA - 2) break;
            }
            bm->data[pos] = '\0';
            bm->dataLen = pos + 1;
            bm->result = 0;
        }
        ReplyMsg(msg);
        break;

    case ABMSG_DEBUG_PAUSE:
        /* Debugger is pausing us. Reply immediately (so daemon doesn't block),
         * then enter a wait loop until we get a RESUME message. */
        bm->result = 0;
        ReplyMsg(msg);
        printf("[DEBUG] Paused by debugger\n");
        /* Block here: wait for messages, only exit on RESUME */
        {
            struct Message *dmsg;
            BOOL paused = TRUE;
            while (paused && connected) {
                WaitPort(reply_port);
                while ((dmsg = GetMsg(reply_port)) != NULL) {
                    struct BridgeMsg *dbm = (struct BridgeMsg *)dmsg;
                    if (dbm->type == ABMSG_DEBUG_RESUME) {
                        dbm->result = 0;
                        ReplyMsg(dmsg);
                        paused = FALSE;
                        printf("[DEBUG] Resumed\n");
                        break;
                    }
                    /* Process other messages while paused (var get, etc) */
                    process_daemon_msg(dmsg);
                }
            }
        }
        break;

    case ABMSG_SHUTDOWN:
        /* Daemon is shutting down */
        connected = FALSE;
        daemon_port = NULL;
        ReplyMsg(msg);
        break;

    default:
        ReplyMsg(msg);
        break;
    }
}

void ab_poll(void)
{
    struct Message *msg;

    if (!connected || !reply_port) return;

    /* Check for daemon-initiated messages */
    while ((msg = GetMsg(reply_port)) != NULL) {
        process_daemon_msg(msg);
        if (!connected) return;
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

/* ---- Hook Registry ---- */

void ab_register_hook(const char *name, const char *description,
                      ab_hook_fn_t fn)
{
    char data[AB_MAX_DATA];
    int i, slot = -1;

    if (!connected || !name || !fn) return;

    /* Find existing or free slot */
    for (i = 0; i < AB_MAX_HOOKS; i++) {
        if (hook_table[i].active && strcmp(hook_table[i].name, name) == 0) {
            slot = i;
            break;
        }
    }
    if (slot < 0) {
        for (i = 0; i < AB_MAX_HOOKS; i++) {
            if (!hook_table[i].active) {
                slot = i;
                break;
            }
        }
    }
    if (slot < 0) return; /* Table full */

    hook_table[slot].active = TRUE;
    strncpy(hook_table[slot].name, name, 33);
    hook_table[slot].name[33] = '\0';
    strncpy(hook_table[slot].description, description ? description : "", 63);
    hook_table[slot].description[63] = '\0';
    hook_table[slot].fn = fn;

    /* Notify daemon: "name|description" */
    sprintf(data, "%s|", name);
    strncat(data, description ? description : "", AB_MAX_DATA - strlen(data) - 1);
    data[AB_MAX_DATA - 1] = '\0';
    send_simple(ABMSG_HOOK_REGISTER, 0, data, strlen(data) + 1);
}

void ab_unregister_hook(const char *name)
{
    int i;

    if (!connected || !name) return;

    for (i = 0; i < AB_MAX_HOOKS; i++) {
        if (hook_table[i].active && strcmp(hook_table[i].name, name) == 0) {
            hook_table[i].active = FALSE;
            break;
        }
    }

    send_simple(ABMSG_HOOK_UNREGISTER, 0, name, strlen(name) + 1);
}

/* ---- Memory Region Registry ---- */

void ab_register_memregion(const char *name, APTR addr, ULONG size,
                           const char *description)
{
    char data[AB_MAX_DATA];
    int i, slot = -1;
    int len;

    if (!connected || !name || !addr || size == 0) return;

    /* Find existing or free slot */
    for (i = 0; i < AB_MAX_MEMREGIONS; i++) {
        if (memreg_table[i].active && strcmp(memreg_table[i].name, name) == 0) {
            slot = i;
            break;
        }
    }
    if (slot < 0) {
        for (i = 0; i < AB_MAX_MEMREGIONS; i++) {
            if (!memreg_table[i].active) {
                slot = i;
                break;
            }
        }
    }
    if (slot < 0) return; /* Table full */

    memreg_table[slot].active = TRUE;
    strncpy(memreg_table[slot].name, name, 33);
    memreg_table[slot].name[33] = '\0';
    strncpy(memreg_table[slot].description, description ? description : "", 63);
    memreg_table[slot].description[63] = '\0';
    memreg_table[slot].addr = addr;
    memreg_table[slot].size = size;

    /* Notify daemon: "name|addr_hex|size|description" */
    sprintf(data, "%s|%08lx|%lu|",
            name, (unsigned long)addr, (unsigned long)size);
    strncat(data, description ? description : "", AB_MAX_DATA - strlen(data) - 1);
    data[AB_MAX_DATA - 1] = '\0';
    len = strlen(data);
    send_simple(ABMSG_MEMREG_REGISTER, 0, data, len + 1);
}

void ab_unregister_memregion(const char *name)
{
    int i;

    if (!connected || !name) return;

    for (i = 0; i < AB_MAX_MEMREGIONS; i++) {
        if (memreg_table[i].active && strcmp(memreg_table[i].name, name) == 0) {
            memreg_table[i].active = FALSE;
            break;
        }
    }

    send_simple(ABMSG_MEMREG_UNREGISTER, 0, name, strlen(name) + 1);
}

/* ---- Resource Tracker ---- */

void ab_track_alloc(const char *tag, APTR ptr, ULONG size)
{
    int i;
    if (!ptr) return;

    for (i = 0; i < AB_MAX_RESOURCES; i++) {
        if (!res_table[i].active) {
            res_table[i].active = TRUE;
            res_table[i].resType = RES_TYPE_MEM;
            res_table[i].state = RES_STATE_OPEN;
            strncpy(res_table[i].tag, tag ? tag : "mem", 31);
            res_table[i].tag[31] = '\0';
            res_table[i].ptr = ptr;
            res_table[i].size = size;
            return;
        }
    }
    /* Table full - silently drop */
}

void ab_track_free(APTR ptr)
{
    int i;
    if (!ptr) return;

    for (i = 0; i < AB_MAX_RESOURCES; i++) {
        if (res_table[i].active &&
            res_table[i].ptr == ptr &&
            res_table[i].state == RES_STATE_OPEN) {
            res_table[i].state = RES_STATE_CLOSED;
            return;
        }
    }
}

void ab_track_open(const char *tag, APTR handle)
{
    int i;
    if (!handle) return;

    for (i = 0; i < AB_MAX_RESOURCES; i++) {
        if (!res_table[i].active) {
            res_table[i].active = TRUE;
            res_table[i].resType = RES_TYPE_HANDLE;
            res_table[i].state = RES_STATE_OPEN;
            strncpy(res_table[i].tag, tag ? tag : "handle", 31);
            res_table[i].tag[31] = '\0';
            res_table[i].ptr = handle;
            res_table[i].size = 0;
            return;
        }
    }
}

void ab_track_close(APTR handle)
{
    int i;
    if (!handle) return;

    for (i = 0; i < AB_MAX_RESOURCES; i++) {
        if (res_table[i].active &&
            res_table[i].ptr == handle &&
            res_table[i].state == RES_STATE_OPEN) {
            res_table[i].state = RES_STATE_CLOSED;
            return;
        }
    }
}

/* ---- Performance Profiler ---- */

void ab_perf_frame_start(void)
{
    perf_frame_start_vhpos = read_vhpos();
}

void ab_perf_frame_end(void)
{
    ULONG end = read_vhpos();
    ULONG delta;

    /* Handle wrap-around: VPOSR+VHPOSR is a 32-bit value where
     * bits 16-24 are vertical position. If end < start, a new
     * frame started (V wrapped from ~312 to 0). */
    if (end >= perf_frame_start_vhpos) {
        delta = end - perf_frame_start_vhpos;
    } else {
        /* Wrapped - assume one full frame (~312 lines * 256 hpos) */
        delta = (0x013F00 - perf_frame_start_vhpos) + end;
    }

    /* Store in ring buffer */
    perf_frame_times[perf_frame_head] = delta;
    perf_frame_head = (perf_frame_head + 1) % AB_PERF_FRAME_HISTORY;
    if (perf_frame_count < AB_PERF_FRAME_HISTORY) {
        perf_frame_count++;
    }

    /* Update stats */
    perf_frame_total += delta;
    perf_frame_num++;
    if (delta < perf_frame_min) perf_frame_min = delta;
    if (delta > perf_frame_max) perf_frame_max = delta;
}

void ab_perf_section_start(const char *label)
{
    int i;
    if (!label) return;

    /* Find existing section or allocate new one */
    for (i = 0; i < AB_MAX_PERF_SECTIONS; i++) {
        if (perf_sections[i].active &&
            strcmp(perf_sections[i].label, label) == 0) {
            perf_sections[i].start_vhpos = read_vhpos();
            return;
        }
    }

    /* Allocate new section */
    for (i = 0; i < AB_MAX_PERF_SECTIONS; i++) {
        if (!perf_sections[i].active) {
            perf_sections[i].active = TRUE;
            strncpy(perf_sections[i].label, label, 23);
            perf_sections[i].label[23] = '\0';
            perf_sections[i].start_vhpos = read_vhpos();
            perf_sections[i].total = 0;
            perf_sections[i].min_val = 0xFFFFFFFF;
            perf_sections[i].max_val = 0;
            perf_sections[i].count = 0;
            return;
        }
    }
}

void ab_perf_section_end(const char *label)
{
    int i;
    ULONG end, delta;

    if (!label) return;

    end = read_vhpos();

    for (i = 0; i < AB_MAX_PERF_SECTIONS; i++) {
        if (perf_sections[i].active &&
            strcmp(perf_sections[i].label, label) == 0) {
            if (end >= perf_sections[i].start_vhpos) {
                delta = end - perf_sections[i].start_vhpos;
            } else {
                delta = (0x013F00 - perf_sections[i].start_vhpos) + end;
            }

            perf_sections[i].total += delta;
            perf_sections[i].count++;
            if (delta < perf_sections[i].min_val) {
                perf_sections[i].min_val = delta;
            }
            if (delta > perf_sections[i].max_val) {
                perf_sections[i].max_val = delta;
            }
            return;
        }
    }
}

/* ---- Test Harness ---- */

static char test_suite_name[64];
static int test_pass_count = 0;
static int test_fail_count = 0;
static int test_total_count = 0;

void ab_test_begin(const char *suiteName)
{
    char data[AB_MAX_DATA];

    test_pass_count = 0;
    test_fail_count = 0;
    test_total_count = 0;

    strncpy(test_suite_name, suiteName ? suiteName : "unnamed", 63);
    test_suite_name[63] = '\0';

    /* Send TEST_BEGIN to daemon as a log with special prefix */
    sprintf(data, "TEST_BEGIN|%s", test_suite_name);
    if (connected) {
        send_simple(ABMSG_LOG, 0, data, strlen(data) + 1);
    }
}

int ab_test_assert(int condition, const char *testName,
                   const char *file, int line)
{
    char data[AB_MAX_DATA];

    test_total_count++;
    if (condition) {
        test_pass_count++;
        sprintf(data, "TEST_PASS|%s|%s|%ld",
                testName ? testName : "?",
                file ? file : "?",
                (long)line);
    } else {
        test_fail_count++;
        sprintf(data, "TEST_FAIL|%s|%s|%ld",
                testName ? testName : "?",
                file ? file : "?",
                (long)line);
    }

    if (connected) {
        send_simple(ABMSG_LOG, 0, data, strlen(data) + 1);
    }

    return condition ? 1 : 0;
}

void ab_test_end(void)
{
    char data[AB_MAX_DATA];

    sprintf(data, "TEST_END|%s|%ld|%ld|%ld",
            test_suite_name,
            (long)test_pass_count,
            (long)test_fail_count,
            (long)test_total_count);

    if (connected) {
        send_simple(ABMSG_LOG, 0, data, strlen(data) + 1);
    }
}
