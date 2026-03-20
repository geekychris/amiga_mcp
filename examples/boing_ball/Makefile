CC = m68k-amigaos-gcc
CFLAGS = -noixemul -O2 -m68020 -Wall -g -I../../amiga-bridge/include
LDFLAGS = -noixemul -g -L../../amiga-bridge -lbridge -lamiga

TARGET = boing_ball

SRCS = main.c tables.c physics.c sphere.c copper.c render.c sound.c
OBJS = $(SRCS:.c=.o)

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) -o $@ $^ $(LDFLAGS)

%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $<

clean:
	rm -f *.o $(TARGET)
