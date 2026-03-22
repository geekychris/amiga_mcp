CC = m68k-amigaos-gcc
CFLAGS = -noixemul -O2 -m68020 -Wall
LDFLAGS = -noixemul -g -lamiga

TARGET = launcher

all: $(TARGET)

$(TARGET): main.o
	$(CC) -o $@ $^ $(LDFLAGS)

%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $<

clean:
	rm -f *.o $(TARGET)

.PHONY: all clean
