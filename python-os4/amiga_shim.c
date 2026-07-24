/*
 * amiga_shim.c — POSIX shims for CPython on OS4.
 *
 * Newlib on OS4 is missing a handful of POSIX-ish functions that
 * CPython core files reference. We provide minimal implementations
 * here and force-link this .o into the interpreter build.
 *
 * Long-term this belongs under `Amiga/` inside a proper platform
 * directory in the CPython tree (matching PC/ and Mac/). Phase 1
 * ships it as a single external .c to keep patches to CPython
 * itself minimal.
 */

#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include "amiga_shim.h"

/* Newlib on OS4 has setenv but no unsetenv. Use setenv with empty
 * value as a poor-man's substitute — the env var will still exist
 * but with an empty string, which is what most callers effectively
 * test for. Full removal would need to walk the env array. */
int unsetenv(const char *name)
{
    if (!name || !*name) return -1;
    return setenv(name, "", 1);
}

int initgroups(const char *user, int group)
{
    (void)user; (void)group;
    return 0;   /* no-op success — Amiga has no supplementary groups */
}

int setrlimit(int resource, const struct rlimit *rlim)
{
    (void)resource; (void)rlim;
    errno = ENOSYS;
    return -1;
}

int getrlimit(int resource, struct rlimit *rlim)
{
    (void)resource;
    if (rlim) { rlim->rlim_cur = 0; rlim->rlim_max = 0; }
    errno = ENOSYS;
    return -1;
}
