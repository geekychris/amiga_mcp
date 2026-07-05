#include "http.h"

#include <string.h>

/* ─── request builder ────────────────────────────────────────────────── */

static int append(char *dst, int dstSize, int off, const char *s)
{
    while (*s) {
        if (off >= dstSize - 1) return -1;
        dst[off++] = *s++;
    }
    dst[off] = '\0';
    return off;
}

static int append_num(char *dst, int dstSize, int off, long v)
{
    char stack[16];
    int sl = 0;
    long uv = v < 0 ? -v : v;
    if (v < 0) {
        if (off >= dstSize - 1) return -1;
        dst[off++] = '-';
    }
    if (uv == 0) {
        if (off >= dstSize - 1) return -1;
        dst[off++] = '0';
    } else {
        while (uv > 0 && sl < (int)sizeof(stack)) { stack[sl++] = (char)('0' + (uv % 10)); uv /= 10; }
        while (sl > 0) {
            if (off >= dstSize - 1) return -1;
            dst[off++] = stack[--sl];
        }
    }
    dst[off] = '\0';
    return off;
}

int http_build_post(char *dst, int dstSize,
                    const char *path, const char *host, int port,
                    const char *content_type, const char *body,
                    int body_len)
{
    int off = 0;
    off = append(dst, dstSize, off, "POST ");
    if (off < 0) return -1;
    off = append(dst, dstSize, off, path);           if (off < 0) return -1;
    off = append(dst, dstSize, off, " HTTP/1.1\r\nHost: "); if (off < 0) return -1;
    off = append(dst, dstSize, off, host);           if (off < 0) return -1;
    if (port != 80) {
        if (off >= dstSize - 1) return -1;
        dst[off++] = ':';
        off = append_num(dst, dstSize, off, port);   if (off < 0) return -1;
    }
    off = append(dst, dstSize, off, "\r\nUser-Agent: hubert/0.3\r\nAccept: application/x-ndjson\r\nContent-Type: ");
    if (off < 0) return -1;
    off = append(dst, dstSize, off, content_type);   if (off < 0) return -1;
    off = append(dst, dstSize, off, "\r\nContent-Length: "); if (off < 0) return -1;
    off = append_num(dst, dstSize, off, body_len);   if (off < 0) return -1;
    off = append(dst, dstSize, off, "\r\nConnection: close\r\n\r\n"); if (off < 0) return -1;
    /* body */
    {
        int i;
        for (i = 0; i < body_len; i++) {
            if (off >= dstSize - 1) return -1;
            dst[off++] = body[i];
        }
    }
    dst[off] = '\0';
    return off;
}

/* ─── streaming reader ───────────────────────────────────────────────── */

/* Fill buf from the wire (once). Returns 1 on success, 0 on peer close,
 * -1 on error. */
static int refill(HttpStream *s)
{
    int got = 0;
    int space;
    NetResult r;

    /* Compact if we've consumed anything. */
    if (s->buf_pos > 0) {
        int rem = s->buf_len - s->buf_pos;
        if (rem > 0) memmove(s->buf, s->buf + s->buf_pos, (size_t)rem);
        s->buf_len = rem;
        s->buf_pos = 0;
    }
    space = (int)sizeof(s->buf) - s->buf_len - 1;
    if (space <= 0) return -1;   /* no room */
    r = net_recv(s->conn, s->buf + s->buf_len, space, &got, s->io_timeout_ms);
    if (r == NET_ERR_CLOSED) return 0;
    if (r != NET_OK)         return -1;
    if (got == 0)            return 0;   /* treat as EOF */
    s->buf_len += got;
    s->buf[s->buf_len] = '\0';
    return 1;
}

/* Read the next CRLF-terminated line into out (without the CRLF). Returns
 * 1 on success, 0 on clean EOF before a line, -1 on error. */
static int read_wire_line(HttpStream *s, char *out, int outSize)
{
    int o = 0;
    for (;;) {
        while (s->buf_pos < s->buf_len) {
            char ch = s->buf[s->buf_pos++];
            if (ch == '\r') continue;
            if (ch == '\n') { out[o] = '\0'; return 1; }
            if (o < outSize - 1) out[o++] = ch;
        }
        {
            int r = refill(s);
            if (r <= 0) {
                if (o > 0) { out[o] = '\0'; return 1; }
                return r;
            }
        }
    }
}

/* Read exactly `n` bytes from the wire into `out`. Returns 1/0/-1. */
static int read_exact(HttpStream *s, char *out, int n)
{
    int got = 0;
    while (got < n) {
        int avail = s->buf_len - s->buf_pos;
        int take;
        if (avail <= 0) {
            int r = refill(s);
            if (r <= 0) return r;
            continue;
        }
        take = avail;
        if (take > (n - got)) take = n - got;
        memcpy(out + got, s->buf + s->buf_pos, (size_t)take);
        s->buf_pos += take;
        got += take;
    }
    return 1;
}

