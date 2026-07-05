/*
 * hubert — main REPL.
 *
 * A small interactive shell for AmigaOS. Structure:
 *   - Open a RAW: console so we get keypress-level input (no OS line edit).
 *   - Loop:
 *       print prompt, run the line editor until Enter, tokenize, dispatch
 *       (built-in first, else external via SystemTagList), remember in history.
 *
 * Every piece of interesting logic lives in the sibling modules so tests
 * can drive them directly; this file is the "wire it up" glue.
 */
#include <stdio.h>
#include <string.h>

#include <exec/types.h>
#include <proto/dos.h>
#include <proto/exec.h>
#include <dos/dos.h>
#include <dos/dostags.h>

#include "history.h"
#include "tokenizer.h"
#include "line_editor.h"
#include "env.h"
#include "builtins.h"
#include "executor.h"
#include "bridge_client.h"
#include "llm_direct.h"
#include "jsonx.h"

#define VERSION "0.4"

/* Compile-time defaults for the direct LLM connection. Override at build
 * time with -DLLM_HOST=\"...\" etc. */
#ifndef LLM_HOST
#define LLM_HOST "spark.hitorro.com"
#endif
#ifndef LLM_PORT
#define LLM_PORT 11434
#endif
#ifndef LLM_MODEL
/* llama3.3 has proper tool-calling training — nemotron 70B fabricates
 * tool output rather than emitting a tool_calls frame. Both are on the
 * same Ollama server; switch here to prefer whichever fits the task. */
#define LLM_MODEL "llama3.3"
#endif
#ifndef LLM_MAX_STEPS
#define LLM_MAX_STEPS 6
#endif

/* ─── Bridge integration ─────────────────────────────────────────────────
 *
 * When amiga-bridge is running we register as a client so the host can:
 *   - see every command line we run (as INFO log entries),
 *   - read the current working directory via a CVAR,
 *   - PUSH commands at us via the `run` hook, receiving stdout back as the
 *     hook result. That's what phase 3's agent loop uses to drive us.
 *
 * ab_init returning non-zero is not fatal: hubert still works as a plain
 * shell without a bridge running. All ab_* calls are cheap when disconnected.
 */
static int  g_bridge_ok = 0;
static char g_bridge_cwd[256];      /* CVAR mirror of ShellCtx.cwd */
static struct ShellCtx *g_ctx_ref = 0;   /* set once main() has one */

/* Capture buffer used by run() hook. We swap ShellCtx.out to point at a
 * function that fills this while a hook is executing so the host gets the
 * command's output as the hook result. */
static char g_capture[8192];
static int  g_capture_len;

static void capture_out(struct ShellCtx *ctx, const char *s)
{
    int i = 0;
    (void)ctx;
    while (s[i] && g_capture_len < (int)sizeof(g_capture) - 1) {
        g_capture[g_capture_len++] = s[i++];
    }
    g_capture[g_capture_len] = '\0';
}

/* Path used for redirecting external command stdout during hook_run.
 * SystemTagList doesn't route through ShellCtx.out (that only sees
 * built-ins), so we point stdout at a temp file and slurp it back. */
#define CAPTURE_TMP "T:hubert-hook.out"

/* Append the contents of CAPTURE_TMP to g_capture, then delete it. */
static void slurp_capture_file(void)
{
    BPTR fh;
    LONG n;
    char chunk[512];
    fh = Open((CONST_STRPTR)CAPTURE_TMP, MODE_OLDFILE);
    if (!fh) return;
    for (;;) {
        n = Read(fh, chunk, (LONG)sizeof(chunk));
        if (n <= 0) break;
        {
            LONG i;
            for (i = 0; i < n && g_capture_len < (int)sizeof(g_capture) - 1; i++) {
                g_capture[g_capture_len++] = chunk[i];
            }
            g_capture[g_capture_len] = '\0';
            if (g_capture_len >= (int)sizeof(g_capture) - 1) break;
        }
    }
    Close(fh);
    DeleteFile((CONST_STRPTR)CAPTURE_TMP);
}

/* AmigaOS Console CSI codes we care about for keys */

static BPTR g_console = 0;

/* Write a NUL-terminated string to the console. Small helper used
 * everywhere; ShellCtx.out points here. */
static void write_str(const char *s)
{
    LONG n;
    if (!g_console || !s) return;
    n = (LONG)strlen(s);
    if (n > 0) Write(g_console, (APTR)s, n);
}

static void ctx_out_cb(ShellCtx *ctx, const char *s)
{
    (void)ctx;
    write_str(s);
}

static int platform_chdir_impl(ShellCtx *ctx, const char *path)
{
    BPTR lock, prev;
    (void)ctx;
    lock = Lock((CONST_STRPTR)path, ACCESS_READ);
    if (!lock) return -1;
    prev = CurrentDir(lock);
    if (prev) UnLock(prev);
    return 0;
}

/* Fill the working-directory field from AmigaOS. Called once at startup and
 * after external commands (which can chdir on us via cd tools). */
static void refresh_cwd(ShellCtx *ctx)
{
    BPTR lock = Lock((CONST_STRPTR)"", ACCESS_READ);
    if (!lock) return;
    if (!NameFromLock(lock, (STRPTR)ctx->cwd, sizeof(ctx->cwd))) {
        strcpy(ctx->cwd, "?:");
    }
    UnLock(lock);
}

/* ─── Key parsing ─────────────────────────────────────────────────────── */

/* Read one raw key from the console and translate it into a KeyKind +
 * optional keycode. Handles CSI escape sequences from RAW:. Returns 1 if a
 * key was decoded, 0 on EOF.
 *
 * We *poll* the console with WaitForChar rather than blocking in Read, so
 * the outer loop can call ab_poll() and let host-driven CALLHOOKs (e.g.
 * hubert-drive run …) run while the user is idle at the prompt. On each
 * tick with no key we return the sentinel KEY_CHAR with keycode=0 so the
 * caller can service the bridge and try again. */
