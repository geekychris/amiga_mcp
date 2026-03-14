CC = m68k-amigaos-gcc
AS = vasmm68k_mot
CFLAGS = -noixemul -O2 -m68020 -Wall -I../../amiga-bridge/include
ASFLAGS = -Fhunk -m68020 -no-opt -quiet
LDFLAGS = -noixemul -L../../amiga-bridge -lbridge -lamiga

TARGET = starfield

C_OBJS = startup.o
ASM_OBJS = starfield.o ptplayer.o

OBJS = $(C_OBJS) $(ASM_OBJS)

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) -o $@ $^ $(LDFLAGS)

starfield.o: starfield.asm
	$(AS) $(ASFLAGS) -o $@ $<

ptplayer.o: ptplayer.asm
	$(AS) -Fhunk -m68000 -no-opt -quiet -o $@ $<

%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $<

clean:
	rm -f *.o $(TARGET)
