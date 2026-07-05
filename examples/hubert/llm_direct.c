#include "llm_direct.h"

#include "http.h"
#include "jsonx.h"
#include "net.h"

#include <string.h>

/* ─── message list ───────────────────────────────────────────────────── */

void llm_msgs_init(LlmMessages *m) { m->count = 0; }

int llm_msgs_add(LlmMessages *m, LlmRole role, const char *content)
{
    int i;
    if (m->count >= LLM_MAX_MESSAGES) return 0;
    m->entries[m->count].role = role;
    for (i = 0; content[i] && i < LLM_MAX_MESSAGE_LEN - 1; i++) {
        m->entries[m->count].content[i] = content[i];
    }
    m->entries[m->count].content[i] = '\0';
    m->count++;
    return 1;
}

/* ─── JSON body builder ──────────────────────────────────────────────── */

static const char *role_name(LlmRole r)
{
    switch (r) {
        case LLM_ROLE_SYSTEM:    return "system";
        case LLM_ROLE_USER:      return "user";
        case LLM_ROLE_ASSISTANT: return "assistant";
        case LLM_ROLE_TOOL:      return "tool";
    }
    return "user";
}

static int put(char *dst, int cap, int off, const char *s)
{
    while (*s) {
        if (off >= cap - 1) return -1;
        dst[off++] = *s++;
    }
    dst[off] = '\0';
    return off;
}

static int put_json_string(char *dst, int cap, int off, const char *s)
{
    if (off >= cap - 1) return -1;
    dst[off++] = '"';
    while (*s) {
        char c = *s++;
        if (off >= cap - 2) return -1;
        switch (c) {
            case '"':  dst[off++] = '\\'; dst[off++] = '"';  break;
            case '\\': dst[off++] = '\\'; dst[off++] = '\\'; break;
            case '\n': dst[off++] = '\\'; dst[off++] = 'n';  break;
            case '\r': dst[off++] = '\\'; dst[off++] = 'r';  break;
            case '\t': dst[off++] = '\\'; dst[off++] = 't';  break;
            default:
                if ((unsigned char)c < 0x20) {
                    /* Skip control chars — they're rare and hard to encode
                     * without printf. */
                    dst[off++] = '?';
                } else {
                    dst[off++] = c;
                }
        }
    }
    if (off >= cap - 1) return -1;
    dst[off++] = '"';
    dst[off] = '\0';
    return off;
}

/* Build the request body. Fixed set of tools (just run_command for now). */
static int build_body(char *dst, int cap, const char *model,
                      const LlmMessages *msgs)
{
    int off = 0;
    int i;
    off = put(dst, cap, off, "{\"model\":");   if (off < 0) return -1;
    off = put_json_string(dst, cap, off, model); if (off < 0) return -1;
    off = put(dst, cap, off, ",\"stream\":true,\"messages\":["); if (off < 0) return -1;
    for (i = 0; i < msgs->count; i++) {
        if (i > 0) { off = put(dst, cap, off, ","); if (off < 0) return -1; }
        off = put(dst, cap, off, "{\"role\":"); if (off < 0) return -1;
        off = put_json_string(dst, cap, off, role_name(msgs->entries[i].role));
        if (off < 0) return -1;
        off = put(dst, cap, off, ",\"content\":");
        if (off < 0) return -1;
        off = put_json_string(dst, cap, off, msgs->entries[i].content);
        if (off < 0) return -1;
        off = put(dst, cap, off, "}"); if (off < 0) return -1;
    }
    /* Tool suite exposed to the model. Keep these compact — the whole
     * request body must fit in the 16KB body buffer. Names/args match
     * what bi_ask dispatches on. */
    off = put(dst, cap, off,
        "],\"tools\":["

        "{\"type\":\"function\",\"function\":{"
        "\"name\":\"run_command\","
        "\"description\":\"Run an AmigaDOS command line and return its exit code plus stdout. Use for any shell operation (dir, type, copy, execute programs, etc.).\","
        "\"parameters\":{\"type\":\"object\",\"properties\":{"
        "\"command\":{\"type\":\"string\",\"description\":\"The AmigaDOS command line to run.\"}"
        "},\"required\":[\"command\"]}}},"

        "{\"type\":\"function\",\"function\":{"
        "\"name\":\"read_file\","
        "\"description\":\"Read a text file from the AmigaOS filesystem and return its contents. Prefer for inspecting files rather than piping type through run_command.\","
        "\"parameters\":{\"type\":\"object\",\"properties\":{"
        "\"path\":{\"type\":\"string\",\"description\":\"AmigaDOS path, e.g. S:User-Startup or DH2:Dev/foo.\"}"
        "},\"required\":[\"path\"]}}},"

        "{\"type\":\"function\",\"function\":{"
        "\"name\":\"write_file\","
        "\"description\":\"Write text content to a file, overwriting if it exists. Use with care.\","
        "\"parameters\":{\"type\":\"object\",\"properties\":{"
        "\"path\":{\"type\":\"string\",\"description\":\"Destination AmigaDOS path.\"},"
        "\"content\":{\"type\":\"string\",\"description\":\"The text to write.\"}"
        "},\"required\":[\"path\",\"content\"]}}},"

        "{\"type\":\"function\",\"function\":{"
        "\"name\":\"list_dir\","
        "\"description\":\"List a directory. Returns filenames one per line.\","
        "\"parameters\":{\"type\":\"object\",\"properties\":{"
        "\"path\":{\"type\":\"string\",\"description\":\"AmigaDOS directory path (e.g. SYS:).\"}"
        "},\"required\":[\"path\"]}}},"

        "{\"type\":\"function\",\"function\":{"
        "\"name\":\"system_info\","
        "\"description\":\"Return brief info about the running AmigaOS (Kickstart, Workbench, free chip/fast RAM). No parameters.\","
        "\"parameters\":{\"type\":\"object\",\"properties\":{}}}}"

        "]}"
    );
    if (off < 0) return -1;
    return off;
}

