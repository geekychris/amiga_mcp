/*
 * process_launcher.c - Launch Amiga programs from host commands
 *
 * Uses SystemTags() to run commands with output capture.
 * All functions suppress system requesters (pr_WindowPtr = -1)
 * to prevent blocking the bridge daemon.
 */

#include <exec/types.h>
#include <exec/memory.h>
#include <dos/dos.h>
#include <dos/dosextens.h>
#include <dos/dostags.h>
#include <proto/exec.h>
#include <proto/dos.h>

#include <string.h>
#include <stdio.h>

#include "bridge_internal.h"

/* Output capture buffer */
#define PROC_OUTPUT_SIZE 480

/*
 * Check if a file/path exists.
 * Suppresses system requesters.
 * Returns 1 if exists, 0 if not.
 */
static int path_exists(const char *path)
{
    BPTR lock;
    struct Process *pr;
    APTR oldWinPtr;

    if (!path || path[0] == '\0') return 0;

    pr = (struct Process *)FindTask(NULL);
    oldWinPtr = pr->pr_WindowPtr;
    pr->pr_WindowPtr = (APTR)-1;

    lock = Lock((CONST_STRPTR)path, ACCESS_READ);

    pr->pr_WindowPtr = oldWinPtr;

    if (lock) {
        UnLock(lock);
        return 1;
    }
    return 0;
}

/*
 * Extract the executable path from a command string.
 * Handles "path arg1 arg2" -> "path"
 * and quoted paths like "\"path with spaces\" args" -> "path with spaces"
 * Writes into outBuf, returns outBuf.
 */
static char *extract_exe_path(const char *command, char *outBuf, int bufSize)
{
    int i = 0;

    if (!command || !outBuf) {
        outBuf[0] = '\0';
        return outBuf;
    }

    /* Skip leading whitespace */
    while (*command == ' ' || *command == '\t') command++;

    if (*command == '"') {
        /* Quoted path */
        command++;
        while (*command && *command != '"' && i < bufSize - 1) {
            outBuf[i++] = *command++;
        }
    } else {
        /* Unquoted - take until space */
        while (*command && *command != ' ' && *command != '\t' && i < bufSize - 1) {
            outBuf[i++] = *command++;
        }
    }
    outBuf[i] = '\0';
    return outBuf;
}

/*
 * Launch a command using SystemTags().
 * Captures output to resultBuf (truncated to bufSize).
 * Returns the SystemTags return code, or -1 on error.
 *
 * WARNING: This BLOCKS the bridge until the command finishes.
 * Use proc_run_async() for long-running programs.
 */
int proc_launch(ULONG cmdId, const char *command, char *resultBuf, int bufSize)
{
    BPTR outFh;
    BPTR nilFh;
    LONG rc;
    char tmpName[64];
    char logbuf[UI_MAX_LOG_LEN];
    struct Process *pr;
    APTR oldWinPtr;

    if (!command || !resultBuf) return -1;

    /* Log launch */
    strncpy(logbuf, "Launch: ", 9);
    strncat(logbuf, command, UI_MAX_LOG_LEN - 10);
    logbuf[UI_MAX_LOG_LEN - 1] = '\0';
    ui_add_log(logbuf);

    /* Suppress system requesters to prevent blocking */
    pr = (struct Process *)FindTask(NULL);
    oldWinPtr = pr->pr_WindowPtr;
    pr->pr_WindowPtr = (APTR)-1;

    /* Create temp file for output capture */
    sprintf(tmpName, "T:ab_out_%lu", (unsigned long)cmdId);

    outFh = Open((CONST_STRPTR)tmpName, MODE_NEWFILE);
    if (!outFh) {
        pr->pr_WindowPtr = oldWinPtr;
        strcpy(resultBuf, "Cannot create output file");
        return -1;
    }

    nilFh = Open((CONST_STRPTR)"NIL:", MODE_OLDFILE);

    /* Run the command */
    rc = SystemTags((CONST_STRPTR)command,
                    SYS_Output, (ULONG)outFh,
                    SYS_Input, (ULONG)nilFh,
                    SYS_Asynch, FALSE,
                    NP_StackSize, 8192,
                    TAG_DONE);

    if (nilFh) Close(nilFh);
    Close(outFh);

    /* Restore requester */
    pr->pr_WindowPtr = oldWinPtr;

    /* Read captured output */
    {
        BPTR readFh = Open((CONST_STRPTR)tmpName, MODE_OLDFILE);
        if (readFh) {
            LONG bytesRead = Read(readFh, resultBuf, (LONG)(bufSize - 1));
            Close(readFh);
            if (bytesRead < 0) bytesRead = 0;
            resultBuf[bytesRead] = '\0';

            /* Replace newlines with semicolons for protocol */
            {
                int i;
                for (i = 0; resultBuf[i]; i++) {
                    if (resultBuf[i] == '\n') resultBuf[i] = ';';
                    if (resultBuf[i] == '\r') resultBuf[i] = ' ';
                }
                /* Trim trailing semicolons/spaces */
                i = strlen(resultBuf);
                while (i > 0 && (resultBuf[i-1] == ';' || resultBuf[i-1] == ' ')) {
                    resultBuf[--i] = '\0';
                }
            }
        } else {
            resultBuf[0] = '\0';
        }

        /* Clean up temp file */
        DeleteFile((CONST_STRPTR)tmpName);
    }

    /* Log result */
    sprintf(logbuf, "RC=%ld", (long)rc);
    ui_add_log(logbuf);

    return (int)rc;
}

