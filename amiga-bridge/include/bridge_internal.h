#ifndef BRIDGE_INTERNAL_H
#define BRIDGE_INTERNAL_H

#include <exec/types.h>
#include <exec/ports.h>
#include "bridge_ipc.h"

/* Maximum protocol line length */
#define BRIDGE_MAX_LINE 1024

/* ---- serial_io.c ---- */
int serial_open(ULONG baud);
void serial_close(void);
int serial_write(const char *buf, int len);
void serial_start_read(void);
int serial_check_read(char *out_byte);
ULONG serial_get_signal(void);
BOOL serial_is_open(void);

/* ---- ipc_manager.c ---- */
int ipc_init(void);
void ipc_cleanup(void);
ULONG ipc_get_signal(void);
void ipc_process(void);
void ipc_send_to_client(struct MsgPort *replyPort, UWORD type,
                        ULONG clientId, ULONG cmdId,
                        const char *data, ULONG dataLen);

/* ---- client_registry.c ---- */

/* Per-client tracking of registered vars, hooks, memory regions */
struct ClientVarInfo {
    char name[34];
    int  type;
};

struct ClientHookInfo {
    char name[34];
    char description[64];
};

struct ClientMemRegInfo {
    char  name[34];
    char  description[64];
    ULONG addr;
    ULONG size;
};

struct ClientEntry {
    BOOL            active;
    ULONG           clientId;
    char            name[34];
    struct MsgPort *replyPort;
    ULONG           msgCount;
    ULONG           lastTick;

    /* Registered variables (metadata only — values live in client) */
    struct ClientVarInfo vars[AB_MAX_VARS];
    int varCount;

    /* Registered hooks */
    struct ClientHookInfo hooks[AB_MAX_HOOKS];
    int hookCount;

    /* Registered memory regions */
    struct ClientMemRegInfo memregs[AB_MAX_MEMREGIONS];
    int memregCount;
};

int client_register(const char *name, struct MsgPort *replyPort);
void client_unregister(ULONG clientId);
struct ClientEntry *client_find(ULONG clientId);
struct ClientEntry *client_find_by_name(const char *name);
int client_count(void);
struct ClientEntry *client_get_by_index(int index);
int client_list(char *buf, int bufSize);
int client_build_line(char *buf, int bufSize);
void client_debug_dump(char *buf, int bufSize);

/* Client metadata helpers */
void client_add_var(struct ClientEntry *ce, const char *name, int type);
void client_remove_var(struct ClientEntry *ce, const char *name);
void client_add_hook(struct ClientEntry *ce, const char *name, const char *desc);
void client_remove_hook(struct ClientEntry *ce, const char *name);
void client_add_memreg(struct ClientEntry *ce, const char *name,
                       ULONG addr, ULONG size, const char *desc);
void client_remove_memreg(struct ClientEntry *ce, const char *name);

/* ---- protocol_handler.c ---- */
void protocol_parse_line(const char *line);
void protocol_send_log(const char *clientName, int level,
                       ULONG tick, const char *message);
void protocol_send_var(const char *clientName, const char *name,
                       int type, const char *value);
void protocol_send_heartbeat(ULONG tick, ULONG chipFree, ULONG fastFree);
void protocol_send_mem(APTR addr, ULONG size, const UBYTE *data);
void protocol_send_cmd_response(ULONG cmdId, const char *status,
                                const char *responseData);
void protocol_send_clients(void);
void protocol_send_tasks(void);
void protocol_send_libs(void);
void protocol_send_devices(void);
void protocol_send_dir(const char *path);
void protocol_send_file(const char *path, ULONG offset, ULONG size);
void protocol_send_fileinfo(const char *path);
void protocol_send_raw(const char *line);

/* TX/RX counters */
extern ULONG g_tx_count;
extern ULONG g_rx_count;

/* Shutdown flag - set by protocol handler, checked by main loop */
extern BOOL g_shutdown_requested;

/* ---- system_inspector.c ---- */
int sys_list_tasks(char *buf, int bufSize);
int sys_list_libs(char *buf, int bufSize);
int sys_list_devices(char *buf, int bufSize);
int sys_list_volumes(char *buf, int bufSize);
void sys_avail_mem(ULONG *chipFree, ULONG *fastFree);
int sys_inspect_mem(APTR addr, ULONG size, UBYTE *outBuf, ULONG outBufSize);
int sys_break_task(const char *name);
void sys_handle_memmap(void);
void sys_handle_stackinfo(const char *taskname);
void sys_handle_chipregs(void);
void sys_handle_readregs(void);
void sys_handle_search(const char *args);
void sys_handle_libinfo(const char *name);
void sys_handle_devinfo(const char *name);
void sys_handle_libfuncs(const char *args);
void sys_handle_volumes_ext(void);
void sys_handle_ports(void);
void sys_handle_sysinfo(void);
void sys_init_uptime(void);
void sys_handle_uptime(void);
int sys_signal_task_by_addr(ULONG addr, ULONG sigMask);

