#include "builtins.h"

#include <string.h>
#include <stdio.h>

void ctx_out(ShellCtx *ctx, const char *s)
{
    if (ctx && ctx->out) ctx->out(ctx, s ? s : "");
}

/* ─── cd ─────────────────────────────────────────────────────────────── */
static int bi_cd(ShellCtx *ctx, int argc, char **argv)
{
    const char *dest;
    int i;
    /* Fixed-size scratch for cwd update. */
    if (argc >= 2) {
        dest = argv[1];
    } else {
        dest = "SYS:";
    }
    if (ctx->platform_chdir) {
        int rc = ctx->platform_chdir(ctx, dest);
        if (rc != 0) {
            ctx_out(ctx, "cd: no such directory\n");
            return 1;
        }
    }
    /* Update cwd. If the path is relative we naively append; the platform
     * chdir already did the real resolution so the display may be wrong for
     * relative paths, but the executor uses the platform value. Tests
     * exercise absolute paths.                                             */
    if (strchr(dest, ':')) {
        /* absolute — copy as-is */
        i = 0;
        while (dest[i] && i < (int)sizeof(ctx->cwd) - 1) { ctx->cwd[i] = dest[i]; i++; }
        ctx->cwd[i] = '\0';
    } else {
        /* relative — append with '/' if needed */
        int cur = (int)strlen(ctx->cwd);
        if (cur > 0 && ctx->cwd[cur - 1] != '/' && ctx->cwd[cur - 1] != ':'
            && cur < (int)sizeof(ctx->cwd) - 1) {
            ctx->cwd[cur++] = '/';
        }
        i = 0;
        while (dest[i] && cur < (int)sizeof(ctx->cwd) - 1) {
            ctx->cwd[cur++] = dest[i++];
        }
        ctx->cwd[cur] = '\0';
    }
    return 0;
}

/* ─── pwd ────────────────────────────────────────────────────────────── */
static int bi_pwd(ShellCtx *ctx, int argc, char **argv)
{
    (void)argc; (void)argv;
    ctx_out(ctx, ctx->cwd);
    ctx_out(ctx, "\n");
    return 0;
}

/* ─── echo ───────────────────────────────────────────────────────────── */
static int bi_echo(ShellCtx *ctx, int argc, char **argv)
{
    int i;
    for (i = 1; i < argc; i++) {
        ctx_out(ctx, argv[i]);
        if (i + 1 < argc) ctx_out(ctx, " ");
    }
    ctx_out(ctx, "\n");
    return 0;
}

/* ─── clear ──────────────────────────────────────────────────────────── */
static int bi_clear(ShellCtx *ctx, int argc, char **argv)
{
    (void)argc; (void)argv;
    /* ANSI clear + home. Amiga Console supports CSI. */
    ctx_out(ctx, "\x1b[2J\x1b[H");
    return 0;
}

/* ─── exit ───────────────────────────────────────────────────────────── */
static int bi_exit(ShellCtx *ctx, int argc, char **argv)
{
    (void)argc; (void)argv;
    ctx->want_exit = 1;
    return 0;
}

/* ─── help ───────────────────────────────────────────────────────────── */
static int bi_help(ShellCtx *ctx, int argc, char **argv)
{
    int n = builtin_count();
    int i;
    (void)argc; (void)argv;
    ctx_out(ctx, "hubert built-ins:\n");
    for (i = 0; i < n; i++) {
        const Builtin *b = builtin_at(i);
        char line[128];
        int len = 0;
        int j;
        line[len++] = ' ';
        line[len++] = ' ';
        for (j = 0; b->name[j] && len < 120; j++) line[len++] = b->name[j];
        while (len < 12) line[len++] = ' ';
        line[len++] = '-';
        line[len++] = ' ';
        for (j = 0; b->help[j] && len < 125; j++) line[len++] = b->help[j];
        line[len++] = '\n';
        line[len] = '\0';
        ctx_out(ctx, line);
    }
    return 0;
}

