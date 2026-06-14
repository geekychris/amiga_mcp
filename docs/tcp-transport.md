# TCP/RoadShow Transport (network instead of serial)

The `amiga-bridge` daemon can talk to `amiga-devbench` over TCP/IP using the
Amiga's `bsdsocket.library` (RoadShow, AmiTCP, Miami, or an emulator's bsdsocket
emulation) instead of a serial cable. The Amiga **listens**; devbench connects.

The serial path is unchanged — running the daemon with no arguments still uses
`serial.device` exactly as before. TCP is opt-in via a command-line argument.

## On the Amiga
1. Have a TCP/IP stack running (RoadShow recommended; `bsdsocket.library` must be
   available).
2. Run the daemon in TCP mode:

       run >NIL: DH0:Dev/amiga-bridge TCP 2345

   - no args  → serial, 115200 baud (unchanged behavior)
   - `TCP`     → TCP server on the default port 2345
   - `TCP <port>` → TCP server on `<port>`

   The bridge status window logs `TCP: listening`, then `TCP: client connected`
   once devbench attaches.
3. Note the Amiga's IP (RoadShow: `ShowNetStatus`, or your router's DHCP table).

## On the host
Edit `devbench.toml`:

    [serial]
    mode = "tcp"
    host = "<amiga-ip>"   # the Amiga's LAN address
    port = 2345           # must match the daemon's TCP port

    [emulator]
    auto_start = false    # no emulator process to launch for real hardware

Start devbench (`python -m amiga_devbench`) and verify with the `amiga_ping`
MCP tool, then `amiga_sysinfo`.

## How it works
The daemon's transport is selected at startup behind a small dispatch layer
(`transport.c`): serial I/O via `serial_io.c`, or TCP via `net_io.c`. In TCP
mode the daemon creates a non-blocking listening socket and registers a
`bsdsocket` SIGIO signal, which slots into the daemon's existing `Wait()` loop
so the line protocol above the transport is untouched. A single host client is
served at a time; a new connection replaces an existing one. On connect the
daemon sends `READY|1.0`.

## Testing without real hardware
WinUAE and FS-UAE provide a built-in `bsdsocket.library` emulation that maps
Amiga sockets to host sockets. Enable it, run `amiga-bridge TCP 2345` inside the
emulator, and point `devbench.toml` at `127.0.0.1:2345`. For a real RoadShow
stack under emulation, use a WinUAE configuration with a working TCP/IP setup.
