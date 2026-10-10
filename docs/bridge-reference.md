<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Chris Collins <chris@hitorro.com> -->

# Bridge protocol and client library

[Documentation index](README.md) · [Install DevBench](quickstart.md)

## Bridge Protocol

Line-based text protocol over serial. Each message is `\n`-terminated,
fields are pipe-delimited (`|`), maximum 1024 characters per line.

### Amiga → Host Messages

#### Logging & Status
| Message | Format | Description |
|---|---|---|
| LOG | `LOG\|level\|tick\|message` | App log (level: D/I/W/E) |
| CLOG | `CLOG\|client\|level\|tick\|message` | Client-attributed log |
| HB | `HB\|tick\|freeChip\|freeFast` | Heartbeat |
| PONG | `PONG\|clientCount\|freeChip\|freeFast` | Ping response |
| READY | `READY\|version` | Daemon startup |

#### Variables
| Message | Format | Description |
|---|---|---|
| VAR | `VAR\|name\|type\|value` | Variable value (type: i32/u32/str/f32/ptr) |
| CVAR | `CVAR\|client\|name\|type\|value` | Client-attributed variable |

#### Memory
| Message | Format | Description |
|---|---|---|
| MEM | `MEM\|addr_hex\|size\|hex_data` | Memory dump response |

#### System Info
| Message | Format | Description |
|---|---|---|
| CLIENTS | `CLIENTS\|count\|name1,name2,...` | Connected clients |
| TASKS | `TASKS\|count\|name(pri,state,type),...` | Task list |
| LIBS | `LIBS\|count\|name(v.r),...` | Library list |
| DEVICES | `DEVICES\|count\|name(v.r),...` | Device list |
| VOLUMES | `VOLUMES\|count\|name1,name2,...` | Volume list |

#### Files
| Message | Format | Description |
|---|---|---|
| DIR | `DIR\|path\|count\|name(size,type),...` | Directory listing |
| FILE | `FILE\|path\|size\|offset\|hex_data` | File content |
| FILEINFO | `FILEINFO\|path\|size\|date\|protbits` | File metadata |

#### Process & Commands
| Message | Format | Description |
|---|---|---|
| PROC | `PROC\|id\|status\|output` | Process completion |
| CMD | `CMD\|id\|status\|response` | Command response |

#### Client Introspection
| Message | Format | Description |
|---|---|---|
| HOOKS | `HOOKS\|client\|count\|name:desc,...` | Hook list |
| MEMREGS | `MEMREGS\|client\|count\|name:addr:size:desc,...` | Memory regions |
| CINFO | `CINFO\|name\|id\|msgs\|vars:...\|hooks:...\|memregs:...` | Full client info |

#### Hardware & Inspection
| Message | Format | Description |
|---|---|---|
| MEMMAP | `MEMMAP\|count\|name:attr:lower:upper:free:largest,...` | Memory region map |
| STACKINFO | `STACKINFO\|task\|spLower\|spUpper\|spReg\|size\|used\|free` | Task stack info |
| CHIPREGS | `CHIPREGS\|count\|name:addr:value\|...` | Custom chip register values |
| REGS | `REGS\|D0=val\|D1=val\|...\|SP=val\|SR=val` | CPU register snapshot |
| SEARCH | `SEARCH\|count\|addr1,addr2,...` | Memory search results |

#### Acknowledgements
| Message | Format | Description |
|---|---|---|
| OK | `OK\|context\|detail` | Success |
| ERR | `ERR\|context\|detail` | Error |

### Host → Amiga Commands

#### Connection
| Command | Format | Description |
|---|---|---|
| PING | `PING` | Request status |
| SHUTDOWN | `SHUTDOWN` | Terminate daemon |

#### Variables
| Command | Format | Description |
|---|---|---|
| GETVAR | `GETVAR\|name` | Get variable value |
| SETVAR | `SETVAR\|name\|value` | Set variable value |

#### Memory
| Command | Format | Description |
|---|---|---|
| INSPECT | `INSPECT\|addr_hex\|size` | Request memory dump |
| WRITEMEM | `WRITEMEM\|addr_hex\|hex_data` | Write to memory |

#### System Queries
| Command | Format | Description |
|---|---|---|
| LISTCLIENTS | `LISTCLIENTS` | List clients |
| LISTTASKS | `LISTTASKS` | List tasks |
| LISTLIBS | `LISTLIBS` | List libraries |
| LISTDEVS | `LISTDEVS` | List devices |
| LISTVOLUMES | `LISTVOLUMES` | List volumes |

#### Filesystem
| Command | Format | Description |
|---|---|---|
| LISTDIR | `LISTDIR\|path` | List directory |
| READFILE | `READFILE\|path\|offset\|size` | Read file |
| WRITEFILE | `WRITEFILE\|path\|offset\|hex_data` | Write file |
| FILEINFO | `FILEINFO\|path` | Get file info |
| DELETEFILE | `DELETEFILE\|path` | Delete file |
| MAKEDIR | `MAKEDIR\|path` | Create directory |

#### Process Control
| Command | Format | Description |
|---|---|---|
| LAUNCH | `LAUNCH\|id\|command` | Run and wait |
| RUN | `RUN\|id\|command` | Run async |
| DOSCOMMAND | `DOSCOMMAND\|id\|command` | Run AmigaDOS command |
| BREAK | `BREAK\|task_name` | Send CTRL-C |
| SCRIPT | `SCRIPT\|id\|script_text` | Execute script (newlines → `;`) |
| STOP | `STOP\|client_name` | Stop client (CTRL-C + IPC shutdown) |

