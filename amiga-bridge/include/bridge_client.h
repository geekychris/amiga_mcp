#ifndef BRIDGE_CLIENT_H
#define BRIDGE_CLIENT_H

#include <exec/types.h>
#include "bridge_ipc.h"

/* Initialize bridge client - finds daemon, registers app
 * appName: application name (max 32 chars)
 * Returns: 0 on success, -1 on failure */
int ab_init(const char *appName);

/* Shutdown bridge client - unregisters from daemon */
void ab_cleanup(void);

/* Check if connected to daemon */
BOOL ab_is_connected(void);

/* Send a log message with printf-style formatting */
void ab_log(int level, const char *fmt, ...);

/* Convenience logging macros */
#define AB_D(fmt, ...) ab_log(AB_DEBUG, fmt, ##__VA_ARGS__)
#define AB_I(fmt, ...) ab_log(AB_INFO,  fmt, ##__VA_ARGS__)
#define AB_W(fmt, ...) ab_log(AB_WARN,  fmt, ##__VA_ARGS__)
#define AB_E(fmt, ...) ab_log(AB_ERROR, fmt, ##__VA_ARGS__)

/* Register a variable for remote inspection
 * name: variable name (max 32 chars)
 * type: one of AB_TYPE_* constants
 * ptr:  pointer to the variable */
void ab_register_var(const char *name, int type, void *ptr);

/* Unregister a previously registered variable */
void ab_unregister_var(const char *name);

/* Push current value of a registered variable to daemon */
void ab_push_var(const char *name);

/* Send heartbeat to daemon */
void ab_heartbeat(void);

/* Send a memory dump through daemon to host */
void ab_send_mem(APTR addr, ULONG size);

/* Poll for incoming commands from daemon - call from main loop */
void ab_poll(void);

/* Command handler callback type */
typedef void (*ab_cmd_handler_t)(ULONG id, const char *data);

/* Set handler for commands forwarded from host */
void ab_set_cmd_handler(ab_cmd_handler_t handler);

/* Respond to a command from host */
void ab_cmd_respond(ULONG id, const char *status, const char *data);

#endif /* BRIDGE_CLIENT_H */
