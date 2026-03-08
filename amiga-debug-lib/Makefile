CC = m68k-amigaos-gcc
AR = m68k-amigaos-ar
CFLAGS = -noixemul -O2 -m68000 -Wall -Iinclude
SRCS = src/debug.c src/serial.c src/log.c src/inspect.c
OBJS = $(SRCS:.c=.o)
LIB = libdebug.a

all: $(LIB)

$(LIB): $(OBJS)
	$(AR) rcs $@ $^

%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $<

clean:
	rm -f $(OBJS) $(LIB)
