CC = m68k-amigaos-gcc
CFLAGS = -noixemul -O2 -m68020 -Wall -I../../amiga-bridge/include
LDFLAGS = -noixemul -L../../amiga-bridge -lbridge -lamiga

TARGET = test_example

all: $(TARGET)

$(TARGET): test_example.c
	$(CC) $(CFLAGS) -o $@ $< $(LDFLAGS)

clean:
	rm -f $(TARGET)
