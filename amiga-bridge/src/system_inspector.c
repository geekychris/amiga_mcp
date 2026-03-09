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

/*
 * List all tasks (ready + waiting + current).
 * Format: TASKS|count|name1(pri1,state1),name2(pri2,state2),...
 */
int sys_list_tasks(char *buf, int bufSize)
{
    struct Node *node;
    int pos;
    int count = 0;
    char entry[80];

    /* Use Forbid/Permit instead of Disable/Enable - safer for sprintf calls */
    Forbid();

    sprintf(buf, "TASKS|");
    pos = strlen(buf);

    /* Current task */
    if (SysBase->ThisTask) {
        struct Task *t = SysBase->ThisTask;
        int written;
        sprintf(entry, "%s(%ld,run)",
                              t->tc_Node.ln_Name ? t->tc_Node.ln_Name : "?",
                              (long)t->tc_Node.ln_Pri);
        written = strlen(entry);
        if (pos + written < bufSize - 2) {
            strcpy(buf + pos, entry);
            pos += written;
            count++;
        }
    }

    /* Ready list */
    for (node = SysBase->TaskReady.lh_Head;
         node->ln_Succ != NULL;
         node = node->ln_Succ) {
        struct Task *t = (struct Task *)node;
        int written;
        sprintf(entry, "%s(%ld,ready)",
                              t->tc_Node.ln_Name ? t->tc_Node.ln_Name : "?",
                              (long)t->tc_Node.ln_Pri);
        written = strlen(entry);
        if (pos + written + 1 >= bufSize - 2) break;
        if (count > 0) buf[pos++] = ',';
        strcpy(buf + pos, entry);
        pos += written;
        count++;
    }

    /* Wait list */
    for (node = SysBase->TaskWait.lh_Head;
         node->ln_Succ != NULL;
         node = node->ln_Succ) {
        struct Task *t = (struct Task *)node;
        int written;
        sprintf(entry, "%s(%ld,wait)",
                              t->tc_Node.ln_Name ? t->tc_Node.ln_Name : "?",
                              (long)t->tc_Node.ln_Pri);
        written = strlen(entry);
        if (pos + written + 1 >= bufSize - 2) break;
        if (count > 0) buf[pos++] = ',';
        strcpy(buf + pos, entry);
        pos += written;
        count++;
    }

    Permit();

    /* Insert count after TASKS| */
    {
        char countbuf[16];
        char tmpbuf[BRIDGE_MAX_LINE];
        int clen;
        sprintf(countbuf, "%ld|", (long)count);
        clen = strlen(countbuf);
        /* Rebuild: "TASKS|count|entries" */
        strcpy(tmpbuf, buf + 6); /* save entries after "TASKS|" */
        strcpy(buf + 6, countbuf);
        strcpy(buf + 6 + clen, tmpbuf);
        pos += clen;
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
    int pos;
    int count = 0;
    char entry[80];

    Forbid();

    sprintf(buf, "LIBS|");
    pos = strlen(buf);

    for (node = SysBase->LibList.lh_Head;
         node->ln_Succ != NULL;
         node = node->ln_Succ) {
        struct Library *lib = (struct Library *)node;
        int written;
        sprintf(entry, "%s(v%ld.%ld)",
                              lib->lib_Node.ln_Name ? lib->lib_Node.ln_Name : "?",
                              (long)lib->lib_Version,
                              (long)lib->lib_Revision);
        written = strlen(entry);
        if (pos + written + 1 >= bufSize - 2) break;
        if (count > 0) buf[pos++] = ',';
        strcpy(buf + pos, entry);
        pos += written;
        count++;
    }

    Permit();

    {
        char countbuf[16];
        char tmpbuf[BRIDGE_MAX_LINE];
        int clen;
        sprintf(countbuf, "%ld|", (long)count);
        clen = strlen(countbuf);
        strcpy(tmpbuf, buf + 5);
        strcpy(buf + 5, countbuf);
        strcpy(buf + 5 + clen, tmpbuf);
        pos += clen;
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
    int pos;
    int count = 0;
    char entry[80];

    Forbid();

    sprintf(buf, "DEVICES|");
    pos = strlen(buf);

    for (node = SysBase->DeviceList.lh_Head;
         node->ln_Succ != NULL;
         node = node->ln_Succ) {
        struct Device *dev = (struct Device *)node;
        int written;
        sprintf(entry, "%s(v%ld.%ld)",
                              dev->dd_Library.lib_Node.ln_Name ?
                              dev->dd_Library.lib_Node.ln_Name : "?",
                              (long)dev->dd_Library.lib_Version,
                              (long)dev->dd_Library.lib_Revision);
        written = strlen(entry);
        if (pos + written + 1 >= bufSize - 2) break;
        if (count > 0) buf[pos++] = ',';
        strcpy(buf + pos, entry);
        pos += written;
        count++;
    }

    Permit();

    {
        char countbuf[16];
        char tmpbuf[BRIDGE_MAX_LINE];
        int clen;
        sprintf(countbuf, "%ld|", (long)count);
        clen = strlen(countbuf);
        strcpy(tmpbuf, buf + 8);
        strcpy(buf + 8, countbuf);
        strcpy(buf + 8 + clen, tmpbuf);
        pos += clen;
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
    int pos;
    int count = 0;

    sprintf(buf, "VOLUMES|");
    pos = strlen(buf);

    dl = LockDosList(LDF_VOLUMES | LDF_READ);
    while ((dl = NextDosEntry(dl, LDF_VOLUMES)) != NULL) {
        const char *name;
        char namebuf[108];
        int nlen;
        int written;

        /* BSTR name: first byte is length */
        if (dl->dol_Name) {
            UBYTE *bstr = (UBYTE *)BADDR(dl->dol_Name);
            nlen = bstr[0];
            if (nlen > 107) nlen = 107;
            CopyMem(bstr + 1, namebuf, nlen);
            namebuf[nlen] = ':';
            namebuf[nlen + 1] = '\0';
            name = namebuf;
        } else {
            name = "?:";
        }

        written = strlen(name);
        if (pos + written + 1 >= bufSize - 16) break;
        if (count > 0) buf[pos++] = ',';
        strcpy(buf + pos, name);
        pos += written;
        count++;
    }
    UnLockDosList(LDF_VOLUMES | LDF_READ);

    /* Insert count */
    {
        char countbuf[16];
        char tmpbuf[BRIDGE_MAX_LINE];
        int clen;
        sprintf(countbuf, "%ld|", (long)count);
        clen = strlen(countbuf);
        strcpy(tmpbuf, buf + 8);
        strcpy(buf + 8, countbuf);
        strcpy(buf + 8 + clen, tmpbuf);
        pos += clen;
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
 * Read memory at given address. Returns number of bytes read,
 * or -1 on error. We do a simple copy - on real hardware
 * accessing invalid addresses could cause a bus error,
 * but we trust the host to send valid addresses.
 */
int sys_inspect_mem(APTR addr, ULONG size, UBYTE *outBuf, ULONG outBufSize)
{
    ULONG copySize;

    if (!addr || size == 0) return -1;
    if (size > outBufSize) size = outBufSize;
    if (size > 256) size = 256;

    copySize = size;

    /* Allow all addresses - even low memory (ExecBase pointer at 0x4)
     * and hardware registers (0xBFxxxx CIA, 0xDFFxxx custom chips) */

    CopyMem(addr, outBuf, copySize);
    return (int)copySize;
}
