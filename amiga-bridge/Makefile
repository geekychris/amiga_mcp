# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Chris Collins

# ARCH=m68k (default) — classic AmigaOS 3.x on 680x0
# ARCH=ppc            — AmigaOS 4.1 on PowerPC (sam460ex / QEMU)
# Invoke: `make ARCH=ppc` from a shell inside the appropriate cross-compiler
# Docker image (see scripts/build-bridge-ppc.sh for the wrapper).
ARCH ?= m68k

ifeq ($(ARCH),ppc)
  CC = ppc-amigaos-gcc
  AR = ppc-amigaos-ar
  # PPC OS4: -mcrt=newlib links against the standard C runtime; drop the
  # 68k-specific -noixemul flag. -mcpu=440 matches the sam460ex CPU.
  # -D__USE_INLINE__      makes <proto/*> pull in inline4/*.h so classic
  #                       names like GetMsg() work as macros for IExec->GetMsg().
  # -D__USE_OLD_TIMEVAL__ keeps `struct timerequest`/`struct timeval` with
  #                       tr_node/tr_time/tv_secs/tv_micro layout (OS4 renamed
  #                       to TimeRequest/TimeVal to avoid POSIX collision).
  CFLAGS = -mcrt=newlib -O2 -mcpu=440 -Wall -Iinclude -MMD -MP \
           -D__PPC__ -D__USE_INLINE__ -D__USE_OLD_TIMEVAL__
  LDFLAGS = -mcrt=newlib -lauto
else
  CC = m68k-amigaos-gcc
  AR = m68k-amigaos-ar
  # -MMD -MP auto-generate per-object .d header-dependency files so that
  # editing a header (e.g. bumping the version in bridge_internal.h) rebuilds
  # every object that includes it — not just the .c files that changed.
  CFLAGS = -noixemul -O0 -m68020 -Wall -Iinclude -MMD -MP
  LDFLAGS = -noixemul -lamiga
endif

# Daemon sources split into portable + 68k-specific. The portable set
# uses only text-protocol parsing, DOS/exec APIs, and stdlib — those
# translate straight to PPC OS4. The 68k-only set uses inline __asm("d1")
# register captures (snoop.c, debugger.c) or parses the 68k exception
# frame layout (crash_handler.c) — needs per-arch stubs before PPC.
DAEMON_SRCS_PORTABLE = src/main.c src/serial_io.c src/ipc_manager.c \
                       src/client_registry.c src/protocol_handler.c \
                       src/system_inspector.c src/fs_access.c \
                       src/process_launcher.c src/gfx_inspector.c \
                       src/audio_inspector.c src/intuition_inspector.c \
                       src/input_inject.c src/font_browser.c \
                       src/chipwrite_logger.c src/pool_tracker.c \
                       src/clipboard_bridge.c src/arexx_bridge.c \
                       src/transport.c src/net_io.c \
                       src/b64.c src/script_util.c src/version.c
DAEMON_SRCS_68K_ONLY = src/crash_handler.c src/snoop.c src/debugger.c

ifeq ($(ARCH),ppc)
  DAEMON_SRCS = $(DAEMON_SRCS_PORTABLE) src/arch_stubs_ppc.c
else
  DAEMON_SRCS = $(DAEMON_SRCS_PORTABLE) $(DAEMON_SRCS_68K_ONLY)
endif
DAEMON_OBJS = $(DAEMON_SRCS:.c=.o)
DAEMON_BIN = amiga-bridge

# Client library
CLIENT_SRCS = client/bridge_client.c
CLIENT_OBJS = $(CLIENT_SRCS:.c=.o)
CLIENT_LIB = libbridge.a

.PHONY: all clean daemon client FORCE

# `all` must be the default goal: the `src/version.o: FORCE` rule below would
# otherwise become the first (default) target, so a bare `make` would only
# rebuild the build-stamp and never link the daemon.
.DEFAULT_GOAL := all

# Always rebuild the build-stamp so __DATE__/__TIME__ reflect this build.
src/version.o: FORCE
FORCE:

all: daemon client

daemon: $(DAEMON_BIN)

client: $(CLIENT_LIB)

$(DAEMON_BIN): $(DAEMON_OBJS)
	$(CC) -o $@ $^ $(LDFLAGS)

$(CLIENT_LIB): $(CLIENT_OBJS)
	$(AR) rcs $@ $^

# snoop.c and debugger.c need -O2 so the m68k __asm("dN") register captures
# work correctly. On PPC OS4 the calling convention differs, so the current
# implementations are 68k-only — they'll need per-arch stubs when we port
# to PPC. For now build both files with -O2 using the arch-appropriate CFLAGS.
ifeq ($(ARCH),ppc)
  ASM_CAPTURE_FLAGS = -mcrt=newlib -O2 -mcpu=440 -Wall -Iinclude -MMD -MP \
                      -D__PPC__ -D__USE_INLINE__ -D__USE_OLD_TIMEVAL__
else
  ASM_CAPTURE_FLAGS = -noixemul -O2 -m68020 -Wall -Iinclude -MMD -MP
endif

src/snoop.o: src/snoop.c
	$(CC) $(ASM_CAPTURE_FLAGS) -c -o $@ $<

src/debugger.o: src/debugger.c
	$(CC) $(ASM_CAPTURE_FLAGS) -c -o $@ $<

%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $<

# Pull in auto-generated header dependencies (created by -MMD above).
-include $(DAEMON_OBJS:.o=.d) $(CLIENT_OBJS:.o=.d)

clean:
	rm -f $(DAEMON_OBJS) $(DAEMON_BIN)
	rm -f $(CLIENT_OBJS) $(CLIENT_LIB)
	rm -f $(DAEMON_OBJS:.o=.d) $(CLIENT_OBJS:.o=.d)
