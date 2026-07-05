/*
 * http.h — minimal HTTP/1.1 client optimised for Ollama.
 *
 * We only need one shape: POST a JSON body, then read a streaming
 * ``application/x-ndjson`` response (chunked transfer-encoding). The
 * reader exposes complete NDJSON lines regardless of how the chunk
 * boundaries land, so the caller can just loop on http_read_line().
 *
 * All parsing is done on plain byte buffers — no libc printf/scanf, no
 * malloc past net_connect's one allocation. Fits ANSI C, tests
 * exercise it against the net.c host stub.
 */
#ifndef HUBERT_HTTP_H
#define HUBERT_HTTP_H

#include "net.h"

#define HTTP_MAX_LINE 4096

typedef struct HttpStream {
    NetConn *conn;
    int       status;          /* HTTP status code (200 …) */
    int       chunked;         /* transfer-encoding: chunked? */
    /* Read buffer — we may receive more bytes than we return, and we buffer
     * chunk framing bytes in here too. */
    char      buf[HTTP_MAX_LINE * 2];
    int       buf_len;         /* bytes valid in buf */
    int       buf_pos;         /* next byte to consume */
    /* When chunked: bytes remaining in current chunk (>0 means we're
     * inside a chunk body). -1 means "not started". */
    long      chunk_remaining;
    int       body_done;       /* saw the terminating 0-chunk */
    int       io_timeout_ms;
} HttpStream;

/* Build a POST request into `dst` (returns bytes written, -1 on overflow).
 * Adds Content-Length automatically from body_len. */
int http_build_post(char *dst, int dstSize,
                    const char *path, const char *host, int port,
                    const char *content_type, const char *body,
                    int body_len);

/* Send the pre-built request bytes then read headers. Populates
 * s->status, s->chunked. Returns 0 on success. */
int http_open_from_request(HttpStream *s, NetConn *c,
                           const char *request, int request_len,
                           int io_timeout_ms);

/* Read one line from the response body (works whether chunked or not).
 * Blocks up to the stream's io timeout. Writes a NUL-terminated string
 * (without trailing \n) into out.
 *
 * Returns:
 *   1  line read (of length >=0)
 *   0  end of body — no more data
 *  -1  I/O error / timeout */
int http_read_line(HttpStream *s, char *out, int outSize);

void http_stream_close(HttpStream *s);

#endif
