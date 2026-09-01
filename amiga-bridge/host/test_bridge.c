// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Chris Collins

/*
 * test_bridge.c - Host unit tests for pure-C code extracted from the bridge
 * daemon. Builds with clang on Linux/macOS. See amiga-bridge/host/Makefile.
 *
 * Each test function returns 0 on success, non-zero on failure. main() runs
 * them all and reports pass/fail counts. Exit code is the number of failures
 * so `make test` fails visibly in CI.
 */
#include "script_util.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stddef.h>

/* --- Assertion helpers ---------------------------------------------------- */

static int _failures_this_test = 0;

#define ASSERT(cond) \
    do { \
        if (!(cond)) { \
            fprintf(stderr, "  ASSERT failed at %s:%d: %s\n", \
                    __FILE__, __LINE__, #cond); \
            _failures_this_test++; \
        } \
    } while (0)

#define ASSERT_EQ_STR(actual, expected) \
    do { \
        if (strcmp((actual), (expected)) != 0) { \
            fprintf(stderr, "  ASSERT_EQ_STR failed at %s:%d\n", \
                    __FILE__, __LINE__); \
            fprintf(stderr, "    expected: %s\n", (expected)); \
            fprintf(stderr, "    actual:   %s\n", (actual)); \
            _failures_this_test++; \
        } \
    } while (0)

#define ASSERT_EQ_MEM(actual, expected, len) \
    do { \
        if (memcmp((actual), (expected), (len)) != 0) { \
            fprintf(stderr, "  ASSERT_EQ_MEM failed at %s:%d (len=%zu)\n", \
                    __FILE__, __LINE__, (size_t)(len)); \
            _failures_this_test++; \
        } \
    } while (0)

#define ASSERT_EQ_SIZE(actual, expected) \
    do { \
        size_t _a = (size_t)(actual), _e = (size_t)(expected); \
        if (_a != _e) { \
            fprintf(stderr, "  ASSERT_EQ_SIZE failed at %s:%d: " \
                    "expected %zu, got %zu\n", \
                    __FILE__, __LINE__, _e, _a); \
            _failures_this_test++; \
        } \
    } while (0)

/* --- Capture callback ----------------------------------------------------- */

typedef struct {
    char  *buf;
    size_t cap;
    size_t len;
} Sink;

static void sink_write(const void *data, size_t n, void *ctx)
{
    Sink *s = (Sink *)ctx;
    if (s->len + n > s->cap) {
        /* Silently drop — test will fail via a length check. */
        return;
    }
    memcpy(s->buf + s->len, data, n);
    s->len += n;
}

static Sink make_sink(char *buf, size_t cap)
{
    Sink s = { buf, cap, 0 };
    return s;
}

/* --- Tests --------------------------------------------------------------- */

/* Empty payload writes exactly one newline. */
static int test_empty_payload(void)
{
    _failures_this_test = 0;
    char out[16] = {0};
    char scratch[480];
    Sink s = make_sink(out, sizeof(out));

    script_write_semicolon_delimited("", 0, scratch, sizeof(scratch),
                                     sink_write, &s);

    ASSERT_EQ_SIZE(s.len, 1);
    ASSERT(out[0] == '\n');
    return _failures_this_test;
}

/* Short payload with no semicolons gets a trailing newline appended. */
static int test_short_no_semicolons(void)
{
    _failures_this_test = 0;
    char out[32] = {0};
    char scratch[480];
    Sink s = make_sink(out, sizeof(out));

    script_write_semicolon_delimited("hello", 5, scratch, sizeof(scratch),
                                     sink_write, &s);

    ASSERT_EQ_SIZE(s.len, 6);
    ASSERT_EQ_MEM(out, "hello\n", 6);
    return _failures_this_test;
}

/* Semicolons become newlines; final byte becomes newline so no double NL. */
static int test_short_with_semicolons(void)
{
    _failures_this_test = 0;
    char out[32] = {0};
    char scratch[480];
    Sink s = make_sink(out, sizeof(out));

    script_write_semicolon_delimited("a;b;c", 5, scratch, sizeof(scratch),
                                     sink_write, &s);

    ASSERT_EQ_SIZE(s.len, 6);
    ASSERT_EQ_MEM(out, "a\nb\nc\n", 6);
    return _failures_this_test;
}

/* Trailing semicolon becomes '\n' — no redundant newline appended. */
static int test_trailing_semicolon(void)
{
    _failures_this_test = 0;
    char out[32] = {0};
    char scratch[480];
    Sink s = make_sink(out, sizeof(out));

    script_write_semicolon_delimited("a;b;", 4, scratch, sizeof(scratch),
                                     sink_write, &s);

    ASSERT_EQ_SIZE(s.len, 4);
    ASSERT_EQ_MEM(out, "a\nb\n", 4);
    return _failures_this_test;
}

/* Payload already ending in '\n' — no double newline appended. */
static int test_trailing_newline_preserved(void)
{
    _failures_this_test = 0;
    char out[32] = {0};
    char scratch[480];
    Sink s = make_sink(out, sizeof(out));

    script_write_semicolon_delimited("hello\n", 6, scratch, sizeof(scratch),
                                     sink_write, &s);

    ASSERT_EQ_SIZE(s.len, 6);
    ASSERT_EQ_MEM(out, "hello\n", 6);
    return _failures_this_test;
}

/* Payload of exactly bufsize bytes — single chunk, no truncation. */
static int test_exactly_bufsize(void)
{
    _failures_this_test = 0;
    enum { BUFSIZE = 480 };
    char src[BUFSIZE];
    char out[BUFSIZE + 2];
    char scratch[BUFSIZE];
    memset(src, 'x', BUFSIZE);
    memset(out, 0, sizeof(out));
    Sink s = make_sink(out, sizeof(out));

    script_write_semicolon_delimited(src, BUFSIZE, scratch, BUFSIZE,
                                     sink_write, &s);

    /* BUFSIZE 'x's, then a trailing '\n' because last byte was 'x' not '\n'. */
    ASSERT_EQ_SIZE(s.len, BUFSIZE + 1);
    ASSERT_EQ_MEM(out, src, BUFSIZE);
    ASSERT(out[BUFSIZE] == '\n');
    return _failures_this_test;
}

/* Payload one byte larger than bufsize — REGRESSION TEST for the pre-v1.20
 * truncation bug that dropped bytes past index 479. Must span two chunks. */
static int test_over_bufsize_no_truncation(void)
{
    _failures_this_test = 0;
    enum { BUFSIZE = 480, LEN = 481 };
    char src[LEN];
    char out[LEN + 2];
    char scratch[BUFSIZE];
    /* Distinct final byte so we can detect truncation. */
    memset(src, 'x', LEN);
    src[LEN - 1] = 'Z';
    memset(out, 0, sizeof(out));
    Sink s = make_sink(out, sizeof(out));

    script_write_semicolon_delimited(src, LEN, scratch, BUFSIZE,
                                     sink_write, &s);

    /* Old buggy code stopped at 479 bytes and appended one '\n' = 480 total.
     * Correct code writes all 481 bytes plus a trailing '\n' = 482 total,
     * with 'Z' at index 480. */
    ASSERT_EQ_SIZE(s.len, LEN + 1);
    ASSERT(out[LEN - 1] == 'Z');   /* the tail byte survives */
    ASSERT(out[LEN] == '\n');      /* trailing newline appended */
    return _failures_this_test;
}

/* Long payload (~2 KB, several chunks). Every byte accounted for. */
static int test_long_payload_survives(void)
{
    _failures_this_test = 0;
    enum { BUFSIZE = 480, LEN = 2048 };
    char src[LEN];
    char out[LEN + 2];
    char scratch[BUFSIZE];
    /* Fill with 'a'..'z' cycling so the ordering can be verified. */
    for (size_t i = 0; i < LEN; i++) src[i] = (char)('a' + (i % 26));
    memset(out, 0, sizeof(out));
    Sink s = make_sink(out, sizeof(out));

    script_write_semicolon_delimited(src, LEN, scratch, BUFSIZE,
                                     sink_write, &s);

    ASSERT_EQ_SIZE(s.len, LEN + 1);
    ASSERT_EQ_MEM(out, src, LEN);
    ASSERT(out[LEN] == '\n');
    return _failures_this_test;
}

/* Semicolon that lands exactly at a chunk boundary still gets translated. */
static int test_semicolon_at_chunk_boundary(void)
{
    _failures_this_test = 0;
    enum { BUFSIZE = 16 };
    /* Semicolon at index 15 — last byte of first chunk under BUFSIZE=16. */
    const char *src = "aaaaaaaaaaaaaaa;bbb";  /* 15 'a's, ';', 3 'b's = 19 */
    size_t len = 19;
    char out[32] = {0};
    char scratch[BUFSIZE];
    Sink s = make_sink(out, sizeof(out));

    script_write_semicolon_delimited(src, len, scratch, BUFSIZE,
                                     sink_write, &s);

    ASSERT_EQ_SIZE(s.len, 20);
    ASSERT_EQ_MEM(out, "aaaaaaaaaaaaaaa\nbbb\n", 20);
    return _failures_this_test;
}

/* Small bufsize (8 bytes) — chunker still processes correctly. Documents that
 * bufsize is a caller choice and any positive size works. */
static int test_tiny_bufsize(void)
{
    _failures_this_test = 0;
    char out[64] = {0};
    char scratch[8];
    Sink s = make_sink(out, sizeof(out));

    script_write_semicolon_delimited("one;two;three;four", 18,
                                     scratch, sizeof(scratch),
                                     sink_write, &s);

    ASSERT_EQ_SIZE(s.len, 19);
    ASSERT_EQ_MEM(out, "one\ntwo\nthree\nfour\n", 19);
    return _failures_this_test;
}

/* --- Runner -------------------------------------------------------------- */

typedef int (*test_fn)(void);
typedef struct { const char *name; test_fn fn; } TestCase;

static const TestCase TESTS[] = {
    { "empty_payload",              test_empty_payload },
    { "short_no_semicolons",        test_short_no_semicolons },
    { "short_with_semicolons",      test_short_with_semicolons },
    { "trailing_semicolon",         test_trailing_semicolon },
    { "trailing_newline_preserved", test_trailing_newline_preserved },
    { "exactly_bufsize",            test_exactly_bufsize },
    { "over_bufsize_no_truncation", test_over_bufsize_no_truncation },
    { "long_payload_survives",      test_long_payload_survives },
    { "semicolon_at_chunk_boundary",test_semicolon_at_chunk_boundary },
    { "tiny_bufsize",               test_tiny_bufsize },
};
static const size_t NUM_TESTS = sizeof(TESTS) / sizeof(TESTS[0]);

int main(void)
{
    int total_failures = 0;
    int failed_test_count = 0;

    for (size_t i = 0; i < NUM_TESTS; i++) {
        printf("[%zu/%zu] %s ... ", i + 1, NUM_TESTS, TESTS[i].name);
        fflush(stdout);
        int f = TESTS[i].fn();
        if (f == 0) {
            printf("PASS\n");
        } else {
            printf("FAIL (%d assertion(s))\n", f);
            failed_test_count++;
            total_failures += f;
        }
    }

    printf("\n");
    printf("Tests: %zu total, %d passed, %d failed\n",
           NUM_TESTS, (int)NUM_TESTS - failed_test_count, failed_test_count);
    printf("Assertions failed: %d\n", total_failures);
    return failed_test_count;
}
