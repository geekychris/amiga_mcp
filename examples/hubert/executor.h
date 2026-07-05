/*
 * executor.h — run external commands.
 *
 * On the real Amiga, this reassembles argv into a single command line and
 * hands it to SystemTagList(). In tests we swap in a stub via
 * executor_set_hook so we can assert what command was requested without
 * spawning anything.
 */
#ifndef HUBERT_EXECUTOR_H
#define HUBERT_EXECUTOR_H

#include "builtins.h"

/* Signature the test hook uses. Return the "exit code" of the fake command.
 * When set, the real SystemTagList path is skipped. */
typedef int (*ExecHook)(ShellCtx *ctx, const char *command_line, void *ud);

void executor_set_hook(ExecHook fn, void *ud);
void executor_clear_hook(void);

/* On the Amiga build only: set the file handle SystemTagList should use
 * as SYS_Output for child processes. Without this, external commands like
 * `dir` inherit whatever stdout our own CLI has — which is NIL: when
 * hubert was launched via `run`, so the output vanishes. main.c calls
 * this once after opening the RAW: console. Type is intentionally void*
 * so the header stays clean of exec/dos.h; the Amiga impl casts to BPTR. */
void executor_set_output(void *fh_bptr);

/* Run argv as an external command. Returns the exit code (or -1 on
 * assembly failure). */
int executor_run(ShellCtx *ctx, int argc, char **argv);

/* Reassemble argv[1..argc-1] joined by spaces (argv[0] is the program) into
 * dst. Requoting is naive but matches how the tests + real cmdline look.
 * Returns the length written (excluding NUL) or -1 on overflow. */
int executor_join(char *dst, int dstSize, int argc, char **argv);

#endif
