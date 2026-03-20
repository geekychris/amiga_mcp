CC = m68k-amigaos-gcc
CFLAGS = -noixemul -O2 -m68020 -Wall -I../../amiga-bridge/include
LDFLAGS = -noixemul -g -L../../amiga-bridge -lbridge -lamiga
TARGET = frank_the_frog

SRCS = main.c gfx.c frank.c lanes.c playfield.c score.c sound.c modplay.c
OBJS = $(SRCS:.c=.o)

all: $(TARGET) closescr

$(TARGET): $(OBJS)
	$(CC) -o $@ $^ $(LDFLAGS)

closescr: closescr.c
	$(CC) $(CFLAGS) -o $@ $< -noixemul -lamiga

%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $<

clean:
	rm -f *.o $(TARGET) closescr
