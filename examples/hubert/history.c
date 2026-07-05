#include "history.h"

#include <string.h>

static void safe_copy(char *dst, int dstSize, const char *src)
{
    int i = 0;
    if (dstSize <= 0) return;
    while (src && src[i] && i < dstSize - 1) {
        dst[i] = src[i];
        i++;
    }
    dst[i] = '\0';
}

void history_init(History *h)
{
    int i;
    h->count = 0;
    h->head = 0;
    h->cursor = -1;
    for (i = 0; i < HISTORY_MAX_ENTRIES; i++) h->entries[i][0] = '\0';
}

void history_reset_cursor(History *h)
{
    h->cursor = -1;
}

void history_add(History *h, const char *line)
{
    int newest_idx;
    if (!line || !line[0]) return;
    /* Skip if identical to the newest entry — quiet repeat suppression. */
    if (h->count > 0) {
        newest_idx = (h->head - 1 + HISTORY_MAX_ENTRIES) % HISTORY_MAX_ENTRIES;
        if (strcmp(h->entries[newest_idx], line) == 0) {
            history_reset_cursor(h);
            return;
        }
    }
    safe_copy(h->entries[h->head], HISTORY_MAX_LINE + 1, line);
    h->head = (h->head + 1) % HISTORY_MAX_ENTRIES;
    if (h->count < HISTORY_MAX_ENTRIES) h->count++;
    history_reset_cursor(h);
}

const char *history_at(const History *h, int i)
{
    int idx;
    if (i < 0 || i >= h->count) return 0;
    /* i=0 -> newest, i=1 -> second newest, ... */
    idx = (h->head - 1 - i + HISTORY_MAX_ENTRIES * 2) % HISTORY_MAX_ENTRIES;
    return h->entries[idx];
}

int history_recall_prev(History *h, char *out, int outSize)
{
    const char *e;
    int next_cursor;
    if (h->count == 0) return 0;
    next_cursor = (h->cursor < 0) ? 0 : h->cursor + 1;
    if (next_cursor >= h->count) return 0;   /* already at oldest */
    h->cursor = next_cursor;
    e = history_at(h, h->cursor);
    if (!e) return 0;
    safe_copy(out, outSize, e);
    return 1;
}

int history_recall_next(History *h, char *out, int outSize)
{
    const char *e;
    if (h->count == 0) return 0;
    if (h->cursor <= 0) {
        h->cursor = -1;
        if (outSize > 0) out[0] = '\0';
        return 1;
    }
    h->cursor--;
    e = history_at(h, h->cursor);
    if (!e) return 0;
    safe_copy(out, outSize, e);
    return 1;
}
