# Using the `amiga-dev` MCP in a Claude Code session

`amiga-devbench` is the MCP **server**: it talks to the Amiga and exposes the
`amiga_*` tools over HTTP at `http://localhost:3000/mcp`. Claude Code is the MCP
**client**. So in any session you need two things: **devbench running**, and
**Claude Code pointed at it**.

```
Claude Code (any session) ──MCP/HTTP :3000──► amiga-devbench ──TCP :2345──► amiga-bridge (Amiga)
```

## One-time host setup

```bash
git clone https://github.com/geekychris/amiga_mcp   # or your fork
cd amiga_mcp
pip install -e amiga-devbench                        # MCP server + deps
```

Docker (or WSL Docker on Windows) is only needed for the **build/deploy** tools
(`amiga_build`, etc.). Pure inspect/control of a running Amiga does not need it.

## Step 1 — Run the Amiga side

On the Amiga (real hardware or emulator), start the daemon in TCP mode and note
its IP:

```
run >NIL: amiga-bridge TCP 2345      ; the window shows "TCP: listening"
ShowNetStatus                         ; note the Amiga's IP, e.g. 192.168.200.125
```

(No arguments = serial mode at 115200 baud, unchanged.)

## Step 2 — Start devbench (the MCP server) on the host

Point it at the Amiga and serve MCP on port 3000. Two equivalent ways:

**CLI flags (quickest):**

```bash
python -m amiga_devbench --serial-host 192.168.200.125 --serial-port 2345 --no-emulator --port 3000
```

**Or via `devbench.toml`** (then just `python -m amiga_devbench`):

```toml
[serial]
mode = "tcp"
host = "192.168.200.125"   # the Amiga's IP
port = 2345                 # must match the daemon's TCP port

[emulator]
auto_start = false          # real hardware; set true (+ a config) to auto-launch an emulator
```

Leave it running. You should see `Connected to Amiga via TCP ...` and
`Bridge READY received`. It also serves a **web UI** at `http://localhost:3000/`.

## Step 3 — Register the MCP server with Claude Code

**A) A session in this repo — nothing to do.** `.mcp.json` already registers it:

```json
{ "mcpServers": { "amiga-dev": { "type": "streamable-http", "url": "http://localhost:3000/mcp" } } }
```

Start Claude Code in the repo folder and **approve the `amiga-dev` server** when
prompted.

**B) A session in any other folder — register it once:**

```bash
# project scope (writes ./.mcp.json in that folder):
claude mcp add --scope project --transport http amiga-dev http://localhost:3000/mcp
# or user scope (available in every project):
claude mcp add --scope user    --transport http amiga-dev http://localhost:3000/mcp
```

## Step 4 — Verify inside the session

- Run `/mcp` — `amiga-dev` should show **connected**.
- Call a tool: `amiga_ping` -> "Amiga alive...", or `amiga_sysinfo`.
  (Tools may be deferred and surface as `mcp__amiga-dev__*`; they load on demand.)

## Typical usage

- **Inspect/control a running Amiga:** `amiga_sysinfo`, `amiga_list_tasks` /
  `amiga_list_libs` / `amiga_list_volumes`, `amiga_list_dir`,
  `amiga_inspect_memory`, `amiga_dos_command`, `amiga_copper_list`,
  `amiga_sprites`, ...
- **Your own app linked with `libbridge.a`:** `amiga_list_clients`,
  `amiga_get_var` / `amiga_set_var`, `amiga_call_hook`, `amiga_read_memregion`,
  `amiga_watch_logs`, `amiga_stop_client`.
- **Full dev loop:** `amiga_build` -> deploy (`amiga_push_file` over the bridge,
  or a shared `deploy_dir`) -> `amiga_launch` / `amiga_dos_command "run ..."` ->
  inspect live. (Build/deploy need the Docker toolchain.)

## Gotchas

- **devbench must stay running** for the whole session. Stop it and `/mcp` shows
  disconnected and the tools fail.
- **One devbench <-> one Amiga.** To switch targets, restart devbench with a new
  `--serial-host`.
- **Port match:** devbench `--serial-port` must equal the daemon's `TCP <port>`;
  the `.mcp.json` URL port must equal devbench `--port`.
- **Real LAN = no NAT** -> devbench connects straight to the Amiga's IP. (The
  reverse-tunnel needed for a SLIRP-NAT'd emulator is not needed on hardware.)
- **Emulator instead of hardware?** Set `[emulator] auto_start = true` (and a
  `config`), or run your emulator and point `--serial-host/--serial-port` at its
  serial-over-TCP bridge.
