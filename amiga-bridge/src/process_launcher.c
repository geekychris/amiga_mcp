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

/* ---- Process tracking ---- */

#define MAX_TRACKED_PROCS 16

struct TrackedProc {
    BOOL   active;
    int    id;
    char   command[128];
    ULONG  startTick;
};

static struct TrackedProc g_procs[MAX_TRACKED_PROCS];
static int g_next_proc_id = 1;

/* Track a new async process. Called after proc_run_async succeeds. */
static int proc_track(const char *command)
{
    int i;
    for (i = 0; i < MAX_TRACKED_PROCS; i++) {
        if (!g_procs[i].active) {
            g_procs[i].active = TRUE;
            g_procs[i].id = g_next_proc_id++;
            strncpy(g_procs[i].command, command, 127);
            g_procs[i].command[127] = '\0';
            g_procs[i].startTick = *(volatile ULONG *)0xDFF004;
            return g_procs[i].id;
        }
    }
    return -1;
}

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

    {
        int trackId = proc_track(command);
        if (trackId > 0) {
            sprintf(resultBuf, "Started (proc %ld): ", (long)trackId);
        } else {
            strncpy(resultBuf, "Started: ", bufSize - 1);
        }
        strncat(resultBuf, command, bufSize - strlen(resultBuf) - 1);
        resultBuf[bufSize - 1] = '\0';
    }
    return 0;
}

void proc_init(void)
{
    int i;
    for (i = 0; i < MAX_TRACKED_PROCS; i++) {
        g_procs[i].active = FALSE;
    }
    g_next_proc_id = 1;
}

void proc_cleanup(void)
{
    /* Nothing to free - static buffers */
}

/*
 * List tracked processes.
 * Format: PROCLIST|count|id1:cmd1:status1,id2:cmd2:status2,...
 * status: running or exited
 */
int proc_list(char *buf, int bufSize)
{
    int pos;
    int count = 0;
    int i;
    static char entry[180];
    char countStr[16];
    int headerPos;

    sprintf(buf, "PROCLIST|");
    pos = strlen(buf);
    headerPos = pos;
    memset(buf + pos, ' ', 5);
    pos += 5;
    buf[pos] = '\0';

    for (i = 0; i < MAX_TRACKED_PROCS; i++) {
        if (!g_procs[i].active) continue;

        sprintf(entry, "%ld:%s:running",
                (long)g_procs[i].id,
                g_procs[i].command);
        entry[sizeof(entry) - 1] = '\0';

        if (count > 0) {
            if (pos + 1 >= bufSize - 1) break;
            buf[pos++] = ',';
        }
        {
            int elen = strlen(entry);
            if (pos + elen >= bufSize - 1) break;
            memcpy(buf + pos, entry, elen);
            pos += elen;
            buf[pos] = '\0';
        }
        count++;
    }

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
 * Get status of a specific tracked process.
 * Format: PROCSTAT|id|command|status
 */
int proc_stat(int procId, char *buf, int bufSize)
{
    int i;
    for (i = 0; i < MAX_TRACKED_PROCS; i++) {
        if (g_procs[i].active && g_procs[i].id == procId) {
            sprintf(buf, "PROCSTAT|%ld|%s|running",
                    (long)g_procs[i].id,
                    g_procs[i].command);
            return strlen(buf);
        }
    }
    sprintf(buf, "ERR|PROCSTAT|Process %ld not found", (long)procId);
    return -1;
}

/*
 * Send a signal to a tracked process.
 * sigType: 0=CTRL_C, 1=CTRL_D, 2=CTRL_E, 3=CTRL_F
 */
int proc_signal(int procId, ULONG sigType)
{
    int i;
    static const ULONG sigMasks[] = {
        SIGBREAKF_CTRL_C, SIGBREAKF_CTRL_D,
        SIGBREAKF_CTRL_E, SIGBREAKF_CTRL_F
    };

    if (sigType > 3) return -1;

    for (i = 0; i < MAX_TRACKED_PROCS; i++) {
        if (g_procs[i].active && g_procs[i].id == procId) {
            /* Extract executable name from command for FindTask */
            static char exeName[128];
            char *sp;
            strncpy(exeName, g_procs[i].command, 127);
            exeName[127] = '\0';
            /* Take just the filename part */
            sp = strrchr(exeName, '/');
            if (!sp) sp = strrchr(exeName, ':');
            if (sp) {
                memmove(exeName, sp + 1, strlen(sp + 1) + 1);
            }
            /* Trim args */
            sp = strchr(exeName, ' ');
            if (sp) *sp = '\0';

            {
                struct Task *task;
                Forbid();
                task = FindTask((CONST_STRPTR)exeName);
                if (task) {
                    Signal(task, sigMasks[sigType]);
                }
                Permit();
                return task ? 0 : -1;
            }
        }
    }
    return -1;
}

/* Remove a tracked process (call when we know it's done) */
void proc_remove(int procId)
{
    int i;
    for (i = 0; i < MAX_TRACKED_PROCS; i++) {
        if (g_procs[i].active && g_procs[i].id == procId) {
            g_procs[i].active = FALSE;
            return;
        }
    }
}
