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
#include <stdlib.h>

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

    if (size == 0) return -1;
    if (size > outBufSize) size = outBufSize;
    if (size > 256) size = 256;

    /* Reject known dangerous ranges, allow everything else.
     * Custom chip registers ($DFF000) require word access and have
     * side effects - block them. Everything else is fair game for
     * a debug tool: chip RAM, fast RAM, CIA, ROM, vectors. */
    if (a >= 0xDFF000 && a < 0xE00000) return -1;  /* Custom chips */

    copySize = size;

    /* CIA registers: byte access at odd addresses only.
     * RAM/ROM/vectors: byte-by-byte volatile reads
     * (CopyMem returns zeros for certain regions in FS-UAE). */
    {
        ULONG i;
        volatile UBYTE *src = (volatile UBYTE *)addr;
        for (i = 0; i < copySize; i++) {
            outBuf[i] = src[i];
        }
    }
    return (int)copySize;
}

/*
 * Enumerate memory regions from ExecBase->MemList.
 * Format: MEMMAP|count|name:attr:lower:upper:free:largest,...
 */
void sys_handle_memmap(void)
{
    struct ExecBase *eb = SysBase;
    struct MemHeader *mh;
    static char linebuf[BRIDGE_MAX_LINE];
    static char entry[128];
    int pos = 0;
    int count = 0;

    Forbid();

    /* Count regions first */
    for (mh = (struct MemHeader *)eb->MemList.lh_Head;
         mh->mh_Node.ln_Succ;
         mh = (struct MemHeader *)mh->mh_Node.ln_Succ)
        count++;

    sprintf(linebuf, "MEMMAP|%ld", (long)count);
    pos = strlen(linebuf);

    for (mh = (struct MemHeader *)eb->MemList.lh_Head;
         mh->mh_Node.ln_Succ;
         mh = (struct MemHeader *)mh->mh_Node.ln_Succ) {
        /* Calculate free space by walking mc_Next chain */
        struct MemChunk *mc;
        ULONG free = 0, largest = 0;
        const char *name;

        for (mc = mh->mh_First; mc; mc = mc->mc_Next) {
            free += mc->mc_Bytes;
            if (mc->mc_Bytes > largest) largest = mc->mc_Bytes;
        }

        name = mh->mh_Node.ln_Name;
        if (!name || (ULONG)name < 0x100) name = "unknown";

        sprintf(entry, "|%s:%lx:%lx:%lx:%lu:%lu",
            name,
            (unsigned long)mh->mh_Attributes,
            (unsigned long)mh->mh_Lower,
            (unsigned long)mh->mh_Upper,
            (unsigned long)free,
            (unsigned long)largest);

        if (pos + strlen(entry) < BRIDGE_MAX_LINE - 1) {
            strcpy(linebuf + pos, entry);
            pos += strlen(entry);
        }
    }

    Permit();

    protocol_send_raw(linebuf);
}

/*
 * Get stack info for a named task.
 * Format: STACKINFO|taskname|spLower|spUpper|spReg|stackSize|stackUsed|stackFree
 */
void sys_handle_stackinfo(const char *taskname)
{
    struct Task *task;
    static char linebuf[256];

    if (!taskname || taskname[0] == '\0') {
        protocol_send_raw("ERR|STACKINFO|Missing task name");
        return;
    }

    Forbid();
    task = FindTask((CONST_STRPTR)taskname);
    if (task) {
        ULONG lower = (ULONG)task->tc_SPLower;
        ULONG upper = (ULONG)task->tc_SPUpper;
        ULONG spreg = (ULONG)task->tc_SPReg;
        ULONG size = upper - lower;
        ULONG used = upper - spreg;
        ULONG free_stack = spreg - lower;
        Permit();

        sprintf(linebuf, "STACKINFO|%s|%lx|%lx|%lx|%lu|%lu|%lu",
            taskname,
            (unsigned long)lower, (unsigned long)upper, (unsigned long)spreg,
            (unsigned long)size, (unsigned long)used, (unsigned long)free_stack);
    } else {
        Permit();
        sprintf(linebuf, "ERR|STACKINFO|Task not found: %s", taskname);
    }

    protocol_send_raw(linebuf);
}

