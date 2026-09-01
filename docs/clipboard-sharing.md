<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Chris Collins -->

# Clipboard sharing between the mac host and the AmigaOS 4 guest

Two shell wrappers ship in `scripts/`:

| Tool | Direction | What it does |
| ---- | --------- | ------------ |
| `qclip`  | mac → guest | Pushes text into the guest's `clipboard.device` unit 0 (IFF FTXT/CHRS) so native OS4 apps see it via Cmd-V / Edit → Paste |
| `qpaste` | guest → mac | Reads the guest clipboard's first FTXT/CHRS chunk, prints to stdout, optionally pipes into `pbcopy` |

Both are pure shell scripts that call amiga_mcp devbench REST
endpoints (`/api/tools/clipboard/{get,set}`), which in turn drive
the bridge's `CLIPGET` / `CLIPSET` protocol messages, which in turn
call `clipboard_bridge.c`'s IFF wrapping over `clipboard.device`.
No new guest-side binary is needed — every piece was already in
place; the shell wrappers just make it usable from a mac prompt.

## Install

The scripts live in this repo at `scripts/qclip` and
`scripts/qpaste`. They're already executable. Put them on your
`$PATH` in whichever way you prefer:

```bash
# Symlink into /usr/local/bin (needs sudo on some setups)
ln -s "$PWD/scripts/qclip"  /usr/local/bin/
ln -s "$PWD/scripts/qpaste" /usr/local/bin/

# ...or add scripts/ to PATH in ~/.zshrc
export PATH="$HOME/code/claude_world/amiga_mcp/scripts:$PATH"

# ...or one-shot aliases in ~/.zshrc
alias qclip='/Users/chris/code/claude_world/amiga_mcp/scripts/qclip'
alias qpaste='/Users/chris/code/claude_world/amiga_mcp/scripts/qpaste'
```

The scripts have no dependencies beyond `curl`, `python3`, and
optionally `pbpaste`/`pbcopy` (macOS built-ins). They talk HTTP to
devbench on `127.0.0.1:3000` by default; override with `--host` /
`--port` for a remote or non-default setup.

## Prerequisites

- QEMU running with the OS4 guest booted.
- amiga-bridge daemon running on the guest (auto-started via
  `S:User-Startup`).
- amiga-devbench REST server running on the mac
  (`python3 -m amiga_devbench --profile qemu-os4 …`) and connected.

If the bridge window shows `AmigaBridge v1.20 - TCP :2345 (bsdsocket)`
and `curl http://localhost:3000/api/tools/clipboard/get` returns a
JSON body, you're wired up.

## qclip — push into guest clipboard

Source text priority (first non-empty wins):

1. `argv` — everything after the flags is joined with spaces
2. `stdin` when it's a pipe/redirect
3. `pbpaste` (mac clipboard) when neither of the above applies

Examples:

```bash
qclip "https://example.com/foo.pdf"        # argv
echo "hello guest" | qclip                 # stdin
qclip                                      # mac clipboard → guest
pbpaste | qclip                            # explicit
```

Successful output goes to stderr and exits 0:

```
qclip: sent 27 bytes to guest clipboard
```

