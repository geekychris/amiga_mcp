#include "tokenizer.h"

#include <string.h>

static int is_space(char c) { return c == ' ' || c == '\t'; }

TokResult tokenize(TokenList *t, const char *src)
{
    int i = 0, o = 0;
    char quote = 0;   /* 0, '\'', or '"' */
    int in_word = 0;
    int argc = 0;

    t->argc = 0;
    t->argv[0] = 0;
    t->buf[0] = '\0';
    if (!src) return TOK_OK;

    for (;;) {
        char c = src[i];
        if (c == '\0') break;
        if (o >= TOK_MAX_LEN - 1) return TOK_ERR_TOO_LONG;

        if (!in_word) {
            if (is_space(c)) { i++; continue; }
            if (argc >= TOK_MAX_ARGS) return TOK_ERR_TOO_MANY_ARGS;
            t->argv[argc++] = &t->buf[o];
            in_word = 1;
            /* fall through to the char handling below */
        }

        if (quote) {
            if (c == quote) { quote = 0; i++; continue; }
            if (quote == '"' && c == '\\' && src[i + 1]) {
                t->buf[o++] = src[i + 1];
                i += 2;
                continue;
            }
            t->buf[o++] = c;
            i++;
            continue;
        }

        if (c == '\'' || c == '"') { quote = c; i++; continue; }
        if (c == '\\' && src[i + 1]) {
            t->buf[o++] = src[i + 1];
            i += 2;
            continue;
        }
        if (is_space(c)) {
            t->buf[o++] = '\0';
            in_word = 0;
            i++;
            continue;
        }
        t->buf[o++] = c;
        i++;
    }
    if (quote) return TOK_ERR_UNTERMINATED_QUOTE;
    t->buf[o] = '\0';
    t->argv[argc] = 0;
    t->argc = argc;
    return TOK_OK;
}
