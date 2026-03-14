CC = m68k-amigaos-gcc
AS = vasmm68k_mot
CFLAGS = -noixemul -O2 -m68020 -Wall -I../../amiga-bridge/include
ASFLAGS = -Fhunk -m68020 -no-opt -quiet
LDFLAGS = -noixemul -L../../amiga-bridge -lbridge -lamiga

TARGET = starfield

OBJS = startup.o starfield.o

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) -o $@ $^ $(LDFLAGS)

starfield.o: starfield.asm
	$(AS) $(ASFLAGS) -o $@ $<

startup.o: startup.c
	$(CC) $(CFLAGS) -c -o $@ $<

clean:
	rm -f *.o $(TARGET)
