/*
 * test_core.c — host-runnable unit tests for the pure hubert modules.
 *
 * These use a tiny embedded harness rather than the bridge-side one because
 * they compile with the SYSTEM cc (not m68k) and don't need bridge_client.
 * The Amiga-side smoke test in test_amiga.c re-runs the same suite through
 * the bridge harness so the same expectations hold on the emulator too.
 */
#include <stdio.h>
#include <string.h>

#include "../history.h"
#include "../tokenizer.h"
#include "../line_editor.h"
#include "../env.h"
#include "../builtins.h"
#include "../executor.h"

static int g_pass = 0;
static int g_fail = 0;

#define CHECK(expr, name) do { \
    if (expr) { g_pass++; } \
    else { g_fail++; fprintf(stderr, "FAIL %s at %s:%d\n", name, __FILE__, __LINE__); } \
} while (0)

/* ─── history ─────────────────────────────────────────────────────────── */

static void test_history_basic(void)
{
    History h;
    char buf[128];
    history_init(&h);

    history_add(&h, "ls");
    history_add(&h, "cd DH0:");
    history_add(&h, "echo hi");

    /* newest first, ignoring empty */
    CHECK(strcmp(history_at(&h, 0), "echo hi") == 0, "hist_newest");
    CHECK(strcmp(history_at(&h, 2), "ls") == 0, "hist_oldest");
    CHECK(history_at(&h, 3) == 0, "hist_out_of_range");

    /* recall walks back through the list without duplicates */
    CHECK(history_recall_prev(&h, buf, sizeof(buf)), "recall1");
    CHECK(strcmp(buf, "echo hi") == 0, "recall_newest");
    CHECK(history_recall_prev(&h, buf, sizeof(buf)), "recall2");
    CHECK(strcmp(buf, "cd DH0:") == 0, "recall_mid");
    CHECK(history_recall_prev(&h, buf, sizeof(buf)), "recall3");
    CHECK(strcmp(buf, "ls") == 0, "recall_oldest");
    CHECK(!history_recall_prev(&h, buf, sizeof(buf)), "recall_past_oldest");

    CHECK(history_recall_next(&h, buf, sizeof(buf)), "next1");
    CHECK(strcmp(buf, "cd DH0:") == 0, "next_forward");
}

static void test_history_dedup_and_ignore(void)
{
    History h;
    history_init(&h);
    history_add(&h, "");         /* ignored */
    history_add(&h, "ls");
    history_add(&h, "ls");       /* duplicate — skipped */
    history_add(&h, "cd");
    CHECK(h.count == 2, "hist_dedup_count");
    CHECK(strcmp(history_at(&h, 0), "cd") == 0, "hist_dedup_head");
    CHECK(strcmp(history_at(&h, 1), "ls") == 0, "hist_dedup_second");
}

/* ─── tokenizer ───────────────────────────────────────────────────────── */

static void test_tokenize_simple(void)
{
    TokenList t;
    CHECK(tokenize(&t, "ls DH0:") == TOK_OK, "tok_simple_ok");
    CHECK(t.argc == 2, "tok_simple_argc");
    CHECK(strcmp(t.argv[0], "ls") == 0, "tok_simple_argv0");
    CHECK(strcmp(t.argv[1], "DH0:") == 0, "tok_simple_argv1");
}

static void test_tokenize_quotes(void)
{
    TokenList t;
    CHECK(tokenize(&t, "echo \"hello world\" 'a b'") == TOK_OK, "tok_quotes_ok");
    CHECK(t.argc == 3, "tok_quotes_argc");
    CHECK(strcmp(t.argv[0], "echo") == 0, "tok_quotes_argv0");
    CHECK(strcmp(t.argv[1], "hello world") == 0, "tok_quotes_dquote");
    CHECK(strcmp(t.argv[2], "a b") == 0, "tok_quotes_squote");
}

static void test_tokenize_edge(void)
{
    TokenList t;
    CHECK(tokenize(&t, "") == TOK_OK, "tok_empty_ok");
    CHECK(t.argc == 0, "tok_empty_argc");
    CHECK(tokenize(&t, "   ") == TOK_OK, "tok_ws_ok");
    CHECK(t.argc == 0, "tok_ws_argc");
    CHECK(tokenize(&t, "echo 'unterm") == TOK_ERR_UNTERMINATED_QUOTE,
          "tok_unterm");
    CHECK(tokenize(&t, "a\\ b") == TOK_OK, "tok_esc_space_ok");
    CHECK(t.argc == 1, "tok_esc_space_argc");
    CHECK(strcmp(t.argv[0], "a b") == 0, "tok_esc_space_value");
}

/* ─── line_editor ─────────────────────────────────────────────────────── */

static void feed_str(LineEditor *ed, const char *s)
{
    while (*s) line_editor_key(ed, KEY_CHAR, *s++);
}

