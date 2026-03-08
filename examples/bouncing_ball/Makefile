CC = m68k-amigaos-gcc
CFLAGS = -noixemul -O2 -m68000 -Wall -I../../amiga-debug-lib/include
LDFLAGS = -noixemul -L../../amiga-debug-lib -ldebug -lamiga
TARGET = bouncing_ball

all: $(TARGET)

$(TARGET): main.o
	$(CC) -o $@ $^ $(LDFLAGS)

%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $<

clean:
	rm -f *.o $(TARGET)