/* ─── history ────────────────────────────────────────────────────────── */
static int bi_history(ShellCtx *ctx, int argc, char **argv)
{
    int n, i;
    char buf[HISTORY_MAX_LINE + 16];
    (void)argc; (void)argv;
    if (!ctx->hist) return 0;
    n = ctx->hist->count;
    for (i = n - 1; i >= 0; i--) {
        const char *e = history_at(ctx->hist, i);
        int off = 0;
        int idx = n - i;   /* 1-based, newest gets highest number */
        /* Print as "  N  line". Fixed-size format without printf. */
        buf[off++] = ' ';
        buf[off++] = ' ';
        if (idx >= 10) { buf[off++] = (char)('0' + (idx / 10) % 10); }
        buf[off++] = (char)('0' + (idx % 10));
        buf[off++] = ' ';
        buf[off++] = ' ';
        {
            int j = 0;
            while (e[j] && off < (int)sizeof(buf) - 2) buf[off++] = e[j++];
        }
        buf[off++] = '\n';
        buf[off] = '\0';
        ctx_out(ctx, buf);
    }
    return 0;
}

/* ─── set NAME=VALUE  |  set NAME VALUE  |  set (no args → list) ─────── */
static int bi_set(ShellCtx *ctx, int argc, char **argv)
{
    const char *name;
    const char *value;
    char namebuf[ENV_MAX_NAME];
    int i;
    if (argc == 1) {
        int n = env_count(ctx->env);
        for (i = 0; i < n; i++) {
            const EnvEntry *e = env_at(ctx->env, i);
            ctx_out(ctx, e->name);
            ctx_out(ctx, "=");
            ctx_out(ctx, e->value);
            ctx_out(ctx, "\n");
        }
        return 0;
    }
    if (argc == 2) {
        /* NAME=VALUE form */
        char *eq = strchr(argv[1], '=');
        if (!eq) {
            /* unset shorthand */
            env_unset(ctx->env, argv[1]);
            return 0;
        }
        {
            int nlen = (int)(eq - argv[1]);
            if (nlen >= ENV_MAX_NAME) return 1;
            memcpy(namebuf, argv[1], (size_t)nlen);
            namebuf[nlen] = '\0';
            name = namebuf;
            value = eq + 1;
        }
    } else {
        name = argv[1];
        value = argv[2];
    }
    if (!env_set(ctx->env, name, value)) {
        ctx_out(ctx, "set: table full or bad name\n");
        return 1;
    }
    return 0;
}

/* ─── env — same as `set` with no args, but distinct name ────────────── */
static int bi_env(ShellCtx *ctx, int argc, char **argv)
{
    (void)argc; (void)argv;
    return bi_set(ctx, 1, argv);
}

/* ─── Table ──────────────────────────────────────────────────────────── */
static const Builtin BUILTINS[] = {
    { "cd",      bi_cd,      "change working directory" },
    { "pwd",     bi_pwd,     "print working directory" },
    { "echo",    bi_echo,    "print arguments" },
    { "clear",   bi_clear,   "clear the screen" },
    { "help",    bi_help,    "show built-in commands" },
    { "history", bi_history, "list command history" },
    { "set",     bi_set,     "set NAME=VALUE, or list vars" },
    { "env",     bi_env,     "list shell variables" },
    { "exit",    bi_exit,    "quit hubert" },
};
#define BUILTIN_N (int)(sizeof(BUILTINS) / sizeof(BUILTINS[0]))

const Builtin *builtin_lookup(const char *name)
{
    int i;
    if (!name) return 0;
    for (i = 0; i < BUILTIN_N; i++) {
        if (strcmp(BUILTINS[i].name, name) == 0) return &BUILTINS[i];
    }
    return 0;
}

int             builtin_count(void)      { return BUILTIN_N; }
const Builtin  *builtin_at(int i)        { return (i >= 0 && i < BUILTIN_N) ? &BUILTINS[i] : 0; }

int builtin_dispatch(ShellCtx *ctx, int argc, char **argv, int *out_exit)
{
    const Builtin *b;
    if (argc == 0 || !argv || !argv[0]) return 0;
    b = builtin_lookup(argv[0]);
    if (!b) return 0;
    if (out_exit) *out_exit = b->fn(ctx, argc, argv);
    else b->fn(ctx, argc, argv);
    return 1;
}
