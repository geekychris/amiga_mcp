/*
 * builtins.h — shell built-in commands.
 *
 * Kept as a dispatch table so tests can look up "is this a builtin?" and
 * execute deterministically. Each handler takes (ctx, argc, argv) and
 * returns an exit code; 0 == success.
 *
 * The io_out callback lets tests capture output without touching stdout.
 * Real Amiga side wires it to the Console window's write.
 */
#ifndef HUBERT_BUILTINS_H
#define HUBERT_BUILTINS_H

#include "env.h"
#include "history.h"

typedef struct ShellCtx {
    Env      *env;
    History  *hist;
    /* Working directory buffer, owned by the ctx. `cd` mutates it and
     * (on the Amiga app) actually calls CurrentDir(). Tests inspect it. */
    char      cwd[256];
    int       want_exit;   /* set by `exit` — main loop reads this */
    int       last_exit;   /* previous command's exit code */
    /* Output sink. If NULL, no output. */
    void    (*out)(struct ShellCtx *ctx, const char *s);
    void     *out_userdata;
    /* Optional: platform "chdir" implementation. Called by `cd` after the
     * cwd string is updated so the Amiga host can call CurrentDir(). If
     * NULL, `cd` only updates the internal cwd (used by tests). */
    int     (*platform_chdir)(struct ShellCtx *ctx, const char *path);
} ShellCtx;

typedef int (*BuiltinFn)(ShellCtx *ctx, int argc, char **argv);

typedef struct {
    const char *name;
    BuiltinFn   fn;
    const char *help;   /* one-line description for `help` */
} Builtin;

/* Look up a builtin by name; returns NULL if it isn't one. */
const Builtin *builtin_lookup(const char *name);

/* Iterate all builtins (for `help`). */
int             builtin_count(void);
const Builtin  *builtin_at(int i);

/* Convenience: dispatch by argv[0] if it's a builtin, run it, return the
 * exit code via *out_exit. Returns 1 if a builtin ran, 0 otherwise. */
int builtin_dispatch(ShellCtx *ctx, int argc, char **argv, int *out_exit);

/* Small helpers used by main.c + tests. */
void ctx_out(ShellCtx *ctx, const char *s);

#endif