#### Hardware & Inspection
| Command | Format | Description |
|---|---|---|
| MEMMAP | `MEMMAP` | Request memory region map |
| STACKINFO | `STACKINFO\|taskname` | Request stack info for a task |
| CHIPREGS | `CHIPREGS` | Read safe custom chip registers |
| READREGS | `READREGS` | Capture CPU registers |
| SEARCH | `SEARCH\|addr_hex\|size\|pattern_hex` | Search memory for byte pattern |

#### Hooks & Memory Regions
| Command | Format | Description |
|---|---|---|
| LISTHOOKS | `LISTHOOKS\|client` | List hooks |
| CALLHOOK | `CALLHOOK\|id\|client\|hook\|args` | Call hook |
| LISTMEMREGS | `LISTMEMREGS\|client` | List memory regions |
| READMEMREG | `READMEMREG\|client\|region` | Read region |
| CLIENTINFO | `CLIENTINFO\|client` | Get client details |

#### Capabilities & Process Management
| Command | Format | Description |
|---|---|---|
| CAPABILITIES | `CAPABILITIES` | Query daemon version, protocol level, supported commands |
| PROCLIST | `PROCLIST` | List tracked async processes |
| PROCSTAT | `PROCSTAT\|id` | Get status of a tracked process |
| SIGNAL | `SIGNAL\|id\|sigType` | Send signal to tracked process (0=CTRL-C, 1=CTRL-D, 2=CTRL-E, 3=CTRL-F) |

#### Extended Filesystem Operations
| Command | Format | Description |
|---|---|---|
| RENAME | `RENAME\|oldPath\|newPath` | Rename or move a file |
| COPY | `COPY\|src\|dst` | Copy a file server-side (no host round-trip) |
| APPEND | `APPEND\|path\|hexData` | Append hex-encoded data to a file |
| CHECKSUM | `CHECKSUM\|path` | Compute CRC32 checksum and file size |
| PROTECT | `PROTECT\|path` | Get protection bits |
| PROTECT | `PROTECT\|path\|bits` | Set protection bits (hex) |
| SETCOMMENT | `SETCOMMENT\|path\|comment` | Set file comment (filenote) |

#### Assign Management
| Command | Format | Description |
|---|---|---|
| ASSIGNS | `ASSIGNS` | List all DOS assigns |
| ASSIGN | `ASSIGN\|name\|path` | Create/replace an assign |
| ASSIGN | `ASSIGN\|name\|path\|ADD` | Add path to multi-assign |
| ASSIGN | `ASSIGN\|name\|\|REMOVE` | Remove an assign |

#### File Tail (Live Streaming)
| Command | Format | Description |
|---|---|---|
| TAIL | `TAIL\|path` | Start tailing a file for new data |
| STOPTAIL | `STOPTAIL` | Stop tailing |

### Amiga → Host (New Messages)

| Message | Format | Description |
|---|---|---|
| CAPABILITIES | `CAPABILITIES\|version\|protocolLevel\|maxLine\|cmd1,cmd2,...` | Daemon capabilities |
| PROCLIST | `PROCLIST\|count\|id:cmd:status,...` | Tracked process list |
| PROCSTAT | `PROCSTAT\|id\|command\|status` | Single process status |
| TAILDATA | `TAILDATA\|path\|hexData` | New data appended to tailed file |
| CHECKSUM | `CHECKSUM\|path\|crc32\|size` | File CRC32 and size |
| ASSIGNS | `ASSIGNS\|count\|name:path:type,...` | Assign list (type: A=assign, L=late, N=nonbinding) |
| PROTECT | `PROTECT\|path\|bits` | File protection bits (hex) |

---

## Client Library API

### Header: `bridge_client.h`

```c
#include "bridge_client.h"
```

### Variable Types

```c
#define AB_TYPE_I32  0   /* signed 32-bit integer */
#define AB_TYPE_U32  1   /* unsigned 32-bit integer */
#define AB_TYPE_STR  2   /* null-terminated string */
#define AB_TYPE_F32  3   /* 32-bit float */
#define AB_TYPE_PTR  4   /* pointer (displayed as hex) */
```

### Functions

#### Initialization
```c
int  ab_init(const char *appName);   /* Returns 0 on success, -1 on failure */
void ab_cleanup(void);               /* Unregister from daemon */
BOOL ab_is_connected(void);          /* Check daemon connection */
```

#### Logging
```c
void ab_log(int level, const char *fmt, ...);   /* Printf-style */

/* Convenience macros */
AB_D(fmt, ...)   /* DEBUG */
AB_I(fmt, ...)   /* INFO */
AB_W(fmt, ...)   /* WARN */
AB_E(fmt, ...)   /* ERROR */
```

#### Variables
```c
void ab_register_var(const char *name, int type, void *ptr);
void ab_unregister_var(const char *name);
void ab_push_var(const char *name);   /* Send current value to host */
```

#### Heartbeat & Memory
```c
void ab_heartbeat(void);                      /* Send status pulse */
void ab_send_mem(APTR addr, ULONG size);      /* Dump memory to host */
```

#### Command Handling
```c
typedef void (*ab_cmd_handler_t)(ULONG id, const char *data);
void ab_set_cmd_handler(ab_cmd_handler_t handler);
void ab_poll(void);                            /* Check for commands (non-blocking) */
void ab_cmd_respond(ULONG id, const char *status, const char *data);
```

#### Hooks (Host-Callable Functions)
```c
typedef int (*ab_hook_fn_t)(const char *args, char *resultBuf, int bufSize);
void ab_register_hook(const char *name, const char *description, ab_hook_fn_t fn);
void ab_unregister_hook(const char *name);
```

#### Memory Regions
```c
void ab_register_memregion(const char *name, APTR addr, ULONG size,
                           const char *description);
void ab_unregister_memregion(const char *name);
```

---
