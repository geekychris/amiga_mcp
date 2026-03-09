CC = m68k-amigaos-gcc
CFLAGS = -noixemul -O0 -m68020 -Wall -I../../amiga-bridge/include
LDFLAGS = -noixemul -L../../amiga-bridge -lbridge -lamiga
TARGET = system_monitor

all: $(TARGET)

$(TARGET): main.o
	$(CC) -o $@ $^ $(LDFLAGS)

%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $<

clean:
	rm -f *.o $(TARGET)