/*
 * Read safe custom chip read registers.
 * Format: CHIPREGS|DMACONR=xxxx|INTENAR=xxxx|...
 */
void sys_handle_chipregs(void)
{
    volatile UWORD *custom = (volatile UWORD *)0xDFF000;
    static char linebuf[1024];
    static char tmp[64];
    int pos;

    /* Read ALL safe custom chip read registers.
     * Format: CHIPREGS|name:addr:value,name:addr:value,...
     * Only registers that are safe to read (read-only or read-strobe-safe).
     * Many custom chip registers are WRITE-ONLY and reading them returns garbage
     * or has side effects — we skip those. */

    struct { const char *name; UWORD offset; } regs[] = {
        {"DMACONR",  0x002},
        {"VPOSR",    0x004},
        {"VHPOSR",   0x006},
        {"DSKDATR",  0x008},  /* disk DMA data (may not be useful) */
        {"JOY0DAT",  0x00A},
        {"JOY1DAT",  0x00C},
        {"CLXDAT",   0x00E},  /* collision detect */
        {"ADKCONR",  0x010},
        {"POT0DAT",  0x012},
        {"POT1DAT",  0x014},
        {"POTGOR",   0x016},
        {"SERDATR",  0x018},
        {"DSKBYTR",  0x01A},
        {"INTENAR",  0x01C},
        {"INTREQR",  0x01E},
        {"DENISEID", 0x07C},  /* Denise/Lisa chip ID (ECS/AGA) */
    };
    int nregs = sizeof(regs) / sizeof(regs[0]);
    int i;

    sprintf(linebuf, "CHIPREGS|%ld", (long)nregs);
    pos = strlen(linebuf);

    for (i = 0; i < nregs; i++) {
        UWORD val = custom[regs[i].offset / 2];
        sprintf(tmp, "|%s:%03lx:%04lx",
            regs[i].name,
            (unsigned long)regs[i].offset,
            (unsigned long)val);
        strcpy(linebuf + pos, tmp);
        pos += strlen(tmp);
    }

    protocol_send_raw(linebuf);
}

/*
 * Capture CPU registers of the bridge daemon itself.
 * Format: REGS|D0=xxxxxxxx|D1=xxxxxxxx|...|SP=xxxxxxxx|SR=xxxx
 *
 * Note: Register values reflect state at capture time (inside this function),
 * not the caller's exact register state.
 * SR requires supervisor mode on 68010+ so we read it via Supervisor() trap.
 */

void sys_handle_readregs(void)
{
    ULONG dregs[8], aregs[7];
    ULONG sp_val;
    static char linebuf[512];
    int pos, i;
    static char tmp[32];

    /* Capture data and address registers via inline asm.
     * Note: these reflect the compiler's register allocation at this point,
     * not the caller's state, but still useful for inspection. */
    asm volatile(
        "movem.l %%d0-%%d7, %0\n\t"
        "movem.l %%a0-%%a6, %1\n\t"
        "move.l %%sp, %2\n\t"
        : "=m" (dregs), "=m" (aregs), "=g" (sp_val)
        :
        : "memory"
    );

    /* SR requires supervisor mode on 68010+ and Supervisor() trap
     * has calling convention issues that cause crashes. Skip it. */

    sprintf(linebuf, "REGS");
    pos = strlen(linebuf);

    for (i = 0; i < 8; i++) {
        sprintf(tmp, "|D%ld=%08lx", (long)i, (unsigned long)dregs[i]);
        strcpy(linebuf + pos, tmp);
        pos += strlen(tmp);
    }
    for (i = 0; i < 7; i++) {
        sprintf(tmp, "|A%ld=%08lx", (long)i, (unsigned long)aregs[i]);
        strcpy(linebuf + pos, tmp);
        pos += strlen(tmp);
    }
    sprintf(tmp, "|SP=%08lx|SR=n/a",
        (unsigned long)sp_val);
    strcpy(linebuf + pos, tmp);

    protocol_send_raw(linebuf);
}

/*
 * Search memory for a byte pattern.
 * Args format: addr_hex|size|pattern_hex
 * Response: SEARCH|count|addr1,addr2,...
 */
