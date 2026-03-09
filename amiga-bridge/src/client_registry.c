/*
 * client_registry.c - Client tracking for AmigaBridge daemon
 *
 * Manages registered client applications.
 */

#include <exec/types.h>
#include <proto/exec.h>

#include <string.h>
#include <stdio.h>

#include "bridge_internal.h"

static struct ClientEntry clients[AB_MAX_CLIENTS];
static ULONG next_client_id = 1;

int client_register(const char *name, struct MsgPort *replyPort)
{
    int i;
    int slot = -1;

    if (!name || !replyPort) return -1;

    /* Check for duplicate name */
    for (i = 0; i < AB_MAX_CLIENTS; i++) {
        if (clients[i].active && strcmp(clients[i].name, name) == 0) {
            /* Already registered - update reply port */
            clients[i].replyPort = replyPort;
            return (int)clients[i].clientId;
        }
    }

    /* Find free slot */
    for (i = 0; i < AB_MAX_CLIENTS; i++) {
        if (!clients[i].active) {
            slot = i;
            break;
        }
    }

    if (slot < 0) return -1; /* Full */

    /* Clear the entire slot first */
    memset(&clients[slot], 0, sizeof(struct ClientEntry));

    clients[slot].active = TRUE;
    clients[slot].clientId = next_client_id++;
    strncpy(clients[slot].name, name, 33);
    clients[slot].name[33] = '\0';
    clients[slot].replyPort = replyPort;
    clients[slot].msgCount = 0;
    clients[slot].lastTick = 0;

    return (int)clients[slot].clientId;
}

void client_unregister(ULONG clientId)
{
    int i;
    for (i = 0; i < AB_MAX_CLIENTS; i++) {
        if (clients[i].active && clients[i].clientId == clientId) {
            printf("[CLIENT] UNREGISTER id=%lu name='%s'\n",
                   (unsigned long)clientId, clients[i].name);
            clients[i].active = FALSE;
            clients[i].replyPort = NULL;
            g_ui_dirty = TRUE;
            return;
        }
    }
}

/*
 * Debug: compact client state dump.
 * Starts with known marker (42) to detect serial corruption.
 */
void client_debug_dump(char *buf, int bufSize)
{
    int cc = 0;
    int i;

    for (i = 0; i < AB_MAX_CLIENTS; i++) {
        if (clients[i].active) cc++;
    }

    /* Use multiple small sprintf calls - use %ld not %d (amiga.lib reads %d as 16-bit) */
    sprintf(buf, "CDBG|42|cc=%ld", (long)cc);
    i = strlen(buf);
    sprintf(buf + i, "|sz=%ld", (long)sizeof(struct ClientEntry));
    i = strlen(buf);
    sprintf(buf + i, "|mx=%ld", (long)AB_MAX_CLIENTS);
    i = strlen(buf);
    sprintf(buf + i, "|a0=%ld", (long)clients[0].active);
    i = strlen(buf);
    sprintf(buf + i, "|id=%lu", (unsigned long)clients[0].clientId);
    i = strlen(buf);
}

struct ClientEntry *client_find(ULONG clientId)
{
    int i;
    for (i = 0; i < AB_MAX_CLIENTS; i++) {
        if (clients[i].active && clients[i].clientId == clientId) {
            return &clients[i];
        }
    }
    return NULL;
}

struct ClientEntry *client_find_by_name(const char *name)
{
    int i;
    if (!name) return NULL;
    for (i = 0; i < AB_MAX_CLIENTS; i++) {
        if (clients[i].active && strcmp(clients[i].name, name) == 0) {
            return &clients[i];
        }
    }
    return NULL;
}

int client_count(void)
{
    int i;
    int count = 0;
    for (i = 0; i < AB_MAX_CLIENTS; i++) {
        if (clients[i].active) count++;
    }
    return count;
}


int client_list(char *buf, int bufSize)
{
    int i;
    int pos = 0;

    buf[0] = '\0';

    for (i = 0; i < AB_MAX_CLIENTS; i++) {
        if (clients[i].active) {
            int written;
            if (pos > 0 && pos < bufSize - 1) {
                buf[pos++] = ',';
            }
            sprintf(buf + pos, "%s(%lu)",
                    clients[i].name,
                    (unsigned long)clients[i].clientId);
            pos += strlen(buf + pos);
            if (pos >= bufSize - 1) break;
        }
    }

    buf[pos] = '\0';
    return pos;
}

/*
 * Build the complete CLIENTS protocol line in one function,
 * avoiding separate count/list calls that showed stack issues.
 * Format: CLIENTS|count|name1(id1),name2(id2),...
 */
int client_build_line(char *buf, int bufSize)
{
    int i;
    int count = 0;
    int pos;
    int entryStart;

    /* First pass: count active clients */
    for (i = 0; i < AB_MAX_CLIENTS; i++) {
        if (clients[i].active) count++;
    }

    sprintf(buf, "CLIENTS|%ld|", (long)count);
    pos = strlen(buf);
    entryStart = pos;

    /* Second pass: build list */
    for (i = 0; i < AB_MAX_CLIENTS; i++) {
        if (clients[i].active) {
            int written;
            if (pos > entryStart && pos < bufSize - 2) {
                buf[pos++] = ',';
            }
            sprintf(buf + pos, "%s(%lu)",
                    clients[i].name,
                    (unsigned long)clients[i].clientId);
            pos += strlen(buf + pos);
            if (pos >= bufSize - 64) break;
        }
    }

    buf[pos] = '\0';
    return pos;
}