static int parse_status(const char *line)
{
    /* "HTTP/1.1 200 OK" — skip token, spaces, then 3 digits. */
    int i = 0;
    while (line[i] && line[i] != ' ') i++;
    while (line[i] == ' ') i++;
    if (line[i] < '0' || line[i] > '9') return -1;
    return (line[i] - '0') * 100 + (line[i+1] - '0') * 10 + (line[i+2] - '0');
}

static int lower_eq(const char *a, const char *b)
{
    while (*a && *b) {
        char ca = *a, cb = *b;
        if (ca >= 'A' && ca <= 'Z') ca = (char)(ca + 32);
        if (cb >= 'A' && cb <= 'Z') cb = (char)(cb + 32);
        if (ca != cb) return 0;
        a++; b++;
    }
    return *a == 0 && *b == 0;
}

int http_open_from_request(HttpStream *s, NetConn *c,
                           const char *request, int request_len,
                           int io_timeout_ms)
{
    NetResult r;
    char line[HTTP_MAX_LINE];
    int first = 1;

    memset(s, 0, sizeof(*s));
    s->conn = c;
    s->io_timeout_ms = io_timeout_ms;
    s->chunk_remaining = -1;

    r = net_send_all(c, request, request_len, io_timeout_ms);
    if (r != NET_OK) return -1;

    for (;;) {
        int rc = read_wire_line(s, line, (int)sizeof(line));
        if (rc <= 0) return -1;
        if (first) {
            s->status = parse_status(line);
            if (s->status < 100) return -1;
            first = 0;
            continue;
        }
        if (line[0] == '\0') break;   /* end of headers */
        /* Look for Transfer-Encoding: chunked (case-insensitive on the
         * header name; value we compare tail-loosely). */
        {
            const char *colon = strchr(line, ':');
            if (!colon) continue;
            {
                char name[64];
                int nlen = (int)(colon - line);
                int j;
                if (nlen > (int)sizeof(name) - 1) nlen = (int)sizeof(name) - 1;
                for (j = 0; j < nlen; j++) {
                    char ch = line[j];
                    if (ch >= 'A' && ch <= 'Z') ch = (char)(ch + 32);
                    name[j] = ch;
                }
                name[nlen] = '\0';
                if (strcmp(name, "transfer-encoding") == 0) {
                    const char *v = colon + 1;
                    while (*v == ' ') v++;
                    if (lower_eq(v, "chunked")) s->chunked = 1;
                }
            }
        }
    }
    return 0;
}

/* Read one NDJSON line from the response body.
 * We reassemble across chunk boundaries because Ollama sometimes splits
 * even short frames across two chunks. */
int http_read_line(HttpStream *s, char *out, int outSize)
{
    int o = 0;
    if (s->body_done) return 0;

    for (;;) {
        /* If we're between chunks, read the size line. */
        if (s->chunked && s->chunk_remaining <= 0) {
            char sizeLine[64];
            long v = 0;
            int i;
            int rc;
            /* Consume trailing CRLF from previous chunk if any — handled
             * by the size line reader (blank lines yield ""). */
            rc = read_wire_line(s, sizeLine, (int)sizeof(sizeLine));
            if (rc <= 0) {
                if (o > 0) { out[o] = '\0'; return 1; }
                return rc == 0 ? 0 : -1;
            }
            if (sizeLine[0] == '\0') continue;   /* stray blank between chunks */
            for (i = 0; sizeLine[i]; i++) {
                char ch = sizeLine[i];
                int d;
                if (ch >= '0' && ch <= '9') d = ch - '0';
                else if (ch >= 'a' && ch <= 'f') d = 10 + ch - 'a';
                else if (ch >= 'A' && ch <= 'F') d = 10 + ch - 'A';
                else break;
                v = v * 16 + d;
            }
            if (v == 0) {
                s->body_done = 1;
                if (o > 0) { out[o] = '\0'; return 1; }
                return 0;
            }
            s->chunk_remaining = v;
        }

        /* Read bytes from the current chunk (or from unchunked body)
         * looking for a newline. */
        if (s->buf_pos >= s->buf_len) {
            int r = refill(s);
            if (r <= 0) {
                if (o > 0) { out[o] = '\0'; return 1; }
                return r;
            }
        }
        {
            char ch = s->buf[s->buf_pos];
            /* If chunked, don't consume beyond the chunk. */
            if (s->chunked && s->chunk_remaining <= 0) continue;
            s->buf_pos++;
            if (s->chunked) s->chunk_remaining--;
            if (ch == '\r') continue;
            if (ch == '\n') { out[o] = '\0'; return 1; }
            if (o < outSize - 1) out[o++] = ch;
        }
    }
}

void http_stream_close(HttpStream *s)
{
    if (s && s->conn) { net_close(s->conn); s->conn = 0; }
}
