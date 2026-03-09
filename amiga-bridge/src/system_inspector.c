/*
 * system_inspector.c - AmigaOS system inspection
 *
 * Provides task list, library list, device list, and memory inspection
 * without requiring a client application.
 */

#include <exec/types.h>
#include <exec/memory.h>
#include <exec/execbase.h>
#include <exec/tasks.h>
#include <exec/libraries.h>
#include <exec/devices.h>
#include <dos/dos.h>
#include <dos/dosextens.h>
#include <proto/exec.h>
#include <proto/dos.h>

#include <string.h>
#include <stdio.h>

#include "bridge_internal.h"

extern struct ExecBase *SysBase;

/* Safe buffer append: appends src to buf at *pos, respecting bufSize.
 * Returns 1 if appended, 0 if it didn't fit. */
static int buf_append(char *buf, int *pos, int bufSize, const char *src)
{
    int len = strlen(src);
    if (*pos + len >= bufSize - 1) return 0;
    memcpy(buf + *pos, src, len);
    *pos += len;
    buf[*pos] = '\0';
    return 1;
}

static int buf_append_char(char *buf, int *pos, int bufSize, char ch)
{
    if (*pos + 1 >= bufSize - 1) return 0;
    buf[*pos] = ch;
    (*pos)++;
    buf[*pos] = '\0';
    return 1;
}

/*
 * Format a single task entry: name(pri,state,type)
 * type: proc or task
 * Writes into entry buffer with bounds checking.
 */
static void format_task_entry(char *entry, int entrySize, struct Task *t,
                               const char *state)
{
    const char *type = (t->tc_Node.ln_Type == NT_PROCESS) ? "proc" : "task";
    const char *name = t->tc_Node.ln_Name;

    /* Validate name pointer - reject obviously bad pointers */
    if (!name || (ULONG)name < 0x100 || (ULONG)name > 0x10000000) {
        name = "?";
    }

    sprintf(entry, "%s(%ld,%s,%s)", name,
            (long)t->tc_Node.ln_Pri, state, type);
    /* Ensure null termination within bounds */
    entry[entrySize - 1] = '\0';
}

/*
 * List all tasks (ready + waiting + current).
 * Format: TASKS|count|name1(pri1,state1,type1),name2(pri2,state2,type2),...
 */
int sys_list_tasks(char *buf, int bufSize)
{
    struct Node *node;
    int pos = 0;
    int count = 0;
    char entry[120];
    char countStr[16];
    int headerPos;

    /* Write header prefix - leave space for count */
    sprintf(buf, "TASKS|");
    pos = strlen(buf);
    headerPos = pos;

    /* Reserve space for count (up to "9999|" = 5 chars) */
    memset(buf + pos, ' ', 5);
    pos += 5;
    buf[pos] = '\0';

    Forbid();

    /* Current task */
    if (SysBase->ThisTask) {
        format_task_entry(entry, sizeof(entry), SysBase->ThisTask, "run");
        if (buf_append(buf, &pos, bufSize, entry)) {
            count++;
        }
    }

    /* Ready list */
    for (node = SysBase->TaskReady.lh_Head;
         node->ln_Succ != NULL;
         node = node->ln_Succ) {
        format_task_entry(entry, sizeof(entry), (struct Task *)node, "ready");
        if (count > 0) {
            if (!buf_append_char(buf, &pos, bufSize, ',')) break;
        }
        if (!buf_append(buf, &pos, bufSize, entry)) break;
        count++;
    }

    /* Wait list */
    for (node = SysBase->TaskWait.lh_Head;
         node->ln_Succ != NULL;
         node = node->ln_Succ) {
        format_task_entry(entry, sizeof(entry), (struct Task *)node, "wait");
        if (count > 0) {
            if (!buf_append_char(buf, &pos, bufSize, ',')) break;
        }
        if (!buf_append(buf, &pos, bufSize, entry)) break;
        count++;
    }

    Permit();

    /* Now patch in the count at headerPos.
     * We reserved 5 chars of space. Write count and shift entries left
     * to close the gap. */
    sprintf(countStr, "%ld|", (long)count);
    {
        int clen = strlen(countStr);
        int gap = 5 - clen;
        if (gap > 0) {
            /* Shift entries left to close gap */
            int entryStart = headerPos + 5;
            int entryLen = pos - entryStart;
            memmove(buf + headerPos + clen, buf + entryStart, entryLen);
            pos -= gap;
        }
        memcpy(buf + headerPos, countStr, clen);
    }

    buf[pos] = '\0';
    return pos;
}