static int read_key(KeyKind *out_kind, char *out_char)
{
    UBYTE b;
    LONG n;

    /* 20ms poll — long enough that we don't burn CPU, short enough that
     * hook responses feel snappy. */
    if (!WaitForChar(g_console, 20000)) {
        *out_kind = KEY_CHAR;
        *out_char = 0;
        return 1;   /* "no key this tick", loop again after ab_poll */
    }

    n = Read(g_console, &b, 1);
    if (n <= 0) return 0;

    switch (b) {
    case 8:       *out_kind = KEY_BACKSPACE; return 1;
    case 127:     *out_kind = KEY_BACKSPACE; return 1;
    case 13: case 10:
                  *out_kind = KEY_ENTER;    return 1;
    case 3:       *out_kind = KEY_CLEAR_LINE; return 1;  /* Ctrl-C: cancel edit */
    case 9:       *out_kind = KEY_TAB;         return 1;  /* Tab: complete path */
    case 21:      *out_kind = KEY_CLEAR_LINE; return 1;  /* Ctrl-U */
    case 23:      *out_kind = KEY_KILL_WORD;  return 1;  /* Ctrl-W */
    case 0x9B: {
        /* CSI — read the sequence. On AmigaOS RAW:, cursor/function keys
         * come through as 0x9B (SS3-like) followed by a letter or digits+~. */
        UBYTE seq[8];
        int slen = 0;
        for (;;) {
            n = Read(g_console, &seq[slen], 1);
            if (n <= 0) break;
            slen++;
            if (slen >= (int)sizeof(seq)) break;
            /* Terminator: A-Z, a-z, ~ */
            if ((seq[slen - 1] >= 'A' && seq[slen - 1] <= 'Z') ||
                (seq[slen - 1] >= 'a' && seq[slen - 1] <= 'z') ||
                seq[slen - 1] == '~') break;
        }
        if (slen == 1) {
            switch (seq[0]) {
            case 'A': *out_kind = KEY_UP;    return 1;
            case 'B': *out_kind = KEY_DOWN;  return 1;
            case 'C': *out_kind = KEY_RIGHT; return 1;
            case 'D': *out_kind = KEY_LEFT;  return 1;
            }
        }
        /* Home/End on Amiga come as "Sh" prefixed; treat unknown sequences
         * as no-ops rather than confusing the editor. */
        *out_kind = KEY_CHAR; *out_char = 0;
        return 1;
    }
    default:
        if (b >= 32 && b < 127) { *out_kind = KEY_CHAR; *out_char = (char)b; return 1; }
        /* Silently drop other control bytes; they'd confuse the editor. */
        *out_kind = KEY_CHAR; *out_char = 0;
        return 1;
    }
}

/* ─── Rendering ───────────────────────────────────────────────────────── */

/* Redraw the current line without relying on ANSI CSI sequences — some
 * RAW: console builds on AmiKit ignore them, which leaves ghost chars from
 * the previous render underneath the current one. Instead we:
 *   1. \r (carriage return — cursor to column 0),
 *   2. write prompt + full line text,
 *   3. write spaces to erase leftover chars from any longer previous line,
 *   4. \b (backspace, non-destructive on RAW:) to walk the cursor back to
 *      the logical caret position.
 *
 * We remember the last-drawn width so step 3 only pads when we shrank —
 * no wasted output when the line grows.
 */
static void redraw(const char *prompt, const LineEditor *ed)
{
    static int prev_total = 0;
    int prompt_len = (int)strlen(prompt);
    int text_len   = (int)strlen(line_editor_text(ed));
    int total      = prompt_len + text_len;
    int caret_col  = prompt_len + line_editor_cursor(ed);
    int i;

    write_str("\r");
    write_str(prompt);
    write_str(line_editor_text(ed));

    if (prev_total > total) {
        for (i = 0; i < prev_total - total; i++) write_str(" ");
        for (i = 0; i < prev_total - total; i++) write_str("\b");
    }
    prev_total = total;

    /* Cursor is at column `total` — walk it back to caret_col with BS. */
    for (i = total - caret_col; i > 0; i--) write_str("\b");
}

/* ─── REPL ────────────────────────────────────────────────────────────── */

static void banner(void)
{
    write_str("\nhubert " VERSION " — type 'help' for built-ins, 'exit' to quit\n\n");
}

/* Forward decl — bi_ask lives after hook_llm_event (agent state and hook
 * are defined further down) but run_one dispatches to it. */
static int bi_ask(struct ShellCtx *ctx, int argc, char **argv);
static int bi_askreset(struct ShellCtx *ctx, int argc, char **argv);

static void run_one(ShellCtx *ctx, const char *line)
{
    TokenList toks;
    TokResult tr;
    int exit_code = 0;

    tr = tokenize(&toks, line);
    if (tr != TOK_OK) {
        write_str("hubert: bad quoting\n");
        ctx->last_exit = 2;
        return;
    }
    if (toks.argc == 0) return;

    /* Special case: `ask` / `askreset` live in main.c because they need
     * state that the pure builtins table doesn't have. Handle before the
     * regular dispatch. */
    if (strcmp(toks.argv[0], "ask") == 0) {
        exit_code = bi_ask(ctx, toks.argc, toks.argv);
        ctx->last_exit = exit_code;
    } else if (strcmp(toks.argv[0], "askreset") == 0) {
        exit_code = bi_askreset(ctx, toks.argc, toks.argv);
        ctx->last_exit = exit_code;
    } else if (builtin_dispatch(ctx, toks.argc, toks.argv, &exit_code)) {
        ctx->last_exit = exit_code;
    } else {
        exit_code = executor_run(ctx, toks.argc, toks.argv);
        ctx->last_exit = exit_code;
        /* External command may have moved us — resync display cwd. */
        refresh_cwd(ctx);
    }
    /* Mirror cwd + log every command to the bridge if we're connected. */
    if (g_bridge_ok) {
        int i = 0;
        while (ctx->cwd[i] && i < (int)sizeof(g_bridge_cwd) - 1) {
            g_bridge_cwd[i] = ctx->cwd[i]; i++;
        }
        g_bridge_cwd[i] = '\0';
        AB_I("cmd exit=%ld: %s", (long)exit_code, line);
    }
}