/*
 * Launch a command asynchronously using SystemTags() with SYS_Asynch.
 * Does not capture output - the program runs independently.
 * Validates the executable path exists before launching.
 * Returns 0 on success, -1 on error.
 */
int proc_run_async(ULONG cmdId, const char *command, char *resultBuf, int bufSize)
{
    BPTR nilFh;
    LONG rc;
    char logbuf[UI_MAX_LOG_LEN];
    static char exePath[256];
    struct Process *pr;
    APTR oldWinPtr;

    if (!command || !resultBuf) return -1;

    /* Extract and validate the executable path */
    extract_exe_path(command, exePath, sizeof(exePath));
    if (exePath[0] == '\0') {
        strncpy(resultBuf, "Empty command", bufSize - 1);
        resultBuf[bufSize - 1] = '\0';
        return -1;
    }

    /* Check if the executable exists (skip for built-in commands) */
    if (strchr(exePath, ':') || strchr(exePath, '/')) {
        /* Looks like a path - validate it exists */
        if (!path_exists(exePath)) {
            sprintf(resultBuf, "Not found: %.200s", exePath);
            resultBuf[bufSize - 1] = '\0';
            return -1;
        }
    }

    strncpy(logbuf, "RunAsync: ", 11);
    strncat(logbuf, command, UI_MAX_LOG_LEN - 12);
    logbuf[UI_MAX_LOG_LEN - 1] = '\0';
    ui_add_log(logbuf);

    /* Suppress system requesters */
    pr = (struct Process *)FindTask(NULL);
    oldWinPtr = pr->pr_WindowPtr;
    pr->pr_WindowPtr = (APTR)-1;

    nilFh = Open((CONST_STRPTR)"NIL:", MODE_OLDFILE);

    /* Run the command asynchronously - returns immediately */
    rc = SystemTags((CONST_STRPTR)command,
                    SYS_Output, (ULONG)nilFh,
                    SYS_Input, 0,
                    SYS_Asynch, TRUE,
                    NP_StackSize, 8192,
                    TAG_DONE);

    /* Restore requester */
    pr->pr_WindowPtr = oldWinPtr;

    /* With SYS_Asynch=TRUE, do NOT close nilFh - the system owns it now.
     * rc == -1 means error, otherwise the process was started. */
    if (rc == -1) {
        if (nilFh) Close(nilFh);
        strcpy(resultBuf, "Failed to start process");
        return -1;
    }

    strncpy(resultBuf, "Started: ", bufSize - 1);
    strncat(resultBuf, command, bufSize - strlen(resultBuf) - 1);
    resultBuf[bufSize - 1] = '\0';
    return 0;
}
