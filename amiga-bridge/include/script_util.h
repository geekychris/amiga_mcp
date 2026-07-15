/*
 * script_util.h - Pure-C helpers extracted from protocol_handler.c so they
 * can be built and unit-tested on a host (Linux/macOS) without the AmigaOS
 * cross-compiler and without the exec/dos runtime.
 *
 * Rule for anything living here: <stddef.h> and <string.h> only, no Amiga
 * headers, no static state that would break parallel testing.
 */
#ifndef SCRIPT_UTIL_H
#define SCRIPT_UTIL_H

#include <stddef.h>

/* Signature of the destination-write callback. Mirrors the shape of a
 * buffered writer (return value ignored on Amiga because DOS Write() errors
 * can't be usefully recovered mid-script; the test harness returns 0). */
typedef void (*script_write_fn)(const void *buf, size_t len, void *ctx);

/*
 * Write `src[0..len)` to `write_fn(ctx, ...)` in chunks of at most `bufsize`
 * bytes, converting every ';' to '\n' as it goes. Appends a trailing '\n' if
 * the last byte written was NOT already '\n' (including the len == 0 case).
 *
 * `buf` is caller-owned scratch of at least `bufsize` bytes. Using a
 * caller-owned buffer instead of an internal `static` array lets multiple
 * tests run without sharing state.
 *
 * Behavioural contract that must survive future refactors:
 *   - Every byte of src is written verbatim (with ';' -> '\n' translation),
 *     even when len exceeds bufsize. This is the invariant that PR #7/#9
 *     restored after a static-buffer truncation bug capped at 479 bytes.
 *   - An empty payload (len == 0) writes exactly one '\n'.
 *   - No allocation, no globals, no libc calls other than memcpy.
 */
void script_write_semicolon_delimited(const char *src, size_t len,
                                      char *buf, size_t bufsize,
                                      script_write_fn write_fn, void *ctx);

#endif /* SCRIPT_UTIL_H */