/* ─── Agent state (phase 3) ──────────────────────────────────────────── */
static int  g_agent_pending = 0;   /* 1 while a turn is streaming back */
static int  g_agent_session = 0;

static int starts_with(const char *s, const char *p)
{
    while (*p) { if (*s++ != *p++) return 0; }
    return 1;
}

/* ─── Bridge hook: host-driven command execution ─────────────────────────
 *
 * The host (or the LLM in phase 3) invokes this via CALLHOOK. `args` is a
 * single command line; we run it with output captured into a scratch buffer
 * that we hand back as the hook result. Returns 0 always — the exit code
 * lives in ctx->last_exit and gets shown at the top of the result.
 */
static int hook_run(const char *args, char *resultBuf, int bufSize)
{
    ShellCtx *ctx = g_ctx_ref;
    void (*saved_out)(struct ShellCtx *, const char *);
    int header_len;

    if (!ctx) {
        int n = 0;
        const char *msg = "shell not ready";
        while (msg[n] && n < bufSize - 1) { resultBuf[n] = msg[n]; n++; }
        resultBuf[n] = '\0';
        return 1;
    }
    saved_out = ctx->out;
    ctx->out = capture_out;
    g_capture_len = 0; g_capture[0] = '\0';

    /* Dispatch, but for external commands we want their stdout too — the
     * built-in path already writes through capture_out (ShellCtx.out), but
     * SystemTagList doesn't. Recognise the built-in vs external case by
     * peeking at argv[0] and, for externals, run a rewritten command line
     * with >CAPTURE_TMP appended so the DOS shell redirects stdout to it.
     * Then slurp it into g_capture. */
    {
        TokenList toks;
        int is_builtin;
        if (tokenize(&toks, args ? args : "") != TOK_OK || toks.argc == 0) {
            run_one(ctx, args ? args : "");
        } else {
            is_builtin = (strcmp(toks.argv[0], "ask") == 0) ||
                         (builtin_lookup(toks.argv[0]) != 0);
            if (is_builtin) {
                run_one(ctx, args ? args : "");
            } else {
                /* Build "<original> >CAPTURE_TMP" and hand to
                 * SystemTagList directly, then read the file. */
                static char cmdline[1024];
                int off = 0;
                int i;
                for (i = 0; args && args[i] && off < (int)sizeof(cmdline) - 40; i++) {
                    cmdline[off++] = args[i];
                }
                cmdline[off++] = ' ';
                cmdline[off++] = '>';
                {
                    const char *tp = CAPTURE_TMP;
                    while (*tp && off < (int)sizeof(cmdline) - 1) cmdline[off++] = *tp++;
                }
                cmdline[off] = '\0';
                /* Delete any prior temp file first, ignoring failures. */
                DeleteFile((CONST_STRPTR)CAPTURE_TMP);
                ctx->last_exit = (int)SystemTagList((CONST_STRPTR)cmdline, 0);
                slurp_capture_file();
                refresh_cwd(ctx);
            }
        }
    }
    ctx->out = saved_out;

    /* Result format: first line is "exit=<n>", rest is captured output. */
    {
        int rc = ctx->last_exit;
        int neg = rc < 0;
        int v = neg ? -rc : rc;
        char stack[16];
        int sl = 0;
        header_len = 0;
        /* header: "exit=<rc>\n" */
        {
            const char *h = "exit=";
            int i = 0;
            while (h[i] && header_len < bufSize - 1) resultBuf[header_len++] = h[i++];
        }
        if (neg && header_len < bufSize - 1) resultBuf[header_len++] = '-';
        if (v == 0) { if (header_len < bufSize - 1) resultBuf[header_len++] = '0'; }
        else {
            while (v > 0 && sl < (int)sizeof(stack)) { stack[sl++] = (char)('0' + (v % 10)); v /= 10; }
            while (sl > 0 && header_len < bufSize - 1) resultBuf[header_len++] = stack[--sl];
        }
        /* Escape the header terminator so it survives send_line's
         * newline-splits the wire protocol. */
        if (header_len < bufSize - 2) {
            resultBuf[header_len++] = '\\';
            resultBuf[header_len++] = 'n';
        }
    }
    /* Append captured output, escaping \n and | which are the pipe-
     * protocol's line + field separators. The host reverses this. */
    {
        int i = 0;
        while (g_capture[i] && header_len < bufSize - 2) {
            char c = g_capture[i++];
            if (c == '\n') {
                resultBuf[header_len++] = '\\';
                resultBuf[header_len++] = 'n';
            } else if (c == '|') {
                resultBuf[header_len++] = '\\';
                resultBuf[header_len++] = '|';
            } else {
                resultBuf[header_len++] = c;
            }
        }
        resultBuf[header_len] = '\0';
    }
    return 0;
}

