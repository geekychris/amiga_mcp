# Host-buildable unit tests for pure-C code extracted from the bridge daemon.
# Runs on macOS / Linux with plain clang or gcc — no Amiga cross-compiler.
#
# Usage:
#   make          # build the test binary
#   make test     # build and run
#   make clean

CC      ?= clang
CFLAGS  ?= -std=c99 -Wall -Wextra -Wpedantic -O2 -g \
           -I../include
LDFLAGS ?=

# Extracted pure-C sources under test.
SRCS = ../src/script_util.c test_bridge.c
OBJS = $(SRCS:.c=.o)

TEST_BIN = test_bridge

.PHONY: all test clean

all: $(TEST_BIN)

test: $(TEST_BIN)
	./$(TEST_BIN)

$(TEST_BIN): $(OBJS)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $<

clean:
	rm -f $(OBJS) $(TEST_BIN)
