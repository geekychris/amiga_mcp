<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Chris Collins <chris@hitorro.com> -->

# Connect an MCP client

[Documentation index](README.md) · [Install DevBench](quickstart.md) ·
[Tool reference](mcp-tools.md)

DevBench is an HTTP MCP server at `http://localhost:3000/mcp`. There are two
ways to connect a client:

- **The launcher (recommended).** The client runs
  `python -m amiga_devbench.mcp_stdio`, which talks MCP over stdio and passes
  it on to DevBench. If nothing listens on DevBench's port, the launcher starts
  DevBench in the background first. It never stops DevBench: the web UI and
  other clients may be using it, and it holds the only connection to the Amiga.
- **The URL.** The client connects to `http://localhost:3000/mcp` itself.
  DevBench must already be running, and if it isn't, the client reports a
  transport error.

When DevBench can't be reached, the launcher still completes the client's
handshake and says why, for example "DevBench is not running at …" followed
by the command that starts it. In place of the Amiga tools it offers one tool,
`amiga_devbench_connect`, which checks again and connects. The Amiga tools
then appear without restarting the client. If DevBench stops or restarts
during a session, the launcher reconnects or reports it the same way. It
sends a request to the new session only when the old one certainly never got
it; a tool call cut off while DevBench ran it is reported, not run twice.

## The launcher

The launcher takes DevBench's own options (`--config`, `--profile`,
`--port`, `--no-emulator` …). They choose the port to connect to, and are
passed on unchanged when it starts DevBench. Two options are its own:

| Option | Effect |
|---|---|
| `--no-start` | Only connect; never start DevBench |
| `--start-timeout S` | Seconds to wait for DevBench to answer (default 20) |
| `--private` | Give the session its own DevBench and WinUAE (below) |

A DevBench it starts writes to `logs/devbench-<port>.log` in the project
root. Use absolute paths and `--project-root`: clients start the launcher
in a directory of their choosing, and DevBench finds its files from there.

The launcher starts DevBench only when the port refuses connections. If the
port is in use but `/health` doesn't answer, it reports that instead: a new
DevBench stops any DevBench already on its port.

## A private WinUAE per session

With `--private`, each MCP session gets a DevBench and a WinUAE of its own,
so several agents, or an agent and you, can debug at the same time without
taking each other's WinUAE GDB port or guest bridge:

```sh
.venv/bin/python -m amiga_devbench.mcp_stdio --private \
  --config "$R/devbench.local.toml" --profile winuae --project-root "$R" \
  --emulator-binary ~/git/WinUAE_mac/WinUAE/bin/winuae \
  --emulator-config ~/.amiga-devbench/DevBench.uae
```

The `.uae` must boot DevBench's WinUAE guest: a folder whose
`S/Startup-Sequence` starts `amiga-bridge TCP <port>`. The session

- copies that folder (at most 256 MiB) and starts the bridge in the copy on
  a free port; programs are deployed into the copy;
- writes a `.uae` that boots the copy, mounts every other writable folder
  or hardfile read-only and drops serial ports, so the two emulators never
  write to the same disk; your configuration is not changed;
- starts DevBench on free HTTP, WinUAE GDB and bridge ports; DevBench
  starts the WinUAE. The client's server instructions name the ports, and
  the web UI of the session is at the HTTP port. Its settings are generated
  for the session, so its Settings tab cannot save them (`--no-save-config`).

Both stop, and the session folder is removed, when the client ends the
session. If the launcher is killed, the next `--private` launch stops what
it left running. The binary and configuration can also come from
`[emulator] binary` and `config`.

## Start the server yourself

With a URL registration, or to watch DevBench's output, start it from your
checkout after following the installation guide:

```sh
.venv/bin/python -m amiga_devbench --config devbench.local.toml --no-emulator
```

On Windows, use `.\.venv\Scripts\python.exe`. Add `--simulator` to verify
the installation without an Amiga. Check <http://localhost:3000/health>:
the HTTP service can be healthy even when `serial.connected` is false.

## Codex CLI

Register once, with your checkout's path:

```sh
R=$HOME/git/amiga_mcp
codex mcp add amiga-dev -- "$R/.venv/bin/python" -m amiga_devbench.mcp_stdio \
    --project-root "$R" --config "$R/devbench.local.toml" --no-emulator
codex mcp get amiga-dev
```

Start a new Codex session and use `/mcp` to inspect the server. Codex uses
its own `config.toml`; the repository's `.mcp.json` is for Claude Code.
The equivalent Codex configuration is:

```toml
[mcp_servers.amiga-dev]
command = "/Users/you/git/amiga_mcp/.venv/bin/python"
args = ["-m", "amiga_devbench.mcp_stdio",
        "--project-root", "/Users/you/git/amiga_mcp",
        "--config", "/Users/you/git/amiga_mcp/devbench.local.toml",
        "--no-emulator"]
```

To connect by URL instead, with DevBench already running:

```sh
codex mcp add amiga-dev --url http://localhost:3000/mcp
```

See the [official Codex MCP documentation](https://developers.openai.com/codex/mcp)
for configuration scope and client options.

## Claude Code

The repository's `.mcp.json` declares `amiga-dev` by URL, so DevBench must be
running. When using Claude Code in this checkout, approve that server when
prompted.

To use the launcher in this checkout, register it at local scope, which takes
precedence over `.mcp.json` and stays out of the repository:

```sh
R=$HOME/git/amiga_mcp
claude mcp add --scope local amiga-dev -- "$R/.venv/bin/python" -m amiga_devbench.mcp_stdio \
    --project-root "$R" --config "$R/devbench.local.toml" --no-emulator
```

For use in other projects, use `--scope user` instead, either with the
launcher as above or by URL:

```sh
claude mcp add --scope user --transport http amiga-dev http://localhost:3000/mcp
```

Use `/mcp` in the client to inspect its connection.

## Verify the target

On a real target, ask the client to call `amiga_ping`, then
`amiga_list_tasks`. These verify the bridge connection; listing MCP tools
alone does not. With `--simulator`, use `amiga_log` and `/health` heartbeats
instead: some simulated replies, including ping and task lists, do not
match the current bridge protocol. Simulator responses are not evidence
of a connection to a physical or emulated Amiga.

Application-specific tools such as `amiga_get_var` and `amiga_call_hook`
also require a running program linked with `libbridge.a`. The
`amiga_fsuae_*` family requires patched FS-UAE independently of the bridge.

If you change `[server] port`, update the MCP URL to match; the launcher
reads the port from the same configuration. The bridge's
`[serial] port` is separate and does not belong in the MCP URL.