/* ─── Bridge hook: LLM event stream ─────────────────────────────────────
 *
 * The host's LLM proxy pushes events into us via CALLHOOK hubert llm_event
 * with one of these payloads:
 *   TOKEN|<text>              — streaming delta to write to the console
 *   TOOL|<id>|<name>|<args>   — the model asked us to run a shell command;
 *                                we execute it locally and return its output
 *                                as the hook result. Host feeds that back to
 *                                the LLM as a tool_result.
 *   DONE|<status>             — end of turn (status is "ok" or an error)
 *
 * Hook results become the CALLHOOK reply on the host side, so the tool
 * result gets there without a separate uplink message.
 */
static int hook_llm_event(const char *args, char *resultBuf, int bufSize)
{
    if (!args) { resultBuf[0] = '\0'; return 0; }

    if (starts_with(args, "TOKEN|")) {
        write_str(args + 6);
        resultBuf[0] = '\0';
        return 0;
    }
    if (starts_with(args, "DONE|")) {
        g_agent_pending = 0;
        write_str("\n");
        resultBuf[0] = '\0';
        return 0;
    }
    if (starts_with(args, "TOOL|")) {
        /* Payload after "TOOL|" is <id>|<name>|<cmdline>. We don't parse
         * further than "everything after the second bar is the command
         * line" — the model is expected to send a fully-formed line. */
        const char *p = args + 5;
        int bars = 0;
        while (*p) {
            if (*p == '|') { bars++; if (bars == 2) { p++; break; } }
            p++;
        }
        if (bars < 2) {
            const char *e = "tool: malformed payload";
            int i = 0;
            while (e[i] && i < bufSize - 1) { resultBuf[i] = e[i]; i++; }
            resultBuf[i] = '\0';
            return 1;
        }
        /* Reuse hook_run's capture path so the LLM gets the same clean
         * "exit=N\n<output>" it can parse. */
        return hook_run(p, resultBuf, bufSize);
    }
    resultBuf[0] = '\0';
    return 0;
}

/* ─── Direct LLM + agent loop ────────────────────────────────────────────
 *
 * bi_ask opens a TCP socket to spark.hitorro.com via bsdsocket, streams
 * an Ollama /api/chat response, and prints token deltas to the console
 * as they arrive. When the model emits a tool_call we dispatch it
 * locally, feed the result back as a `tool` message, and loop until the
 * model stops asking for tools or LLM_MAX_STEPS runs out.
 *
 * Conversation state is a static LlmMessages so successive `ask` calls
 * keep the context; the `askreset` command clears it. This is what turns
 * ask into a persistent-agent surface rather than one-shot Q&A.
 *
 * Tool suite (advertised to the model in llm_direct.c):
 *   - run_command  (any AmigaDOS shell line)
 *   - read_file    (contents of a file)
 *   - write_file   (create/overwrite)
 *   - list_dir     (filenames one per line)
 *   - system_info  (kickstart + free ram summary)
 */

static ShellCtx *g_ask_ctx = 0;
static LlmMessages g_convo;
static int  g_convo_ready = 0;
static const char SYSTEM_PROMPT[] =
    "You are an AmigaOS agent embedded in a terminal. You can run tools "
    "on the machine to inspect and modify it. Prefer short answers. When "
    "you need real state (files, sysinfo, directory contents, command "
    "output), CALL A TOOL — never fabricate output. Tools available: "
    "run_command, read_file, write_file, list_dir, system_info.";

static void convo_ensure(void)
{
    if (g_convo_ready) return;
    llm_msgs_init(&g_convo);
    llm_msgs_add(&g_convo, LLM_ROLE_SYSTEM, SYSTEM_PROMPT);
    g_convo_ready = 1;
}

static void convo_reset(void)
{
    g_convo_ready = 0;
    convo_ensure();
}

static void ask_stream_cb(const LlmDelta *d, void *ud)
{
    (void)ud;
    if (d->content && d->content[0] && g_ask_ctx) {
        ctx_out(g_ask_ctx, d->content);
    }
}

/* Execute a tool call locally, populate `out` with the result the model
 * should see next (exit code + captured output). Uses the same routing
 * as hook_run: built-ins go through ctx->out; external commands are
 * SystemTagList'd with stdout redirected to a temp file. */
static void run_tool_command(ShellCtx *ctx, const char *cmdline,
                             char *out, int outSize)
{
    void (*saved_out)(struct ShellCtx *, const char *);
    TokenList toks;
    int off = 0;

    if (outSize <= 0) return;

    saved_out = ctx->out;
    ctx->out = capture_out;
    g_capture_len = 0; g_capture[0] = '\0';

    if (tokenize(&toks, cmdline) != TOK_OK || toks.argc == 0) {
        run_one(ctx, cmdline);
    } else {
        int is_builtin = (strcmp(toks.argv[0], "ask") == 0) ||
                         (builtin_lookup(toks.argv[0]) != 0);
        if (is_builtin) {
            run_one(ctx, cmdline);
        } else {
            static char shellcmd[1024];
            int so = 0;
            int i;
            for (i = 0; cmdline[i] && so < (int)sizeof(shellcmd) - 40; i++) {
                shellcmd[so++] = cmdline[i];
            }
            shellcmd[so++] = ' '; shellcmd[so++] = '>';
            {
                const char *tp = CAPTURE_TMP;
                while (*tp && so < (int)sizeof(shellcmd) - 1) shellcmd[so++] = *tp++;
            }
            shellcmd[so] = '\0';
            DeleteFile((CONST_STRPTR)CAPTURE_TMP);
            ctx->last_exit = (int)SystemTagList((CONST_STRPTR)shellcmd, 0);
            slurp_capture_file();
            refresh_cwd(ctx);
        }
    }
    ctx->out = saved_out;

    /* Format the tool result the same as hook_run: "exit=<N>\n<output>". */
    {
        const char *h = "exit=";
        int i = 0;
        while (h[i] && off < outSize - 1) out[off++] = h[i++];
    }
    {
        int rc = ctx->last_exit;
        int neg = rc < 0;
        int v = neg ? -rc : rc;
        char stack[16];
        int sl = 0;
        if (neg && off < outSize - 1) out[off++] = '-';
        if (v == 0) { if (off < outSize - 1) out[off++] = '0'; }
        else {
            while (v > 0 && sl < (int)sizeof(stack)) { stack[sl++] = (char)('0' + (v % 10)); v /= 10; }
            while (sl > 0 && off < outSize - 1) out[off++] = stack[--sl];
        }
    }
    if (off < outSize - 1) out[off++] = '\n';
    {
        int i = 0;
        while (g_capture[i] && off < outSize - 1) out[off++] = g_capture[i++];
    }
    out[off] = '\0';
}

