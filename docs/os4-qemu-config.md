<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Chris Collins -->

# QEMU sam460ex — the actually-working OS4 config

This is the reference config for AmigaOS 4.1 FE on QEMU sam460ex with
a working amiga-bridge over TCP.  It has been re-derived from scratch
more than once in this repo's history; capturing the final shape here
so the next revert has a source of truth to look at.

## The launch command

Codified in `scripts/start-qemu-os4.sh`.  The load-bearing pieces:

```
qemu-system-ppc \
    -machine sam460ex \
    -m 512 \
    -drive file=~/AmigaOS4/amigaos4-system.hdf,format=raw,if=ide,index=0 \
    -drive file=~/AmigaOS4/amigaos4-dev.hdf,format=raw,if=ide,index=1 \
    -serial tcp::2346,server,nowait \
    -netdev user,id=n0,hostfwd=tcp:127.0.0.1:2345-:2345 \
    -device rtl8139,netdev=n0 \
    -display cocoa,zoom-to-fit=on,show-cursor=on \
    -name 'AmigaOS 4.1 - DevBench'
```

Why each flag:

| flag | why |
| ---- | --- |
| `-machine sam460ex` | The A-EON Sam460ex machine model — matches what OS4.1 FE was actually released to run on. |
| `-m 512` | 512MB is the QEMU sam460ex cap in practice; OS4 handles anything up to that. |
| `-drive ... if=ide,index=0/1` | Two IDE disks: system HDF (index 0) + dev HDF (index 1). Guest sees them as `SYS:` / `DH1:`. |
| `-serial tcp::2346,server,nowait` | UART16550 routed to host TCP :2346. Console output + serial-mode bridge fallback. |
| `-netdev user,id=n0,hostfwd=tcp:127.0.0.1:2345-:2345` | User-mode (slirp) NAT.  `hostfwd` forwards host `127.0.0.1:2345` → guest `:2345` so devbench can reach the TCP bridge from the mac.  Bind is loopback-only on purpose (don't expose the bridge to the LAN). |
| `-device rtl8139,netdev=n0` | Add-in NIC on the PCI bus.  **The old `-net nic,model=rtl8139 -net user` shorthand silently fails on sam460ex** with `requested NIC (model rtl8139) was not created (not supported by this machine?)` — the modern `-netdev`/`-device` split is what its PCI bus wants.  OS4 has an rtl8139.device driver in the base install so no extra software needed. |
| `-display cocoa,zoom-to-fit=on,show-cursor=on` | Cocoa on macOS.  **`zoom-to-fit=on` is the piece that lets you drag the window edge to resize** — otherwise the window is stuck at the guest's frame size.  `show-cursor=on` keeps the host cursor visible so you don't lose it into the guest. |

## What NOT to use

- **`-net nic,model=rtl8139 -net user`** — old syntax.  On sam460ex it prints "not created (not supported by this machine?)" and the guest gets no NIC at all.  Silent enough to look like a Roadshow problem.  Use `-netdev`/`-device` instead.
- **`-display default`** — on macOS this picks Cocoa with fixed size.  Window doesn't resize.  Explicit `-display cocoa,zoom-to-fit=on` fixes it.
- **`-nic none`** — kills bsdsocket-based apps on OS4.  Including the amiga-bridge TCP mode.  Don't do this.
- **Random unpinned QEMU versions** — the sam460ex machine has evolved.  We target QEMU >= 7.x.  Test with `qemu-system-ppc -machine sam460ex -device help` and confirm rtl8139 is in the list.

## The bridge, once QEMU is up

The OS4 amiga-bridge daemon supports **two transports**:

1. **TCP over bsdsocket** — faster, more robust, this is the default we've committed to.
2. **serial.device** — the classic fallback, kept for console output and diagnostic use.

Launch mode is picked by the first CLI arg:

```
DH1:amiga-bridge          # serial.device unit 0, 115200 baud
DH1:amiga-bridge TCP      # TCP server on 0.0.0.0:2345 (the default we want)
DH1:amiga-bridge TCP 9999 # TCP on a non-default port
```

**Every guide we've written since the pivot** says "launch with `TCP`" and yet
the temptation to fall back to serial keeps winning.  Don't do it.  If the
serial mode looks like it's connecting but silently dropping messages, the
answer is almost never "debug the serial link" — the answer is "launch the
bridge with `TCP` instead."

## Devbench connection

```
python3 -m amiga_devbench --profile qemu-os4
```

That profile (in `devbench.toml`) is `mode = "tcp"`, `host = "127.0.0.1"`,
`port = 2345`.  Devbench opens a TCP socket to the hostfwd forward, which
lands inside the guest, where the bridge is listening.  Bidirectional
frames start flowing immediately.

If you see `Serial: disconnected` / `Host: Waiting...` in the AmigaBridge
status window on OS4, you're in **serial mode**.  Close it (Ctrl+C or click
the close gadget) and relaunch with `TCP`.

## Full boot sequence (from cold)

1. `bash scripts/start-qemu-os4.sh` on the mac.
2. Wait for OS4 to reach the Workbench (~60–120s cold, faster on repeat).
3. On OS4 shell: `DH1:amiga-bridge TCP`
4. On mac: `python3 -m amiga_devbench --profile qemu-os4` (if not already running).
5. Confirm: use `amiga_ping` MCP tool from Claude Code, or hit the devbench web UI at `http://localhost:3000/`.

## Networking on OS4 (Roadshow)

`ping google.com` inside OS4 works via QEMU's user-mode NAT once the rtl8139
NIC is present.  If it doesn't:

- **Roadshow prefs**: interface for rtl8139.device must be Up + configured
  DHCP.  Check the SYS:Prefs/Network config panel.
- **DNS**: user-mode NAT provides DHCP; Roadshow should pick up nameservers
  from it automatically.  If not, hard-code `10.0.2.3` (QEMU's built-in DNS
  proxy).
- **Route**: default route should be `10.0.2.2` (QEMU's built-in gateway).

Without a working NIC the bridge TCP path still works — it binds
`bsdsocket.library` to the loopback layer, which doesn't need an interface
to be up.  So even a broken Roadshow config doesn't stop the bridge; just
things like web_notes' HTTP server won't be reachable from off-guest.

## The one thing that keeps getting reverted

**The NIC syntax.**  Every time somebody edits `start-qemu-os4.sh` and drops
back to `-net nic,model=rtl8139 -net user`, everything on the OS4 side that
uses `bsdsocket.library` — including the TCP-mode bridge — silently breaks.
The QEMU warning about "not supported by this machine" scrolls past in the
first second of QEMU startup and is easy to miss.  Please read the docstring
at the top of `scripts/start-qemu-os4.sh` before touching this file.
