/*
 * env.h — shell-local variable table (used by the `set` built-in and later
 * by variable expansion). Kept separate from AmigaDOS local/global env
 * variables so tests can round-trip without touching the system.
 *
 * Fixed-capacity; a "set" that would overflow reports failure rather than
 * evicting silently.
 */
#ifndef HUBERT_ENV_H
#define HUBERT_ENV_H

#define ENV_MAX_ENTRIES 64
#define ENV_MAX_NAME    32
#define ENV_MAX_VALUE   256

typedef struct {
    char name[ENV_MAX_NAME];
    char value[ENV_MAX_VALUE];
} EnvEntry;

typedef struct {
    EnvEntry entries[ENV_MAX_ENTRIES];
    int count;
} Env;

void env_init(Env *e);

/* Set or replace. Returns 1 on success, 0 if the table is full or name is
 * invalid (empty or overlong). */
int  env_set(Env *e, const char *name, const char *value);

/* Fetch the value or 0 if not found. */
const char *env_get(const Env *e, const char *name);

/* Unset by name. Returns 1 if something was removed. */
int  env_unset(Env *e, const char *name);

/* Iterate: index-based read-only view for `env` builtin. */
int  env_count(const Env *e);
const EnvEntry *env_at(const Env *e, int i);

#endif