/* Read a file's contents into out (NUL-terminated, truncated). */
static void tool_read_file(const char *path, char *out, int outSize)
{
    BPTR fh;
    LONG n;
    int off = 0;
    char chunk[512];

    if (!path || !path[0]) { strcpy(out, "error: empty path"); return; }
    fh = Open((CONST_STRPTR)path, MODE_OLDFILE);
    if (!fh) {
        int i;
        const char *msg = "error: cannot open ";
        for (i = 0; msg[i] && off < outSize - 1; i++) out[off++] = msg[i];
        for (i = 0; path[i] && off < outSize - 1; i++) out[off++] = path[i];
        out[off] = '\0';
        return;
    }
    for (;;) {
        n = Read(fh, chunk, (LONG)sizeof(chunk));
        if (n <= 0) break;
        {
            LONG i;
            for (i = 0; i < n && off < outSize - 1; i++) out[off++] = chunk[i];
            if (off >= outSize - 1) break;
        }
    }
    out[off] = '\0';
    Close(fh);
}

static void tool_write_file(const char *path, const char *content,
                            char *out, int outSize)
{
    BPTR fh;
    int clen;
    int written;
    if (!path || !path[0]) { strcpy(out, "error: empty path"); return; }
    if (!content) content = "";
    fh = Open((CONST_STRPTR)path, MODE_NEWFILE);
    if (!fh) {
        int i, off = 0;
        const char *msg = "error: cannot create ";
        for (i = 0; msg[i] && off < outSize - 1; i++) out[off++] = msg[i];
        for (i = 0; path[i] && off < outSize - 1; i++) out[off++] = path[i];
        out[off] = '\0';
        return;
    }
    clen = (int)strlen(content);
    written = (int)Write(fh, (APTR)content, (LONG)clen);
    Close(fh);
    sprintf(out, "wrote %ld bytes to %s", (long)written, path);
    if (outSize > 0) out[outSize - 1] = '\0';
}

/* Directory listing via ExAll — but keeping the hubert binary compact,
 * we route through DOS' List command via SystemTagList and capture. */
static void tool_list_dir(const char *path, char *out, int outSize)
{
    static char cmdline[512];
    int off = 0, i;
    const char *pre = "List ";
    const char *post = " NOHEAD QUICK >";
    for (i = 0; pre[i] && off < (int)sizeof(cmdline) - 1; i++) cmdline[off++] = pre[i];
    if (!path || !path[0]) path = "SYS:";
    for (i = 0; path[i] && off < (int)sizeof(cmdline) - 1; i++) cmdline[off++] = path[i];
    for (i = 0; post[i] && off < (int)sizeof(cmdline) - 1; i++) cmdline[off++] = post[i];
    {
        const char *tp = CAPTURE_TMP;
        while (*tp && off < (int)sizeof(cmdline) - 1) cmdline[off++] = *tp++;
    }
    cmdline[off] = '\0';
    DeleteFile((CONST_STRPTR)CAPTURE_TMP);
    (void)SystemTagList((CONST_STRPTR)cmdline, 0);
    g_capture_len = 0; g_capture[0] = '\0';
    slurp_capture_file();
    /* Copy into caller's buffer. */
    {
        int j = 0;
        while (g_capture[j] && j < outSize - 1) { out[j] = g_capture[j]; j++; }
        out[j] = '\0';
    }
}

static void tool_system_info(char *out, int outSize)
{
    /* SysBase is opaque to us; we can at least report free chip + fast RAM
     * which is what the model most often wants for "what's the current
     * memory pressure" style questions. */
    ULONG chip = AvailMem(MEMF_CHIP);
    ULONG fast = AvailMem(MEMF_FAST);
    sprintf(out, "free_chip=%lu free_fast=%lu",
        (unsigned long)chip, (unsigned long)fast);
    if (outSize > 0) out[outSize - 1] = '\0';
}

/* Dispatch a tool call. Result is formatted as "exit=<N>\n<data>" — same
 * shape as run_command's output so the model sees a uniform response. */
