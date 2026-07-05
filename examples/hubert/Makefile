# hubert — build both the m68k binary and the host-side unit tests.
#
# The Amiga binary uses m68k-amigaos-gcc (via Docker at repo root). The
# host tests use the system cc — the pure-logic modules compile on any
# ANSI C compiler, main.c is Amiga-only and stays out of the host build.
#
# From the repo root run `make -C examples/hubert` for the Amiga build,
# or `make -C examples/hubert hosttest` to compile + run the host tests
# directly on this Mac/Linux without any emulator.

CC       = m68k-amigaos-gcc
CFLAGS   = -noixemul -O2 -m68020 -Wall -DHAVE_AMIGA_DOS \
           -I../../amiga-bridge/include
LDFLAGS  = -noixemul -g -L../../amiga-bridge -lbridge -lamiga
TARGET   = hubert

CORE_OBJS = history.o tokenizer.o line_editor.o env.o builtins.o executor.o \
            net.o http.o jsonx.o llm_direct.o

all: $(TARGET)

$(TARGET): main.o $(CORE_OBJS)
	$(CC) -o $@ $^ $(LDFLAGS)

%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $<

# ─── Host test runner ────────────────────────────────────────────────────
#
# Compiles the pure modules with the host cc (no AmigaOS types touched)
# then runs them. Fast (sub-second) so we can iterate on shell logic
# without an emulator round-trip.

HOSTCC       = cc
HOSTCFLAGS   = -O0 -g -Wall -Wextra -Wno-unused-parameter -std=c99
HOSTTEST_BIN     = tests/test_core
HOSTTEST_SRCS    = tests/test_core.c \
                   history.c tokenizer.c line_editor.c env.c builtins.c executor.c
HOSTDIRECT_BIN   = tests/test_direct
HOSTDIRECT_SRCS  = tests/test_direct.c net.c http.c jsonx.c llm_direct.c

hosttest: $(HOSTTEST_BIN) $(HOSTDIRECT_BIN)
	./$(HOSTTEST_BIN)
	@echo
	./$(HOSTDIRECT_BIN)

$(HOSTTEST_BIN): $(HOSTTEST_SRCS)
	$(HOSTCC) $(HOSTCFLAGS) -o $@ $(HOSTTEST_SRCS)

$(HOSTDIRECT_BIN): $(HOSTDIRECT_SRCS)
	$(HOSTCC) $(HOSTCFLAGS) -o $@ $(HOSTDIRECT_SRCS)

clean:
	rm -f *.o $(TARGET) $(HOSTTEST_BIN) $(HOSTDIRECT_BIN)

.PHONY: all clean hosttest
