#ifndef BRIDGE_CLIENT_H
#define BRIDGE_CLIENT_H

#include <exec/types.h>
#include "bridge_ipc.h"

/* Initialize bridge client - finds daemon, registers app.
 * Also sets task name to appName for FindTask/BREAK.
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

/* ---- Hook Registry ---- */

/* Hook callback type - receives args, writes result into buf (max bufSize).
 * Return 0 for success, non-zero for error. */
typedef int (*ab_hook_fn_t)(const char *args, char *resultBuf, int bufSize);

/* Register a callable hook that the host can invoke by name */
void ab_register_hook(const char *name, const char *description,
                      ab_hook_fn_t fn);

/* Unregister a hook */
void ab_unregister_hook(const char *name);

/* ---- Memory Region Registry ---- */

/* Register a named memory region for remote inspection */
void ab_register_memregion(const char *name, APTR addr, ULONG size,
                           const char *description);

/* Unregister a memory region */
void ab_unregister_memregion(const char *name);

/* ---- Resource Tracker ---- */

/* Track a memory allocation for leak detection */
void ab_track_alloc(const char *tag, APTR ptr, ULONG size);

/* Track a memory free (marks allocation as freed) */
void ab_track_free(APTR ptr);

/* Track opening a resource handle (file, library, device, etc.) */
void ab_track_open(const char *tag, APTR handle);

/* Track closing a resource handle */
void ab_track_close(APTR handle);

/* ---- Performance Profiler ---- */

/* Mark the start of a frame (call once per main loop iteration) */
void ab_perf_frame_start(void);

/* Mark the end of a frame */
void ab_perf_frame_end(void);

/* Mark the start of a named code section within a frame */
void ab_perf_section_start(const char *label);

/* Mark the end of a named code section */
void ab_perf_section_end(const char *label);

/* ---- Test Harness ---- */

/* Begin a test suite with a name. Sends TEST_BEGIN to host. */
void ab_test_begin(const char *suiteName);

/* Assert a condition. If fail, logs file/line and message.
 * Returns 1 if passed, 0 if failed. */
int ab_test_assert(int condition, const char *testName,
                   const char *file, int line);

/* End test suite. Sends TEST_END with summary (pass/fail/total). */
void ab_test_end(void);

/* Convenience macro that auto-fills file and line */
#define AB_ASSERT(cond, name) ab_test_assert((cond), (name), __FILE__, __LINE__)

#endif /* BRIDGE_CLIENT_H */
