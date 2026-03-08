#include <exec/types.h>
#include <exec/memory.h>
#include <proto/exec.h>

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "debug.h"

/* External functions */
extern int serial_write(const char *buf, int len);
extern int serial_read(char *buf, int maxlen);
extern ULONG dbg_get_tick(void);

/* Variable registry */
typedef struct {
    char name[33];
    int type;
    void *ptr;
    BOOL active;
} DbgVar;

static DbgVar var_registry[DBG_MAX_VARS];
static int var_count = 0;
static dbg_cmd_handler_t cmd_handler = NULL;

/* Incoming command line buffer */
static char cmd_buf[DBG_MAX_LINE];
static int cmd_buf_pos = 0;

static const char hex_chars[] = "0123456789ABCDEF";

void dbg_register_var(const char *name, int type, void *ptr)
{
    int i;

    /* Check if already registered, update if so */
    for (i = 0; i < var_count; i++) {
        if (var_registry[i].active && strcmp(var_registry[i].name, name) == 0) {
            var_registry[i].type = type;
            var_registry[i].ptr = ptr;
            return;
        }
    }

    /* Find empty slot */
    for (i = 0; i < DBG_MAX_VARS; i++) {
        if (!var_registry[i].active) {
            strncpy(var_registry[i].name, name, 32);
            var_registry[i].name[32] = '\0';
            var_registry[i].type = type;
            var_registry[i].ptr = ptr;
            var_registry[i].active = TRUE;
            if (i >= var_count) var_count = i + 1;
            return;
        }
    }
}

void dbg_unregister_var(const char *name)
{
    int i;
    for (i = 0; i < var_count; i++) {
        if (var_registry[i].active && strcmp(var_registry[i].name, name) == 0) {
            var_registry[i].active = FALSE;
            return;
        }
    }
}

static DbgVar *find_var(const char *name)
{
    int i;
    for (i = 0; i < var_count; i++) {
        if (var_registry[i].active && strcmp(var_registry[i].name, name) == 0) {
            return &var_registry[i];
        }
    }
    return NULL;
}

void dbg_send_var(const char *name)
{
    DbgVar *v = find_var(name);
    char line[DBG_MAX_LINE];
    int len;

    if (!v) {
        len = snprintf(line, sizeof(line), "VAR|%s|err|not_found\n", name);
        if (len > 0) serial_write(line, len);
        return;
    }

    switch (v->type) {
        case DBG_TYPE_I32:
            len = snprintf(line, sizeof(line), "VAR|%s|i32|%ld\n",
                          v->name, *(LONG *)v->ptr);
            break;
        case DBG_TYPE_U32:
            len = snprintf(line, sizeof(line), "VAR|%s|u32|%lu\n",
                          v->name, *(ULONG *)v->ptr);
            break;
        case DBG_TYPE_STR:
            len = snprintf(line, sizeof(line), "VAR|%s|str|%s\n",
                          v->name, (char *)v->ptr);
            break;
        case DBG_TYPE_PTR:
            len = snprintf(line, sizeof(line), "VAR|%s|ptr|%08lx\n",
                          v->name, (ULONG)*(void **)v->ptr);
            break;
        default:
            len = snprintf(line, sizeof(line), "VAR|%s|???|unknown\n", v->name);
            break;
    }

    if (len > 0) serial_write(line, len);
}

void dbg_send_mem(APTR addr, ULONG size)
{
    char line[DBG_MAX_LINE];
    UBYTE *p = (UBYTE *)addr;
    ULONG offset = 0;

    while (offset < size) {
        ULONG chunk = size - offset;
        ULONG i;
        int pos;

        if (chunk > 256) chunk = 256;

        /* Header: MEM|addr|size| */
        pos = snprintf(line, sizeof(line), "MEM|%08lx|%lu|",
                      (ULONG)(p + offset), chunk);

        /* Hex encode the data */
        for (i = 0; i < chunk && pos < (int)sizeof(line) - 2; i++) {
            UBYTE b = p[offset + i];
            line[pos++] = hex_chars[(b >> 4) & 0x0F];
            line[pos++] = hex_chars[b & 0x0F];
        }
        line[pos++] = '\n';
        line[pos] = '\0';

        serial_write(line, pos);
        offset += chunk;
    }
}

void dbg_heartbeat(void)
{
    char line[DBG_MAX_LINE];
    ULONG free_chip, free_fast;
    int len;

    free_chip = AvailMem(MEMF_CHIP);
    free_fast = AvailMem(MEMF_FAST);

    len = snprintf(line, sizeof(line), "HB|%lu|%lu|%lu\n",
                  dbg_get_tick(), free_chip, free_fast);
    if (len > 0) serial_write(line, len);
}

void dbg_set_cmd_handler(dbg_cmd_handler_t handler)
{
    cmd_handler = handler;
}

static void process_command(const char *line)
{
    char cmd[16];
    char arg1[64];
    char arg2[DBG_MAX_LINE];
    int n;

    /* Parse command type */
    n = sscanf(line, "%15[^|]|%63[^|]|%[^\n]", cmd, arg1, arg2);
    if (n < 1) return;

    if (strcmp(cmd, "PING") == 0) {
        dbg_heartbeat();
    }
    else if (strcmp(cmd, "GETVAR") == 0 && n >= 2) {
        dbg_send_var(arg1);
    }
    else if (strcmp(cmd, "SETVAR") == 0 && n >= 3) {
        DbgVar *v = find_var(arg1);
        if (v) {
            switch (v->type) {
                case DBG_TYPE_I32:
                    *(LONG *)v->ptr = atol(arg2);
                    break;
                case DBG_TYPE_U32:
                    *(ULONG *)v->ptr = strtoul(arg2, NULL, 10);
                    break;
                case DBG_TYPE_STR:
                    strncpy((char *)v->ptr, arg2, 255);
                    break;
                default:
                    break;
            }
            dbg_send_var(arg1);
        }
    }
    else if (strcmp(cmd, "INSPECT") == 0 && n >= 3) {
        ULONG addr = strtoul(arg1, NULL, 16);
        ULONG size = strtoul(arg2, NULL, 10);
        if (size > 0 && size <= 4096) {
            dbg_send_mem((APTR)addr, size);
        }
    }
    else if (strcmp(cmd, "EXEC") == 0 && n >= 3) {
        if (cmd_handler) {
            ULONG id = strtoul(arg1, NULL, 10);
            cmd_handler(id, arg2);
        }
    }
}

void dbg_poll(void)
{
    char tmp[256];
    int nread, i;

    nread = serial_read(tmp, sizeof(tmp));
    if (nread <= 0) return;

    for (i = 0; i < nread; i++) {
        if (tmp[i] == '\n' || tmp[i] == '\r') {
            if (cmd_buf_pos > 0) {
                cmd_buf[cmd_buf_pos] = '\0';
                process_command(cmd_buf);
                cmd_buf_pos = 0;
            }
        } else if (cmd_buf_pos < (int)sizeof(cmd_buf) - 1) {
            cmd_buf[cmd_buf_pos++] = tmp[i];
        }
    }
}
