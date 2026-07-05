#include "executor.h"

#include <string.h>

/* Real AmigaOS bits are only compiled in when we're targeting the emulator;
 * tests running on the host have HAVE_AMIGA_DOS undefined. The Makefile
 * defines it for the hubert target and leaves it off for the tests. */
#ifdef HAVE_AMIGA_DOS
#include <proto/dos.h>
#include <dos/dos.h>
#include <dos/dostags.h>
#endif

static ExecHook g_hook = 0;
static void    *g_hook_ud = 0;
static void    *g_out_fh  = 0;   /* BPTR for SYS_Output — set by executor_set_output */

void executor_set_hook(ExecHook fn, void *ud) { g_hook = fn; g_hook_ud = ud; }
void executor_clear_hook(void)                { g_hook = 0; g_hook_ud = 0; }
void executor_set_output(void *fh_bptr)       { g_out_fh = fh_bptr; }

int executor_join(char *dst, int dstSize, int argc, char **argv)
{
    int i, off = 0;
    if (dstSize <= 0) return -1;
    for (i = 0; i < argc; i++) {
        const char *a = argv[i];
        int need_quote = 0;
        int j;
        for (j = 0; a[j]; j++) if (a[j] == ' ' || a[j] == '\t') { need_quote = 1; break; }
        if (i > 0) {
            if (off + 1 >= dstSize) return -1;
            dst[off++] = ' ';
        }
        if (need_quote) {
            if (off + 1 >= dstSize) return -1;
            dst[off++] = '"';
        }
        for (j = 0; a[j]; j++) {
            if (off + 1 >= dstSize) return -1;
            dst[off++] = a[j];
        }
        if (need_quote) {
            if (off + 1 >= dstSize) return -1;
            dst[off++] = '"';
        }
    }
    dst[off] = '\0';
    return off;
}

int executor_run(ShellCtx *ctx, int argc, char **argv)
{
    char line[1024];
    int len;
    if (argc <= 0) return -1;
    len = executor_join(line, (int)sizeof(line), argc, argv);
    if (len < 0) {
        ctx_out(ctx, "hubert: command line too long\n");
        return -1;
    }
    if (g_hook) return g_hook(ctx, line, g_hook_ud);

#ifdef HAVE_AMIGA_DOS
    /* SystemTagList is synchronous. If main.c called executor_set_output
     * with the RAW: console handle, force child stdout there so `dir`,
     * `list`, `type`, etc. actually appear in the hubert window even
     * when we were spawned by `run` (which detaches stdout to NIL:). */
    if (g_out_fh) {
        return (int)SystemTags((CONST_STRPTR)line,
                               SYS_Output, (BPTR)g_out_fh,
                               SYS_Input,  0,
                               TAG_DONE);
    }
    return (int)SystemTagList((CONST_STRPTR)line, 0);
#else
    /* Host build with no hook set: fall back to a "command not found" style
     * response so tests that forget to install a hook fail loudly. */
    ctx_out(ctx, "hubert(host): no executor hook set for: ");
    ctx_out(ctx, line);
    ctx_out(ctx, "\n");
    return -1;
#endif
}