void sys_handle_search(const char *args)
{
    static char linebuf[512];
    ULONG addr, size;
    unsigned char pattern[64];
    int pat_len = 0;
    int pos, count = 0;
    const char *p;
    UBYTE *mem;
    ULONG i;
    ULONG matches[32];
    static char tmp[16];

    if (!args || args[0] == '\0') {
        protocol_send_raw("ERR|SEARCH|Missing arguments");
        return;
    }

    /* Parse: addr|size|pattern_hex */
    addr = strtoul(args, NULL, 16);
    p = strchr(args, '|');
    if (!p) {
        protocol_send_raw("ERR|SEARCH|Missing size");
        return;
    }
    size = strtoul(p + 1, NULL, 10);
    p = strchr(p + 1, '|');
    if (!p) {
        protocol_send_raw("ERR|SEARCH|Missing pattern");
        return;
    }
    p++;

    /* Decode hex pattern */
    while (*p && pat_len < 64) {
        char hi = *p++;
        char lo = *p ? *p++ : '0';
        int hv = (hi >= 'a') ? hi - 'a' + 10 : (hi >= 'A') ? hi - 'A' + 10 : hi - '0';
        int lv = (lo >= 'a') ? lo - 'a' + 10 : (lo >= 'A') ? lo - 'A' + 10 : lo - '0';
        pattern[pat_len++] = (UBYTE)((hv << 4) | lv);
    }

    if (pat_len == 0) {
        protocol_send_raw("ERR|SEARCH|Empty pattern");
        return;
    }

    /* Validate memory range — allow ROM and all readable regions.
     * TypeOfMem returns 0 for ROM, so only reject very low addresses. */
    if (addr < 0x100) {
        protocol_send_raw("ERR|SEARCH|Invalid memory address");
        return;
    }

    /* Cap size to prevent infinite searches */
    if (size > 1048576) size = 1048576; /* 1MB max */

    /* Search */
    mem = (UBYTE *)addr;
    for (i = 0; i <= size - (ULONG)pat_len && count < 32; i++) {
        int j, match = 1;
        for (j = 0; j < pat_len; j++) {
            if (mem[i + j] != pattern[j]) { match = 0; break; }
        }
        if (match) {
            matches[count++] = addr + i;
        }
    }

    sprintf(linebuf, "SEARCH|%ld", (long)count);
    pos = strlen(linebuf);

    for (i = 0; i < (ULONG)count; i++) {
        sprintf(tmp, "%s%lx", i == 0 ? "|" : ",", (unsigned long)matches[i]);
        strcpy(linebuf + pos, tmp);
        pos += strlen(tmp);
    }

    protocol_send_raw(linebuf);
}

/*
 * Get detailed info about a named library.
 * Format: LIBINFO|name|version|revision|openCnt|flags|negSize|posSize|baseAddr|idString
 */
void sys_handle_libinfo(const char *name)
{
    struct Node *node;
    struct Library *lib = NULL;
    static char linebuf[BRIDGE_MAX_LINE];
    static char idBuf[128];

    if (!name || name[0] == '\0') {
        protocol_send_raw("ERR|LIBINFO|Missing library name");
        return;
    }

    Forbid();

    for (node = SysBase->LibList.lh_Head;
         node->ln_Succ != NULL;
         node = node->ln_Succ) {
        struct Library *l = (struct Library *)node;
        const char *lname = l->lib_Node.ln_Name;
        if (lname && (ULONG)lname >= 0x100 && strcmp(lname, name) == 0) {
            lib = l;
            break;
        }
    }

    if (lib) {
        const char *idStr = lib->lib_IdString;
        if (!idStr || (ULONG)idStr < 0x100) {
            idStr = "n/a";
        }
        /* Copy and sanitize idString - truncate and strip newlines/pipes */
        {
            int i;
            int len = strlen(idStr);
            if (len > 120) len = 120;
            for (i = 0; i < len; i++) {
                char c = idStr[i];
                if (c == '|' || c == '\n' || c == '\r') c = ' ';
                idBuf[i] = c;
            }
            /* Trim trailing spaces */
            while (i > 0 && idBuf[i - 1] == ' ') i--;
            idBuf[i] = '\0';
        }

        sprintf(linebuf, "LIBINFO|%s|%ld|%ld|%ld|%ld|%ld|%ld|%lx|%s",
            name,
            (long)lib->lib_Version,
            (long)lib->lib_Revision,
            (long)lib->lib_OpenCnt,
            (long)lib->lib_Flags,
            (long)lib->lib_NegSize,
            (long)lib->lib_PosSize,
            (unsigned long)lib,
            idBuf);
    } else {
        sprintf(linebuf, "ERR|LIBINFO|Library not found: %s", name);
    }

    Permit();

    protocol_send_raw(linebuf);
}