static void dispatch_tool(ShellCtx *ctx, const char *name, const char *args,
                          char *out, int outSize)
{
    static char path[256];
    static char content[LLM_MAX_MESSAGE_LEN];
    static char cmd[LLM_MAX_MESSAGE_LEN];

    if (!args) args = "";
    if (outSize <= 0) return;

    if (strcmp(name, "run_command") == 0) {
        /* Arguments are a JSON object like {"command":"..."} — the caller
         * already handed us the raw JSON in `args`. */
        if (jsonx_string(args, "command", cmd, (int)sizeof(cmd)) != JSONX_OK) {
            strcpy(out, "exit=1\nerror: missing 'command'");
            return;
        }
        run_tool_command(ctx, cmd, out, outSize);
        return;
    }
    if (strcmp(name, "read_file") == 0) {
        static char body[LLM_MAX_MESSAGE_LEN];
        if (jsonx_string(args, "path", path, (int)sizeof(path)) != JSONX_OK) {
            strcpy(out, "exit=1\nerror: missing 'path'"); return;
        }
        tool_read_file(path, body, (int)sizeof(body));
        sprintf(out, "exit=0\n%s", body);
        out[outSize - 1] = '\0';
        return;
    }
    if (strcmp(name, "write_file") == 0) {
        static char body[128];
        if (jsonx_string(args, "path", path, (int)sizeof(path)) != JSONX_OK) {
            strcpy(out, "exit=1\nerror: missing 'path'"); return;
        }
        if (jsonx_string(args, "content", content, (int)sizeof(content)) != JSONX_OK) {
            content[0] = '\0';
        }
        tool_write_file(path, content, body, (int)sizeof(body));
        sprintf(out, "exit=0\n%s", body);
        out[outSize - 1] = '\0';
        return;
    }
    if (strcmp(name, "list_dir") == 0) {
        static char body[LLM_MAX_MESSAGE_LEN];
        if (jsonx_string(args, "path", path, (int)sizeof(path)) != JSONX_OK) {
            strcpy(path, "SYS:");
        }
        tool_list_dir(path, body, (int)sizeof(body));
        sprintf(out, "exit=0\n%s", body);
        out[outSize - 1] = '\0';
        return;
    }
    if (strcmp(name, "system_info") == 0) {
        static char body[128];
        tool_system_info(body, (int)sizeof(body));
        sprintf(out, "exit=0\n%s", body);
        out[outSize - 1] = '\0';
        return;
    }
    sprintf(out, "exit=1\nunknown tool: %s", name);
    if (outSize > 0) out[outSize - 1] = '\0';
}

static int bi_ask(ShellCtx *ctx, int argc, char **argv)
{
    static LlmConfig cfg;
    static char prompt[900];
    static char tool_name[64];
    static char tool_args[LLM_MAX_MESSAGE_LEN];
    static char tool_result[LLM_MAX_MESSAGE_LEN];
    int off = 0, i, j, step;

    if (argc < 2) {
        ctx_out(ctx, "usage: ask <prompt>\n"
                     "       askreset  (clear conversation memory)\n");
        return 1;
    }

    for (i = 1; i < argc; i++) {
        if (i > 1 && off < (int)sizeof(prompt) - 2) prompt[off++] = ' ';
        for (j = 0; argv[i][j] && off < (int)sizeof(prompt) - 1; j++) {
            prompt[off++] = argv[i][j];
        }
    }
    prompt[off] = '\0';

    cfg.host = LLM_HOST;
    cfg.port = LLM_PORT;
    cfg.model = LLM_MODEL;
    cfg.connect_timeout_ms = 15000;
    cfg.io_timeout_ms = 180000;

    convo_ensure();
    if (!llm_msgs_add(&g_convo, LLM_ROLE_USER, prompt)) {
        /* Conversation full — recycle: keep system + last 8 messages. */
        int keep_from;
        int m;
        keep_from = g_convo.count - 8;
        if (keep_from < 1) keep_from = 1;
        for (m = 1; m + (keep_from - 1) < g_convo.count; m++) {
            g_convo.entries[m] = g_convo.entries[m + keep_from - 1];
        }
        g_convo.count = 1 + (g_convo.count - keep_from);
        llm_msgs_add(&g_convo, LLM_ROLE_USER, prompt);
    }

    g_ask_ctx = ctx;
    ctx_out(ctx, "\n");

    for (step = 0; step < LLM_MAX_STEPS; step++) {
        int rc;
        tool_name[0] = '\0';
        tool_args[0] = '\0';

        rc = llm_chat_stream(&cfg, &g_convo, ask_stream_cb, 0,
                             tool_name, (int)sizeof(tool_name),
                             tool_args, (int)sizeof(tool_args));
        if (rc != 0) {
            static char errbuf[64];
            const char *msg;
            switch (rc) {
                case -2: msg = "\nask: connect failed\n"; break;
                case -3: msg = "\nask: HTTP request failed\n"; break;
                case -4: msg = "\nask: HTTP non-200 status\n"; break;
                case -5: msg = "\nask: stream I/O error\n"; break;
                default: sprintf(errbuf, "\nask: LLM error %d\n", rc); msg = errbuf;
            }
            ctx_out(ctx, msg);
            g_ask_ctx = 0;
            return 1;
        }

        if (tool_name[0] == '\0') {
            /* No tool call — response is complete. Persist an empty
             * assistant turn so the model sees this exchange in the next
             * ask (the streamed content isn't captured, but it's visible
             * on the terminal — the model doesn't need it for coherence
             * because Ollama also keeps the emitted content in its history
             * message... but since we manage messages ourselves we don't
             * see that either. This is a known simplification.) */
            ctx_out(ctx, "\n");
            g_ask_ctx = 0;
            return 0;
        }

        ctx_out(ctx, "\n[tool: ");
        ctx_out(ctx, tool_name);
        ctx_out(ctx, " ");
        ctx_out(ctx, tool_args);
        ctx_out(ctx, "]\n");

        dispatch_tool(ctx, tool_name, tool_args, tool_result,
                      (int)sizeof(tool_result));
        ctx_out(ctx, tool_result);
        ctx_out(ctx, "\n");

        if (!llm_msgs_add(&g_convo, LLM_ROLE_ASSISTANT, "")) break;
        if (!llm_msgs_add(&g_convo, LLM_ROLE_TOOL, tool_result)) break;
    }
    ctx_out(ctx, "\nask: step budget exhausted\n");
    g_ask_ctx = 0;
    return 1;
}

/* `askreset` — clear the conversation memory. */
static int bi_askreset(ShellCtx *ctx, int argc, char **argv)
{
    (void)argc; (void)argv;
    convo_reset();
    ctx_out(ctx, "ask: conversation reset\n");
    return 0;
}

