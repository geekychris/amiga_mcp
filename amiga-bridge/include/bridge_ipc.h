#ifndef BRIDGE_IPC_H
#define BRIDGE_IPC_H

#include <exec/types.h>
#include <exec/ports.h>

#define BRIDGE_PORT_NAME "AMIGABRIDGE"

/* Message types client->daemon */
#define ABMSG_REGISTER        1
#define ABMSG_UNREGISTER      2
#define ABMSG_LOG             3
#define ABMSG_VAR_REGISTER    4
#define ABMSG_VAR_UNREGISTER  5
#define ABMSG_VAR_PUSH        6
#define ABMSG_HEARTBEAT       7
#define ABMSG_MEM_DUMP        8
#define ABMSG_CMD_RESPONSE    9
#define ABMSG_HOOK_REGISTER   10
#define ABMSG_HOOK_UNREGISTER 11
#define ABMSG_MEMREG_REGISTER   12
#define ABMSG_MEMREG_UNREGISTER 13
#define ABMSG_RESOURCE_LIST     14
#define ABMSG_PERF_DATA         15

/* Message types daemon->client */
#define ABMSG_CMD_FORWARD  20
#define ABMSG_VAR_GET      21
#define ABMSG_VAR_SET      22
#define ABMSG_SHUTDOWN     23
#define ABMSG_HOOK_CALL    24
#define ABMSG_GET_RESOURCES 25
#define ABMSG_GET_PERF      26

/* Log levels */
#define AB_DEBUG 0
#define AB_INFO  1
#define AB_WARN  2
#define AB_ERROR 3

/* Variable types */
#define AB_TYPE_I32 0
#define AB_TYPE_U32 1
#define AB_TYPE_STR 2
#define AB_TYPE_F32 3
#define AB_TYPE_PTR 4

#define AB_MAX_DATA 256
#define AB_MAX_CLIENTS 16
#define AB_MAX_VARS 32
#define AB_MAX_HOOKS 16
#define AB_MAX_MEMREGIONS 8
#define AB_MAX_RESOURCES 64
#define AB_MAX_PERF_SECTIONS 8
#define AB_PERF_FRAME_HISTORY 60

struct BridgeMsg {
    struct Message  msg;        /* Standard exec message (has reply port) */
    UWORD           version;    /* Protocol version = 1 */
    UWORD           type;       /* ABMSG_* */
    ULONG           clientId;   /* Assigned by daemon on REGISTER */
    ULONG           cmdId;      /* For request/response matching */
    LONG            result;     /* 0=ok, negative=error */
    ULONG           dataLen;
    char            data[AB_MAX_DATA];
    APTR            extData;
    ULONG           extDataLen;
};

#endif /* BRIDGE_IPC_H */
