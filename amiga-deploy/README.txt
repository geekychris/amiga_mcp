==========================================================================
 AmigaBridge - TCP/RoadShow deployment for a REAL Amiga
==========================================================================

This folder contains everything you copy TO the Amiga. Nothing else is
needed on the Amiga except a running TCP/IP stack (RoadShow recommended).

Contents
--------
  amiga-bridge              The bridge daemon (m68020, -noixemul). Run in
                            TCP mode; it listens for amiga-devbench.
  mcptest                   Optional test client: registers a variable, a
                            hook and a memory region, then idles ~10 min.
                            Use it to verify the client-dependent MCP tools.
  install-bridge-tcp.script Optional: adds the daemon to S:User-Startup so
                            it auto-launches on boot.

Requirements on the Amiga
-------------------------
  - 68020+ CPU (built with -m68020).
  - A TCP/IP stack providing bsdsocket.library (RoadShow, AmiTCP, Miami...).
    Confirm it is up:   ShowNetStatus
    and note the Amiga's IP address (e.g. 192.168.1.50).
  - A network interface that is reachable from the host running devbench
    (same LAN; no NAT between them).

--------------------------------------------------------------------------
 STEP-BY-STEP
--------------------------------------------------------------------------

1. Copy this whole folder to the Amiga, e.g. to  DH0:AmigaBridge
   (floppy, CF/SD card, network share - whatever you use).

2. Make sure your TCP/IP stack is running:
       ShowNetStatus
   Note the "Local host address" - that is the Amiga's IP.

3. Start the daemon in TCP mode from a Shell:
       cd DH0:AmigaBridge
       run >NIL: amiga-bridge TCP 2345
   A small "AmigaBridge v1.0" window opens and should show:
       TCP: listening
   (no args = serial mode; "TCP" alone = port 2345; "TCP <port>" = custom.)

4. On the HOST (the machine running amiga-devbench), edit devbench.toml:
       [serial]
       mode = "tcp"
       host = "<the Amiga's IP from step 2>"
       port = 2345

       [emulator]
       auto_start = false

5. Start devbench on the host:
       python -m amiga_devbench
   The bridge window on the Amiga should change to:
       TCP: client connected      Host: Connected
   and devbench logs:  "Bridge READY received".

6. Verify from your MCP client (Claude Code) - call:
       amiga_ping        -> "Amiga alive ..."
       amiga_sysinfo     -> Exec version, CPU, RAM
       amiga_list_libs   -> the library list
   A real PONG + sysinfo from the Amiga = the full chain works.

--------------------------------------------------------------------------
 OPTIONAL: auto-start on boot
--------------------------------------------------------------------------
   If you copied the folder somewhere other than DH0:AmigaBridge, edit the
   path inside install-bridge-tcp.script first, then run:
       Execute install-bridge-tcp.script
   This appends one line to S:User-Startup. Reboot to confirm.

--------------------------------------------------------------------------
 OPTIONAL: validate the client-dependent tools
--------------------------------------------------------------------------
   With the daemon running, in another Shell:
       run >NIL: DH0:AmigaBridge/mcptest
   Then from the MCP client:
       amiga_list_clients              -> shows "mcptest"
       amiga_get_var score             -> 42
       amiga_set_var score 100         -> then get_var -> 100
       amiga_call_hook mcptest echo hi -> "echo:hi"
       amiga_read_memregion mcptest buffer  -> hex dump 00 01 02 ...
   (mcptest exits by itself after ~10 minutes.)

--------------------------------------------------------------------------
 NOTES
--------------------------------------------------------------------------
 - The serial path is unchanged: running "amiga-bridge" with no arguments
   still uses serial.device at 115200 baud, exactly as before.
 - No NAT: devbench connects directly to the Amiga's IP. If the host cannot
   reach the Amiga, check the Amiga's IP/route and any firewall on the host.
 - To rebuild from source on the host:
       make bridge        (produces amiga-bridge/amiga-bridge)
   The mcptest source is in the repo at
   emulator/test-emu/src/testclient.c (links libbridge.a).
==========================================================================
