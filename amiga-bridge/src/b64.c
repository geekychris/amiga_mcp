/*
 * b64.c - Base64 encode/decode for the bridge line protocol.
 *
 * Base64 packs 3 bytes into 4 printable chars (33% overhead) versus hex's
 * 100%, and its alphabet (A-Za-z0-9+/ with '=' padding) contains no '|' or
 * '\n', so it is safe inside the pipe-delimited, newline-terminated protocol.
 * Used for the bulk binary payloads (screenshots, file transfer, memory).
 */
#include <exec/types.h>
#include "bridge_internal.h"

static const char b64_alphabet[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

/*
 * Encode `len` bytes into base64. `out` must hold ((len+2)/3)*4 + 1 bytes.
 * NUL-terminates and returns the number of characters written.
 */
ULONG b64_encode(const UBYTE *data, ULONG len, char *out)
{
    ULONG i = 0, o = 0;

    while (i + 3 <= len) {
        ULONG n = ((ULONG)data[i] << 16) | ((ULONG)data[i + 1] << 8) | data[i + 2];
        out[o++] = b64_alphabet[(n >> 18) & 63];
        out[o++] = b64_alphabet[(n >> 12) & 63];
        out[o++] = b64_alphabet[(n >> 6) & 63];
        out[o++] = b64_alphabet[n & 63];
        i += 3;
    }
    if (len - i == 1) {
        ULONG n = (ULONG)data[i] << 16;
        out[o++] = b64_alphabet[(n >> 18) & 63];
        out[o++] = b64_alphabet[(n >> 12) & 63];
        out[o++] = '=';
        out[o++] = '=';
    } else if (len - i == 2) {
        ULONG n = ((ULONG)data[i] << 16) | ((ULONG)data[i + 1] << 8);
        out[o++] = b64_alphabet[(n >> 18) & 63];
        out[o++] = b64_alphabet[(n >> 12) & 63];
        out[o++] = b64_alphabet[(n >> 6) & 63];
        out[o++] = '=';
    }
    out[o] = '\0';
    return o;
}

/*
 * Decode `inlen` base64 chars into bytes. Skips padding and any stray
 * whitespace/non-alphabet chars. `out` must hold (inlen/4)*3 bytes.
 * Returns the number of bytes written.
 */
ULONG b64_decode(const char *in, ULONG inlen, UBYTE *out)
{
    ULONG i, o = 0;
    ULONG acc = 0;
    int bits = 0;

    for (i = 0; i < inlen; i++) {
        int c = (unsigned char)in[i];
        int v;
        if (c >= 'A' && c <= 'Z') v = c - 'A';
        else if (c >= 'a' && c <= 'z') v = c - 'a' + 26;
        else if (c >= '0' && c <= '9') v = c - '0' + 52;
        else if (c == '+') v = 62;
        else if (c == '/') v = 63;
        else continue;               /* '=' padding, whitespace, noise */
        acc = (acc << 6) | (ULONG)v;
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            out[o++] = (UBYTE)((acc >> bits) & 0xFF);
        }
    }
    return o;
}