static void test_line_editor_typing(void)
{
    History h;
    LineEditor ed;
    history_init(&h);
    line_editor_init(&ed, &h);

    feed_str(&ed, "hello");
    CHECK(strcmp(line_editor_text(&ed), "hello") == 0, "le_type");
    CHECK(line_editor_cursor(&ed) == 5, "le_cursor_end");

    line_editor_key(&ed, KEY_LEFT, 0);
    line_editor_key(&ed, KEY_LEFT, 0);
    line_editor_key(&ed, KEY_BACKSPACE, 0);
    CHECK(strcmp(line_editor_text(&ed), "helo") == 0, "le_bs_mid");
    CHECK(line_editor_cursor(&ed) == 2, "le_bs_cursor");

    line_editor_key(&ed, KEY_HOME, 0);
    CHECK(line_editor_cursor(&ed) == 0, "le_home");
    line_editor_key(&ed, KEY_END, 0);
    CHECK(line_editor_cursor(&ed) == 4, "le_end");
}

static void test_line_editor_history(void)
{
    History h;
    LineEditor ed;
    history_init(&h);
    history_add(&h, "cd DH0:");
    history_add(&h, "ls");
    line_editor_init(&ed, &h);

    line_editor_key(&ed, KEY_UP, 0);
    CHECK(strcmp(line_editor_text(&ed), "ls") == 0, "le_up1");
    line_editor_key(&ed, KEY_UP, 0);
    CHECK(strcmp(line_editor_text(&ed), "cd DH0:") == 0, "le_up2");
    line_editor_key(&ed, KEY_DOWN, 0);
    CHECK(strcmp(line_editor_text(&ed), "ls") == 0, "le_down1");
    line_editor_key(&ed, KEY_DOWN, 0);
    CHECK(strcmp(line_editor_text(&ed), "") == 0, "le_down_empty");
}

static void test_line_editor_kill_word(void)
{
    History h;
    LineEditor ed;
    history_init(&h);
    line_editor_init(&ed, &h);
    feed_str(&ed, "cd DH0:Dev/game");
    line_editor_key(&ed, KEY_KILL_WORD, 0);
    CHECK(strcmp(line_editor_text(&ed), "cd ") == 0, "le_kill_word");
}

/* ─── env ─────────────────────────────────────────────────────────────── */

static void test_env(void)
{
    Env e;
    env_init(&e);
    CHECK(env_set(&e, "PATH", "SYS:C") == 1, "env_set");
    CHECK(strcmp(env_get(&e, "PATH"), "SYS:C") == 0, "env_get");
    CHECK(env_set(&e, "PATH", "SYS:C DH2:Dev") == 1, "env_set_update");
    CHECK(strcmp(env_get(&e, "PATH"), "SYS:C DH2:Dev") == 0, "env_get_updated");
    CHECK(env_unset(&e, "PATH") == 1, "env_unset");
    CHECK(env_get(&e, "PATH") == 0, "env_get_gone");
    CHECK(env_set(&e, "", "x") == 0, "env_reject_empty_name");
}

/* ─── builtins ────────────────────────────────────────────────────────── */

static char g_out_capture[4096];
static int  g_out_len;

static void capture_out(ShellCtx *ctx, const char *s)
{
    int i = 0;
    (void)ctx;
    while (s[i] && g_out_len < (int)sizeof(g_out_capture) - 1) {
        g_out_capture[g_out_len++] = s[i++];
    }
    g_out_capture[g_out_len] = '\0';
}
static void capture_reset(void) { g_out_len = 0; g_out_capture[0] = '\0'; }

/* stub platform_chdir that always succeeds unless target starts with "BAD" */
static int fake_chdir(ShellCtx *ctx, const char *p)
{
    (void)ctx;
    if (strncmp(p, "BAD", 3) == 0) return -1;
    return 0;
}

static ShellCtx make_ctx(Env *env, History *hist)
{
    ShellCtx c;
    memset(&c, 0, sizeof(c));
    c.env = env; c.hist = hist;
    strcpy(c.cwd, "SYS:");
    c.out = capture_out;
    c.platform_chdir = fake_chdir;
    return c;
}

static void test_builtin_echo(void)
{
    Env env; History hist;
    ShellCtx c;
    char *argv[] = { "echo", "hi", "there", 0 };
    int exit_code = 99;
    env_init(&env); history_init(&hist);
    c = make_ctx(&env, &hist);
    capture_reset();
    CHECK(builtin_dispatch(&c, 3, argv, &exit_code) == 1, "bi_echo_dispatch");
    CHECK(exit_code == 0, "bi_echo_exit");
    CHECK(strcmp(g_out_capture, "hi there\n") == 0, "bi_echo_out");
}

