CC = m68k-amigaos-gcc
CFLAGS = -noixemul -O2 -m68020 -Wall -I../../amiga-bridge/include
LDFLAGS = -noixemul -L../../amiga-bridge -lbridge -lamiga

TARGET = test_new_features

all: $(TARGET)

$(TARGET): test_new_features.c
	$(CC) $(CFLAGS) -o $@ $< $(LDFLAGS)

clean:
	rm -f $(TARGET)
