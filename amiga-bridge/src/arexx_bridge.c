/*
 * arexx_bridge.c - Send ARexx commands to applications with ARexx ports
 *
 * Creates a reply port, builds RexxMsg structures, and sends them to
 * named ARexx ports on any running application. Uses non-blocking
 * poll pattern so the bridge main loop isn't blocked.
 */

#include <exec/types.h>
#include <exec/memory.h>
#include <exec/ports.h>
#include <exec/execbase.h>
#include <rexx/storage.h>
#include <rexx/rxslib.h>
#include <rexx/errors.h>
#include <proto/exec.h>
#include <proto/rexxsyslib.h>

#include <string.h>
#include <stdio.h>

#include "bridge_internal.h"

extern struct ExecBase *SysBase;
static struct MsgPort *g_replyPort = NULL;

/* Pending ARexx command state */
static struct RexxMsg *g_pendingMsg = NULL;
static ULONG g_pendingTimeout = 0;  /* ticks remaining */

/*
 * Initialize ARexx bridge.
 */
void arexx_init(void)
{
    RexxSysBase = (struct RxsLib *)OpenLibrary(
        (CONST_STRPTR)"rexxsyslib.library", 0);
    if (!RexxSysBase) {
        printf("ARexx: WARNING - rexxsyslib.library not available\n");
        return;
    }

    g_replyPort = CreateMsgPort();
    if (!g_replyPort) {
        printf("ARexx: WARNING - could not create reply port\n");
        CloseLibrary((struct Library *)RexxSysBase);
        RexxSysBase = NULL;
        return;
    }

    printf("ARexx: initialized\n");
}

/*
 * Cleanup ARexx resources.
 */
void arexx_cleanup(void)
{
    if (g_pendingMsg) {
        /* Can't safely free a message that's still at the target port.
         * Just abandon it. */
        g_pendingMsg = NULL;
    }
    if (g_replyPort) {
        struct Message *msg;
        while ((msg = GetMsg(g_replyPort)) != NULL) {
            ClearRexxMsg((struct RexxMsg *)msg, 1);
            DeleteRexxMsg((struct RexxMsg *)msg);
        }
        DeleteMsgPort(g_replyPort);
        g_replyPort = NULL;
    }
    if (RexxSysBase) {
        CloseLibrary((struct Library *)RexxSysBase);
        RexxSysBase = NULL;
    }
    printf("ARexx: cleaned up\n");
}

/*
 * Get the signal mask for the ARexx reply port.
 * Returns 0 if no port or no pending command.
 */
ULONG arexx_get_signal(void)
{
    if (g_replyPort && g_pendingMsg) {
        return 1UL << g_replyPort->mp_SigBit;
    }
    return 0;
}

/*
 * Poll for ARexx reply. Called from main loop when reply port signals
 * or on timer tick. Non-blocking.
 */
void arexx_poll(void)
{
    static char linebuf[BRIDGE_MAX_LINE];
    static char resultbuf[BRIDGE_MAX_LINE - 64];
    struct Message *reply;
    struct RexxMsg *rmsg;
    LONG rc;

    if (!g_pendingMsg || !g_replyPort) return;

    reply = GetMsg(g_replyPort);
    if (!reply) {
        /* Check timeout */
        if (g_pendingTimeout > 0) {
            g_pendingTimeout--;
        }
        if (g_pendingTimeout == 0) {
            printf("ARexx: command timed out\n");
            /* Message is still at target port - we can't safely free it.
             * Try to remove it from the target's port if still queued. */
            Forbid();
            Remove((struct Node *)g_pendingMsg);
            Permit();
            ClearRexxMsg(g_pendingMsg, 1);
            DeleteRexxMsg(g_pendingMsg);
            g_pendingMsg = NULL;
            protocol_send_raw("ERR|AREXX|Command timed out");
        }
        return;
    }

    /* Got a reply! */
    rmsg = g_pendingMsg;
    g_pendingMsg = NULL;

    rc = rmsg->rm_Result1;
    resultbuf[0] = '\0';

    printf("ARexx: got reply rc=%ld Result2=%ld\n",
           (long)rc, (long)rmsg->rm_Result2);

    if (rc == 0 && rmsg->rm_Result2) {
        STRPTR resstr = (STRPTR)rmsg->rm_Result2;
        int rlen = strlen((char *)resstr);
        int i;

        if (rlen > (int)(sizeof(resultbuf) - 1))
            rlen = (int)(sizeof(resultbuf) - 1);

        strncpy(resultbuf, (char *)resstr, rlen);
        resultbuf[rlen] = '\0';

        /* Sanitize for protocol transport */
        for (i = 0; i < rlen; i++) {
            if (resultbuf[i] == '|' || resultbuf[i] == '\n' ||
                resultbuf[i] == '\r') {
                resultbuf[i] = ' ';
            } else if (resultbuf[i] < 0x20 && resultbuf[i] != '\0') {
                resultbuf[i] = ' ';
            }
        }

        DeleteArgstring(resstr);
    }

    /* Clean up */
    ClearRexxMsg(rmsg, 1);
    DeleteRexxMsg(rmsg);

    sprintf(linebuf, "AREXXRESULT|%ld|%s", (long)rc, resultbuf);
    protocol_send_raw(linebuf);
}

