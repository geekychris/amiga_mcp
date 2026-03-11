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
#include <dos/var.h>
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
    static char entry[128];
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

/*
 * Rename/move a file.
 * Returns 0 on success, -1 on error.
 */
int fs_rename(const char *oldPath, const char *newPath)
{
    if (!oldPath || !newPath) return -1;
    if (Rename((CONST_STRPTR)oldPath, (CONST_STRPTR)newPath)) return 0;
    return -1;
}

/*
 * Copy a file on the Amiga side (no round-trip through host).
 * Reads source in chunks and writes to destination.
 * Returns 0 on success, -1 on error.
 */
int fs_copy(const char *srcPath, const char *dstPath)
{
    BPTR srcFh, dstFh;
    static UBYTE copyBuf[512];
    LONG bytesRead;
    struct Process *pr;
    APTR oldWinPtr;

    if (!srcPath || !dstPath) return -1;

    pr = (struct Process *)FindTask(NULL);
    oldWinPtr = pr->pr_WindowPtr;
    pr->pr_WindowPtr = (APTR)-1;

    srcFh = Open((CONST_STRPTR)srcPath, MODE_OLDFILE);
    if (!srcFh) {
        pr->pr_WindowPtr = oldWinPtr;
        return -1;
    }

    dstFh = Open((CONST_STRPTR)dstPath, MODE_NEWFILE);
    if (!dstFh) {
        Close(srcFh);
        pr->pr_WindowPtr = oldWinPtr;
        return -1;
    }

    pr->pr_WindowPtr = oldWinPtr;

    while ((bytesRead = Read(srcFh, copyBuf, 512)) > 0) {
        LONG written = Write(dstFh, copyBuf, bytesRead);
        if (written != bytesRead) {
            Close(srcFh);
            Close(dstFh);
            return -1;
        }
    }

    Close(srcFh);
    Close(dstFh);

    return (bytesRead < 0) ? -1 : 0;
}

/*
 * Get or set protection bits.
 * setMode=0: read bits into *bits, return 0 on success
 * setMode=1: set bits from *bits, return 0 on success
 * Returns -1 on error.
 */
int fs_protect(const char *path, ULONG *bits, int setMode)
{
    struct Process *pr;
    APTR oldWinPtr;

    if (!path || !bits) return -1;

    pr = (struct Process *)FindTask(NULL);
    oldWinPtr = pr->pr_WindowPtr;
    pr->pr_WindowPtr = (APTR)-1;

    if (setMode) {
        /* Set protection bits */
        BOOL ok = SetProtection((CONST_STRPTR)path, (LONG)*bits);
        pr->pr_WindowPtr = oldWinPtr;
        return ok ? 0 : -1;
    } else {
        /* Read protection bits */
        BPTR lock = Lock((CONST_STRPTR)path, ACCESS_READ);
        pr->pr_WindowPtr = oldWinPtr;

        if (!lock) return -1;
        {
            struct FileInfoBlock *fib = (struct FileInfoBlock *)
                AllocMem(sizeof(struct FileInfoBlock), MEMF_PUBLIC | MEMF_CLEAR);
            if (!fib) {
                UnLock(lock);
                return -1;
            }
            if (Examine(lock, fib)) {
                *bits = (ULONG)fib->fib_Protection;
                FreeMem(fib, sizeof(struct FileInfoBlock));
                UnLock(lock);
                return 0;
            }
            FreeMem(fib, sizeof(struct FileInfoBlock));
            UnLock(lock);
        }
        return -1;
    }
}

/*
 * Set file comment (filenote).
 * Returns 0 on success, -1 on error.
 */
int fs_set_comment(const char *path, const char *comment)
{
    if (!path || !comment) return -1;
    if (SetComment((CONST_STRPTR)path, (CONST_STRPTR)comment)) return 0;
    return -1;
}

/*
 * Compute CRC32 checksum of a file.
 * Returns 0 on success, -1 on error.
 * Sets *crc32Out and *sizeOut.
 */
