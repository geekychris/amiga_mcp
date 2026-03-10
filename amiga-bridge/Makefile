CC = m68k-amigaos-gcc
AR = m68k-amigaos-ar
CFLAGS = -noixemul -O0 -m68020 -Wall -Iinclude
LDFLAGS = -noixemul -lamiga

# Daemon sources
DAEMON_SRCS = src/main.c src/serial_io.c src/ipc_manager.c \
              src/client_registry.c src/protocol_handler.c \
              src/system_inspector.c src/fs_access.c \
              src/process_launcher.c src/gfx_inspector.c \
              src/crash_handler.c src/snoop.c
DAEMON_OBJS = $(DAEMON_SRCS:.c=.o)
DAEMON_BIN = amiga-bridge

# Client library
CLIENT_SRCS = client/bridge_client.c
CLIENT_OBJS = $(CLIENT_SRCS:.c=.o)
CLIENT_LIB = libbridge.a

.PHONY: all clean daemon client

all: daemon client

daemon: $(DAEMON_BIN)

client: $(CLIENT_LIB)

$(DAEMON_BIN): $(DAEMON_OBJS)
	$(CC) -o $@ $^ $(LDFLAGS)

$(CLIENT_LIB): $(CLIENT_OBJS)
	$(AR) rcs $@ $^

# snoop.c needs -O2 so register __asm("d1") captures work correctly
src/snoop.o: src/snoop.c
	$(CC) -noixemul -O2 -m68020 -Wall -Iinclude -c -o $@ $<

%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $<

clean:
	rm -f $(DAEMON_OBJS) $(DAEMON_BIN)
	rm -f $(CLIENT_OBJS) $(CLIENT_LIB)
