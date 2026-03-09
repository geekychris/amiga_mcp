/*
 * process_launcher.c - Launch Amiga programs from host commands
 *
 * Uses SystemTags() to run commands with output capture.
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
 * Launch a command using SystemTags().
 * Captures output to resultBuf (truncated to bufSize).
 * Returns the SystemTags return code, or -1 on error.
 */
int proc_launch(ULONG cmdId, const char *command, char *resultBuf, int bufSize)
{
    BPTR outFh;
    BPTR nilFh;
    LONG rc;
    char tmpName[64];
    char logbuf[UI_MAX_LOG_LEN];

    if (!command || !resultBuf) return -1;

    /* Log launch */
    strncpy(logbuf, "Launch: ", 9);
    strncat(logbuf, command, UI_MAX_LOG_LEN - 10);
    logbuf[UI_MAX_LOG_LEN - 1] = '\0';
    ui_add_log(logbuf);

    /* Create temp file for output capture */
    sprintf(tmpName, "T:ab_out_%lu", (unsigned long)cmdId);

    outFh = Open((CONST_STRPTR)tmpName, MODE_NEWFILE);
    if (!outFh) {
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
 * Returns 0 on success, -1 on error.
 */
int proc_run_async(ULONG cmdId, const char *command, char *resultBuf, int bufSize)
{
    BPTR nilFh;
    LONG rc;
    char logbuf[UI_MAX_LOG_LEN];

    if (!command || !resultBuf) return -1;

    strncpy(logbuf, "RunAsync: ", 11);
    strncat(logbuf, command, UI_MAX_LOG_LEN - 12);
    logbuf[UI_MAX_LOG_LEN - 1] = '\0';
    ui_add_log(logbuf);

    nilFh = Open((CONST_STRPTR)"NIL:", MODE_OLDFILE);

    /* Run the command asynchronously - returns immediately */
    rc = SystemTags((CONST_STRPTR)command,
                    SYS_Output, (ULONG)nilFh,
                    SYS_Input, 0,
                    SYS_Asynch, TRUE,
                    NP_StackSize, 8192,
                    TAG_DONE);

    /* With SYS_Asynch=TRUE, do NOT close nilFh - the system owns it now.
     * rc == -1 means error, otherwise the process was started. */
    if (rc == -1) {
        if (nilFh) Close(nilFh);
        strcpy(resultBuf, "Failed to start process");
        return -1;
    }

    sprintf(resultBuf, "Started: %s", command);
    return 0;
}
