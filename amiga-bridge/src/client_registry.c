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

struct ClientEntry *client_get_by_index(int index)
{
    int i;
    int count = 0;
    for (i = 0; i < AB_MAX_CLIENTS; i++) {
        if (clients[i].active) {
            if (count == index) return &clients[i];
            count++;
        }
    }
    return NULL;
}


int client_list(char *buf, int bufSize)
{
    int i;
    int pos = 0;
    char entry[48];

    buf[0] = '\0';

    for (i = 0; i < AB_MAX_CLIENTS; i++) {
        if (clients[i].active) {
            int written;
            sprintf(entry, "%s(%lu)",
                    clients[i].name,
                    (unsigned long)clients[i].clientId);
            written = strlen(entry);
            if (pos > 0 && pos + 1 + written >= bufSize - 1) break;
            if (pos > 0) buf[pos++] = ',';
            memcpy(buf + pos, entry, written);
            pos += written;
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
/* ---- Client metadata helpers ---- */

void client_add_var(struct ClientEntry *ce, const char *name, int type)
{
    int i;
    if (!ce || !name) return;

    /* Check for existing */
    for (i = 0; i < ce->varCount; i++) {
        if (strcmp(ce->vars[i].name, name) == 0) {
            ce->vars[i].type = type;
            return;
        }
    }

    if (ce->varCount >= AB_MAX_VARS) return;
    strncpy(ce->vars[ce->varCount].name, name, 33);
    ce->vars[ce->varCount].name[33] = '\0';
    ce->vars[ce->varCount].type = type;
    ce->varCount++;
}

void client_remove_var(struct ClientEntry *ce, const char *name)
{
    int i;
    if (!ce || !name) return;

    for (i = 0; i < ce->varCount; i++) {
        if (strcmp(ce->vars[i].name, name) == 0) {
            /* Shift remaining entries */
            if (i < ce->varCount - 1) {
                memmove(&ce->vars[i], &ce->vars[i + 1],
                        (ce->varCount - i - 1) * sizeof(struct ClientVarInfo));
            }
            ce->varCount--;
            return;
        }
    }
}

void client_add_hook(struct ClientEntry *ce, const char *name, const char *desc)
{
    int i;
    if (!ce || !name) return;

    /* Check for existing */
    for (i = 0; i < ce->hookCount; i++) {
        if (strcmp(ce->hooks[i].name, name) == 0) {
            strncpy(ce->hooks[i].description, desc ? desc : "", 63);
            ce->hooks[i].description[63] = '\0';
            return;
        }
    }

    if (ce->hookCount >= AB_MAX_HOOKS) return;
    strncpy(ce->hooks[ce->hookCount].name, name, 33);
    ce->hooks[ce->hookCount].name[33] = '\0';
    strncpy(ce->hooks[ce->hookCount].description, desc ? desc : "", 63);
    ce->hooks[ce->hookCount].description[63] = '\0';
    ce->hookCount++;
}

void client_remove_hook(struct ClientEntry *ce, const char *name)
{
    int i;
    if (!ce || !name) return;

    for (i = 0; i < ce->hookCount; i++) {
        if (strcmp(ce->hooks[i].name, name) == 0) {
            if (i < ce->hookCount - 1) {
                memmove(&ce->hooks[i], &ce->hooks[i + 1],
                        (ce->hookCount - i - 1) * sizeof(struct ClientHookInfo));
            }
            ce->hookCount--;
            return;
        }
    }
}

void client_add_memreg(struct ClientEntry *ce, const char *name,
                       ULONG addr, ULONG size, const char *desc)
{
    int i;
    if (!ce || !name) return;

    /* Check for existing */
    for (i = 0; i < ce->memregCount; i++) {
        if (strcmp(ce->memregs[i].name, name) == 0) {
            ce->memregs[i].addr = addr;
            ce->memregs[i].size = size;
            strncpy(ce->memregs[i].description, desc ? desc : "", 63);
            ce->memregs[i].description[63] = '\0';
            return;
        }
    }

    if (ce->memregCount >= AB_MAX_MEMREGIONS) return;
    strncpy(ce->memregs[ce->memregCount].name, name, 33);
    ce->memregs[ce->memregCount].name[33] = '\0';
    ce->memregs[ce->memregCount].addr = addr;
    ce->memregs[ce->memregCount].size = size;
    strncpy(ce->memregs[ce->memregCount].description, desc ? desc : "", 63);
    ce->memregs[ce->memregCount].description[63] = '\0';
    ce->memregCount++;
}

void client_remove_memreg(struct ClientEntry *ce, const char *name)
{
    int i;
    if (!ce || !name) return;

    for (i = 0; i < ce->memregCount; i++) {
        if (strcmp(ce->memregs[i].name, name) == 0) {
            if (i < ce->memregCount - 1) {
                memmove(&ce->memregs[i], &ce->memregs[i + 1],
                        (ce->memregCount - i - 1) * sizeof(struct ClientMemRegInfo));
            }
            ce->memregCount--;
            return;
        }
    }
}

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
            char entry[48];
            int written;
            sprintf(entry, "%s(%lu)",
                    clients[i].name,
                    (unsigned long)clients[i].clientId);
            written = strlen(entry);
            if (pos + 1 + written >= bufSize - 2) break;
            if (pos > entryStart) {
                buf[pos++] = ',';
            }
            memcpy(buf + pos, entry, written);
            pos += written;
        }
    }

    buf[pos] = '\0';
    return pos;
}
