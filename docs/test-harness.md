# Test Harness

Automated test framework for Amiga programs using the bridge client library.

## Overview

The test harness lets you write structured tests in your Amiga C programs. Test results flow through the bridge protocol to the host, where they appear in the web UI and are accessible via MCP tools.

## API

Include `bridge_client.h` and link with `-lbridge`.

### ab_test_begin(suiteName)

Starts a test suite. Call once before your assertions.

```c
ab_test_begin("my_suite");
```

### AB_ASSERT(condition, testName)

Macro that evaluates `condition`. Logs a PASS or FAIL result with the given test name.

```c
AB_ASSERT(result == 42, "correct_answer");
```

### ab_test_end()

Ends the test suite and logs a summary (total, passed, failed).

```c
ab_test_end();
```

## Result Flow

1. `AB_ASSERT` sends LOG messages with `TEST_` prefixes through the serial bridge protocol
2. The bridge daemon forwards these over serial to the host
3. `amiga-devbench` receives and parses the test results
4. Results are available in the web UI and via MCP

## Web UI

Test results appear in the **Test Harness** panel. Start your test program on the Amiga and results populate in real time, showing pass/fail status for each assertion and a summary at the end.

## MCP

Use the `amiga_run_tests` tool to trigger and collect test results programmatically. This is useful for CI-style workflows driven by Claude Code.

## Build

```makefile
CC = m68k-amigaos-gcc
CFLAGS = -noixemul -O2 -m68020 -Wall -I../../amiga-bridge/include
LDFLAGS = -noixemul -L../../amiga-bridge -lbridge -lamiga
```

## Example

```c
#include "bridge_client.h"

int main(void)
{
    if (!ab_init("my_tests")) return 1;

    ab_test_begin("arithmetic");
    AB_ASSERT(2 + 2 == 4, "addition");
    AB_ASSERT(3 * 3 == 9, "multiplication");
    ab_test_end();

    ab_cleanup();
    return 0;
}
```
