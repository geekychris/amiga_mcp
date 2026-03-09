/*
 * fs_access.c - Filesystem access for AmigaBridge daemon
 *
 * Provides directory listing, file read/write, and file management
 * using AmigaOS Lock/Examine/ExNext API.
 */

#include <exec/types.h>
#include <exec/memory.h>
#include <dos/dos.h>
#include <dos/dosextens.h>
#include <dos/exall.h>
#include <proto/exec.h>
#include <proto/dos.h>

#include <string.h>
#include <stdio.h>

#include "bridge_internal.h"

/*
 * List directory contents.
 * Format: DIR|path|count|name1(size1,type1),name2(size2,type2),...
 * type: F=file, D=dir
 * Returns length written or -1 on error.
 */
int fs_list_dir(const char *path, char *buf, int bufSize)
{
    BPTR lock;
    struct FileInfoBlock *fib;
    int pos;
    int count = 0;
    char entry[128];
    BOOL ok;
    struct Process *pr;
    APTR oldWinPtr;

    if (!path || !buf) return -1;

    /* Disable "Please insert volume" requester to prevent blocking */
    pr = (struct Process *)FindTask(NULL);
    oldWinPtr = pr->pr_WindowPtr;
    pr->pr_WindowPtr = (APTR)-1;

    lock = Lock((CONST_STRPTR)path, ACCESS_READ);

    /* Restore requester */
    pr->pr_WindowPtr = oldWinPtr;

    if (!lock) return -1;

    fib = (struct FileInfoBlock *)AllocDosObject(DOS_FIB, NULL);
    if (!fib) {
        UnLock(lock);
        return -1;
    }

    ok = Examine(lock, fib);
    if (!ok) {
        FreeDosObject(DOS_FIB, fib);
        UnLock(lock);
        return -1;
    }

    sprintf(buf, "DIR|%s|", path);
    pos = strlen(buf);

    /* Reserve 5 chars for count, then fill entries */
    {
        int headerPos = pos;
        char countStr[16];

        memset(buf + pos, ' ', 5);
        pos += 5;
        buf[pos] = '\0';

        while (ExNext(lock, fib)) {
            const char *typeStr;
            int written;

            if (fib->fib_DirEntryType > 0) {
                typeStr = "D";
            } else {
                typeStr = "F";
            }

            sprintf(entry, "%s(%ld,%s)",
                    fib->fib_FileName,
                    (long)fib->fib_Size,
                    typeStr);
            entry[sizeof(entry) - 1] = '\0';
            written = strlen(entry);

            if (pos + written + 1 >= bufSize - 2) break;

            if (count > 0) buf[pos++] = ',';
            memcpy(buf + pos, entry, written);
            pos += written;
            buf[pos] = '\0';
            count++;
        }

        /* Patch count into reserved space */
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
    }

    buf[pos] = '\0';

    FreeDosObject(DOS_FIB, fib);
    UnLock(lock);
    return pos;
}

/*
 * Read a chunk of a file.
 * Returns 0 on success, -1 on error.
 */
int fs_read_file(const char *path, ULONG offset, ULONG size,
                 UBYTE *buf, ULONG bufSize, ULONG *actualRead)
{
    BPTR fh;
    LONG seekResult;
    LONG bytesRead;
    struct Process *pr;
    APTR oldWinPtr;

    if (!path || !buf || !actualRead) return -1;
    if (size > bufSize) size = bufSize;

    pr = (struct Process *)FindTask(NULL);
    oldWinPtr = pr->pr_WindowPtr;
    pr->pr_WindowPtr = (APTR)-1;

    fh = Open((CONST_STRPTR)path, MODE_OLDFILE);

    pr->pr_WindowPtr = oldWinPtr;

    if (!fh) return -1;

    if (offset > 0) {
        seekResult = Seek(fh, (LONG)offset, OFFSET_BEGINNING);
        if (seekResult == -1) {
            Close(fh);
            return -1;
        }
    }

    bytesRead = Read(fh, buf, (LONG)size);
    Close(fh);

    if (bytesRead < 0) return -1;

    *actualRead = (ULONG)bytesRead;
    return 0;
}

/*
 * Write data to a file at given offset.
 * Creates the file if it doesn't exist.
 * Returns 0 on success, -1 on error.
 */
int fs_write_file(const char *path, ULONG offset,
                  const UBYTE *data, ULONG size)
{
    BPTR fh;
    LONG seekResult;
    LONG written;

    if (!path || !data) return -1;

    if (offset > 0) {
        /* Open existing file for read/write */
        fh = Open((CONST_STRPTR)path, MODE_OLDFILE);
    } else {
        /* Create or truncate */
        fh = Open((CONST_STRPTR)path, MODE_NEWFILE);
    }

    if (!fh) return -1;

    if (offset > 0) {
        seekResult = Seek(fh, (LONG)offset, OFFSET_BEGINNING);
        if (seekResult == -1) {
            Close(fh);
            return -1;
        }
    }

    written = Write(fh, (APTR)data, (LONG)size);
    Close(fh);

    if (written != (LONG)size) return -1;
    return 0;
}

/*
 * Get file info.
 * Format: FILEINFO|path|size|type|protection|comment
 * Returns length written or -1 on error.
 */
int fs_file_info(const char *path, char *buf, int bufSize)
{
    BPTR lock;
    struct FileInfoBlock *fib;
    BOOL ok;
    int pos;
    const char *typeStr;
    struct Process *pr;
    APTR oldWinPtr;

    if (!path || !buf) return -1;

    pr = (struct Process *)FindTask(NULL);
    oldWinPtr = pr->pr_WindowPtr;
    pr->pr_WindowPtr = (APTR)-1;

    lock = Lock((CONST_STRPTR)path, ACCESS_READ);

    pr->pr_WindowPtr = oldWinPtr;

    if (!lock) return -1;

    fib = (struct FileInfoBlock *)AllocMem(sizeof(struct FileInfoBlock),
                                           MEMF_PUBLIC | MEMF_CLEAR);
    if (!fib) {
        UnLock(lock);
        return -1;
    }

    ok = Examine(lock, fib);
    UnLock(lock);

    if (!ok) {
        FreeMem(fib, sizeof(struct FileInfoBlock));
        return -1;
    }

    if (fib->fib_DirEntryType > 0) {
        typeStr = "DIR";
    } else {
        typeStr = "FILE";
    }

    sprintf(buf, "FILEINFO|");
    strncat(buf, path, bufSize - 64);
    {
        char meta[64];
        sprintf(meta, "|%ld|%s|%08lx|",
                (long)fib->fib_Size,
                typeStr,
                (unsigned long)fib->fib_Protection);
        strncat(buf, meta, bufSize - strlen(buf) - 1);
    }
    strncat(buf, fib->fib_Comment, bufSize - strlen(buf) - 1);
    buf[bufSize - 1] = '\0';
    pos = strlen(buf);

    FreeMem(fib, sizeof(struct FileInfoBlock));

    return pos;
}

/*
 * Delete a file.
 * Returns 0 on success, -1 on error.
 */
int fs_delete(const char *path)
{
    if (!path) return -1;
    if (DeleteFile((CONST_STRPTR)path)) return 0;
    return -1;
}

/*
 * Create a directory.
 * Returns 0 on success, -1 on error.
 */
int fs_makedir(const char *path)
{
    BPTR lock;

    if (!path) return -1;

    lock = CreateDir((CONST_STRPTR)path);
    if (lock) {
        UnLock(lock);
        return 0;
    }
    return -1;
}