/* ─── Tab completion ─────────────────────────────────────────────────────
 *
 * On TAB we look at the token at the cursor and treat it as an AmigaDOS
 * path prefix. We split it into "dir" + "prefix" (dir is the volume or
 * assign or subpath ending in ':' or '/', prefix is the leaf being typed).
 * Then we Lock the dir, walk it with Examine/ExNext, and:
 *
 *   0 matches   — beep (no change)
 *   1 match     — append the completion; a '/' after directories
 *   >1 matches  — advance to the longest common prefix, then print the
 *                 candidate list under the prompt
 *
 * We intentionally don't touch the command-name (argv[0]) side of things
 * yet — completing files is by far the most useful thing on AmigaOS.
 */

/* Find the start index of the word under `cursor`. Words are anything
 * bounded by whitespace. */
static int word_start(const char *buf, int cursor)
{
    int i = cursor;
    while (i > 0 && buf[i - 1] != ' ' && buf[i - 1] != '\t') i--;
    return i;
}

/* Split "path/prefix" or "vol:prefix" into (dir_prefix, leaf). dir_out
 * always ends with a separator ('/' or ':'), which lets subsequent
 * concatenation work. If there's no separator (bare leaf), dir_out is set
 * to "" and the caller uses the current CWD. */
static void split_path(const char *token, char *dir_out, int dir_size,
                       const char **leaf_out)
{
    int i, last = -1;
    for (i = 0; token[i]; i++) {
        if (token[i] == '/' || token[i] == ':') last = i;
    }
    if (last < 0) {
        dir_out[0] = '\0';
        *leaf_out = token;
        return;
    }
    {
        int len = last + 1;
        if (len > dir_size - 1) len = dir_size - 1;
        memcpy(dir_out, token, (size_t)len);
        dir_out[len] = '\0';
    }
    *leaf_out = token + last + 1;
}

/* Fill matches[] with up to max entries whose names start with `prefix`.
 * Returns count. Each match includes a trailing '/' if the entry is a
 * directory. dir_path == "" means the current directory (Lock ""). */
#define MATCH_MAX 32
#define MATCH_LEN 96
static int find_matches(const char *dir_path, const char *prefix,
                        char matches[MATCH_MAX][MATCH_LEN])
{
    BPTR lock;
    struct FileInfoBlock *fib;
    int prefix_len = (int)strlen(prefix);
    int count = 0;

    lock = Lock((CONST_STRPTR)(dir_path[0] ? dir_path : ""), SHARED_LOCK);
    if (!lock) return 0;

    fib = (struct FileInfoBlock *)AllocDosObject(DOS_FIB, 0);
    if (!fib) { UnLock(lock); return 0; }

    if (Examine(lock, fib)) {
        while (ExNext(lock, fib)) {
            const char *n = (const char *)fib->fib_FileName;
            int nlen = (int)strlen(n);
            int i, match = 1;

            if (nlen < prefix_len) continue;
            for (i = 0; i < prefix_len; i++) {
                char a = n[i], b = prefix[i];
                if (a >= 'A' && a <= 'Z') a = (char)(a + 32);
                if (b >= 'A' && b <= 'Z') b = (char)(b + 32);
                if (a != b) { match = 0; break; }
            }
            if (!match) continue;

            {
                int copy = nlen;
                int add_slash = (fib->fib_DirEntryType > 0) ? 1 : 0;
                if (copy + add_slash > MATCH_LEN - 1) copy = MATCH_LEN - 1 - add_slash;
                memcpy(matches[count], n, (size_t)copy);
                if (add_slash) matches[count][copy++] = '/';
                matches[count][copy] = '\0';
                count++;
                if (count >= MATCH_MAX) break;
            }
        }
    }
    FreeDosObject(DOS_FIB, fib);
    UnLock(lock);
    return count;
}

/* Longest common prefix of the first `count` match strings, written into
 * dst (NUL-terminated). Case-insensitive on Amiga file systems. */
static void common_prefix(char matches[MATCH_MAX][MATCH_LEN], int count,
                          char *dst, int dstSize)
{
    int col = 0;
    if (count <= 0) { dst[0] = '\0'; return; }
    while (col < dstSize - 1) {
        char c = matches[0][col];
        int i;
        if (c == '\0') break;
        for (i = 1; i < count; i++) {
            char d = matches[i][col];
            char cl = (c >= 'A' && c <= 'Z') ? (char)(c + 32) : c;
            char dl = (d >= 'A' && d <= 'Z') ? (char)(d + 32) : d;
            if (dl != cl) return;
        }
        dst[col] = c;
        col++;
        dst[col] = '\0';
    }
}

/* Handle a TAB key. Returns 1 if we should redraw (buffer changed or list
 * printed and prompt needs to be reissued). */
