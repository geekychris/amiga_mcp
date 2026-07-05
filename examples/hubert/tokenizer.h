/*
 * tokenizer.h — split a command line into argv, respecting quotes.
 *
 * AmigaDOS quoting is idiosyncratic (double quotes with * as escape) but we
 * take the more familiar Unix-y interpretation for a shell app: single
 * quotes preserve everything, double quotes preserve most, backslash escapes
 * one char. This matches user expectations from bash/zsh they're likely
 * more familiar with than the original Amiga Shell.
 */
#ifndef HUBERT_TOKENIZER_H
#define HUBERT_TOKENIZER_H

#define TOK_MAX_ARGS  32
#define TOK_MAX_LEN   1024

typedef struct {
    /* argv[i] is a pointer into `buf`. NULL-terminated after argc entries. */
    char *argv[TOK_MAX_ARGS + 1];
    int argc;
    /* Backing store owned by the caller; tokenize writes NULs into it. */
    char buf[TOK_MAX_LEN];
} TokenList;

typedef enum {
    TOK_OK = 0,
    TOK_ERR_TOO_LONG,
    TOK_ERR_TOO_MANY_ARGS,
    TOK_ERR_UNTERMINATED_QUOTE,
} TokResult;

/* Copy src into tokens->buf and split. Empty input yields argc=0.
 * Whitespace outside quotes is any of space/tab. Returns a TokResult. */
TokResult tokenize(TokenList *tokens, const char *src);

#endif