Then, on the guest, any app that reads `clipboard.device` (Edit
menus, TrapezePDF's **File → Open URL from Clipboard**, etc.) sees
that text.

## qpaste — read guest clipboard

Prints the first FTXT/CHRS chunk to stdout. With `--copy` (`-c`),
also pipes the same text into the mac clipboard via `pbcopy`:

```bash
qpaste             # print to stdout
qpaste --copy      # print AND land in mac clipboard
```

Empty clipboard is not an error — exits 0 with no output.

## Flags (both tools)

| Flag | Default | Meaning |
| ---- | ------- | ------- |
| `--host HOST` | `127.0.0.1` | devbench REST host |
| `--port N`    | `3000`      | devbench REST port |
| `-h`, `--help` | — | show usage and exit |
| `-c`, `--copy` (qpaste only) | off | also pipe into mac clipboard |

## Newline handling — read before pasting code

The bridge protocol frames `CLIPSET|<text>\n` on a single line, and
`sanitize_text()` in `clipboard_bridge.c` folds newlines to spaces
before writing to `clipboard.device` anyway. So `qclip` pre-folds
`\r\n` and `\n` into spaces when input is multi-line, and prints a
warning:

```
qclip: warning: input has 5 lines; newlines are folded to spaces
       (bridge protocol is line-based, ~1024 char limit)
```

Practical guidance:

- **URLs / IDs / short one-liners** — round-trip cleanly, use freely.
- **Multi-line code snippets** — will arrive as one line with spaces
  where the newlines were. Usually still legible; not useful for
  paste-into-editor scenarios.
- **Anything > 1024 chars** — will be truncated on the guest side
  by the bridge's fixed buffer.
- **Binary data** — not supported by CHRS; use `/api/transfer` for
  files instead.

If you need lossless multi-line or binary support, that's a bridge
protocol upgrade — base64 encoding of CHRS payload, or a length-
prefix framing. Neither is done today. `/api/transfer` +
`/api/launch` is the current escape hatch for large payloads.

## End-to-end example — open a mac-clipped URL in TrapezePDF

Assumes TrapezePDF is already open on the guest.

```bash
# On the mac — copy a URL to the mac clipboard first (Cmd-C somewhere)
pbpaste                          # confirm mac clipboard has the URL
qclip                            # push mac clipboard → guest
```

Then on the guest, in TrapezePDF: **File → Open URL from Clipboard**
(shortcut **Right-Amiga + U**). The URL is fetched via
`openssl s_client` and rendered.

Reverse example — pull an OS4-generated log line back to the mac:

```bash
# On the guest, some app has written a session id or trace token
# to clipboard.device (many OS4 utilities do this on request).
# On the mac:
qpaste --copy                    # also stashes it in the mac clipboard
# Now Cmd-V in your terminal / editor pastes what the guest had.
```

## Troubleshooting

**`qclip: devbench call failed (is bridge up?)`** — devbench isn't
responding on `127.0.0.1:3000`. Check:
```
curl http://localhost:3000/api/status
lsof -iTCP:3000 -sTCP:LISTEN
```

**`qclip: bridge returned: {"error":"iffparse or clipboard not available"}`** —
`iffparse.library` or `clipboard.device` failed to open on the
guest at bridge startup. Check the AmigaBridge log window for
`Clipboard: WARNING` messages; the guest may need a reboot if the
device got wedged.

**`qpaste` prints garbage / wrong text** — some OS4 apps write
proprietary IFF forms (ILBM images, custom chunks) to
clipboard.device. `qpaste` only reads the first FTXT/CHRS text
chunk, so images and other non-text clipboards read as empty or
partial. This is expected.

**Nothing on the guest side sees the text** — TrapezePDF's File →
Open URL from Clipboard reads via the same `clipboard.device`
API, so if it doesn't see what `qclip` sent, most likely the
bridge disconnected between the set and the read. Confirm with
`qpaste` immediately after `qclip`.

## Under the hood

Round-trip data path:

```
mac shell (qclip)
  │  POST /api/tools/clipboard/set  {"text": "..."}
  ▼
amiga-devbench (server.py)
  │  send({"type": "CLIPSET", "text": "..."})
  │  → format_command → "CLIPSET|...\n"
  ▼
bridge TCP :2345 (via QEMU hostfwd :2347 → guest :2345)
  │  protocol_handler.c dispatches CLIPSET → clip_handle_set(args)
  ▼
clipboard_bridge.c
  │  AllocIFF() / OpenIFF(IFFF_WRITE)
  │  PushChunk(FORM/FTXT) → PushChunk(CHRS) → WriteChunkBytes
  ▼
clipboard.device unit 0  →  any OS4 app that reads it
```

`qpaste` is the mirror path — CLIPGET / `clip_handle_get` /
`StopChunk(FTXT,CHRS)` / `ReadChunkBytes` back up through
devbench to the mac shell.
