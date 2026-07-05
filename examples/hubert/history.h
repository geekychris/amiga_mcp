/*
 * history.h — command history ring buffer for hubert.
 *
 * Fixed-size, no malloc — the whole shell aims to be predictable memory-wise
 * on 68020 hardware. New entries push out oldest ones. history_recall lets
 * the line editor walk backwards (older) or forwards (newer) around a cursor
 * that starts one past the newest entry (== "current empty line").
 */
#ifndef HUBERT_HISTORY_H
#define HUBERT_HISTORY_H

#define HISTORY_MAX_ENTRIES 64
#define HISTORY_MAX_LINE    255   /* one less than the line-editor buffer */

typedef struct {
    char entries[HISTORY_MAX_ENTRIES][HISTORY_MAX_LINE + 1];
    int  count;      /* number of live entries (0..HISTORY_MAX_ENTRIES) */
    int  head;       /* index one past the newest — where next add lands */
    int  cursor;     /* used by history_recall; -1 == "current empty line" */
} History;

void history_init(History *h);

/* Add a line. Empty lines and exact duplicates of the newest entry are
 * skipped so up-arrow doesn't have to walk over "cd/cd/cd" repeats. */
void history_add(History *h, const char *line);

/* Return the line at logical offset i from newest (0 == newest, 1 == second
 * newest, ...). NULL if i is out of range. */
const char *history_at(const History *h, int i);

/* Cursor-driven walk for the line editor:
 *   history_recall_prev writes the "older" entry into out and advances the
 *   cursor. Returns 1 on success, 0 if already at oldest.
 *   history_recall_next writes the "newer" entry, or "" when the cursor
 *   walks past newest back to the empty line. Returns 1 always if the
 *   history has entries. */
int history_recall_prev(History *h, char *out, int outSize);
int history_recall_next(History *h, char *out, int outSize);

/* Reset the recall cursor to "current empty line". Call when a new line
 * is committed or when the editor is reset. */
void history_reset_cursor(History *h);

#endif
