/*
 * test_direct.c — host tests for http.c / jsonx.c / llm_direct.c.
 *
 * Uses the net.c host stub so we can script an entire Ollama response
 * (chunked framing, NDJSON payload) and assert on:
 *   - the exact request bytes we sent,
 *   - the tokens the streaming callback saw,
 *   - the tool call detected.
 */
#include <stdio.h>
#include <string.h>

#include "../net.h"
#include "../http.h"
#include "../jsonx.h"
#include "../llm_direct.h"

static int g_pass = 0;
static int g_fail = 0;

#define CHECK(expr, name) do { \
    if (expr) { g_pass++; } \
    else { g_fail++; fprintf(stderr, "FAIL %s at %s:%d\n", name, __FILE__, __LINE__); } \
} while (0)

/* ─── jsonx ──────────────────────────────────────────────────────────── */

static void test_jsonx_basic(void)
{
    const char *js = "{\"a\":\"hello\",\"b\":true,\"c\":42,\"d\":{\"e\":\"nested\"}}";
    char buf[64];
    int b;
    CHECK(jsonx_string(js, "a", buf, sizeof(buf)) == JSONX_OK, "jx_a");
    CHECK(strcmp(buf, "hello") == 0, "jx_a_val");
    CHECK(jsonx_bool(js, "b", &b) == JSONX_OK && b == 1, "jx_b");
    CHECK(jsonx_string(js, "d.e", buf, sizeof(buf)) == JSONX_OK, "jx_de");
    CHECK(strcmp(buf, "nested") == 0, "jx_de_val");
    CHECK(jsonx_string(js, "missing", buf, sizeof(buf)) == JSONX_NOT_FOUND, "jx_missing");
}

static void test_jsonx_arrays_and_escapes(void)
{
    const char *js =
        "{\"tool_calls\":["
        "{\"function\":{\"name\":\"run_command\",\"arguments\":{\"command\":\"echo \\\"hi\\\"\"}}}"
        "]}";
    char buf[128];
    CHECK(jsonx_string(js, "tool_calls[0].function.name", buf, sizeof(buf)) == JSONX_OK,
          "jx_name");
    CHECK(strcmp(buf, "run_command") == 0, "jx_name_val");
    CHECK(jsonx_string(js, "tool_calls[0].function.arguments.command",
                       buf, sizeof(buf)) == JSONX_OK, "jx_cmd");
    CHECK(strcmp(buf, "echo \"hi\"") == 0, "jx_cmd_unescape");
}

static void test_jsonx_unicode_escape(void)
{
    /* é -> é in UTF-8 (0xC3 0xA9) */
    const char *js = "{\"x\":\"caf\\u00e9\"}";
    char buf[16];
    CHECK(jsonx_string(js, "x", buf, sizeof(buf)) == JSONX_OK, "jx_uc_ok");
    CHECK((unsigned char)buf[0] == 'c' && (unsigned char)buf[3] == 0xC3 &&
          (unsigned char)buf[4] == 0xA9, "jx_uc_utf8");
}

/* ─── http request builder ───────────────────────────────────────────── */

static void test_http_build_post(void)
{
    char buf[512];
    int n = http_build_post(buf, sizeof(buf), "/api/chat",
                            "spark.hitorro.com", 11434, "application/json",
                            "{\"m\":\"n\"}", 9);
    CHECK(n > 0, "hb_ok");
    CHECK(strstr(buf, "POST /api/chat HTTP/1.1\r\n") != 0, "hb_line");
    CHECK(strstr(buf, "Host: spark.hitorro.com:11434\r\n") != 0, "hb_host");
    CHECK(strstr(buf, "Content-Length: 9\r\n") != 0, "hb_clen");
    CHECK(strstr(buf, "\r\n\r\n{\"m\":\"n\"}") != 0, "hb_body");
}

/* ─── streaming reader over the net stub ─────────────────────────────── */

static void test_http_stream_chunked_ndjson(void)
{
    HttpStream hs;
    NetConn *c;
    NetResult err;
    char line[HTTP_MAX_LINE];
    int rl;

    net_stub_reset();
    /* Two frames, chunked. Chunk size in hex + CRLF, then payload + CRLF,
     * then 0-chunk terminates. */
    net_stub_queue_recv(
        "HTTP/1.1 200 OK\r\n"
        "Content-Type: application/x-ndjson\r\n"
        "Transfer-Encoding: chunked\r\n"
        "\r\n"
        "12\r\n"
        "{\"n\":1,\"m\":\"hi\"}\n"
        "\r\n"
        "13\r\n"
        "{\"n\":2,\"m\":\"bye\"}\n"
        "\r\n"
        "0\r\n\r\n"
    );
    net_stub_queue_recv_close();

    c = net_connect("spark.hitorro.com", 11434, 5000, &err);
    CHECK(c != 0, "conn");

    CHECK(http_open_from_request(&hs, c, "REQ", 3, 5000) == 0, "hs_open");
    CHECK(hs.status == 200, "hs_status");
    CHECK(hs.chunked == 1, "hs_chunked");

    rl = http_read_line(&hs, line, sizeof(line));
    CHECK(rl == 1 && strcmp(line, "{\"n\":1,\"m\":\"hi\"}") == 0, "hs_line1");
    rl = http_read_line(&hs, line, sizeof(line));
    CHECK(rl == 1 && strcmp(line, "{\"n\":2,\"m\":\"bye\"}") == 0, "hs_line2");
    rl = http_read_line(&hs, line, sizeof(line));
    CHECK(rl == 0, "hs_eof");
    http_stream_close(&hs);
}

/* ─── llm_direct end-to-end with scripted stream ─────────────────────── */

