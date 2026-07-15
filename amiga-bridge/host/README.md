# Bridge host-tests

Unit tests for pure-C code extracted from the AmigaOS bridge daemon. Runs on macOS / Linux with plain clang or gcc — no Amiga cross-compiler required.

## Run

```bash
cd amiga-bridge/host
make test
```

Expected output:

```
[1/10] empty_payload ... PASS
...
Tests: 10 total, 10 passed, 0 failed
```

## What's covered

Each file under test is a host-buildable chunk of the daemon code, imported directly from `amiga-bridge/src/` — the same source that ships in the Amiga binary is what these tests exercise. Testable modules today:

| File | What it does | Test file |
|---|---|---|
| `script_util.c` | Streaming `;` → `\n` chunker used by `SCRIPT` command handler | `test_bridge.c` |

## Adding a new host-testable extract

1. Write the logic in a new `amiga-bridge/src/<name>.c` with a header at `amiga-bridge/include/<name>.h`.
2. **Restrict the header to `<stddef.h>` / `<string.h>` / `<stdint.h>`** — no `exec/types.h`, `proto/*`, or AmigaOS macros. Use plain C types in the interface.
3. If the daemon needs to call AmigaOS APIs from the extracted logic, take a function pointer (see `dos_write_fh()` in `protocol_handler.c` for the pattern).
4. Append the new `.c` to `DAEMON_SRCS` in `amiga-bridge/Makefile` so it ships in the Amiga build.
5. Append the new `.c` to `SRCS` in `amiga-bridge/host/Makefile`.
6. Add test functions to `test_bridge.c` and register them in the `TESTS[]` array.

## Why this shape

The bridge is heavily AmigaOS-specific — exec MsgPorts, DOS Write(), Intuition, IDCMP, sig-bits. Cross-compiling the whole daemon for host is not realistic. But most bugs live in **parser / formatter / chunker** code that has no reason to touch the OS. Extracting that pure logic behind a `void *ctx` callback lets us:

- run the full test suite in <1 second on any developer laptop,
- catch regressions in CI without booting an emulator,
- exercise every edge case (buffer boundaries, semicolon-at-boundary, empty payloads) exhaustively.

The v1.20 SCRIPT truncation fix (PR #9) was the trigger: it silently dropped bytes past index 479 for months. `test_bridge.c`'s `over_bufsize_no_truncation` test is a direct regression pin — with the pre-v1.20 buggy chunker restored, 5 of 10 tests fail.
