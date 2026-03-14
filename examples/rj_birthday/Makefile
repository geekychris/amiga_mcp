CC = m68k-amigaos-gcc
AS = vasmm68k_mot
CFLAGS = -noixemul -O2 -m68020 -Wall -I../../amiga-bridge/include
ASFLAGS = -Fhunk -m68000 -no-opt -I/opt/m68k-amigaos/m68k-amigaos/ndk-include/hardware -I/opt/m68k-amigaos/m68k-amigaos/ndk-include/resources
LDFLAGS = -noixemul -L../../amiga-bridge -lbridge -lamiga

TARGET = rj_birthday

SRCS = main.c game.c rooms.c draw.c input.c sound.c
OBJS = $(SRCS:.c=.o) ptplayer.o

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) -o $@ $^ $(LDFLAGS)

ptplayer.o: ptplayer.asm
	$(AS) $(ASFLAGS) -o $@ $<

%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $<

clean:
	rm -f $(OBJS) $(TARGET)

.PHONY: all clean
