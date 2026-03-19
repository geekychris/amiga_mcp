# AmigaOS 4.1 PowerPC Support Plan

## Overview

Add AmigaOS 4.1 (PowerPC) support to the Amiga MCP cross-development environment while preserving full backward compatibility with classic 68k AmigaOS (3.x). The architecture-agnostic serial protocol and IPC layer mean most of the stack works unchanged; the main effort is CPU-specific debugger code and a second emulator setup.

---

## Table of Contents

1. [Architecture Decisions](#1-architecture-decisions)
2. [Phase 1: QEMU Emulator Setup](#2-phase-1-qemu-emulator-setup)
3. [Phase 2: PPC Cross-Compiler Toolchain](#3-phase-2-ppc-cross-compiler-toolchain)
4. [Phase 3: Build System Dual-Architecture](#4-phase-3-build-system-dual-architecture)
5. [Phase 4: Amiga-Bridge PPC Port](#5-phase-4-amiga-bridge-ppc-port)
6. [Phase 5: Debugger PPC Port](#6-phase-5-debugger-ppc-port)
7. [Phase 6: DevBench Architecture Switching](#7-phase-6-devbench-architecture-switching)
8. [Phase 7: Example Apps & Testing](#8-phase-7-example-apps--testing)
9. [Risk Register](#9-risk-register)
10. [Reference Links](#10-reference-links)

---

## 1. Architecture Decisions

### Dual-Mode, Not Fork

The codebase will support both architectures from a single source tree. Selection happens at:

| Layer | How |
|-------|-----|
| **Build system** | `ARCH=ppc` or `ARCH=m68k` make variable |
| **C source** | `#ifdef __PPC__` / `#ifdef __mc68000__` for CPU-specific sections |
| **DevBench config** | `arch = "ppc"` or `arch = "m68k"` in `devbench.toml` |
| **Web UI** | Architecture shown in status bar; symbol loading uses correct toolchain |
| **Serial protocol** | Unchanged — text-based, CPU-agnostic |
| **MCP tools** | Unchanged — no CPU-specific logic |

### What Stays the Same (No Changes)

- `amiga-bridge/src/serial_io.c` — async serial I/O uses exec.library, same on PPC
- `amiga-bridge/src/ipc_manager.c` — MsgPort IPC, same API
- `amiga-bridge/src/client_registry.c` — pure data structures
- `amiga-bridge/src/protocol_handler.c` — text parsing, no CPU code
- `amiga-bridge/src/fs_access.c` — DOS file operations, same API
- `amiga-bridge/src/process_launcher.c` — DOS Execute/CreateNewProc, same API
- `amiga-bridge/client/bridge_client.c` — MsgPort IPC, same API (except inline asm in debugger pause)
- `amiga-bridge/include/bridge_ipc.h` — protocol constants
- All Python host-side code (devbench, protocol, server, MCP tools)
- Web UI (index.html) — architecture display only
- Serial protocol format
- GDB RSP server (register order changes, see Phase 5)

### What Needs PPC-Specific Code

- `amiga-bridge/src/debugger.c` — exception handling, breakpoints, registers
- `amiga-bridge/src/crash_handler.c` — exception frame parsing
- `amiga-bridge/src/snoop.c` — SetFunction patches (calling convention differs)
- `amiga-bridge/client/bridge_client.c` — pause stub inline asm
- `amiga-bridge/Makefile` — compiler selection
- `amiga-devbench/amiga_devbench/symbols.py` — PPC nm/objdump, register names
- `amiga-devbench/amiga_devbench/disasm.py` — PPC disassembly
- `amiga-devbench/amiga_devbench/gdb_server.py` — PPC register order
- `amiga-devbench/amiga_devbench/debugger.py` — PPC register names/count

---

## 2. Phase 1: QEMU Emulator Setup

### 2.1 Install QEMU with PPC Support

QEMU supports the sam460ex board which runs AmigaOS 4.1. Homebrew's QEMU may or may not include PPC targets.

```bash
# Check if Homebrew QEMU has PPC support
qemu-system-ppc --version 2>/dev/null && echo "PPC available" || echo "Need to build"

# Option A: Homebrew (if PPC target is included)
brew install qemu

# Option B: Build from source with PPC target
git clone https://gitlab.com/qemu-project/qemu.git
cd qemu
git checkout v9.2.0  # or latest stable
mkdir build && cd build
../configure --target-list=ppc-softmmu --prefix=/opt/qemu-ppc
make -j$(sysctl -n hw.ncpu)
make install
# Add to PATH: export PATH="/opt/qemu-ppc/bin:$PATH"
```

### 2.2 Obtain AmigaOS 4.1 Media

AmigaOS 4.1 Final Edition is commercial software. You need:

1. **AmigaOS 4.1 FE ISO** — purchase from [Hyperion Entertainment](https://www.hyperion-entertainment.com/)
2. **Sam460ex firmware** — included with the OS or downloadable from A-EON
3. **U-Boot bootloader** — the sam460ex uses U-Boot; a compatible ROM image is needed

### 2.3 Create Hard Drive Image

```bash
# Create a 2GB HDF for the OS installation
qemu-img create -f raw amigaos4-system.hdf 2G

# Create a shared development drive (for deploying binaries)
qemu-img create -f raw amigaos4-dev.hdf 512M
```

### 2.4 QEMU Launch Script

Create `scripts/start-qemu-os4.sh`:

```bash
#!/bin/bash
# Start QEMU sam460ex with AmigaOS 4.1
#
# Serial port exposed as TCP for devbench connection
# Network via user-mode for internet access

QEMU=qemu-system-ppc
HDD_SYSTEM=~/AmigaOS4/amigaos4-system.hdf
HDD_DEV=~/AmigaOS4/amigaos4-dev.hdf
CDROM=~/AmigaOS4/AmigaOS4.1-FE.iso  # For initial install only
SERIAL_PORT=2345

$QEMU \
  -machine sam460ex \
  -m 512 \
  -bios ~/AmigaOS4/u-boot-sam460ex.bin \
  -drive file=$HDD_SYSTEM,format=raw \
  -drive file=$HDD_DEV,format=raw \
  -cdrom $CDROM \
  -serial tcp::${SERIAL_PORT},server,nowait \
  -net nic,model=rtl8139 \
  -net user \
  -display default \
  -name "AmigaOS 4.1 - DevBench"
```

### 2.5 First-Time OS Installation

1. Boot from CDROM: the U-Boot bootloader should detect the CD
2. Follow the AmigaOS 4.1 installer
3. Install to the first HDD (DH0:)
4. Format the second HDD as DH1: (Dev drive for deploying binaries)
5. Set up serial.device for TCP communication (same approach as 68k)

### 2.6 Serial Configuration on AmigaOS 4.1

AmigaOS 4.1's serial.device works similarly but may use different unit numbers:

```
; In S:Startup-Sequence or user-startup
; Ensure serial.device is available
; The QEMU serial port maps to serial.device unit 0
```

### 2.7 FS-UAE Configuration (Alternative)

Create `~/Documents/FS-UAE/Configurations/AmigaOS4-Debug.fs-uae`:

```ini
[fs-uae]
amiga_model = A4000/PPC
# Requires Cyberstorm PPC ROM and Picasso IV ROM
cyberstorm_ppc_rom = /path/to/CyberStormPPC.rom
picasso_iv_rom = /path/to/PicassoIV.rom

chip_memory = 2048
fast_memory = 262144  # 256MB
jit_compiler = 1

hard_drive_0 = /path/to/amigaos4-system.hdf
hard_drive_1 = /path/to/amigaos4-dev.hdf

serial_port = tcp://0.0.0.0:2346
serial_on_demand = false

window_width = 1024
window_height = 768
```

### 2.8 DevBench Configuration

Add to `devbench.toml`:

```toml
[serial]
mode = "tcp"
host = "127.0.0.1"
port = 2345  # Same port, different emulator

[emulator]
# For QEMU PPC:
binary = "/opt/qemu-ppc/bin/qemu-system-ppc"
config = "scripts/start-qemu-os4.sh"
type = "qemu"  # NEW: distinguish from fs-uae

# For FS-UAE 68k (existing):
# binary = "/opt/homebrew/bin/fs-uae"
# config = "~/Documents/FS-UAE/Configurations/AmiKit-Debug.fs-uae"
# type = "fs-uae"

[build]
arch = "ppc"  # or "m68k" — selects toolchain
```

---

## 3. Phase 2: PPC Cross-Compiler Toolchain

### 3.1 Docker Image

```bash
# Pull or build the PPC cross-compiler Docker image
docker pull walkero/amigagccondocker:os4-gcc11

# Or build from AmigaPorts
docker build -t amigadev/crosstools:ppc-amigaos \
  --build-arg BUILD_OS=AmigaOS \
  --build-arg BUILD_PFX=ppc-amigaos \
  https://github.com/AmigaPorts/docker-amiga-gcc.git
```

### 3.2 Verify Toolchain

```bash
docker run --rm amigadev/crosstools:ppc-amigaos ppc-amigaos-gcc --version
docker run --rm amigadev/crosstools:ppc-amigaos ppc-amigaos-nm --version
docker run --rm amigadev/crosstools:ppc-amigaos ppc-amigaos-objdump --version
```

### 3.3 Native Installation (Optional)

If building natively on macOS:

```bash
git clone https://github.com/adtools/amigaos-cross-toolchain.git
cd amigaos-cross-toolchain
./toolchain-ppc --prefix=/opt/amiga-ppc
# Add to PATH: export PATH="/opt/amiga-ppc/bin:$PATH"
```

### 3.4 Key Compiler Differences

| Setting | 68k | PPC |
|---------|-----|-----|
| Compiler | `m68k-amigaos-gcc` | `ppc-amigaos-gcc` |
| Debug flags | `-g` | `-gstabs` |
| CPU flag | `-m68020` | (none needed) |
| ABI flag | `-noixemul` | `-noixemul` (same) |
| Linker | `-lamiga` | `-lauto` (OS4 auto-open) |
| Inline asm | `__asm volatile ("move.l %d0, %0")` | `__asm volatile ("mr %0, 3")` |

---

## 4. Phase 3: Build System Dual-Architecture

### 4.1 Top-Level Makefile Changes

```makefile
# Default architecture
ARCH ?= m68k

ifeq ($(ARCH),ppc)
    DOCKER_IMAGE = amigadev/crosstools:ppc-amigaos
    CC = ppc-amigaos-gcc
    AR = ppc-amigaos-ar
    NM = ppc-amigaos-nm
    OBJDUMP = ppc-amigaos-objdump
    CFLAGS_ARCH = -gstabs
    LDFLAGS_ARCH = -lauto
    DEPLOY_DIR = $(PPC_DEPLOY_DIR)
else
    DOCKER_IMAGE = amigadev/crosstools:m68k-amigaos
    CC = m68k-amigaos-gcc
    AR = m68k-amigaos-ar
    NM = m68k-amigaos-nm
    OBJDUMP = m68k-amigaos-objdump
    CFLAGS_ARCH = -m68020 -g
    LDFLAGS_ARCH = -lamiga
    DEPLOY_DIR = $(M68K_DEPLOY_DIR)
endif

CFLAGS = -noixemul -O2 -Wall -Iinclude $(CFLAGS_ARCH)
LDFLAGS = -noixemul $(LDFLAGS_ARCH)
```

### 4.2 Usage

```bash
make all                  # Build for 68k (default)
make all ARCH=ppc         # Build for PPC
make bridge ARCH=ppc      # Build only bridge daemon for PPC
make examples ARCH=ppc    # Build examples for PPC
make clean ARCH=ppc       # Clean PPC build artifacts
```

### 4.3 Separate Build Directories

To allow both architectures to coexist:

```makefile
BUILD_DIR = build/$(ARCH)
$(BUILD_DIR)/%.o: src/%.c
	@mkdir -p $(BUILD_DIR)
	$(CC) $(CFLAGS) -c -o $@ $<
```

---

## 5. Phase 4: Amiga-Bridge PPC Port

### 5.1 Files Requiring Changes

| File | Change Type | Notes |
|------|-------------|-------|
| `src/debugger.c` | Major rewrite | See Phase 5 |
| `src/crash_handler.c` | Major rewrite | PPC exception frames |
| `src/snoop.c` | Moderate | SetFunction calling convention |
| `src/main.c` | Minor | Remove 68k-specific startup if any |
| `client/bridge_client.c` | Minor | Pause stub asm |
| `Makefile` | Moderate | Dual-arch support |

### 5.2 Conditional Compilation Pattern

```c
/* In debugger.c */
#ifdef __PPC__
  #include "debugger_ppc.h"
  /* PPC-specific implementation */
#else
  #include "debugger_68k.h"
  /* 68k implementation (existing code) */
#endif
```

Or better — split into separate files:

```
src/debugger.c          # Shared interface + dispatch
src/debugger_68k.c      # 68k TRAP/Trace handlers
src/debugger_ppc.c      # PPC trap/exception handlers
```

### 5.3 crash_handler.c PPC Port

AmigaOS 4.1 provides `AddDebugHook()` in exec.library for crash/exception handling:

```c
#ifdef __PPC__
#include <exec/exectags.h>
#include <interfaces/exec.h>

/* OS4 debug hook — cleaner than 68k SetFunction hack */
static struct Hook debug_hook;
static ULONG debug_hook_func(struct Hook *hook, struct Task *task, struct DebugMessage *msg)
{
    if (msg->type == DBHMT_EXCEPTION) {
        /* Capture register state from msg->context */
        /* Send CRASH message over serial */
    }
    return 0;
}

void crash_init(void)
{
    debug_hook.h_Entry = (HOOKFUNC)debug_hook_func;
    IExec->AddDebugHook(&debug_hook, ADHT_GLOBAL);
}

void crash_cleanup(void)
{
    IExec->RemDebugHook(&debug_hook, RDHT_GLOBAL);
}
#endif
```

### 5.4 snoop.c PPC Port

SetFunction on OS4 uses a different calling convention. The "unhook, call, re-hook" pattern still works but the function prototypes use `struct Interface *` instead of direct library base registers:

```c
#ifdef __PPC__
/* OS4 uses interface-based calling */
/* SetFunction still exists but operates on interfaces */
/* May need SetMethod() for interface functions */
#endif
```

### 5.5 bridge_client.c Pause Stub

The pause stub uses inline asm for `Wait()`. PPC version:

```c
#ifdef __PPC__
/* PPC Wait() — uses OS4 interface calling convention */
/* No inline asm needed — just call IExec->Wait() */
static void dbg_pause_stub(void)
{
    IExec->Wait(SIGBREAKF_CTRL_F);
    /* Registers are saved/restored by the exception handler */
}
#endif
```

---

## 6. Phase 5: Debugger PPC Port

### 6.1 PPC Register Set

| 68k | PPC | Description |
|-----|-----|-------------|
| D0-D7 | r0-r7 (GPR) | General purpose (PPC has r0-r31) |
| A0-A7 | r8-r15 or dedicated | Address registers (PPC doesn't distinguish) |
| PC | PC/IAR | Instruction Address Register |
| SR | MSR | Machine State Register |
| — | LR | Link Register (return address) |
| — | CTR | Count Register |
| — | CR | Condition Register |
| — | XER | Fixed-Point Exception Register |

GDB PPC register order: r0-r31, PC, MSR, CR, LR, CTR, XER (38 registers total).

### 6.2 PPC Breakpoint Mechanism

PPC does NOT have TRAP #N instructions like 68k. Options:

1. **`trap` instruction** (opcode `0x7FE00008`) — unconditional trap, causes Program Exception
2. **`tw` / `twi`** — conditional trap (trap if registers match condition)
3. **Illegal instruction** — causes Illegal Instruction exception

Recommended: Use `trap` (opcode `0x7FE00008`, 4 bytes). This is the PPC equivalent of 68k's `TRAP #15`.

```c
#define PPC_TRAP_OPCODE  0x7FE00008  /* trap instruction */
#define PPC_TRAP_SIZE    4           /* 4 bytes (vs 2 for 68k TRAP) */
```

### 6.3 PPC Exception Handling

AmigaOS 4.1 provides structured exception handling:

```c
#ifdef __PPC__
/* Option 1: Use exec debug hooks (recommended for OS4) */
static struct Hook bp_hook;

static ULONG bp_hook_func(struct Hook *hook, struct Task *task, struct DebugMessage *msg)
{
    if (msg->type == DBHMT_EXCEPTION) {
        struct ExceptionContext *ctx = msg->context;
        /* ctx->gpr[0..31] — all 32 GPRs */
        /* ctx->ip — instruction pointer (PC) */
        /* ctx->msr — machine state register */
        /* ctx->lr — link register */
        /* ctx->ctr — count register */
        /* ctx->cr — condition register */

        /* Check if this is our breakpoint */
        ULONG pc = ctx->ip;
        int bp_idx = dbg_find_bp_by_addr(pc);
        if (bp_idx >= 0) {
            /* Save registers */
            /* Restore original instruction */
            /* Signal pause */
        }
    }
    return 0;
}
#endif
```

```c
/* Option 2: Direct exception vector patching (lower level, riskier) */
/* PPC uses IVOR (Interrupt Vector Offset Register) for exception vectors */
/* Less portable but doesn't depend on OS debug API */
```

### 6.4 PPC Single-Step (Trace)

PPC has the MSR[SE] (Single-Step Enable) bit, equivalent to 68k's T1 bit:

```c
/* Set single-step in MSR */
ctx->msr |= MSR_SE;  /* MSR bit 21 = Single-Step Enable */

/* After one instruction, a Trace exception fires */
/* The debug hook receives DBHMT_TRACE type */
```

### 6.5 PPC Backtrace

PPC calling convention stores the return address in LR and saves it to the stack frame:

```c
/* PPC stack frame:
 *   [SP+0]  = back chain (previous SP)
 *   [SP+4]  = saved LR (return address)
 * Walk: follow back chain, read saved LR at each frame */
static void dbg_ppc_backtrace(void)
{
    ULONG *fp = (ULONG *)ctx->gpr[1];  /* r1 = stack pointer */
    while (fp && TypeOfMem(fp)) {
        ULONG lr = fp[1];  /* Saved LR */
        if (!lr) break;
        /* Record frame */
        fp = (ULONG *)fp[0];  /* Back chain */
    }
}
```

### 6.6 Updated debugger.py (Host Side)

```python
PPC_REG_NAMES = [f"r{i}" for i in range(32)] + ["PC", "MSR", "CR", "LR", "CTR", "XER"]

class DebuggerState:
    def __init__(self, arch="m68k"):
        self.arch = arch
        if arch == "ppc":
            self.regs = [0] * 38  # r0-r31 + PC + MSR + CR + LR + CTR + XER
        else:
            self.regs = [0] * 18  # D0-D7 + A0-A7 + PC + SR
```

### 6.7 Updated gdb_server.py

```python
# PPC GDB register order
if arch == "ppc":
    # r0-r31 (32 GPRs), then special: PC, MSR, CR, LR, CTR, XER
    GDB_REG_COUNT = 38
```

---

## 7. Phase 6: DevBench Architecture Switching

### 7.1 Config Changes

Add `arch` field to `devbench.toml`:

```toml
[build]
arch = "m68k"  # or "ppc"
docker_image_m68k = "amigadev/crosstools:m68k-amigaos"
docker_image_ppc = "amigadev/crosstools:ppc-amigaos"
```

### 7.2 config.py Changes

```python
@dataclass
class DevBenchConfig:
    # ... existing fields ...
    arch: str = "m68k"  # "m68k" or "ppc"
    docker_image_ppc: str = "amigadev/crosstools:ppc-amigaos"
```

### 7.3 builder.py Changes

```python
async def build(self, project: str | None = None) -> BuildResult:
    if self._config.arch == "ppc":
        docker_image = self._config.docker_image_ppc
        make_args = "ARCH=ppc"
    else:
        docker_image = self._config.docker_image
        make_args = ""
    # ... rest of build logic
```

### 7.4 symbols.py Changes

```python
async def _run_tool(binary_path, tool, *args):
    if config.arch == "ppc":
        tool = tool.replace("m68k-amigaos-", "ppc-amigaos-")
    # ... existing logic
```

### 7.5 Web UI Changes

- Architecture badge in status bar: "68k" or "PPC"
- Register panel adapts to show r0-r31 or D0-D7/A0-A7
- Disassembly view uses PPC mnemonics when arch=ppc

### 7.6 MCP Tools

No changes needed — tools are architecture-agnostic. The `amiga_build` tool passes `ARCH=ppc` when configured.

---

## 8. Phase 7: Example Apps & Testing

### 8.1 Dual-Build Examples

Each example's Makefile includes the top-level arch logic:

```makefile
include ../../arch.mk  # Sets CC, CFLAGS, etc based on ARCH

TARGET = bouncing_ball
SRCS = main.c
OBJS = $(SRCS:.c=.o)

all: $(TARGET)
$(TARGET): $(OBJS)
	$(CC) -o $@ $^ $(LDFLAGS)
```

### 8.2 PPC-Specific API Changes

AmigaOS 4.1 uses interface-based library access:

```c
#ifdef __PPC__
/* OS4 interface-based calls */
#include <proto/intuition.h>
/* IIntuition->OpenWindowTags(...) instead of OpenWindowTags(...) */
/* But with -lauto, the compiler auto-generates interface lookups */
#endif
```

With `-lauto` linker flag, most classic API calls work unchanged. The compiler auto-generates the interface dispatch code.

### 8.3 Testing Checklist

- [ ] Bridge daemon starts on OS4 and connects via serial
- [ ] Heartbeat messages flow
- [ ] Client registration works
- [ ] GETVAR/SETVAR work
- [ ] Hook calls work
- [ ] Crash handler catches exceptions
- [ ] Debugger: attach/detach
- [ ] Debugger: breakpoints (trap instruction)
- [ ] Debugger: single-step (MSR[SE])
- [ ] Debugger: registers (r0-r31)
- [ ] Debugger: backtrace (LR chain)
- [ ] Symbol loading (ppc-amigaos-nm/objdump)
- [ ] GDB RSP with PPC register order
- [ ] MCP tools work
- [ ] Web UI shows PPC registers

---

## 9. Risk Register

| Risk | Impact | Likelihood | Mitigation |
|------|--------|------------|------------|
| QEMU sam460ex serial not working | High | Medium | Test early; fall back to network socket |
| OS4 debug hooks API underdocumented | Medium | High | Use direct exception vector patching as fallback |
| PPC cross-compiler Docker image broken | Medium | Low | Multiple image options; can build from source |
| AmigaOS 4.1 license cost | Low | Certain | Budget ~$30-50 for the OS license |
| `-lauto` doesn't support all classic APIs | Medium | Medium | Manual interface lookups where needed |
| PPC inline asm complexity | Medium | Medium | Minimize inline asm; use C functions where possible |
| 68k regression from shared code changes | High | Medium | Keep 68k and PPC code in separate files; comprehensive `#ifdef` gating |
| FS-UAE PPC mode limitations (no dir HD) | Medium | Certain | Use QEMU instead; HDF for FS-UAE if needed |

---

## 10. Reference Links

### Emulators
- [QEMU AmigaNG boards documentation](https://www.qemu.org/docs/master/system/ppc/amigang.html)
- [FS-UAE AmigaOS 4.1 setup](https://fs-uae.net/docs/amigaos-4-1/)
- [UTM for macOS](https://mac.getutm.app/)
- [QEMU sam460ex setup guide (GitHub Gist)](https://gist.github.com/joergschultzelutter/cd7fa34a26e64f8b1bcca1f26cf30bbf)

### Cross-Compilers
- [adtools/amigaos-cross-toolchain](https://github.com/adtools/amigaos-cross-toolchain)
- [AmigaPorts/docker-amiga-gcc](https://github.com/AmigaPorts/docker-amiga-gcc)
- [walkero/amigagccondocker (Docker Hub)](https://hub.docker.com/r/walkero/amigagccondocker)
- [Kea Sigma Delta: Cross-compiling guide](https://keasigmadelta.com/blog/quick-guide-cross-compiling-to-amigaos-4-x-the-easy-way/)

### AmigaOS 4 Development
- [AmigaOS Wiki: GDB for Beginners](https://wiki.amigaos.net/wiki/GDB_for_Beginners)
- [AmigaOS Wiki: Exec Debug Interface](https://wiki.amigaos.net/wiki/How_to_open_and_use_the_exec_debug_interface)
- [AmigaOS Wiki: Serial Device](https://wiki.amigaos.net/wiki/Serial_Device)
- [AmigaOS SDK documentation](https://wiki.amigaos.net/wiki/AmigaOS_Manual)
- [Hyperion Entertainment (OS purchase)](https://www.hyperion-entertainment.com/)

### Hardware
- [Sam460ex (Wikipedia)](https://en.wikipedia.org/wiki/Sam460ex)
- [A-EON Technology](https://www.a-eon.com/) — Sam460/X5000 hardware

### PowerPC Architecture
- [PowerPC instruction set reference](https://www.ibm.com/docs/en/aix/7.2?topic=set-fixed-point-trap-instructions)
- [PPC exception handling](https://www.ibm.com/docs/en/aix/7.2?topic=concepts-hardware-exceptions)

---

## Implementation Order

1. **QEMU setup** (Phase 1) — get OS4 running, verify serial works
2. **PPC toolchain** (Phase 2) — verify cross-compilation produces working binaries
3. **Build system** (Phase 3) — dual-arch make, no code changes yet
4. **Bridge port** (Phase 4) — serial + IPC working on PPC (no debugger yet)
5. **Debugger port** (Phase 5) — breakpoints + registers on PPC
6. **DevBench switching** (Phase 6) — config-driven arch selection
7. **Testing** (Phase 7) — full end-to-end validation

Each phase is independently testable. Phase 1-3 can be done without touching any existing 68k code. Phase 4-5 use `#ifdef` guards so 68k is never affected. Phase 6 is additive config only.
