#include <exec/types.h>
#include <devices/timer.h>
#include <proto/exec.h>

#include <stdio.h>
#include <stdarg.h>
#include <string.h>

#include "debug.h"

/* External serial functions */
extern int serial_write(const char *buf, int len);

static ULONG tick_counter = 0;
static const char level_chars[] = "DIWE";

void dbg_log(int level, const char *fmt, ...)
{
    char line[DBG_MAX_LINE];
    char msg[DBG_MAX_LINE - 32];
    va_list ap;
    int len;
    char lvl;

    if (level < DBG_DEBUG || level > DBG_ERROR)
        level = DBG_INFO;
    lvl = level_chars[level];

    /* Format the user message */
    va_start(ap, fmt);
    vsnprintf(msg, sizeof(msg), fmt, ap);
    va_end(ap);

    /* Build protocol line: LOG|level|tick|message */
    len = snprintf(line, sizeof(line), "LOG|%c|%lu|%s\n", lvl, tick_counter, msg);
    if (len > 0) {
        serial_write(line, len);
    }

    tick_counter++;
}

ULONG dbg_get_tick(void)
{
    return tick_counter;
}
