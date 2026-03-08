#ifndef AMIGA_DEBUG_H
#define AMIGA_DEBUG_H

#include <exec/types.h>

/* Log levels */
#define DBG_DEBUG 0
#define DBG_INFO  1
#define DBG_WARN  2
#define DBG_ERROR 3

/* Variable types for registration */
#define DBG_TYPE_I32 0
#define DBG_TYPE_U32 1
#define DBG_TYPE_STR 2
#define DBG_TYPE_F32 3
#define DBG_TYPE_PTR 4

/* Maximum registered variables */
#define DBG_MAX_VARS 64

/* Maximum line length for protocol messages */
#define DBG_MAX_LINE 1024

/* Initialize debug system - opens serial.device
 * baud: baud rate (0 = default 9600)
 * Returns: 0 on success, -1 on failure */
int dbg_init(ULONG baud);

/* Shutdown debug system - closes serial.device */
void dbg_cleanup(void);

/* Send a log message with printf-style formatting */
void dbg_log(int level, const char *fmt, ...);

/* Convenience logging macros */
#define DBG_D(fmt, ...) dbg_log(DBG_DEBUG, fmt, ##__VA_ARGS__)
#define DBG_I(fmt, ...) dbg_log(DBG_INFO,  fmt, ##__VA_ARGS__)
#define DBG_W(fmt, ...) dbg_log(DBG_WARN,  fmt, ##__VA_ARGS__)
#define DBG_E(fmt, ...) dbg_log(DBG_ERROR, fmt, ##__VA_ARGS__)

/* Register a variable for remote inspection
 * name: variable name (max 32 chars, alphanumeric + underscore)
 * type: one of DBG_TYPE_* constants
 * ptr:  pointer to the variable */
void dbg_register_var(const char *name, int type, void *ptr);

/* Unregister a previously registered variable */
void dbg_unregister_var(const char *name);

/* Push current value of a registered variable to host */
void dbg_send_var(const char *name);

/* Send a memory dump to host
 * addr: start address
 * size: number of bytes (will be split into 256-byte chunks) */
void dbg_send_mem(APTR addr, ULONG size);

/* Send heartbeat with tick count and free memory stats */
void dbg_heartbeat(void);

/* Poll for incoming commands from host - call from main loop */
void dbg_poll(void);

/* Custom command handler callback type
 * id:   command ID from host
 * data: command data string */
typedef void (*dbg_cmd_handler_t)(ULONG id, const char *data);

/* Set handler for EXEC commands from host */
void dbg_set_cmd_handler(dbg_cmd_handler_t handler);

#endif /* AMIGA_DEBUG_H */