int fs_checksum(const char *path, ULONG *crc32Out, ULONG *sizeOut)
{
    BPTR fh;
    static UBYTE chkBuf[512];
    LONG bytesRead;
    ULONG crc = 0xFFFFFFFF;
    ULONG fileSize = 0;
    struct Process *pr;
    APTR oldWinPtr;

    /* CRC32 lookup table (standard polynomial 0xEDB88320) */
    static const ULONG crcTable[16] = {
        0x00000000, 0x1DB71064, 0x3B6E20C8, 0x26D930AC,
        0x76DC4190, 0x6B6B51F4, 0x4DB26158, 0x5005713C,
        0xEDB88320, 0xF00F9344, 0xD6D6A3E8, 0xCB61B38C,
        0x9B64C2B0, 0x86D3D2D4, 0xA00AE278, 0xBDBDF21C
    };

    if (!path || !crc32Out || !sizeOut) return -1;

    pr = (struct Process *)FindTask(NULL);
    oldWinPtr = pr->pr_WindowPtr;
    pr->pr_WindowPtr = (APTR)-1;

    fh = Open((CONST_STRPTR)path, MODE_OLDFILE);
    pr->pr_WindowPtr = oldWinPtr;

    if (!fh) return -1;

    while ((bytesRead = Read(fh, chkBuf, 512)) > 0) {
        LONG i;
        for (i = 0; i < bytesRead; i++) {
            crc = crcTable[(crc ^ chkBuf[i]) & 0x0F] ^ (crc >> 4);
            crc = crcTable[(crc ^ (chkBuf[i] >> 4)) & 0x0F] ^ (crc >> 4);
        }
        fileSize += (ULONG)bytesRead;
    }

    Close(fh);

    if (bytesRead < 0) return -1;

    *crc32Out = crc ^ 0xFFFFFFFF;
    *sizeOut = fileSize;
    return 0;
}

/*
 * Append data to an existing file.
 * Returns 0 on success, -1 on error.
 */
int fs_append(const char *path, const UBYTE *data, ULONG size)
{
    BPTR fh;
    LONG written;

    if (!path || !data) return -1;

    fh = Open((CONST_STRPTR)path, MODE_OLDFILE);
    if (!fh) return -1;

    /* Seek to end */
    Seek(fh, 0, OFFSET_END);

    written = Write(fh, (APTR)data, (LONG)size);
    Close(fh);

    if (written != (LONG)size) return -1;
    return 0;
}

/*
 * Get an environment variable.
 * If archive=1, read from ENVARC:name as a file instead of using GetVar().
 * Returns 0 on success, -1 on error.
 */
int fs_get_env(const char *name, int archive, char *buf, int bufSize)
{
    if (!name || !buf || bufSize <= 0) return -1;

    buf[0] = '\0';

    if (archive) {
        /* Read from ENVARC:name as a file */
        static char envpath[300];
        BPTR fh;
        LONG bytesRead;
        struct Process *pr;
        APTR oldWinPtr;

        sprintf(envpath, "ENVARC:%s", name);

        pr = (struct Process *)FindTask(NULL);
        oldWinPtr = pr->pr_WindowPtr;
        pr->pr_WindowPtr = (APTR)-1;

        fh = Open((CONST_STRPTR)envpath, MODE_OLDFILE);
        pr->pr_WindowPtr = oldWinPtr;

        if (!fh) return -1;

        bytesRead = Read(fh, buf, (LONG)(bufSize - 1));
        Close(fh);

        if (bytesRead < 0) return -1;
        buf[bytesRead] = '\0';

        /* Strip trailing newline if present */
        if (bytesRead > 0 && buf[bytesRead - 1] == '\n') {
            buf[bytesRead - 1] = '\0';
        }
        return 0;
    } else {
        /* Use GetVar() from dos.library */
        LONG result = GetVar((CONST_STRPTR)name, (STRPTR)buf,
                             (LONG)(bufSize - 1), 0);
        if (result < 0) return -1;
        buf[result] = '\0';
        return 0;
    }
}

/*
 * Set an environment variable.
 * If archive=1, also write to ENVARC:name for persistence.
 * Returns 0 on success, -1 on error.
 */
int fs_set_env(const char *name, const char *value, int archive)
{
    LONG result;

    if (!name || !value) return -1;

    /* Set in ENV: (volatile) */
    result = SetVar((CONST_STRPTR)name, (CONST_STRPTR)value,
                    (LONG)strlen(value), GVF_GLOBAL_ONLY);
    if (!result) return -1;

    if (archive) {
        /* Also write to ENVARC: for persistence */
        static char envpath[300];
        BPTR fh;
        LONG written;

        sprintf(envpath, "ENVARC:%s", name);
        fh = Open((CONST_STRPTR)envpath, MODE_NEWFILE);
        if (!fh) return -1;

        written = Write(fh, (APTR)value, (LONG)strlen(value));
        Close(fh);

        if (written != (LONG)strlen(value)) return -1;
    }

    return 0;
}

/*
 * Set the datestamp on a file.
 * Uses SetFileDate() from dos.library.
 * Returns 0 on success, -1 on error.
 */
int fs_set_date(const char *path, LONG days, LONG mins, LONG ticks)
{
    struct DateStamp ds;
    struct Process *pr;
    APTR oldWinPtr;
    BOOL ok;

    if (!path) return -1;

    ds.ds_Days = days;
    ds.ds_Minute = mins;
    ds.ds_Tick = ticks;

    pr = (struct Process *)FindTask(NULL);
    oldWinPtr = pr->pr_WindowPtr;
    pr->pr_WindowPtr = (APTR)-1;

    ok = SetFileDate((CONST_STRPTR)path, &ds);

    pr->pr_WindowPtr = oldWinPtr;

    return ok ? 0 : -1;
}
