#include "env.h"

#include <string.h>

static int find_index(const Env *e, const char *name)
{
    int i;
    for (i = 0; i < e->count; i++) {
        if (strcmp(e->entries[i].name, name) == 0) return i;
    }
    return -1;
}

static void safe_copy(char *dst, int dstSize, const char *src)
{
    int i = 0;
    if (dstSize <= 0) return;
    while (src && src[i] && i < dstSize - 1) { dst[i] = src[i]; i++; }
    dst[i] = '\0';
}

void env_init(Env *e) { e->count = 0; }

int env_set(Env *e, const char *name, const char *value)
{
    int idx;
    int nlen;
    if (!name) return 0;
    nlen = (int)strlen(name);
    if (nlen == 0 || nlen >= ENV_MAX_NAME) return 0;
    if (value && (int)strlen(value) >= ENV_MAX_VALUE) return 0;
    idx = find_index(e, name);
    if (idx < 0) {
        if (e->count >= ENV_MAX_ENTRIES) return 0;
        idx = e->count++;
        safe_copy(e->entries[idx].name, ENV_MAX_NAME, name);
    }
    safe_copy(e->entries[idx].value, ENV_MAX_VALUE, value ? value : "");
    return 1;
}

const char *env_get(const Env *e, const char *name)
{
    int idx = find_index(e, name);
    return idx < 0 ? 0 : e->entries[idx].value;
}

int env_unset(Env *e, const char *name)
{
    int idx = find_index(e, name);
    if (idx < 0) return 0;
    /* compact: move the last entry into the hole. Order isn't meaningful. */
    if (idx != e->count - 1) e->entries[idx] = e->entries[e->count - 1];
    e->count--;
    return 1;
}

int env_count(const Env *e) { return e->count; }

const EnvEntry *env_at(const Env *e, int i)
{
    if (i < 0 || i >= e->count) return 0;
    return &e->entries[i];
}
