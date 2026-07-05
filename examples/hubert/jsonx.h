/*
 * jsonx.h — pinpoint value extraction from a JSON object.
 *
 * We don't need a general JSON parser to consume Ollama's response, we
 * need a couple of paths::
 *
 *   jsonx_string("message.content")    - extract & unescape a string
 *   jsonx_bool  ("done")               - extract a boolean
 *   jsonx_string("message.tool_calls[0].function.name")
 *   jsonx_string("message.tool_calls[0].function.arguments.command")
 *
 * The parser is tokeniser-driven and skips over structures cleanly, so a
 * malformed input just yields "no match" rather than corrupting anything.
 *
 * Path syntax: dots for member access, [N] for array indexing. Leading '$'
 * is accepted and ignored.
 */
#ifndef HUBERT_JSONX_H
#define HUBERT_JSONX_H

typedef enum {
    JSONX_OK = 0,
    JSONX_NOT_FOUND,
    JSONX_TYPE_MISMATCH,
    JSONX_BUF_TOO_SMALL,
    JSONX_PARSE_ERROR,
} JsonxResult;

/* Extract the string at `path` from the JSON in `src`. Unescapes \n, \r,
 * \t, \", \\, \/, \uXXXX (BMP only, encoded as UTF-8). Writes a NUL
 * terminator. */
JsonxResult jsonx_string(const char *src, const char *path,
                         char *out, int outSize);

/* Extract a boolean. */
JsonxResult jsonx_bool(const char *src, const char *path, int *out);

/* Return a pointer to the raw substring of the value at `path` (uncopied,
 * un-unescaped). Useful for "does this key exist?" checks. NULL if not
 * found. */
const char *jsonx_raw(const char *src, const char *path, int *out_len);

#endif
