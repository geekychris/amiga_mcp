<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Chris Collins <chris@hitorro.com> -->

# Connect an MCP client

[Documentation index](README.md) · [Install DevBench](quickstart.md) ·
[Tool reference](mcp-tools.md)

DevBench is an HTTP MCP server at `http://localhost:3000/mcp`. Your client
connects to that address; it does not launch the Python server. Keep DevBench
running in another terminal for the entire session.

## Start the server

From your checkout, after following the installation guide:

```sh
.venv/bin/python -m amiga_devbench --config devbench.local.toml --no-emulator
```

On Windows, use `.\.venv\Scripts\python.exe`. Add `--simulator` to verify
the installation without an Amiga. Check <http://localhost:3000/health>:
the HTTP service can be healthy even when `serial.connected` is false.

## Codex CLI

Register once:

```sh
codex mcp add amiga-dev --url http://localhost:3000/mcp
codex mcp get amiga-dev
```

Start a new Codex session and use `/mcp` to inspect the server. Codex uses
its own `config.toml`; the repository's `.mcp.json` is for Claude Code.
The equivalent Codex configuration is:

```toml
[mcp_servers.amiga-dev]
url = "http://localhost:3000/mcp"
```

See the [official Codex MCP documentation](https://developers.openai.com/codex/mcp)
for configuration scope and client options.

## Claude Code

The repository's `.mcp.json` already declares `amiga-dev`. When using Claude
Code in this checkout, approve that server when prompted.

For use in other projects, register it at user scope:

```sh
claude mcp add --scope user --transport http amiga-dev http://localhost:3000/mcp
```

Use `--scope project` instead if it should be available in only one project.
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

If you change `[server] port`, update the MCP URL to match. The bridge's
`[serial] port` is separate and does not belong in the MCP URL.
