#include "jsonx.h"

#include <string.h>

/* ─── tokeniser primitives ────────────────────────────────────────────── */

static void skip_ws(const char **p)
{
    while (**p == ' ' || **p == '\t' || **p == '\n' || **p == '\r') (*p)++;
}

/* Advance *p past one JSON value (any type). Returns 0 on success. */
static int skip_value(const char **p);

static int skip_string(const char **p)
{
    if (**p != '"') return -1;
    (*p)++;
    while (**p) {
        if (**p == '\\') {
            (*p)++;
            if (!**p) return -1;
            (*p)++;
        } else if (**p == '"') {
            (*p)++;
            return 0;
        } else {
            (*p)++;
        }
    }
    return -1;
}

static int skip_object(const char **p)
{
    if (**p != '{') return -1;
    (*p)++;
    for (;;) {
        skip_ws(p);
        if (**p == '}') { (*p)++; return 0; }
        if (skip_string(p) < 0) return -1;
        skip_ws(p);
        if (**p != ':') return -1;
        (*p)++;
        skip_ws(p);
        if (skip_value(p) < 0) return -1;
        skip_ws(p);
        if (**p == ',') { (*p)++; continue; }
        if (**p == '}') { (*p)++; return 0; }
        return -1;
    }
}

static int skip_array(const char **p)
{
    if (**p != '[') return -1;
    (*p)++;
    for (;;) {
        skip_ws(p);
        if (**p == ']') { (*p)++; return 0; }
        if (skip_value(p) < 0) return -1;
        skip_ws(p);
        if (**p == ',') { (*p)++; continue; }
        if (**p == ']') { (*p)++; return 0; }
        return -1;
    }
}

static int skip_literal(const char **p)
{
    /* number, true, false, null — read until value-terminator */
    while (**p) {
        char c = **p;
        if (c == ',' || c == '}' || c == ']' || c == ' ' || c == '\t' ||
            c == '\n' || c == '\r' || c == '\0') return 0;
        (*p)++;
    }
    return 0;
}

static int skip_value(const char **p)
{
    skip_ws(p);
    if (**p == '"') return skip_string(p);
    if (**p == '{') return skip_object(p);
    if (**p == '[') return skip_array(p);
    return skip_literal(p);
}

/* ─── path segment parsing + descent ──────────────────────────────────── */

typedef struct {
    /* Either key (name+nlen) or array index (idx>=0). */
    const char *name;
    int         nlen;
    int         idx;   /* -1 for object member, >=0 for array index */
} PathSeg;

static const char *next_seg(const char *p, PathSeg *out)
{
    out->name = 0; out->nlen = 0; out->idx = -1;
    if (*p == '\0') return 0;
    if (*p == '[') {
        int v = 0;
        p++;
        while (*p >= '0' && *p <= '9') { v = v * 10 + (*p - '0'); p++; }
        if (*p != ']') return 0;
        p++;
        out->idx = v;
        return p;
    }
    if (*p == '.') p++;
    out->name = p;
    while (*p && *p != '.' && *p != '[') p++;
    out->nlen = (int)(p - out->name);
    if (out->nlen == 0) return 0;
    return p;
}

/* Given *p at the start of a JSON value, descend into member `name` (of
 * length nlen). Returns pointer positioned at the value, or NULL. */
static const char *descend_object(const char *p, const char *name, int nlen)
{
    skip_ws(&p);
    if (*p != '{') return 0;
    p++;
    for (;;) {
        const char *k;
        int klen;
        skip_ws(&p);
        if (*p == '}') return 0;
        if (*p != '"') return 0;
        p++;
        k = p;
        while (*p && *p != '"') {
            if (*p == '\\' && p[1]) p += 2;
            else p++;
        }
        if (*p != '"') return 0;
        klen = (int)(p - k);
        p++;   /* past closing quote */
        skip_ws(&p);
        if (*p != ':') return 0;
        p++;
        skip_ws(&p);
        if (klen == nlen && memcmp(k, name, (size_t)nlen) == 0) return p;
        if (skip_value(&p) < 0) return 0;
        skip_ws(&p);
        if (*p == ',') { p++; continue; }
        return 0;
    }
}

static const char *descend_array(const char *p, int idx)
{
    int cur = 0;
    skip_ws(&p);
    if (*p != '[') return 0;
    p++;
    for (;;) {
        skip_ws(&p);
        if (*p == ']') return 0;
        if (cur == idx) return p;
        if (skip_value(&p) < 0) return 0;
        skip_ws(&p);
        if (*p == ',') { p++; cur++; continue; }
        return 0;
    }
}