/* ---- fs_access.c ---- */
int fs_list_dir(const char *path, char *buf, int bufSize);
int fs_read_file(const char *path, ULONG offset, ULONG size,
                 UBYTE *buf, ULONG bufSize, ULONG *actualRead);
int fs_write_file(const char *path, ULONG offset,
                  const UBYTE *data, ULONG size);
int fs_file_info(const char *path, char *buf, int bufSize);
int fs_delete(const char *path);
int fs_makedir(const char *path);

/* ---- process_launcher.c ---- */
int proc_launch(ULONG cmdId, const char *command, char *resultBuf, int bufSize);
int proc_run_async(ULONG cmdId, const char *command, char *resultBuf, int bufSize);

/* ---- crash_handler.c ---- */
void crash_init(void);
void crash_cleanup(void);
int crash_get_last(char *buf, int bufSize);

/* ---- gfx_inspector.c ---- */
void gfx_handle_screenshot(const char *args);
void gfx_handle_palette(const char *args);
void gfx_handle_setpalette(const char *args);
void gfx_handle_copperlist(const char *args);
void gfx_handle_sprites(const char *args);
void gfx_handle_listwindows(const char *args);

/* ---- snoop.c ---- */
void snoop_start(void);
void snoop_stop(void);
void snoop_drain(void);         /* called from main loop to send buffered events */
BOOL snoop_is_active(void);
void snoop_handle_status(void); /* sends SNOOPSTATE response */

/* ---- audio_inspector.c ---- */
void audio_handle_channels(void);
void audio_handle_sample(const char *args);

/* ---- intuition_inspector.c ---- */
void intui_handle_screens(void);
void intui_handle_windows(const char *args);
void intui_handle_gadgets(const char *args);
void intui_handle_activate(const char *args);
void intui_handle_tofront(const char *args);
void intui_handle_toback(const char *args);
void intui_handle_zip(const char *args);
void intui_handle_move(const char *args);
void intui_handle_size(const char *args);
void intui_handle_scrtofront(const char *args);
void intui_handle_scrtoback(const char *args);

/* ---- input_inject.c ---- */
void input_handle_key(const char *args);
void input_handle_mouse_move(const char *args);
void input_handle_mouse_button(const char *args);
void input_cleanup(void);

/* ---- font_browser.c ---- */
void font_init(void);
void font_cleanup(void);
void font_handle_list(void);
void font_handle_info(const char *args);

/* ---- chipwrite_logger.c ---- */
void chiplog_init(void);
void chiplog_cleanup(void);
void chiplog_handle_start(void);
void chiplog_handle_stop(void);
void chiplog_handle_snapshot(void);
void chiplog_poll(void);

/* ---- pool_tracker.c ---- */
void pool_init(void);
void pool_cleanup(void);
void pool_handle_start(void);
void pool_handle_stop(void);
void pool_handle_list(void);

/* ---- clipboard_bridge.c ---- */
void clip_init(void);
void clip_cleanup(void);
void clip_handle_get(void);
void clip_handle_set(const char *args);

/* ---- arexx_bridge.c ---- */
void arexx_init(void);
void arexx_cleanup(void);
void arexx_handle_ports(void);
void arexx_handle_send(const char *args);
ULONG arexx_get_signal(void);
void arexx_poll(void);

/* ---- process_launcher.c (new) ---- */
void proc_init(void);
void proc_cleanup(void);
void proc_poll(void);
int proc_list(char *buf, int bufSize);
int proc_stat(int procId, char *buf, int bufSize);
int proc_signal(int procId, ULONG sigMask);

/* ---- fs_access.c (new) ---- */
int fs_rename(const char *oldPath, const char *newPath);
int fs_copy(const char *srcPath, const char *dstPath);
int fs_protect(const char *path, ULONG *bits, int setMode);
int fs_set_comment(const char *path, const char *comment);
int fs_checksum(const char *path, ULONG *crc32Out, ULONG *sizeOut);
int fs_append(const char *path, const UBYTE *data, ULONG size);
int fs_get_env(const char *name, int archive, char *buf, int bufSize);
int fs_set_env(const char *name, const char *value, int archive);
int fs_set_date(const char *path, LONG days, LONG mins, LONG ticks);

/* ---- system_inspector.c (new) ---- */
int sys_list_assigns(char *buf, int bufSize);
void sys_handle_assign(const char *args);
void sys_handle_capabilities(void);

/* Tail file streaming */
extern BOOL g_tail_active;
extern char g_tail_path[256];
extern ULONG g_tail_pos;
void tail_poll(void);

/* ---- UI state (main.c) ---- */
#define UI_MAX_LOG_LINES 5
#define UI_MAX_LOG_LEN   50

extern char g_ui_logs[UI_MAX_LOG_LINES][UI_MAX_LOG_LEN];
extern int g_ui_log_head;
extern BOOL g_ui_dirty;
extern BOOL g_serial_connected;
extern BOOL g_host_connected;

void ui_add_log(const char *msg);

#endif /* BRIDGE_INTERNAL_H */
