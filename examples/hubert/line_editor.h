/*
 * line_editor.h — line editor state machine.
 *
 * Owns the "line being typed" buffer + cursor position. The input driver
 * (Raw console for the real Amiga app, direct calls for tests) feeds
 * keypresses in via line_editor_key; the driver reads back what to draw
 * and where to place the cursor via the render callback.
 *
 * Split from the console I/O so the tests can drive it deterministically.
 */
#ifndef HUBERT_LINE_EDITOR_H
#define HUBERT_LINE_EDITOR_H

#include "history.h"

#define LINE_MAX 512

typedef enum {
    KEY_CHAR,        /* keycode is the ASCII char to insert */
    KEY_BACKSPACE,
    KEY_DELETE,
    KEY_LEFT,
    KEY_RIGHT,
    KEY_HOME,
    KEY_END,
    KEY_UP,
    KEY_DOWN,
    KEY_ENTER,
    KEY_CLEAR_LINE,  /* Ctrl-U / kill line */
    KEY_KILL_WORD,   /* Ctrl-W */
    KEY_TAB,         /* completion — main.c handles it */
} KeyKind;

typedef enum {
    ED_STAY,           /* redraw, wait for next key */
    ED_SUBMIT,         /* line ready — read via line_editor_text */
} EdAction;

typedef struct {
    char buf[LINE_MAX];
    int len;
    int cursor;      /* 0..len */
    History *hist;   /* non-owning */
} LineEditor;

void line_editor_init(LineEditor *ed, History *h);
void line_editor_reset(LineEditor *ed);

/* Feed one key. keycode is only used for KEY_CHAR. */
EdAction line_editor_key(LineEditor *ed, KeyKind k, char keycode);

/* Read the current buffer (NUL-terminated). */
const char *line_editor_text(const LineEditor *ed);
int         line_editor_cursor(const LineEditor *ed);

/* Buffer mutation helpers used by main.c's tab-completion. */
void line_editor_delete_range(LineEditor *ed, int from, int to);
void line_editor_insert_str(LineEditor *ed, const char *s);

#endif
