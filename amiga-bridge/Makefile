CC = m68k-amigaos-gcc
AR = m68k-amigaos-ar
# -MMD -MP auto-generate per-object .d header-dependency files so that editing
# a header (e.g. bumping the version in bridge_internal.h) rebuilds every object
# that includes it - not just the .c files that changed.
CFLAGS = -noixemul -O0 -m68020 -Wall -Iinclude -MMD -MP
LDFLAGS = -noixemul -lamiga

# Daemon sources
DAEMON_SRCS = src/main.c src/serial_io.c src/ipc_manager.c \
              src/client_registry.c src/protocol_handler.c \
              src/system_inspector.c src/fs_access.c \
              src/process_launcher.c src/gfx_inspector.c \
              src/crash_handler.c src/snoop.c \
              src/audio_inspector.c src/intuition_inspector.c \
              src/input_inject.c src/font_browser.c \
              src/chipwrite_logger.c src/pool_tracker.c \
              src/clipboard_bridge.c src/arexx_bridge.c \
              src/debugger.c src/transport.c src/net_io.c \
              src/b64.c src/version.c
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

# snoop.c needs -O2 so register __asm("d1") captures work correctly
src/snoop.o: src/snoop.c
	$(CC) -noixemul -O2 -m68020 -Wall -Iinclude -MMD -MP -c -o $@ $<

# debugger.c needs -O2 for register captures in exception handlers
src/debugger.o: src/debugger.c
	$(CC) -noixemul -O2 -m68020 -Wall -Iinclude -MMD -MP -c -o $@ $<

%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $<

# Pull in auto-generated header dependencies (created by -MMD above).
-include $(DAEMON_OBJS:.o=.d) $(CLIENT_OBJS:.o=.d)

clean:
	rm -f $(DAEMON_OBJS) $(DAEMON_BIN)
	rm -f $(CLIENT_OBJS) $(CLIENT_LIB)
	rm -f $(DAEMON_OBJS:.o=.d) $(CLIENT_OBJS:.o=.d)
