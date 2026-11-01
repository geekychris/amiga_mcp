# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Chris Collins <chris@hitorro.com>

# claude_busy — the "Claude is testing" banner, for AmigaOS 4 and classic 68k.
# Built by scripts/build-claude-busy.sh (both, through Docker):
#   claude_busy_os4  (ppc-amigaos-gcc)   claude_busy  (m68k-amigaos-gcc)

ARCH ?= ppc

ifeq ($(ARCH),ppc)
CC = ppc-amigaos-gcc
CFLAGS = -mcrt=newlib -O2 -mcpu=powerpc -Wall -D__PPC__ -D__USE_INLINE__ -D__USE_OLD_TIMEVAL__ -Wno-deprecated-declarations
LIBS = -lauto
TARGET = claude_busy_os4
else
CC = m68k-amigaos-gcc
CFLAGS = -noixemul -O2 -m68020 -Wall
LIBS = -lamiga
TARGET = claude_busy
endif

all: $(TARGET)

$(TARGET): claude_busy.c
	$(CC) $(CFLAGS) -o $@ $< $(LIBS)

clean:
	rm -f claude_busy claude_busy_os4

.PHONY: all clean
