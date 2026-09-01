// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Chris Collins

/*
 * script_util.c - Pure-C helpers, host-buildable.
 *
 * See script_util.h for the contract these functions must preserve.
 */
#include "script_util.h"
#include <string.h>

void script_write_semicolon_delimited(const char *src, size_t len,
                                      char *buf, size_t bufsize,
                                      script_write_fn write_fn, void *ctx)
{
    char last = '\0';

    while (len > 0) {
        size_t chunk = (len > bufsize) ? bufsize : len;
        size_t i;

        memcpy(buf, src, chunk);
        for (i = 0; i < chunk; i++) {
            if (buf[i] == ';') buf[i] = '\n';
        }
        write_fn(buf, chunk, ctx);
        last = buf[chunk - 1];

        src += chunk;
        len -= chunk;
    }

    if (last != '\n') {
        static const char nl = '\n';
        write_fn(&nl, 1, ctx);
    }
}