static void test_builtin_cd(void)
{
    Env env; History hist;
    ShellCtx c;
    char *argv[] = { "cd", "DH0:Work", 0 };
    char *argv_bad[] = { "cd", "BAD:", 0 };
    int exit_code;
    env_init(&env); history_init(&hist);
    c = make_ctx(&env, &hist);
    capture_reset();
    CHECK(builtin_dispatch(&c, 2, argv, &exit_code) == 1, "bi_cd_ran");
    CHECK(exit_code == 0, "bi_cd_ok");
    CHECK(strcmp(c.cwd, "DH0:Work") == 0, "bi_cd_cwd_updated");

    capture_reset();
    builtin_dispatch(&c, 2, argv_bad, &exit_code);
    CHECK(exit_code == 1, "bi_cd_bad_exit");
    CHECK(strstr(g_out_capture, "no such") != 0, "bi_cd_bad_msg");
    CHECK(strcmp(c.cwd, "DH0:Work") == 0, "bi_cd_cwd_unchanged_on_fail");
}

static void test_builtin_pwd(void)
{
    Env env; History hist;
    ShellCtx c;
    char *argv[] = { "pwd", 0 };
    int exit_code;
    env_init(&env); history_init(&hist);
    c = make_ctx(&env, &hist);
    strcpy(c.cwd, "DH2:Dev");
    capture_reset();
    builtin_dispatch(&c, 1, argv, &exit_code);
    CHECK(exit_code == 0, "bi_pwd_ok");
    CHECK(strcmp(g_out_capture, "DH2:Dev\n") == 0, "bi_pwd_out");
}

static void test_builtin_exit(void)
{
    Env env; History hist;
    ShellCtx c;
    char *argv[] = { "exit", 0 };
    int exit_code = -1;
    env_init(&env); history_init(&hist);
    c = make_ctx(&env, &hist);
    builtin_dispatch(&c, 1, argv, &exit_code);
    CHECK(exit_code == 0, "bi_exit_rc");
    CHECK(c.want_exit == 1, "bi_exit_flag");
}

static void test_builtin_set(void)
{
    Env env; History hist;
    ShellCtx c;
    char *argv_kv[] = { "set", "MODEL=nemotron", 0 };
    char *argv_two[] = { "set", "HOST", "spark.hitorro.com", 0 };
    char *argv_unset[] = { "set", "MODEL", 0 };
    int exit_code;
    env_init(&env); history_init(&hist);
    c = make_ctx(&env, &hist);

    builtin_dispatch(&c, 2, argv_kv, &exit_code);
    CHECK(exit_code == 0, "bi_set_kv_ok");
    CHECK(strcmp(env_get(&env, "MODEL"), "nemotron") == 0, "bi_set_kv_val");

    builtin_dispatch(&c, 3, argv_two, &exit_code);
    CHECK(strcmp(env_get(&env, "HOST"), "spark.hitorro.com") == 0, "bi_set_two_args");

    builtin_dispatch(&c, 2, argv_unset, &exit_code);
    CHECK(env_get(&env, "MODEL") == 0, "bi_set_unset_shorthand");
}

/* ─── executor (hook) ─────────────────────────────────────────────────── */

static char g_last_cmd[256];
static int  g_hook_called;
static int hook_capture(ShellCtx *ctx, const char *line, void *ud)
{
    (void)ctx; (void)ud;
    strncpy(g_last_cmd, line, sizeof(g_last_cmd) - 1);
    g_last_cmd[sizeof(g_last_cmd) - 1] = '\0';
    g_hook_called++;
    return 0;
}

static void test_executor_join_and_hook(void)
{
    Env env; History hist;
    ShellCtx c;
    char *argv[] = { "List", "DH2:Dev", "QUICK", 0 };
    int rc;
    char buf[64];
    env_init(&env); history_init(&hist);
    c = make_ctx(&env, &hist);
    executor_set_hook(hook_capture, 0);
    g_hook_called = 0; g_last_cmd[0] = '\0';
    rc = executor_run(&c, 3, argv);
    CHECK(rc == 0, "exec_hook_rc");
    CHECK(g_hook_called == 1, "exec_hook_count");
    CHECK(strcmp(g_last_cmd, "List DH2:Dev QUICK") == 0, "exec_hook_line");
    executor_clear_hook();

    /* join escapes spaces inside argv values by quoting */
    {
        char *quoted[] = { "echo", "hello world", 0 };
        int len = executor_join(buf, (int)sizeof(buf), 2, quoted);
        CHECK(len > 0, "join_ok");
        CHECK(strcmp(buf, "echo \"hello world\"") == 0, "join_quotes");
    }
}

/* ─── run all ─────────────────────────────────────────────────────────── */

int main(void)
{
    test_history_basic();
    test_history_dedup_and_ignore();
    test_tokenize_simple();
    test_tokenize_quotes();
    test_tokenize_edge();
    test_line_editor_typing();
    test_line_editor_history();
    test_line_editor_kill_word();
    test_env();
    test_builtin_echo();
    test_builtin_cd();
    test_builtin_pwd();
    test_builtin_exit();
    test_builtin_set();
    test_executor_join_and_hook();

    fprintf(stderr, "\n%d passed, %d failed\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