/*
 * List public message ports.
 * Command: AREXXPORTS
 * Response: AREXXPORTS|count|port1,port2,...
 */
void arexx_handle_ports(void)
{
    static char linebuf[BRIDGE_MAX_LINE];
    static char portlist[BRIDGE_MAX_LINE - 64];
    struct Node *node;
    int count = 0;
    int pos = 0;

    portlist[0] = '\0';

    Forbid();
    for (node = SysBase->PortList.lh_Head;
         node->ln_Succ;
         node = node->ln_Succ)
    {
        if (node->ln_Name && node->ln_Name[0]) {
            int nlen = strlen(node->ln_Name);

            if (strcmp(node->ln_Name, "AMIGABRIDGE") == 0) continue;

            if (count > 0 && pos < (int)(sizeof(portlist) - 2)) {
                portlist[pos++] = ',';
            }
            if (pos + nlen < (int)(sizeof(portlist) - 1)) {
                int i;
                for (i = 0; i < nlen; i++) {
                    char ch = node->ln_Name[i];
                    if (ch == '|' || ch == ',') ch = '_';
                    portlist[pos++] = ch;
                }
                count++;
            }
        }
    }
    Permit();

    portlist[pos] = '\0';

    sprintf(linebuf, "AREXXPORTS|%ld|%s", (long)count, portlist);
    protocol_send_raw(linebuf);
}

/*
 * Send an ARexx command to a named port.
 * Command: AREXXSEND|port_name|command_string
 *
 * Non-blocking: sends the message and sets up g_pendingMsg.
 * The reply is picked up by arexx_poll() in the main loop.
 *
 * Response (via arexx_poll): AREXXRESULT|rc|result_string
 *                        or: ERR|AREXX|error_message
 */
void arexx_handle_send(const char *args)
{
    static char linebuf[BRIDGE_MAX_LINE];
    char portname[64];
    const char *command;
    const char *sep;
    struct MsgPort *targetPort;
    struct RexxMsg *rmsg;
    int namelen;

    if (!RexxSysBase || !g_replyPort) {
        protocol_send_raw("ERR|AREXX|rexxsyslib not available");
        return;
    }

    if (g_pendingMsg) {
        protocol_send_raw("ERR|AREXX|Another command is pending");
        return;
    }

    if (!args || !args[0]) {
        protocol_send_raw("ERR|AREXX|Usage: AREXXSEND|portname|command");
        return;
    }

    sep = strchr(args, '|');
    if (!sep || !sep[1]) {
        protocol_send_raw("ERR|AREXX|Usage: AREXXSEND|portname|command");
        return;
    }

    namelen = (int)(sep - args);
    if (namelen >= (int)sizeof(portname)) namelen = (int)sizeof(portname) - 1;
    strncpy(portname, args, namelen);
    portname[namelen] = '\0';

    command = sep + 1;

    printf("ARexx: sending '%s' to port '%s'\n", command, portname);

    /* Find the target port */
    Forbid();
    targetPort = FindPort((CONST_STRPTR)portname);
    Permit();

    if (!targetPort) {
        sprintf(linebuf, "ERR|AREXX|Port '%s' not found", portname);
        protocol_send_raw(linebuf);
        return;
    }

    /* Create a RexxMsg */
    rmsg = CreateRexxMsg(g_replyPort, NULL, NULL);
    if (!rmsg) {
        protocol_send_raw("ERR|AREXX|Could not create RexxMsg");
        return;
    }

    rmsg->rm_Args[0] = CreateArgstring(
        (CONST_STRPTR)command, strlen(command));
    if (!rmsg->rm_Args[0]) {
        DeleteRexxMsg(rmsg);
        protocol_send_raw("ERR|AREXX|Could not create argstring");
        return;
    }

    rmsg->rm_Action = RXCOMM | RXFF_RESULT;

    /* Send to target port */
    Forbid();
    targetPort = FindPort((CONST_STRPTR)portname);
    if (targetPort) {
        PutMsg(targetPort, (struct Message *)rmsg);
    }
    Permit();

    if (!targetPort) {
        ClearRexxMsg(rmsg, 1);
        DeleteRexxMsg(rmsg);
        sprintf(linebuf, "ERR|AREXX|Port '%s' disappeared", portname);
        protocol_send_raw(linebuf);
        return;
    }

    /* Store as pending - reply will be picked up by arexx_poll() */
    g_pendingMsg = rmsg;
    g_pendingTimeout = 50;  /* 50 * 200ms timer ticks = 10 seconds */

    printf("ARexx: message sent, waiting for reply...\n");
}