static const char *walk(const char *src, const char *path)
{
    PathSeg seg;
    const char *p = src;
    if (*path == '$') path++;
    while (*path) {
        path = next_seg(path, &seg);
        if (!path) return 0;
        if (seg.idx >= 0) {
            p = descend_array(p, seg.idx);
        } else {
            p = descend_object(p, seg.name, seg.nlen);
        }
        if (!p) return 0;
    }
    skip_ws(&p);
    return p;
}

/* ─── unescape ────────────────────────────────────────────────────────── */

static int hex4(const char *p)
{
    int v = 0, i;
    for (i = 0; i < 4; i++) {
        int d;
        char c = p[i];
        if (c >= '0' && c <= '9') d = c - '0';
        else if (c >= 'a' && c <= 'f') d = 10 + c - 'a';
        else if (c >= 'A' && c <= 'F') d = 10 + c - 'A';
        else return -1;
        v = v * 16 + d;
    }
    return v;
}

static JsonxResult unescape_string(const char *src, char *out, int outSize)
{
    int o = 0;
    const char *p;
    if (*src != '"') return JSONX_TYPE_MISMATCH;
    p = src + 1;
    while (*p) {
        if (*p == '"') { out[o] = '\0'; return JSONX_OK; }
        if (*p == '\\') {
            char c = p[1];
            p += 2;
            switch (c) {
                case 'n': if (o < outSize - 1) out[o++] = '\n'; break;
                case 'r': if (o < outSize - 1) out[o++] = '\r'; break;
                case 't': if (o < outSize - 1) out[o++] = '\t'; break;
                case '"': if (o < outSize - 1) out[o++] = '"';  break;
                case '\\':if (o < outSize - 1) out[o++] = '\\'; break;
                case '/': if (o < outSize - 1) out[o++] = '/';  break;
                case 'b': if (o < outSize - 1) out[o++] = 8;    break;
                case 'f': if (o < outSize - 1) out[o++] = 12;   break;
                case 'u': {
                    int cp = hex4(p);
                    if (cp < 0) return JSONX_PARSE_ERROR;
                    p += 4;
                    /* Encode as UTF-8. BMP-only; surrogate pairs collapse to
                     * '?' — good enough for Ollama tokens which rarely emit
                     * astral chars anyway. */
                    if (cp >= 0xD800 && cp <= 0xDFFF) {
                        if (o < outSize - 1) out[o++] = '?';
                    } else if (cp < 0x80) {
                        if (o < outSize - 1) out[o++] = (char)cp;
                    } else if (cp < 0x800) {
                        if (o + 1 < outSize - 1) {
                            out[o++] = (char)(0xC0 | (cp >> 6));
                            out[o++] = (char)(0x80 | (cp & 0x3F));
                        }
                    } else {
                        if (o + 2 < outSize - 1) {
                            out[o++] = (char)(0xE0 | (cp >> 12));
                            out[o++] = (char)(0x80 | ((cp >> 6) & 0x3F));
                            out[o++] = (char)(0x80 | (cp & 0x3F));
                        }
                    }
                    break;
                }
                default: return JSONX_PARSE_ERROR;
            }
            if (o >= outSize - 1) return JSONX_BUF_TOO_SMALL;
        } else {
            if (o < outSize - 1) out[o++] = *p;
            else return JSONX_BUF_TOO_SMALL;
            p++;
        }
    }
    return JSONX_PARSE_ERROR;   /* unterminated */
}

/* ─── public API ──────────────────────────────────────────────────────── */

JsonxResult jsonx_string(const char *src, const char *path,
                         char *out, int outSize)
{
    const char *v = walk(src, path);
    if (!v) return JSONX_NOT_FOUND;
    if (*v != '"') return JSONX_TYPE_MISMATCH;
    return unescape_string(v, out, outSize);
}

JsonxResult jsonx_bool(const char *src, const char *path, int *out)
{
    const char *v = walk(src, path);
    if (!v) return JSONX_NOT_FOUND;
    if (v[0] == 't' && strncmp(v, "true", 4) == 0)  { *out = 1; return JSONX_OK; }
    if (v[0] == 'f' && strncmp(v, "false", 5) == 0) { *out = 0; return JSONX_OK; }
    return JSONX_TYPE_MISMATCH;
}

const char *jsonx_raw(const char *src, const char *path, int *out_len)
{
    const char *v = walk(src, path);
    const char *end;
    if (!v) return 0;
    end = v;
    if (skip_value(&end) < 0) return 0;
    if (out_len) *out_len = (int)(end - v);
    return v;
}
