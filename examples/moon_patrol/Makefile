CC = m68k-amigaos-gcc
CFLAGS = -noixemul -O2 -m68020 -Wall -I../../amiga-bridge/include
LDFLAGS = -noixemul -g -L../../amiga-bridge -lbridge -lamiga
TARGET = moon_patrol
SRCS = main.c game.c draw.c input.c sound.c
OBJS = $(SRCS:.c=.o)
all: $(TARGET)
$(TARGET): $(OBJS)
	$(CC) -o $@ $^ $(LDFLAGS)
%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $<
clean:
	rm -f $(OBJS) $(TARGET)
.PHONY: all clean
