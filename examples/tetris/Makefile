CC = m68k-amigaos-gcc
AS = vasmm68k_mot
CFLAGS = -noixemul -O2 -m68020 -Wall -g -I../../amiga-bridge/include
ASFLAGS = -Fhunk -m68000 -no-opt \
	-I/opt/m68k-amigaos/m68k-amigaos/ndk-include \
	-I/opt/m68k-amigaos/m68k-amigaos/ndk-include/lvo
LDFLAGS = -noixemul -g -L../../amiga-bridge -lbridge -lamiga

TARGET = stakattack

SRCS = main.c game.c draw.c input.c
OBJS = $(SRCS:.c=.o) ptplayer.o

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) -o $@ $^ $(LDFLAGS)

ptplayer.o: ptplayer.asm
	$(AS) $(ASFLAGS) -o $@ $<

%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $<

clean:
	rm -f *.o $(TARGET)