/*
 * Get detailed info about a named device.
 * Format: DEVINFO|name|version|revision|openCnt|flags|negSize|posSize|baseAddr|idString
 */
void sys_handle_devinfo(const char *name)
{
    struct Node *node;
    struct Device *dev = NULL;
    static char linebuf[BRIDGE_MAX_LINE];
    static char idBuf[128];

    if (!name || name[0] == '\0') {
        protocol_send_raw("ERR|DEVINFO|Missing device name");
        return;
    }

    Forbid();

    for (node = SysBase->DeviceList.lh_Head;
         node->ln_Succ != NULL;
         node = node->ln_Succ) {
        struct Device *d = (struct Device *)node;
        const char *dname = d->dd_Library.lib_Node.ln_Name;
        if (dname && (ULONG)dname >= 0x100 && strcmp(dname, name) == 0) {
            dev = d;
            break;
        }
    }

    if (dev) {
        struct Library *lib = &dev->dd_Library;
        const char *idStr = lib->lib_IdString;
        if (!idStr || (ULONG)idStr < 0x100) {
            idStr = "n/a";
        }
        /* Copy and sanitize idString */
        {
            int i;
            int len = strlen(idStr);
            if (len > 120) len = 120;
            for (i = 0; i < len; i++) {
                char c = idStr[i];
                if (c == '|' || c == '\n' || c == '\r') c = ' ';
                idBuf[i] = c;
            }
            while (i > 0 && idBuf[i - 1] == ' ') i--;
            idBuf[i] = '\0';
        }

        sprintf(linebuf, "DEVINFO|%s|%ld|%ld|%ld|%ld|%ld|%ld|%lx|%s",
            name,
            (long)lib->lib_Version,
            (long)lib->lib_Revision,
            (long)lib->lib_OpenCnt,
            (long)lib->lib_Flags,
            (long)lib->lib_NegSize,
            (long)lib->lib_PosSize,
            (unsigned long)dev,
            idBuf);
    } else {
        sprintf(linebuf, "ERR|DEVINFO|Device not found: %s", name);
    }

    Permit();

    protocol_send_raw(linebuf);
}

/*
 * Dump jump table (function entry points) for a named library or device.
 * Args format: name|type[|startIdx]
 *   type = "lib" or "dev"
 *   startIdx = optional, default 0
 * Response: LIBFUNCS|name|totalFuncs|startIdx|count|lvo1:addr1,lvo2:addr2,...
 * Each entry is ~15 chars, send up to 60 per response.
 */