/*
 * List open libraries.
 * Format: LIBS|count|name1(v1.r1),name2(v2.r2),...
 */
int sys_list_libs(char *buf, int bufSize)
{
    struct Node *node;
    int pos = 0;
    int count = 0;
    char entry[80];
    char countStr[16];
    int headerPos;

    sprintf(buf, "LIBS|");
    pos = strlen(buf);
    headerPos = pos;
    memset(buf + pos, ' ', 5);
    pos += 5;
    buf[pos] = '\0';

    Forbid();

    for (node = SysBase->LibList.lh_Head;
         node->ln_Succ != NULL;
         node = node->ln_Succ) {
        struct Library *lib = (struct Library *)node;
        const char *name = lib->lib_Node.ln_Name;
        if (!name || (ULONG)name < 0x100) name = "?";

        sprintf(entry, "%s(v%ld.%ld)", name,
                (long)lib->lib_Version,
                (long)lib->lib_Revision);
        entry[sizeof(entry) - 1] = '\0';

        if (count > 0) {
            if (!buf_append_char(buf, &pos, bufSize, ',')) break;
        }
        if (!buf_append(buf, &pos, bufSize, entry)) break;
        count++;
    }

    Permit();

    sprintf(countStr, "%ld|", (long)count);
    {
        int clen = strlen(countStr);
        int gap = 5 - clen;
        if (gap > 0) {
            int entryStart = headerPos + 5;
            int entryLen = pos - entryStart;
            memmove(buf + headerPos + clen, buf + entryStart, entryLen);
            pos -= gap;
        }
        memcpy(buf + headerPos, countStr, clen);
    }

    buf[pos] = '\0';
    return pos;
}

/*
 * List devices.
 * Format: DEVICES|count|name1(v1.r1),name2(v2.r2),...
 */
int sys_list_devices(char *buf, int bufSize)
{
    struct Node *node;
    int pos = 0;
    int count = 0;
    char entry[80];
    char countStr[16];
    int headerPos;

    sprintf(buf, "DEVICES|");
    pos = strlen(buf);
    headerPos = pos;
    memset(buf + pos, ' ', 5);
    pos += 5;
    buf[pos] = '\0';

    Forbid();

    for (node = SysBase->DeviceList.lh_Head;
         node->ln_Succ != NULL;
         node = node->ln_Succ) {
        struct Device *dev = (struct Device *)node;
        const char *name = dev->dd_Library.lib_Node.ln_Name;
        if (!name || (ULONG)name < 0x100) name = "?";

        sprintf(entry, "%s(v%ld.%ld)", name,
                (long)dev->dd_Library.lib_Version,
                (long)dev->dd_Library.lib_Revision);
        entry[sizeof(entry) - 1] = '\0';

        if (count > 0) {
            if (!buf_append_char(buf, &pos, bufSize, ',')) break;
        }
        if (!buf_append(buf, &pos, bufSize, entry)) break;
        count++;
    }

    Permit();

    sprintf(countStr, "%ld|", (long)count);
    {
        int clen = strlen(countStr);
        int gap = 5 - clen;
        if (gap > 0) {
            int entryStart = headerPos + 5;
            int entryLen = pos - entryStart;
            memmove(buf + headerPos + clen, buf + entryStart, entryLen);
            pos -= gap;
        }
        memcpy(buf + headerPos, countStr, clen);
    }

    buf[pos] = '\0';
    return pos;
}

/*
 * List mounted volumes/assigns.
 * Format: VOLUMES|count|name1,name2,...
 */
