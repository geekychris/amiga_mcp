# Amiga MCP - Cross-Development Environment

## Project Overview
Amiga cross-development environment with MCP server integration for Claude Code.
Compiles C code on macOS via Docker, deploys to AmiKit emulator, and provides
real-time debug monitoring over serial/TCP.

## Architecture
- **amiga-debug-lib/**: C library linked by Amiga programs. Communicates via serial port.
- **mcp-server/**: TypeScript MCP server connecting to emulator's serial TCP port.
- **examples/**: Sample Amiga programs demonstrating the debug library.
- **docker/**: Dockerfile for cross-compilation environment.

## Build Requirements
- Docker with `amigadev/crosstools:m68k-amigaos` image
- Node.js 20+ for MCP server
- AmiKit or FS-UAE with serial port exposed as TCP (default: `127.0.0.1:1234`)

## Build Commands
```bash
make all          # Build debug lib + all examples via Docker
make lib          # Build only debug library
make examples     # Build only examples
make clean        # Clean all build artifacts
make mcp-server   # Install deps and build MCP server
```

## Amiga C Conventions
- Always use `-noixemul` flag (no Unix emulation, pure AmigaOS)
- Target 68000 baseline with `-m68000`
- Include paths: `-I../../amiga-debug-lib/include`
- Link with: `-L../../amiga-debug-lib -ldebug -lamiga`

## Debug Protocol
Line-based text protocol over serial/TCP, pipe-delimited fields.

### Amiga → Host
- `LOG|level|tick|message` (level: D/I/W/E)
- `MEM|addr_hex|size|hex_data`
- `VAR|name|type|value` (type: i32/u32/str/f32/ptr)
- `HB|tick|free_chip|free_fast`
- `CMD|id|status|response_data`

### Host → Amiga
- `INSPECT|addr_hex|size`
- `GETVAR|name`
- `SETVAR|name|value`
- `PING`
- `EXEC|id|expression`

## MCP Server
- Uses `@modelcontextprotocol/sdk` with StreamableHTTP transport (SSE streaming)
- HTTP server on port 3000 (configurable via `MCP_PORT`)
- Real-time log/status streaming via `amiga_watch_logs` and `amiga_watch_status` tools
- TypeScript with ES modules
- Config via env vars: `AMIGA_SERIAL_HOST`, `AMIGA_SERIAL_PORT`, `AMIGA_PROJECT_ROOT`, `MCP_PORT`

## Testing Without Emulator
Run the Amiga simulator to test the full flow:
```bash
cd mcp-server
npm run simulator   # Terminal 1: starts fake Amiga on TCP 1234
npm run dev         # Terminal 2: starts MCP server on HTTP 3000
```
The simulator emulates a bouncing ball app with all debug protocol features.

## Claude Code MCP Config
```json
{
  "mcpServers": {
    "amiga-dev": {
      "type": "streamable-http",
      "url": "http://localhost:3000/mcp"
    }
  }
}
```
