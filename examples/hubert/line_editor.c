#include "line_editor.h"

#include <string.h>

static void set_from(LineEditor *ed, const char *s)
{
    int i = 0;
    if (!s) s = "";
    while (s[i] && i < LINE_MAX - 1) {
        ed->buf[i] = s[i];
        i++;
    }
    ed->buf[i] = '\0';
    ed->len = i;
    ed->cursor = i;
}

void line_editor_init(LineEditor *ed, History *h)
{
    ed->hist = h;
    line_editor_reset(ed);
}

void line_editor_reset(LineEditor *ed)
{
    ed->buf[0] = '\0';
    ed->len = 0;
    ed->cursor = 0;
    if (ed->hist) history_reset_cursor(ed->hist);
}

const char *line_editor_text(const LineEditor *ed)  { return ed->buf; }
int         line_editor_cursor(const LineEditor *ed) { return ed->cursor; }

static void insert_char(LineEditor *ed, char c)
{
    if (ed->len >= LINE_MAX - 1) return;
    if (ed->cursor < ed->len) {
        /* shift right from cursor..len */
        memmove(&ed->buf[ed->cursor + 1], &ed->buf[ed->cursor],
                (size_t)(ed->len - ed->cursor));
    }
    ed->buf[ed->cursor] = c;
    ed->cursor++;
    ed->len++;
    ed->buf[ed->len] = '\0';
}

static void erase_at(LineEditor *ed, int pos)
{
    if (pos < 0 || pos >= ed->len) return;
    memmove(&ed->buf[pos], &ed->buf[pos + 1],
            (size_t)(ed->len - pos - 1));
    ed->len--;
    ed->buf[ed->len] = '\0';
}

static void kill_word_left(LineEditor *ed)
{
    int i = ed->cursor;
    /* skip trailing spaces */
    while (i > 0 && ed->buf[i - 1] == ' ') i--;
    /* skip the word */
    while (i > 0 && ed->buf[i - 1] != ' ') i--;
    if (i < ed->cursor) {
        memmove(&ed->buf[i], &ed->buf[ed->cursor],
                (size_t)(ed->len - ed->cursor));
        ed->len -= (ed->cursor - i);
        ed->cursor = i;
        ed->buf[ed->len] = '\0';
    }
}

EdAction line_editor_key(LineEditor *ed, KeyKind k, char keycode)
{
    switch (k) {
    case KEY_CHAR:
        if (keycode >= 32 && (unsigned char)keycode < 127) insert_char(ed, keycode);
        return ED_STAY;

    case KEY_BACKSPACE:
        if (ed->cursor > 0) {
            erase_at(ed, ed->cursor - 1);
            ed->cursor--;
        }
        return ED_STAY;

    case KEY_DELETE:
        erase_at(ed, ed->cursor);
        return ED_STAY;

    case KEY_LEFT:
        if (ed->cursor > 0) ed->cursor--;
        return ED_STAY;

    case KEY_RIGHT:
        if (ed->cursor < ed->len) ed->cursor++;
        return ED_STAY;

    case KEY_HOME:
        ed->cursor = 0;
        return ED_STAY;

    case KEY_END:
        ed->cursor = ed->len;
        return ED_STAY;

    case KEY_UP:
        if (ed->hist) {
            char tmp[LINE_MAX];
            if (history_recall_prev(ed->hist, tmp, LINE_MAX)) set_from(ed, tmp);
        }
        return ED_STAY;

    case KEY_DOWN:
        if (ed->hist) {
            char tmp[LINE_MAX];
            if (history_recall_next(ed->hist, tmp, LINE_MAX)) set_from(ed, tmp);
        }
        return ED_STAY;

    case KEY_CLEAR_LINE:
        ed->buf[0] = '\0';
        ed->len = 0;
        ed->cursor = 0;
        return ED_STAY;

    case KEY_KILL_WORD:
        kill_word_left(ed);
        return ED_STAY;

    case KEY_ENTER:
        return ED_SUBMIT;

    case KEY_TAB:
        /* main.c owns the completion policy — it needs filesystem access
         * this pure module doesn't. It calls insert helpers on the editor
         * directly and then re-issues a redraw. */
        return ED_STAY;
    }
    return ED_STAY;
}

/* Public helpers so main.c's tab completion can mutate the buffer without
 * reaching into internal representation. */
void line_editor_delete_range(LineEditor *ed, int from, int to)
{
    if (from < 0) from = 0;
    if (to > ed->len) to = ed->len;
    if (from >= to) return;
    memmove(&ed->buf[from], &ed->buf[to], (size_t)(ed->len - to));
    ed->len -= (to - from);
    ed->buf[ed->len] = '\0';
    if (ed->cursor > ed->len) ed->cursor = ed->len;
    else if (ed->cursor > from) ed->cursor = from;
}

void line_editor_insert_str(LineEditor *ed, const char *s)
{
    if (!s) return;
    while (*s) {
        if (ed->len >= LINE_MAX - 1) break;
        if (ed->cursor < ed->len) {
            memmove(&ed->buf[ed->cursor + 1], &ed->buf[ed->cursor],
                    (size_t)(ed->len - ed->cursor));
        }
        ed->buf[ed->cursor++] = *s++;
        ed->len++;
    }
    ed->buf[ed->len] = '\0';
}
