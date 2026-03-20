/*
 * ipc_manager.c - MsgPort IPC handling for AmigaBridge daemon
 *
 * Creates public "AMIGABRIDGE" port and processes messages from clients.
 */

#include <exec/types.h>
#include <exec/memory.h>
#include <exec/ports.h>
#include <proto/exec.h>
#include <proto/dos.h>

#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#include "bridge_internal.h"

static struct MsgPort *ipc_port = NULL;

/* Pool of BridgeMsg structs for sending to clients */
#define IPC_MSG_POOL_SIZE 8
static struct BridgeMsg msg_pool[IPC_MSG_POOL_SIZE];
static BOOL msg_pool_used[IPC_MSG_POOL_SIZE];

static struct BridgeMsg *alloc_msg(void)
{
    int i;
    for (i = 0; i < IPC_MSG_POOL_SIZE; i++) {
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

static void free_msg(struct BridgeMsg *m)
{
    int i;
    for (i = 0; i < IPC_MSG_POOL_SIZE; i++) {
        if (&msg_pool[i] == m) {
            msg_pool_used[i] = FALSE;
            return;
        }
    }
}

int ipc_init(void)
{
    int i;

    ipc_port = CreateMsgPort();
    if (!ipc_port) return -1;

    ipc_port->mp_Node.ln_Name = (char *)BRIDGE_PORT_NAME;
    ipc_port->mp_Node.ln_Pri = 0;
    AddPort(ipc_port);

    for (i = 0; i < IPC_MSG_POOL_SIZE; i++) {
        msg_pool_used[i] = FALSE;
    }

    return 0;
}

void ipc_cleanup(void)
{
    if (ipc_port) {
        struct Message *msg;

        RemPort(ipc_port);

        /* Drain and reply any pending messages */
        while ((msg = GetMsg(ipc_port)) != NULL) {
            struct BridgeMsg *bm = (struct BridgeMsg *)msg;
            bm->result = -1;
            ReplyMsg(msg);
        }

        DeleteMsgPort(ipc_port);
        ipc_port = NULL;
    }
}

ULONG ipc_get_signal(void)
{
    if (!ipc_port) return 0;
    return 1UL << ipc_port->mp_SigBit;
}

/*
 * Process all pending IPC messages from clients.
 * Each message is dispatched based on type, then replied.
 */
void ipc_process(void)
{
    struct Message *msg;

    while ((msg = GetMsg(ipc_port)) != NULL) {
        struct BridgeMsg *bm = (struct BridgeMsg *)msg;
        int cid;
        struct ClientEntry *ce;
        char logbuf[UI_MAX_LOG_LEN];

        bm->result = 0;

        printf("[IPC] msg type=%d\n", (int)bm->type);

        switch (bm->type) {
        case ABMSG_REGISTER:
            printf("[IPC] REGISTER from '%s'\n", bm->data);
            cid = client_register(bm->data, bm->msg.mn_ReplyPort);
            if (cid >= 0) {
                bm->clientId = (ULONG)cid;
                bm->result = 0;
                printf("[IPC] Registered id=%d, count=%d\n", cid, client_count());
                strcpy(logbuf, "+Client: ");
                strncat(logbuf, bm->data, UI_MAX_LOG_LEN - 12);
                logbuf[UI_MAX_LOG_LEN - 1] = '\0';
                ui_add_log(logbuf);
            } else {
                printf("[IPC] REGISTER failed\n");
                bm->result = -1;
            }
            ReplyMsg(msg);
            /* If debugger requested "pause on launch", signal CTRL_E to
             * the newly registered client AFTER replying. The client's
             * ab_init() checks for CTRL_E and pauses before returning. */
            if (cid >= 0 && dbg_should_pause_on_launch()) {
                /* The client just sent us a message via its reply port.
                 * The reply port's SigTask is the client's task. */
                struct Task *clientTask = bm->msg.mn_ReplyPort->mp_SigTask;
                if (clientTask) {
                    printf("[IPC] Signaling CTRL_E for debug pause\n");
                    Signal(clientTask, SIGBREAKF_CTRL_E);
                }
            }
            g_ui_dirty = TRUE;
            break;

        case ABMSG_UNREGISTER:
            ce = client_find(bm->clientId);
            if (ce) {
                strcpy(logbuf, "-Client: ");
                strncat(logbuf, ce->name, UI_MAX_LOG_LEN - 12);
                logbuf[UI_MAX_LOG_LEN - 1] = '\0';
                ui_add_log(logbuf);
                client_unregister(bm->clientId);
            }
            ReplyMsg(msg);
            break;

        case ABMSG_LOG:
            ce = client_find(bm->clientId);
            if (ce) {
                /* data format: "level|message" */
                int level = AB_INFO;
                const char *logmsg = bm->data;
                if (bm->dataLen > 2 && bm->data[1] == '|') {
                    level = bm->data[0] - '0';
                    logmsg = bm->data + 2;
                }
                protocol_send_log(ce->name, level, 0, logmsg);
                ce->msgCount++;

                /* Show in UI */
                strncpy(logbuf, logmsg, UI_MAX_LOG_LEN - 1);
                logbuf[UI_MAX_LOG_LEN - 1] = '\0';
                ui_add_log(logbuf);
            }
            ReplyMsg(msg);
            break;

        case ABMSG_VAR_REGISTER:
            ce = client_find(bm->clientId);
            if (ce) {
                /* data format: "name|type" */
                char *vsep = strchr(bm->data, '|');
                if (vsep) {
                    char vname[34];
                    int vnlen = (int)(vsep - bm->data);
                    int vtype;
                    if (vnlen > 33) vnlen = 33;
                    strncpy(vname, bm->data, vnlen);
                    vname[vnlen] = '\0';
                    vtype = (int)strtol(vsep + 1, NULL, 10);
                    client_add_var(ce, vname, vtype);
                }
                ce->msgCount++;
            }
            ReplyMsg(msg);
            break;

        case ABMSG_VAR_UNREGISTER:
            ce = client_find(bm->clientId);
            if (ce) {
                client_remove_var(ce, bm->data);
                ce->msgCount++;
            }
            ReplyMsg(msg);
            break;

        case ABMSG_VAR_PUSH:
            ce = client_find(bm->clientId);
            if (ce) {
                /* data format: "name|type|value" */
                char *sep1;
                char *sep2;
                char namebuf[34];
                int vtype = AB_TYPE_I32;
                const char *val = "";

                sep1 = strchr(bm->data, '|');
                if (sep1) {
                    int nlen = (int)(sep1 - bm->data);
                    if (nlen > 33) nlen = 33;
                    strncpy(namebuf, bm->data, nlen);
                    namebuf[nlen] = '\0';
                    vtype = sep1[1] - '0';
                    sep2 = strchr(sep1 + 1, '|');
                    if (sep2) val = sep2 + 1;
                    protocol_send_var(ce->name, namebuf, vtype, val);
                }
                ce->msgCount++;
            }
            ReplyMsg(msg);
            break;

        case ABMSG_HEARTBEAT:
            ce = client_find(bm->clientId);
            if (ce) {
                ULONG chip, fast;
                sys_avail_mem(&chip, &fast);
                protocol_send_heartbeat(0, chip, fast);
                ce->msgCount++;
                ce->lastTick++;
            }
            ReplyMsg(msg);
            break;

        case ABMSG_MEM_DUMP:
            ce = client_find(bm->clientId);
            if (ce) {
                /* data holds hex address and size as "addr|size" */
                /* extData points to memory, extDataLen is size */
                if (bm->extData && bm->extDataLen > 0) {
                    protocol_send_mem(bm->extData, bm->extDataLen,
                                      (const UBYTE *)bm->extData);
                }
                ce->msgCount++;
            }
            ReplyMsg(msg);
            break;

        case ABMSG_HOOK_REGISTER:
            ce = client_find(bm->clientId);
            if (ce) {
                /* data format: "name|description" */
                char *hsep = strchr(bm->data, '|');
                if (hsep) {
                    char hname[34];
                    int hnlen = (int)(hsep - bm->data);
                    if (hnlen > 33) hnlen = 33;
                    strncpy(hname, bm->data, hnlen);
                    hname[hnlen] = '\0';
                    client_add_hook(ce, hname, hsep + 1);
                }
                ce->msgCount++;
            }
            ReplyMsg(msg);
            break;

        case ABMSG_HOOK_UNREGISTER:
            ce = client_find(bm->clientId);
            if (ce) {
                client_remove_hook(ce, bm->data);
                ce->msgCount++;
            }
            ReplyMsg(msg);
            break;

        case ABMSG_MEMREG_REGISTER:
            ce = client_find(bm->clientId);
            if (ce) {
                /* data format: "name|addr_hex|size|description" */
                char *s1 = strchr(bm->data, '|');
                if (s1) {
                    char mrname[34];
                    int mnlen = (int)(s1 - bm->data);
                    ULONG mraddr, mrsize;
                    char *s2, *s3;
                    const char *mrdesc = "";

                    if (mnlen > 33) mnlen = 33;
                    strncpy(mrname, bm->data, mnlen);
                    mrname[mnlen] = '\0';

                    mraddr = strtoul(s1 + 1, NULL, 16);
                    s2 = strchr(s1 + 1, '|');
                    if (s2) {
                        mrsize = strtoul(s2 + 1, NULL, 10);
                        s3 = strchr(s2 + 1, '|');
                        if (s3) mrdesc = s3 + 1;
                    } else {
                        mrsize = 0;
                    }
                    client_add_memreg(ce, mrname, mraddr, mrsize, mrdesc);
                }
                ce->msgCount++;
            }
            ReplyMsg(msg);
            break;

        case ABMSG_MEMREG_UNREGISTER:
            ce = client_find(bm->clientId);
            if (ce) {
                client_remove_memreg(ce, bm->data);
                ce->msgCount++;
            }
            ReplyMsg(msg);
            break;

        case ABMSG_RESOURCE_LIST:
            /* Client is sending resource list (response to query) */
            ce = client_find(bm->clientId);
            if (ce) {
                static char resbuf[BRIDGE_MAX_LINE];
                sprintf(resbuf, "RESOURCES|%s|%s", ce->name, bm->data);
                resbuf[BRIDGE_MAX_LINE - 1] = '\0';
                protocol_send_raw(resbuf);
                ce->msgCount++;
            }
            ReplyMsg(msg);
            break;

        case ABMSG_PERF_DATA:
            /* Client is sending perf data (response to query) */
            ce = client_find(bm->clientId);
            if (ce) {
                static char perfbuf[BRIDGE_MAX_LINE];
                sprintf(perfbuf, "PERF|%s|%s", ce->name, bm->data);
                perfbuf[BRIDGE_MAX_LINE - 1] = '\0';
                protocol_send_raw(perfbuf);
                ce->msgCount++;
            }
            ReplyMsg(msg);
            break;

        case ABMSG_CMD_RESPONSE:
            ce = client_find(bm->clientId);
            if (ce) {
                /* data format: "status|response_data" */
                char *sep = strchr(bm->data, '|');
                if (sep) {
                    char status[16];
                    int slen = (int)(sep - bm->data);
                    if (slen > 15) slen = 15;
                    strncpy(status, bm->data, slen);
                    status[slen] = '\0';
                    protocol_send_cmd_response(bm->cmdId, status, sep + 1);
                } else {
                    protocol_send_cmd_response(bm->cmdId, bm->data, "");
                }
                ce->msgCount++;
            }
            ReplyMsg(msg);
            break;

        default:
            bm->result = -1;
            ReplyMsg(msg);
            break;
        }
    }
}

/*
 * Send a message to a specific client's reply port.
 * Uses pooled message, waits for reply to reclaim it.
 */
void ipc_send_to_client(struct MsgPort *replyPort, UWORD type,
                        ULONG clientId, ULONG cmdId,
                        const char *data, ULONG dataLen)
{
    struct BridgeMsg *bm;
    struct MsgPort *tempPort;

    if (!replyPort) return;

    bm = alloc_msg();
    if (!bm) return;

    tempPort = CreateMsgPort();
    if (!tempPort) {
        free_msg(bm);
        return;
    }

    bm->msg.mn_ReplyPort = tempPort;
    bm->msg.mn_Length = sizeof(struct BridgeMsg);
    bm->type = type;
    bm->clientId = clientId;
    bm->cmdId = cmdId;
    bm->result = 0;

    if (data && dataLen > 0) {
        if (dataLen > AB_MAX_DATA) dataLen = AB_MAX_DATA;
        memcpy(bm->data, data, dataLen);
        bm->dataLen = dataLen;
    } else {
        bm->dataLen = 0;
    }

    bm->extData = NULL;
    bm->extDataLen = 0;

    PutMsg(replyPort, (struct Message *)bm);

    /* Wait for reply with timeout to prevent hanging if client dies.
     * Poll up to ~2 seconds (20 x 100ms delays). */
    {
        struct Message *reply = NULL;
        int retries = 20;
        ULONG portSig = 1UL << tempPort->mp_SigBit;

        while (retries > 0) {
            /* Check if reply arrived */
            reply = GetMsg(tempPort);
            if (reply) break;

            /* Wait with timeout: use SetSignal + brief Delay.
             * Check signal first, then delay if not set. */
            if (SetSignal(0L, 0L) & portSig) {
                reply = GetMsg(tempPort);
                if (reply) break;
            }

            Delay(5); /* ~100ms (50 ticks/sec) */
            retries--;
        }

        if (!reply) {
            /* Timed out waiting for client reply.
             * The message is still in the client's port — we cannot
             * safely free it. Remove it from pool tracking so it
             * becomes a small permanent leak (better than a crash). */
            /* Mark pool slot as permanently used to prevent reuse
             * of a message that might still be referenced */
        } else {
            free_msg(bm);
        }
    }

    DeleteMsgPort(tempPort);
}
