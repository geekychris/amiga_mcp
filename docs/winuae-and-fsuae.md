# Use WinUAE or FS-UAE

[Install DevBench](quickstart.md) · [Documentation index](README.md)

DevBench's guest bridge can serve either emulator. Its native debugger API
currently targets patched FS-UAE; WinUAE's GDB server is a different protocol
and is not a replacement for the bridge connection.

| Feature | WinUAE | FS-UAE |
|---|---|---|
| Guest files, commands, tasks and registered application variables | `amiga-bridge` in the guest | `amiga-bridge` in the guest |
| `amiga_fsuae_*` tools and FS-UAE dashboard tab | Not supported by this backend | Requires patched FS-UAE |
| Local launch through DevBench's emulator manager | Launch WinUAE separately | FS-UAE-oriented launcher |

## Keep separate profiles

Add these profiles to your `devbench.local.toml`. They use distinct **bridge** ports so neither conflicts with a WinUAE GDB server on port 2345:

```toml
[profiles.winuae]
description = "WinUAE guest TCP bridge"
mode = "tcp"
host = "127.0.0.1"
port = 1234
auto_start_emulator = false

[profiles.fsuae]
description = "FS-UAE serial bridge"
mode = "tcp"
host = "127.0.0.1"
port = 1235
auto_start_emulator = false
```

Run one DevBench process at a time. Stop it with Ctrl-C before switching:

```sh
.venv/bin/python -m amiga_devbench --config devbench.local.toml --profile winuae --no-emulator
# Or:
.venv/bin/python -m amiga_devbench --config devbench.local.toml --profile fsuae --no-emulator
```

## Configure the transport

### WinUAE: guest TCP with socket emulation

Enable WinUAE's `bsdsocket.library` emulation. In a `.uae` file:

```ini
bsdsocket_emu=true
```

Start the bridge in the guest with `TCP 1234` (see below). Socket emulation
makes its listener reachable at host address `127.0.0.1:1234`; no guest
network stack or emulator serial listener is needed for this setup.

### FS-UAE: serial over host TCP

Add to your `.fs-uae` configuration:

```ini
[fs-uae]
serial_port = tcp://127.0.0.1:1235
```

Start the guest bridge with `SERIAL 115200`. The emulator supplies the TCP
listener; the guest uses `serial.device`.

### Alternative WinUAE serial transport

WinUAE also provides a TCP serial listener. The Unix configuration key is
`unix.serial_port=TCP:127.0.0.1:1234`; native Windows uses its own target
settings through the GUI. This route did not produce bridge heartbeats in
the macOS installation check. The verified WinUAE route above uses guest
TCP. Do not configure both listeners on the same host port.

Supply your own working ROM/OS configuration. Do not attach the same writable
hardfile to both running emulators. A shared host directory is a convenient
way to deliver the freshly built **68k** bridge to either guest.

On Unix WinUAE, host execute permissions are reflected in Amiga protection
bits. Make the bridge executable on the host (`chmod u+x /path/to/amiga-bridge`).
If you extracted an entire boot volume into a shared directory, preserve
execute permissions on its Amiga commands and loadable libraries too;
missing permissions can prevent boot commands or library loading. FS-UAE
may handle those protection bits through `.uaem` sidecar files instead.

## Start the bridge inside AmigaOS

In an Amiga shell, using the path where you copied it:

WinUAE with the guest TCP configuration above:

```text
Stack 65536
Run >NIL: DH1:Dev/amiga-bridge TCP 1234
```

FS-UAE with the serial configuration above:

```text
Stack 65536
Run >NIL: DH1:Dev/amiga-bridge SERIAL 115200
```

Change `DH1:Dev/` to your actual guest path. The arguments select different
transports; they are not interchangeable. See [TCP transport](tcp-transport.md)
for physical Amigas and guests using a network stack.

Use a complete AmigaOS installation, including its disk libraries. The
bridge build may load libraries such as `diskfont.library` and
`rexxsyslib.library` before entering its main routine. If startup reports a
missing library, resolve it in the guest before troubleshooting TCP.

If DevBench started before the serial listener was ready, call
`amiga_connect` to retry after the bridge has started. The initial failed
connection is not automatically retried.

Check `/health` for a recent heartbeat, then call `amiga_ping` through MCP.
This verifies the guest bridge, independently of native debugger support.

## Optional FS-UAE native debugger

Build [fsuae_remote_patch](https://github.com/geekychris/fsuae_remote_patch)
on macOS/Linux and launch the resulting binary with:

```sh
FSUAE_RPC_PORT=8765 /path/to/fs-uae /path/to/guest.fs-uae
```

Set `[fsuae_rpc] enabled = "auto"` and `port = 8765` in the local DevBench
config. First verify `http://127.0.0.1:8765/v1/ping`, then call
`amiga_fsuae_status`. The bridge serial port remains 1235; it is not 8765.

Install `fs-uae.dat` alongside the binary too. In the FS-UAE build tree,
`make fs-uae.dat` produces the menu and font resources; copying only the
executable can leave the emulator without its UI assets.

The patch project's build script uses a disposable FS-UAE source directory
and resets/cleans it. Never point `FSUAE_SRC` at a checkout containing work.
If building the current script fails:

- Missing `web_index.inc`: the generated header must be copied alongside
  `src/fsuae_rpc.cpp`, or its directory supplied on the compiler include path.
- Conflicting `sleep_millis` declarations: ensure the **whole checkout** is
  on the requested FS-UAE tag. Checking out only the tag's paths can leave
  incompatible headers from the default branch.

These are build-script issues in the separate patch repository, not Python
installation failures. Native debugger support on Windows needs separate
validation; the Python/guest bridge path does not require the patch.