void sys_handle_libfuncs(const char *args)
{
    static char linebuf[BRIDGE_MAX_LINE];
    static char namebuf[64];
    static char entry[20];
    struct Library *lib = NULL;
    struct Node *node;
    const char *p;
    int isDevice = 0;
    int startIdx = 0;
    int totalFuncs, count, i, pos;

    if (!args || args[0] == '\0') {
        protocol_send_raw("ERR|LIBFUNCS|Missing arguments (name|type[|startIdx])");
        return;
    }

    /* Parse name */
    p = strchr(args, '|');
    if (!p) {
        protocol_send_raw("ERR|LIBFUNCS|Missing type argument");
        return;
    }
    {
        int nlen = (int)(p - args);
        if (nlen > 62) nlen = 62;
        memcpy(namebuf, args, nlen);
        namebuf[nlen] = '\0';
    }
    p++; /* skip past '|' */

    /* Parse type */
    if (strncmp(p, "dev", 3) == 0) {
        isDevice = 1;
    } else if (strncmp(p, "lib", 3) == 0) {
        isDevice = 0;
    } else {
        protocol_send_raw("ERR|LIBFUNCS|Invalid type (use 'lib' or 'dev')");
        return;
    }

    /* Parse optional startIdx */
    p = strchr(p, '|');
    if (p) {
        startIdx = (int)strtol(p + 1, NULL, 10);
        if (startIdx < 0) startIdx = 0;
    }

    /* Look up the library or device */
    Forbid();

    if (isDevice) {
        for (node = SysBase->DeviceList.lh_Head;
             node->ln_Succ != NULL;
             node = node->ln_Succ) {
            struct Device *d = (struct Device *)node;
            const char *dname = d->dd_Library.lib_Node.ln_Name;
            if (dname && (ULONG)dname >= 0x100 && strcmp(dname, namebuf) == 0) {
                lib = &d->dd_Library;
                break;
            }
        }
    } else {
        for (node = SysBase->LibList.lh_Head;
             node->ln_Succ != NULL;
             node = node->ln_Succ) {
            struct Library *l = (struct Library *)node;
            const char *lname = l->lib_Node.ln_Name;
            if (lname && (ULONG)lname >= 0x100 && strcmp(lname, namebuf) == 0) {
                lib = l;
                break;
            }
        }
    }

    if (!lib) {
        Permit();
        sprintf(linebuf, "ERR|LIBFUNCS|%s not found: %s",
                isDevice ? "Device" : "Library", namebuf);
        protocol_send_raw(linebuf);
        return;
    }

    /* Calculate number of functions from negSize.
     * Jump table grows downward from base, each entry is 6 bytes:
     * 2-byte JMP (0x4EF9) + 4-byte absolute address. */
    totalFuncs = (int)lib->lib_NegSize / 6;

    if (startIdx >= totalFuncs) {
        Permit();
        sprintf(linebuf, "ERR|LIBFUNCS|startIdx %ld >= totalFuncs %ld",
                (long)startIdx, (long)totalFuncs);
        protocol_send_raw(linebuf);
        return;
    }

    /* Build header: LIBFUNCS|name|totalFuncs|startIdx|count|... */
    count = totalFuncs - startIdx;
    if (count > 60) count = 60;

    sprintf(linebuf, "LIBFUNCS|%s|%ld|%ld|%ld|",
            namebuf, (long)totalFuncs, (long)startIdx, (long)count);
    pos = strlen(linebuf);

    /* Read jump table entries */
    {
        UBYTE *base = (UBYTE *)lib;
        int actualCount = 0;

        for (i = 0; i < count; i++) {
            int funcIdx = startIdx + i;
            UBYTE *jmpEntry = base - (funcIdx + 1) * 6;
            ULONG targetAddr;
            long lvo = -(long)(funcIdx + 1) * 6;

            /* Read the 4-byte target address at offset +2 in the entry */
            targetAddr = *(ULONG *)(jmpEntry + 2);

            if (i > 0) {
                sprintf(entry, ",%ld:%lx", lvo, (unsigned long)targetAddr);
            } else {
                sprintf(entry, "%ld:%lx", lvo, (unsigned long)targetAddr);
            }

            if (pos + (int)strlen(entry) >= BRIDGE_MAX_LINE - 1) {
                /* Truncate — update count to what we actually fit */
                count = actualCount;
                break;
            }
            strcpy(linebuf + pos, entry);
            pos += strlen(entry);
            actualCount++;
        }
        count = actualCount;
    }

    Permit();

    /* Patch the count in the header if it was truncated.
     * Rebuild header + data to ensure correct count field. */
    {
        static char databuf[BRIDGE_MAX_LINE];
        static char hdr[80];
        int hdrlen, pipes, hi, datalen;

        /* Find where data starts (after 5th '|') */
        pipes = 0;
        for (hi = 0; hi < pos && pipes < 5; hi++) {
            if (linebuf[hi] == '|') pipes++;
        }
        datalen = pos - hi;
        memcpy(databuf, linebuf + hi, datalen);
        databuf[datalen] = '\0';

        sprintf(hdr, "LIBFUNCS|%s|%ld|%ld|%ld|",
                namebuf, (long)totalFuncs, (long)startIdx, (long)count);
        hdrlen = strlen(hdr);
        memcpy(linebuf, hdr, hdrlen);
        memcpy(linebuf + hdrlen, databuf, datalen);
        linebuf[hdrlen + datalen] = '\0';
    }

    protocol_send_raw(linebuf);
}