typedef struct {
    char tokens[2048];
    int  tlen;
    int  saw_done;
    char last_tool_name[64];
    char last_tool_args[256];
} StreamCapture;

static void capture_cb(const LlmDelta *d, void *ud)
{
    StreamCapture *c = (StreamCapture *)ud;
    if (d->content) {
        int i = 0;
        while (d->content[i] && c->tlen < (int)sizeof(c->tokens) - 1) {
            c->tokens[c->tlen++] = d->content[i++];
        }
        c->tokens[c->tlen] = '\0';
    }
    if (d->tool_name) {
        int i = 0;
        while (d->tool_name[i] && i < (int)sizeof(c->last_tool_name) - 1) {
            c->last_tool_name[i] = d->tool_name[i]; i++;
        }
        c->last_tool_name[i] = '\0';
    }
    if (d->tool_args) {
        int i = 0;
        while (d->tool_args[i] && i < (int)sizeof(c->last_tool_args) - 1) {
            c->last_tool_args[i] = d->tool_args[i]; i++;
        }
        c->last_tool_args[i] = '\0';
    }
    if (d->done) c->saw_done = 1;
}

static void test_llm_chat_stream_plain(void)
{
    LlmConfig cfg = { "spark.hitorro.com", 11434, "nemotron", 5000, 5000 };
    LlmMessages msgs;
    StreamCapture cap;
    int sent_len;
    const char *sent;
    memset(&cap, 0, sizeof(cap));
    llm_msgs_init(&msgs);
    llm_msgs_add(&msgs, LLM_ROLE_SYSTEM, "You are terse.");
    llm_msgs_add(&msgs, LLM_ROLE_USER, "Hi.");

    net_stub_reset();
    net_stub_queue_recv(
        "HTTP/1.1 200 OK\r\n"
        "Transfer-Encoding: chunked\r\n"
        "\r\n"
        /* three frames — chunk sizes are the exact byte count in hex */
        "2a\r\n{\"message\":{\"content\":\"He\"},\"done\":false}\n\r\n"
        "2c\r\n{\"message\":{\"content\":\"llo!\"},\"done\":false}\n\r\n"
        "27\r\n{\"message\":{\"content\":\"\"},\"done\":true}\n\r\n"
        "0\r\n\r\n"
    );
    net_stub_queue_recv_close();

    CHECK(llm_chat_stream(&cfg, &msgs, capture_cb, &cap, 0, 0, 0, 0) == 0,
          "llm_ok");
    CHECK(strcmp(cap.tokens, "Hello!") == 0, "llm_tokens");
    CHECK(cap.saw_done == 1, "llm_done");
    CHECK(cap.last_tool_name[0] == '\0', "llm_no_tool");

    /* Assert host/port propagated */
    CHECK(strcmp(net_stub_last_host(), "spark.hitorro.com") == 0, "llm_host");
    CHECK(net_stub_last_port() == 11434, "llm_port");

    /* Assert request has POST + Host header + JSON body */
    sent = net_stub_send_captured(&sent_len);
    CHECK(strstr(sent, "POST /api/chat HTTP/1.1\r\n") != 0, "llm_req_line");
    CHECK(strstr(sent, "Host: spark.hitorro.com:11434\r\n") != 0, "llm_req_host");
    CHECK(strstr(sent, "\"model\":\"nemotron\"") != 0, "llm_body_model");
    CHECK(strstr(sent, "\"content\":\"You are terse.\"") != 0, "llm_body_sys");
    CHECK(strstr(sent, "\"content\":\"Hi.\"") != 0, "llm_body_user");
}

static void test_llm_chat_stream_tool_call(void)
{
    LlmConfig cfg = { "spark.hitorro.com", 11434, "nemotron", 5000, 5000 };
    LlmMessages msgs;
    StreamCapture cap;
    memset(&cap, 0, sizeof(cap));
    llm_msgs_init(&msgs);
    llm_msgs_add(&msgs, LLM_ROLE_USER, "list DH2:Dev");

    net_stub_reset();
    /* Final frame carries the tool_calls array (Ollama's shape). */
    net_stub_queue_recv(
        "HTTP/1.1 200 OK\r\n"
        "Transfer-Encoding: chunked\r\n"
        "\r\n"
        "2c\r\n{\"message\":{\"content\":\"sure\"},\"done\":false}\n\r\n"
        "81\r\n{\"message\":{\"content\":\"\",\"tool_calls\":[{\"function\":{\"name\":\"run_command\",\"arguments\":{\"command\":\"List DH2:Dev\"}}}]},\"done\":true}\n\r\n"
        "0\r\n\r\n"
    );
    net_stub_queue_recv_close();

    CHECK(llm_chat_stream(&cfg, &msgs, capture_cb, &cap, 0, 0, 0, 0) == 0,
          "llm_tool_ok");
    CHECK(strcmp(cap.tokens, "sure") == 0, "llm_tool_tokens");
    CHECK(strcmp(cap.last_tool_name, "run_command") == 0, "llm_tool_name");
    /* tool_args is now the raw arguments object so bi_ask can pluck
     * different keys per tool (command/path/content/…). */
    CHECK(strcmp(cap.last_tool_args, "{\"command\":\"List DH2:Dev\"}") == 0,
          "llm_tool_args");
    CHECK(cap.saw_done == 1, "llm_tool_done");
}

/* ─── run all ────────────────────────────────────────────────────────── */

int main(void)
{
    test_jsonx_basic();
    test_jsonx_arrays_and_escapes();
    test_jsonx_unicode_escape();
    test_http_build_post();
    test_http_stream_chunked_ndjson();
    test_llm_chat_stream_plain();
    test_llm_chat_stream_tool_call();

    fprintf(stderr, "\n%d passed, %d failed\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
