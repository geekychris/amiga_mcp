#include <exec/types.h>
#include "debug.h"

/* External serial functions */
extern int serial_open(ULONG baud);
extern void serial_close(void);

static BOOL initialized = FALSE;

int dbg_init(ULONG baud)
{
    if (initialized) return 0;

    if (serial_open(baud) != 0) {
        return -1;
    }

    initialized = TRUE;
    DBG_I("Debug session started");
    dbg_heartbeat();

    return 0;
}

void dbg_cleanup(void)
{
    if (!initialized) return;

    DBG_I("Debug session ended");
    serial_close();
    initialized = FALSE;
}