static int handle_tab(LineEditor *ed, const char *prompt)
{
    const char *buf = line_editor_text(ed);
    int cursor = line_editor_cursor(ed);
    int wstart = word_start(buf, cursor);
    char token[LINE_MAX];
    char dir[256];
    const char *leaf;
    char matches[MATCH_MAX][MATCH_LEN];
    int nmatches;
    int i;
    int tlen = cursor - wstart;

    if (tlen <= 0) return 0;
    if (tlen >= (int)sizeof(token)) tlen = (int)sizeof(token) - 1;
    memcpy(token, buf + wstart, (size_t)tlen);
    token[tlen] = '\0';

    split_path(token, dir, (int)sizeof(dir), &leaf);
    nmatches = find_matches(dir, leaf, matches);

    if (nmatches == 0) {
        write_str("\x07");   /* BEL — nothing matched */
        return 0;
    }

    if (nmatches == 1) {
        /* Replace token with dir+matches[0], leaving a trailing space when
         * the match is a file (not a directory), so the next arg follows
         * cleanly. */
        char newtok[LINE_MAX];
        int n = 0;
        int j;
        for (j = 0; dir[j] && n < (int)sizeof(newtok) - 1; j++) newtok[n++] = dir[j];
        for (j = 0; matches[0][j] && n < (int)sizeof(newtok) - 1; j++) newtok[n++] = matches[0][j];
        if (n > 0 && newtok[n - 1] != '/' && n < (int)sizeof(newtok) - 1) newtok[n++] = ' ';
        newtok[n] = '\0';
        line_editor_delete_range(ed, wstart, cursor);
        line_editor_insert_str(ed, newtok);
        return 1;
    }

    /* Multiple matches: advance to common prefix, then list. */
    {
        char cp[MATCH_LEN];
        char newtok[LINE_MAX];
        int n = 0, j;
        common_prefix(matches, nmatches, cp, (int)sizeof(cp));
        for (j = 0; dir[j] && n < (int)sizeof(newtok) - 1; j++) newtok[n++] = dir[j];
        for (j = 0; cp[j] && n < (int)sizeof(newtok) - 1; j++) newtok[n++] = cp[j];
        newtok[n] = '\0';
        line_editor_delete_range(ed, wstart, cursor);
        line_editor_insert_str(ed, newtok);
    }

    /* Print candidates on a new line, then re-issue the prompt so redraw
     * writes into the correct row. */
    write_str("\n");
    for (i = 0; i < nmatches; i++) {
        write_str("  ");
        write_str(matches[i]);
    }
    write_str("\n");
    write_str(prompt);
    return 1;
}

int main(void)
{
    History hist;
    Env env;
    LineEditor ed;
    ShellCtx ctx;
    KeyKind k;
    char ch;
    char prompt[280];

    /* Open a proper RAW: console so we get keypress-level input. The
     * inherited stdout is line-buffered by DOS; RAW: gives us cursor keys
     * and lets us do our own line editing. */
    g_console = Open((CONST_STRPTR)"RAW:0/0/640/480/hubert/CLOSE",
                     MODE_NEWFILE);
    if (!g_console) {
        Printf("hubert: could not open RAW: console\n");
        return 20;
    }
    /* Tell the executor to route external command stdout back into this
     * console — otherwise `dir`, `list`, `type` output vanishes (their
     * default stdout is whatever hubert's parent CLI had, usually NIL:
     * when we were launched via `run`). */
    executor_set_output((void *)g_console);

    history_init(&hist);
    env_init(&env);
    line_editor_init(&ed, &hist);
    memset(&ctx, 0, sizeof(ctx));
    ctx.hist = &hist;
    ctx.env  = &env;
    ctx.out  = ctx_out_cb;
    ctx.platform_chdir = platform_chdir_impl;
    refresh_cwd(&ctx);
    g_ctx_ref = &ctx;

    /* Attach to amiga-bridge if it's running. Failure is soft — the shell
     * stays fully usable without the bridge. */
    g_bridge_ok = (ab_init("hubert") == 0);
    if (g_bridge_ok) {
        int i = 0;
        while (ctx.cwd[i] && i < (int)sizeof(g_bridge_cwd) - 1) {
            g_bridge_cwd[i] = ctx.cwd[i]; i++;
        }
        g_bridge_cwd[i] = '\0';
        ab_register_var("cwd", AB_TYPE_STR, g_bridge_cwd);
        ab_register_hook("run",
            "Run a command line as if typed at the hubert prompt",
            hook_run);
        ab_register_hook("llm_event",
            "Feed a streaming LLM event: TOKEN|<text>, TOOL|<id>|<name>|<cmd>, DONE|<status>",
            hook_llm_event);
        AB_I("hubert %s started", VERSION);
    }

    banner();

    for (;;) {
        /* Build prompt like "SYS:> ". Kept short so the redraw math stays
         * simple; the cwd may be trimmed if it wouldn't fit. */
        int p = 0;
        int cwd_len = (int)strlen(ctx.cwd);
        int max_cwd = (int)sizeof(prompt) - 4;
        if (cwd_len > max_cwd) cwd_len = max_cwd;
        memcpy(prompt, ctx.cwd, (size_t)cwd_len);
        p = cwd_len;
        prompt[p++] = '>';
        prompt[p++] = ' ';
        prompt[p]   = '\0';

        line_editor_reset(&ed);
        write_str("\n");
        write_str(prompt);

        for (;;) {
            EdAction a;
            /* Service the bridge between keystrokes so the host can push us
             * a `run` hook call or read the cwd CVAR while we're waiting
             * for input. ab_poll is a cheap no-op when disconnected. */
            if (g_bridge_ok) ab_poll();
            if (!read_key(&k, &ch)) {
                /* EOF — treat as exit */
                ctx.want_exit = 1;
                break;
            }
            /* WaitForChar polls signal us with kind=CHAR, code=0 for
             * "no key this tick" — skip so the editor doesn't get spammed
             * with silent no-op edits. */
            if (k == KEY_CHAR && ch == 0) continue;
            if (k == KEY_TAB) {
                handle_tab(&ed, prompt);
                redraw(prompt, &ed);
                continue;
            }
            a = line_editor_key(&ed, k, ch);
            redraw(prompt, &ed);
            if (a == ED_SUBMIT) break;
        }
        write_str("\n");
        if (ctx.want_exit) break;

        history_add(&hist, line_editor_text(&ed));
        run_one(&ctx, line_editor_text(&ed));
    }

    write_str("\nhubert: goodbye\n");
    if (g_bridge_ok) ab_cleanup();
    if (g_console) Close(g_console);
    return 0;
}
