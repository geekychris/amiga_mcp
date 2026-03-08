#!/usr/bin/env node
/**
 * Amiga Simulator - Pretends to be an Amiga app connected via serial TCP.
 * Runs as a TCP server on port 1234 (or AMIGA_SERIAL_PORT).
 * Sends realistic protocol messages and responds to host commands.
 *
 * Usage: npx tsx src/simulator.ts
 */

import { createServer, Socket } from "net";

const PORT = parseInt(process.env.AMIGA_SERIAL_PORT ?? "1234", 10);

// Simulated state
let tick = 0;
let ball_x = 160;
let ball_y = 100;
let ball_dx = 3;
let ball_dy = 2;
let frame_count = 0;
let freeChip = 512000;
let freeFast = 1048576;
let running = true;

// Variable registry for GETVAR/SETVAR
const vars: Record<string, { type: string; get: () => string; set?: (v: string) => void }> = {
  ball_x:      { type: "i32", get: () => String(ball_x),      set: (v) => { ball_x = parseInt(v); } },
  ball_y:      { type: "i32", get: () => String(ball_y),      set: (v) => { ball_y = parseInt(v); } },
  ball_dx:     { type: "i32", get: () => String(ball_dx),     set: (v) => { ball_dx = parseInt(v); } },
  ball_dy:     { type: "i32", get: () => String(ball_dy),     set: (v) => { ball_dy = parseInt(v); } },
  frame_count: { type: "u32", get: () => String(frame_count) },
  free_chip:   { type: "u32", get: () => String(freeChip) },
  free_fast:   { type: "u32", get: () => String(freeFast) },
};

// Simulated memory (just some patterns)
function getMemory(addr: number, size: number): string {
  let hex = "";
  for (let i = 0; i < size; i++) {
    const byte = (addr + i) & 0xFF;
    hex += byte.toString(16).padStart(2, "0").toUpperCase();
  }
  return hex;
}

function sendLine(socket: Socket, line: string) {
  try {
    socket.write(line + "\n");
  } catch { /* ignore write errors */ }
}

function handleCommand(socket: Socket, line: string) {
  const parts = line.split("|");
  const cmd = parts[0];

  switch (cmd) {
    case "PING":
      sendLine(socket, `HB|${tick}|${freeChip}|${freeFast}`);
      break;

    case "GETVAR":
      if (parts.length >= 2) {
        const name = parts[1];
        const v = vars[name];
        if (v) {
          sendLine(socket, `VAR|${name}|${v.type}|${v.get()}`);
        } else {
          sendLine(socket, `VAR|${name}|err|not_found`);
        }
      }
      break;

    case "SETVAR":
      if (parts.length >= 3) {
        const name = parts[1];
        const value = parts[2];
        const v = vars[name];
        if (v && v.set) {
          v.set(value);
          sendLine(socket, `VAR|${name}|${v.type}|${v.get()}`);
          sendLine(socket, `LOG|I|${tick}|Host set ${name} = ${value}`);
        }
      }
      break;

    case "INSPECT":
      if (parts.length >= 3) {
        const addr = parseInt(parts[1], 16);
        let size = parseInt(parts[2]);
        if (size > 4096) size = 4096;
        // Send in 256-byte chunks
        let offset = 0;
        while (offset < size) {
          const chunk = Math.min(256, size - offset);
          const hexData = getMemory(addr + offset, chunk);
          sendLine(socket, `MEM|${(addr + offset).toString(16).padStart(8, "0")}|${chunk}|${hexData}`);
          offset += chunk;
        }
      }
      break;

    case "EXEC":
      if (parts.length >= 3) {
        const id = parts[1];
        const expression = parts.slice(2).join("|");
        if (expression === "reset") {
          ball_x = 160;
          ball_y = 100;
          ball_dx = 3;
          ball_dy = 2;
          sendLine(socket, `CMD|${id}|ok|Ball position reset`);
          sendLine(socket, `LOG|I|${tick}|Ball position reset by host`);
        } else if (expression === "status") {
          sendLine(socket, `CMD|${id}|ok|ball(${ball_x},${ball_y}) vel(${ball_dx},${ball_dy}) frame=${frame_count}`);
        } else {
          sendLine(socket, `CMD|${id}|ok|Executed: ${expression}`);
        }
      }
      break;
  }
}

const tcpServer = createServer((socket) => {
  console.log(`MCP host connected from ${socket.remoteAddress}:${socket.remotePort}`);

  let lineBuf = "";

  // Send startup messages
  sendLine(socket, `LOG|I|${tick++}|Debug session started`);
  sendLine(socket, `HB|${tick}|${freeChip}|${freeFast}`);
  sendLine(socket, `LOG|I|${tick++}|Bouncing Ball demo starting`);
  sendLine(socket, `LOG|I|${tick++}|Window opened: inner 312x178`);

  // Simulation loop - runs the "bouncing ball" logic
  const simInterval = setInterval(() => {
    if (!running) return;

    // Update ball position
    ball_x += ball_dx;
    ball_y += ball_dy;

    // Bounce
    if (ball_x <= 5 || ball_x >= 307) {
      ball_dx = -ball_dx;
      ball_x += ball_dx;
      sendLine(socket, `LOG|D|${tick}|Bounce X at ${ball_x}`);
    }
    if (ball_y <= 5 || ball_y >= 173) {
      ball_dy = -ball_dy;
      ball_y += ball_dy;
      sendLine(socket, `LOG|D|${tick}|Bounce Y at ${ball_y}`);
    }

    frame_count++;
    tick++;

    // Vary free memory slightly to make it realistic
    freeChip += Math.floor(Math.random() * 200) - 100;
    freeFast += Math.floor(Math.random() * 400) - 200;

    // Heartbeat every 60 frames
    if (frame_count % 60 === 0) {
      sendLine(socket, `HB|${tick}|${freeChip}|${freeFast}`);
    }

    // Periodic info logs
    if (frame_count % 120 === 0) {
      sendLine(socket, `LOG|I|${tick}|frame=${frame_count} ball(${ball_x},${ball_y}) vel(${ball_dx},${ball_dy})`);
    }

    // Occasional warnings (memory getting low simulation)
    if (frame_count % 500 === 0 && freeChip < 480000) {
      sendLine(socket, `LOG|W|${tick}|Chip memory getting low: ${freeChip} bytes`);
    }
  }, 50); // ~20fps like the real app

  // Handle incoming commands from host
  socket.on("data", (data) => {
    lineBuf += data.toString();
    const lines = lineBuf.split("\n");
    lineBuf = lines.pop() ?? "";

    for (const line of lines) {
      const trimmed = line.trim();
      if (!trimmed) continue;
      console.log(`  <- ${trimmed}`);
      handleCommand(socket, trimmed);
    }
  });

  socket.on("close", () => {
    console.log("MCP host disconnected");
    clearInterval(simInterval);
  });

  socket.on("error", () => {
    clearInterval(simInterval);
  });
});

tcpServer.listen(PORT, () => {
  console.log(`\n=== Amiga Simulator ===`);
  console.log(`Listening on TCP port ${PORT}`);
  console.log(`Simulating: Bouncing Ball demo`);
  console.log(`Variables: ${Object.keys(vars).join(", ")}`);
  console.log(`Commands: reset, status`);
  console.log(`\nWaiting for MCP server to connect...\n`);
});

tcpServer.on("error", (err) => {
  console.error(`Server error: ${err.message}`);
  process.exit(1);
});
