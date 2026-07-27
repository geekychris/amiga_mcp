# https_get — minimal HTTPS GET client for AmigaOS 4 (PPC).
#
# Cross-compile from macOS/Linux via:
#   docker run --rm -v $(pwd):/w -w /w/tools/https_get \
#       walkero/amigagccondocker:os4-gcc11-arm64 make
#
# Or from the project root:
#   scripts/build-tool-ppc.sh https_get
CC = ppc-amigaos-gcc
CFLAGS = -mcrt=newlib -O2 -mcpu=440 -Wall \
         -D__PPC__ -D__USE_INLINE__ -D__USE_OLD_TIMEVAL__
LDFLAGS = -mcrt=newlib
# Use libamisslauto — it opens amissl.library + amisslmaster + wires
# up the bsdsocket integration automatically. This is what the AmiSSL-
# shipped openssl CLI uses; the tricky "share the socket base" dance
# happens inside libamisslauto's ctor. Link with -lauto for the
# remaining base openinger (Intuition/DOS/Exec).
LIBS = -lamisslauto -lssl -lcrypto -lauto

TARGET = https_get

all: $(TARGET)

$(TARGET): $(TARGET).o
	$(CC) $(LDFLAGS) -o $@ $^ $(LIBS)

%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $<

clean:
	rm -f *.o $(TARGET)