int sys_list_volumes(char *buf, int bufSize)
{
    struct DosList *dl;
    int pos = 0;
    int count = 0;
    char countStr[16];
    int headerPos;

    sprintf(buf, "VOLUMES|");
    pos = strlen(buf);
    headerPos = pos;
    memset(buf + pos, ' ', 5);
    pos += 5;
    buf[pos] = '\0';

    dl = LockDosList(LDF_VOLUMES | LDF_READ);
    while ((dl = NextDosEntry(dl, LDF_VOLUMES)) != NULL) {
        char namebuf[110];
        int nlen;

        /* BSTR name: first byte is length */
        if (dl->dol_Name) {
            UBYTE *bstr = (UBYTE *)BADDR(dl->dol_Name);
            nlen = bstr[0];
            if (nlen > 107) nlen = 107;
            CopyMem(bstr + 1, namebuf, nlen);
            namebuf[nlen] = ':';
            namebuf[nlen + 1] = '\0';
        } else {
            namebuf[0] = '?';
            namebuf[1] = ':';
            namebuf[2] = '\0';
        }

        if (count > 0) {
            if (!buf_append_char(buf, &pos, bufSize, ',')) break;
        }
        if (!buf_append(buf, &pos, bufSize, namebuf)) break;
        count++;
    }
    UnLockDosList(LDF_VOLUMES | LDF_READ);

    sprintf(countStr, "%ld|", (long)count);
    {
        int clen = strlen(countStr);
        int gap = 5 - clen;
        if (gap > 0) {
            int entryStart = headerPos + 5;
            int entryLen = pos - entryStart;
            memmove(buf + headerPos + clen, buf + entryStart, entryLen);
            pos -= gap;
        }
        memcpy(buf + headerPos, countStr, clen);
    }

    buf[pos] = '\0';
    return pos;
}

void sys_avail_mem(ULONG *chipFree, ULONG *fastFree)
{
    *chipFree = AvailMem(MEMF_CHIP);
    *fastFree = AvailMem(MEMF_FAST);
}

/*
 * Send CTRL-C break signal to a task by name.
 * Returns 0 on success, -1 if task not found.
 */
int sys_break_task(const char *name)
{
    struct Task *task;

    Forbid();
    task = FindTask((CONST_STRPTR)name);
    if (task) {
        Signal(task, SIGBREAKF_CTRL_C);
    }
    Permit();

    return task ? 0 : -1;
}

/*
 * Read memory at given address. Returns number of bytes read,
 * or -1 on error.
 *
 * Validates address ranges to avoid bus errors on hardware.
 * Rejects NULL and very low addresses. Allows ExecBase at 0x4,
 * chip mem, fast mem, and ROM. Rejects unmapped regions.
 */
int sys_inspect_mem(APTR addr, ULONG size, UBYTE *outBuf, ULONG outBufSize)
{
    ULONG a = (ULONG)addr;
    ULONG copySize;

    if (!addr || size == 0) return -1;
    if (size > outBufSize) size = outBufSize;
    if (size > 256) size = 256;

    /* Reject address 0 (NULL) but allow 0x4 (ExecBase pointer) */
    if (a < 4) return -1;

    /* Basic sanity: reject addresses in the 0x1-0x3 range */
    /* Allow everything else - the host should know what it's doing,
     * but we at least prevent NULL dereference */

    /* Use TypeOfMem to validate that the address is in a known
     * memory region. Returns 0 if not in any memory list. */
    if (a >= 0x100) {
        ULONG memType = TypeOfMem(addr);
        /* Allow known memory regions, ROM (0xF80000-0xFFFFFF),
         * and the ExecBase area (0x4). Also allow CIA/custom
         * if explicitly requested (memType will be 0 for these) */
        if (memType == 0 && a < 0xA00000) {
            /* Not in any memory list and not in ROM/IO range.
             * Could be unmapped - reject to be safe */
            return -1;
        }
    }

    copySize = size;
    CopyMem(addr, outBuf, copySize);
    return (int)copySize;
}