/* ─── streaming loop ─────────────────────────────────────────────────── */

int llm_chat_stream(const LlmConfig *cfg,
                    const LlmMessages *msgs,
                    LlmDeltaFn cb, void *userdata,
                    char *out_tool_name, int tn_size,
                    char *out_tool_args, int ta_size)
{
    static char body[16384];
    static char request[20000];
    static char line[HTTP_MAX_LINE];
    static char scratch[HTTP_MAX_LINE];
    int body_len, req_len, rc;
    NetConn *conn;
    NetResult nerr;
    HttpStream hs;
    LlmDelta delta;

    if (out_tool_name && tn_size > 0) out_tool_name[0] = '\0';
    if (out_tool_args && ta_size > 0) out_tool_args[0] = '\0';

    body_len = build_body(body, (int)sizeof(body), cfg->model, msgs);
    if (body_len < 0) return -1;

    req_len = http_build_post(request, (int)sizeof(request),
                              "/api/chat", cfg->host, cfg->port,
                              "application/json", body, body_len);
    if (req_len < 0) return -1;

    conn = net_connect(cfg->host, cfg->port, cfg->connect_timeout_ms, &nerr);
    if (!conn) return -2;

    if (http_open_from_request(&hs, conn, request, req_len,
                               cfg->io_timeout_ms) != 0) {
        net_close(conn); return -3;
    }
    if (hs.status < 200 || hs.status >= 300) {
        http_stream_close(&hs); return -4;
    }

    for (;;) {
        int rl = http_read_line(&hs, line, (int)sizeof(line));
        if (rl == 0) break;
        if (rl < 0) { http_stream_close(&hs); return -5; }
        if (line[0] == '\0') continue;

        /* Decode this NDJSON frame. */
        memset(&delta, 0, sizeof(delta));
        rc = jsonx_string(line, "message.content", scratch, (int)sizeof(scratch));
        if (rc == JSONX_OK) delta.content = scratch;
        else                delta.content = "";

        /* Tool call check. */
        {
            int tn_len = 0;
            const char *tc = jsonx_raw(line, "message.tool_calls", &tn_len);
            if (tc) {
                static char tname[128];
                static char targs[LLM_MAX_MESSAGE_LEN];
                if (jsonx_string(line,
                    "message.tool_calls[0].function.name", tname, (int)sizeof(tname)) == JSONX_OK) {
                    delta.tool_name = tname;
                }
                /* Return the raw JSON of the arguments object so the caller
                 * can pluck out whatever keys the tool needs (command / path
                 * / content / ...). Ollama sometimes emits arguments as a
                 * JSON string rather than an object; try to unquote in
                 * that case so downstream jsonx_string still works. */
                {
                    int al;
                    const char *raw = jsonx_raw(line,
                        "message.tool_calls[0].function.arguments", &al);
                    if (raw) {
                        if (*raw == '"') {
                            /* JSON string — unescape into targs */
                            (void)jsonx_string(line,
                                "message.tool_calls[0].function.arguments",
                                targs, (int)sizeof(targs));
                            delta.tool_args = targs;
                        } else {
                            int n = al < (int)sizeof(targs) - 1 ? al : (int)sizeof(targs) - 1;
                            memcpy(targs, raw, (size_t)n);
                            targs[n] = '\0';
                            delta.tool_args = targs;
                        }
                    }
                }
                if (delta.tool_name && out_tool_name && tn_size > 0) {
                    int i = 0;
                    while (delta.tool_name[i] && i < tn_size - 1) {
                        out_tool_name[i] = delta.tool_name[i]; i++;
                    }
                    out_tool_name[i] = '\0';
                }
                if (delta.tool_args && out_tool_args && ta_size > 0) {
                    int i = 0;
                    while (delta.tool_args[i] && i < ta_size - 1) {
                        out_tool_args[i] = delta.tool_args[i]; i++;
                    }
                    out_tool_args[i] = '\0';
                }
            }
        }

        {
            int done = 0;
            if (jsonx_bool(line, "done", &done) == JSONX_OK && done) delta.done = 1;
        }

        if (cb) cb(&delta, userdata);
        if (delta.done) break;
    }

    http_stream_close(&hs);
    return 0;
}
