import { randomUUID } from "node:crypto";
import path from "node:path";
import { fileURLToPath } from "node:url";
import { McpServer } from "@modelcontextprotocol/sdk/server/mcp.js";
import { StreamableHTTPServerTransport } from "@modelcontextprotocol/sdk/server/streamableHttp.js";
import { InMemoryEventStore } from "@modelcontextprotocol/sdk/examples/shared/inMemoryEventStore.js";
import { isInitializeRequest } from "@modelcontextprotocol/sdk/types.js";
import express from "express";
import { z } from "zod";
import { SerialConnection } from "./serial.js";
import { Builder } from "./builder.js";
import {
  levelName,
  formatHexDump,
  type LogMessage,
  type MemDump,
  type Heartbeat,
  type VarValue,
} from "./protocol.js";

const __dirname = path.dirname(fileURLToPath(import.meta.url));

const SERIAL_HOST = process.env.AMIGA_SERIAL_HOST;
const SERIAL_PORT = parseInt(process.env.AMIGA_SERIAL_PORT ?? "1234", 10);
const HTTP_PORT = parseInt(process.env.MCP_PORT ?? "3000", 10);
const PROJECT_ROOT = process.env.AMIGA_PROJECT_ROOT;
const PTY_PATH = process.env.AMIGA_PTY_PATH ?? "/tmp/amiga-serial";

// Auto-detect mode: if AMIGA_SERIAL_HOST is set, use TCP; otherwise use PTY
const USE_PTY = !SERIAL_HOST;

// Shared state
const serial = new SerialConnection(SERIAL_HOST ?? "127.0.0.1", SERIAL_PORT, PTY_PATH);
if (USE_PTY) {
  serial.setMode("pty", { ptyPath: PTY_PATH });
}
const builder = new Builder(PROJECT_ROOT);

// Track active sessions for streaming notifications
const transports: Record<string, StreamableHTTPServerTransport> = {};
const sessionServers: Record<string, McpServer> = {};

function createServer(): McpServer {
  const server = new McpServer(
    { name: "amiga-dev", version: "1.0.0" },
    { capabilities: { logging: {} } }
  );

  // ─── Build Tools ───

  server.tool(
    "amiga_build",
    "Build an Amiga project using Docker cross-compiler. Omit project to build all.",
    {
      project: z
        .string()
        .optional()
        .describe("Subpath to build, e.g. 'examples/hello_world'"),
    },
    async ({ project }, extra) => {
      await server.sendLoggingMessage(
        { level: "info", data: `Building ${project ?? "all projects"}...` },
        extra.sessionId
      );

      const result = await builder.build(project);

      await server.sendLoggingMessage(
        {
          level: result.success ? "info" : "error",
          data: `Build ${result.success ? "succeeded" : "failed"} (${result.duration}ms)`,
        },
        extra.sessionId
      );

      const text = [
        `Build ${result.success ? "SUCCEEDED" : "FAILED"} (${result.duration}ms)`,
        result.output ? `\n--- Output ---\n${result.output}` : "",
        result.errors ? `\n--- Errors ---\n${result.errors}` : "",
      ].join("");
      return { content: [{ type: "text", text }] };
    }
  );

  server.tool(
    "amiga_clean",
    "Clean build artifacts for a project.",
    { project: z.string().optional().describe("Subpath to clean") },
    async ({ project }) => {
      const result = await builder.clean(project);
      return {
        content: [
          {
            type: "text",
            text: `Clean ${result.success ? "done" : "failed"}: ${result.errors || "OK"}`,
          },
        ],
      };
    }
  );

  // ─── Connection Tools ───

  server.tool(
    "amiga_connect",
    "Connect to the Amiga emulator. Uses PTY mode by default (creates /tmp/amiga-serial for FS-UAE). Set mode='tcp' for WinUAE TCP serial.",
    {
      mode: z.enum(["pty", "tcp"]).optional().describe("Connection mode: 'pty' (default, for FS-UAE) or 'tcp' (for WinUAE)"),
      host: z.string().optional().describe("TCP host (default: 127.0.0.1, TCP mode only)"),
      port: z.number().optional().describe("TCP port (default: 1234, TCP mode only)"),
      pty_path: z.string().optional().describe("PTY symlink path (default: /tmp/amiga-serial, PTY mode only)"),
    },
    async ({ mode, host, port, pty_path }, extra) => {
      try {
        if (serial.isConnected) {
          serial.disconnect();
        }

        const connMode = mode ?? serial.connectionMode;

        if (connMode === "tcp") {
          const h = host ?? SERIAL_HOST ?? "127.0.0.1";
          const p = port ?? SERIAL_PORT;
          serial.setTarget(h, p);
          await serial.connect();

          await server.sendLoggingMessage(
            { level: "info", data: `Connected to Amiga via TCP at ${h}:${p}` },
            extra.sessionId
          );

          return {
            content: [{ type: "text", text: `Connected via TCP to ${h}:${p}` }],
          };
        } else {
          const pp = pty_path ?? PTY_PATH;
          serial.setMode("pty", { ptyPath: pp });
          await serial.connect();

          await server.sendLoggingMessage(
            { level: "info", data: `Connected via PTY at ${pp}` },
            extra.sessionId
          );

          return {
            content: [{ type: "text", text: `Connected via PTY at ${pp}. Configure FS-UAE serial_port=${pp}` }],
          };
        }
      } catch (err) {
        return {
          content: [{ type: "text", text: `Connection failed: ${err}` }],
        };
      }
    }
  );

  server.tool(
    "amiga_disconnect",
    "Disconnect from Amiga serial port.",
    {},
    async () => {
      serial.disconnect();
      return { content: [{ type: "text", text: "Disconnected" }] };
    }
  );

  // ─── Streaming Log Monitor ───

  server.tool(
    "amiga_watch_logs",
    "Stream Amiga logs in real-time via MCP notifications. Returns after duration_ms (default 30s). Logs are pushed as they arrive.",
    {
      duration_ms: z
        .number()
        .optional()
        .describe("How long to watch in ms (default: 30000)"),
      level: z.string().optional().describe("Filter: D, I, W, E"),
    },
    async ({ duration_ms, level }, extra) => {
      if (!serial.isConnected) {
        return { content: [{ type: "text", text: "Not connected to Amiga" }] };
      }

      const duration = duration_ms ?? 30000;
      const levelFilter = level?.toUpperCase().charAt(0);
      let count = 0;

      await server.sendLoggingMessage(
        {
          level: "info",
          data: `Watching logs for ${duration / 1000}s${levelFilter ? ` (level=${levelFilter})` : ""}...`,
        },
        extra.sessionId
      );

      return new Promise((resolve) => {
        const onLog = async (msg: LogMessage) => {
          if (levelFilter && msg.level !== levelFilter) return;
          count++;
          try {
            await server.sendLoggingMessage(
              {
                level:
                  msg.level === "E"
                    ? "error"
                    : msg.level === "W"
                      ? "warning"
                      : msg.level === "D"
                        ? "debug"
                        : "info",
                data: `[AMIGA ${levelName(msg.level)}] tick=${msg.tick} ${msg.message}`,
              },
              extra.sessionId
            );
          } catch {
            /* session may have closed */
          }
        };

        serial.on("log", onLog);

        setTimeout(() => {
          serial.removeListener("log", onLog);
          resolve({
            content: [
              {
                type: "text",
                text: `Log watch ended. ${count} messages received in ${duration / 1000}s.`,
              },
            ],
          });
        }, duration);
      });
    }
  );

  // ─── Log History ───

  server.tool(
    "amiga_log",
    "Get recent log messages from buffer.",
    {
      count: z.number().optional().describe("Number of messages (default: 50)"),
      level: z.string().optional().describe("Filter by level: D, I, W, E"),
    },
    async ({ count, level }) => {
      const logs = serial.getRecentLogs(count ?? 50, level);
      if (logs.length === 0) {
        return { content: [{ type: "text", text: "No log messages" }] };
      }
      const text = logs
        .map((l) => `[${levelName(l.level)}] tick=${l.tick} ${l.message}`)
        .join("\n");
      return { content: [{ type: "text", text }] };
    }
  );

  // ─── Memory Inspection ───

  server.tool(
    "amiga_inspect_memory",
    "Request a memory dump from the Amiga. Streams progress via notifications.",
    {
      address: z.string().describe("Hex address, e.g. '00BFE001'"),
      size: z.number().describe("Number of bytes to read (max 4096)"),
    },
    async ({ address, size }, extra) => {
      if (!serial.isConnected) {
        return { content: [{ type: "text", text: "Not connected" }] };
      }

      const expectedBytes = Math.min(size, 4096);

      await server.sendLoggingMessage(
        {
          level: "debug",
          data: `Requesting ${expectedBytes} bytes from 0x${address}...`,
        },
        extra.sessionId
      );

      return new Promise((resolve) => {
        const chunks: MemDump[] = [];

        const onMem = async (msg: MemDump) => {
          chunks.push(msg);
          const received = chunks.reduce((sum, c) => sum + c.size, 0);

          await server.sendLoggingMessage(
            {
              level: "debug",
              data: `Memory dump progress: ${received}/${expectedBytes} bytes`,
            },
            extra.sessionId
          );

          if (received >= expectedBytes) {
            serial.removeListener("mem", onMem);
            const allHex = chunks.map((c) => c.hexData).join("");
            resolve({
              content: [
                { type: "text", text: formatHexDump(address, allHex) },
              ],
            });
          }
        };

        serial.on("mem", onMem);
        serial.send({ type: "INSPECT", address, size: expectedBytes });

        setTimeout(() => {
          serial.removeListener("mem", onMem);
          if (chunks.length > 0) {
            const allHex = chunks.map((c) => c.hexData).join("");
            resolve({
              content: [
                {
                  type: "text",
                  text:
                    formatHexDump(address, allHex) + "\n(partial - timed out)",
                },
              ],
            });
          } else {
            resolve({
              content: [
                { type: "text", text: "Timed out waiting for memory dump" },
              ],
            });
          }
        }, 15000);
      });
    }
  );

  // ───Variable Tools ───

  server.tool(
    "amiga_get_var",
    "Get current value of a registered variable on the Amiga.",
    { name: z.string().describe("Variable name") },
    async ({ name }) => {
      if (!serial.isConnected) {
        return { content: [{ type: "text", text: "Not connected" }] };
      }

      return new Promise((resolve) => {
        const onVar = (msg: VarValue) => {
          if (msg.name === name) {
            serial.removeListener("var", onVar);
            resolve({
              content: [
                {
                  type: "text",
                  text: `${name} (${msg.varType}) = ${msg.value}`,
                },
              ],
            });
          }
        };

        serial.on("var", onVar);
        serial.send({ type: "GETVAR", name });

        setTimeout(() => {
          serial.removeListener("var", onVar);
          const cached = serial.vars.get(name);
          if (cached) {
            resolve({
              content: [
                {
                  type: "text",
                  text: `${name} (${cached.varType}) = ${cached.value} (cached)`,
                },
              ],
            });
          } else {
            resolve({
              content: [
                {
                  type: "text",
                  text: `Variable '${name}' not found or timed out`,
                },
              ],
            });
          }
        }, 3000);
      });
    }
  );

  server.tool(
    "amiga_set_var",
    "Set the value of a registered variable on the Amiga.",
    {
      name: z.string().describe("Variable name"),
      value: z.string().describe("New value"),
    },
    async ({ name, value }, extra) => {
      if (!serial.isConnected) {
        return { content: [{ type: "text", text: "Not connected" }] };
      }
      serial.send({ type: "SETVAR", name, value });

      await server.sendLoggingMessage(
        { level: "info", data: `Set ${name} = ${value}` },
        extra.sessionId
      );

      return {
        content: [{ type: "text", text: `Set ${name} = ${value}` }],
      };
    }
  );

  // ─── Ping / Status ───

  server.tool(
    "amiga_ping",
    "Ping the Amiga to get status/heartbeat.",
    {},
    async (_, extra) => {
      if (!serial.isConnected) {
        return { content: [{ type: "text", text: "Not connected" }] };
      }

      return new Promise((resolve) => {
        const onHb = async (msg: Heartbeat) => {
          serial.removeListener("heartbeat", onHb);

          await server.sendLoggingMessage(
            {
              level: "info",
              data: `Heartbeat: tick=${msg.tick} chip=${msg.freeChip} fast=${msg.freeFast}`,
            },
            extra.sessionId
          );

          resolve({
            content: [
              {
                type: "text",
                text: `Amiga alive - tick: ${msg.tick}, chip: ${msg.freeChip} bytes, fast: ${msg.freeFast} bytes`,
              },
            ],
          });
        };

        serial.on("heartbeat", onHb);
        serial.send({ type: "PING" });

        setTimeout(() => {
          serial.removeListener("heartbeat", onHb);
          resolve({
            content: [
              { type: "text", text: "No response from Amiga (timeout)" },
            ],
          });
        }, 3000);
      });
    }
  );

  // ─── Exec ───

  server.tool(
    "amiga_exec",
    "Send a custom command to the running Amiga app.",
    { command: z.string().describe("Command expression to execute") },
    async ({ command }, extra) => {
      if (!serial.isConnected) {
        return { content: [{ type: "text", text: "Not connected" }] };
      }

      const id = Date.now() % 100000;

      await server.sendLoggingMessage(
        { level: "debug", data: `Executing: ${command}` },
        extra.sessionId
      );

      return new Promise((resolve) => {
        const onCmd = (msg: { id: number; status: string; data: string }) => {
          if (msg.id === id) {
            serial.removeListener("cmd", onCmd);
            resolve({
              content: [
                { type: "text", text: `[${msg.status}] ${msg.data}` },
              ],
            });
          }
        };

        serial.on("cmd", onCmd);
        serial.send({ type: "EXEC", id, expression: command });

        setTimeout(() => {
          serial.removeListener("cmd", onCmd);
          resolve({
            content: [
              { type: "text", text: "Command sent (no response received)" },
            ],
          });
        }, 5000);
      });
    }
  );

  // ─── Streaming Status Watch ───

  server.tool(
    "amiga_watch_status",
    "Stream heartbeats and variable changes in real-time. Pushes updates as MCP notifications.",
    {
      duration_ms: z
        .number()
        .optional()
        .describe("How long to watch in ms (default: 30000)"),
    },
    async ({ duration_ms }, extra) => {
      if (!serial.isConnected) {
        return { content: [{ type: "text", text: "Not connected" }] };
      }

      const duration = duration_ms ?? 30000;
      let hbCount = 0;
      let varCount = 0;

      const onHb = async (msg: Heartbeat) => {
        hbCount++;
        try {
          await server.sendLoggingMessage(
            {
              level: "info",
              data: `[HB] tick=${msg.tick} chip=${msg.freeChip} fast=${msg.freeFast}`,
            },
            extra.sessionId
          );
        } catch {
          /* ignore */
        }
      };

      const onVar = async (msg: VarValue) => {
        varCount++;
        try {
          await server.sendLoggingMessage(
            {
              level: "info",
              data: `[VAR] ${msg.name} (${msg.varType}) = ${msg.value}`,
            },
            extra.sessionId
          );
        } catch {
          /* ignore */
        }
      };

      serial.on("heartbeat", onHb);
      serial.on("var", onVar);

      return new Promise((resolve) => {
        setTimeout(() => {
          serial.removeListener("heartbeat", onHb);
          serial.removeListener("var", onVar);
          resolve({
            content: [
              {
                type: "text",
                text: `Status watch ended. ${hbCount} heartbeats, ${varCount} variable updates in ${duration / 1000}s.`,
              },
            ],
          });
        }, duration);
      });
    }
  );

  // ─── Resources ───

  server.resource("amiga-logs", "amiga://logs", async (uri) => ({
    contents: [
      {
        uri: uri.href,
        mimeType: "text/plain",
        text:
          serial.logs.length > 0
            ? serial.logs
                .map(
                  (l) =>
                    `[${levelName(l.level)}] tick=${l.tick} ${l.message}`
                )
                .join("\n")
            : "No logs",
      },
    ],
  }));

  server.resource("amiga-vars", "amiga://vars", async (uri) => {
    const entries = Array.from(serial.vars.values());
    return {
      contents: [
        {
          uri: uri.href,
          mimeType: "text/plain",
          text:
            entries.length > 0
              ? entries
                  .map((v) => `${v.name} (${v.varType}) = ${v.value}`)
                  .join("\n")
              : "No variables registered",
        },
      ],
    };
  });

  server.resource("amiga-status", "amiga://status", async (uri) => ({
    contents: [
      {
        uri: uri.href,
        mimeType: "application/json",
        text: JSON.stringify(serial.getStatus(), null, 2),
      },
    ],
  }));

  server.resource(
    "amiga-build-output",
    "amiga://build/output",
    async (uri) => ({
      contents: [
        {
          uri: uri.href,
          mimeType: "text/plain",
          text: builder.lastBuildResult
            ? `${builder.lastBuildResult.success ? "SUCCESS" : "FAILED"} (${builder.lastBuildResult.duration}ms)\n\n${builder.lastBuildResult.output}\n${builder.lastBuildResult.errors}`
            : "No build has been run yet",
        },
      ],
    })
  );

  return server;
}

// ─── HTTP Server with StreamableHTTP transport ───

const app = express();
app.use(express.json());

app.post("/mcp", async (req, res) => {
  const sessionId = req.headers["mcp-session-id"] as string | undefined;

  try {
    let transport: StreamableHTTPServerTransport;

    if (sessionId && transports[sessionId]) {
      transport = transports[sessionId];
    } else if (!sessionId && isInitializeRequest(req.body)) {
      const eventStore = new InMemoryEventStore();
      transport = new StreamableHTTPServerTransport({
        sessionIdGenerator: () => randomUUID(),
        eventStore,
        onsessioninitialized: (sid) => {
          console.log(`Session initialized: ${sid}`);
          transports[sid] = transport;
        },
      });

      transport.onclose = () => {
        const sid = transport.sessionId;
        if (sid && transports[sid]) {
          console.log(`Session closed: ${sid}`);
          delete transports[sid];
          delete sessionServers[sid];
        }
      };

      const server = createServer();
      const sid = transport.sessionId;
      if (sid) sessionServers[sid] = server;

      await server.connect(transport);
      await transport.handleRequest(req, res, req.body);
      return;
    } else {
      res.status(400).json({
        jsonrpc: "2.0",
        error: { code: -32000, message: "Bad Request: No valid session ID" },
        id: null,
      });
      return;
    }

    await transport.handleRequest(req, res, req.body);
  } catch (error) {
    console.error("Error handling MCP request:", error);
    if (!res.headersSent) {
      res.status(500).json({
        jsonrpc: "2.0",
        error: { code: -32603, message: "Internal server error" },
        id: null,
      });
    }
  }
});

app.get("/mcp", async (req, res) => {
  const sessionId = req.headers["mcp-session-id"] as string | undefined;
  if (!sessionId || !transports[sessionId]) {
    res.status(400).send("Invalid or missing session ID");
    return;
  }
  await transports[sessionId].handleRequest(req, res);
});

app.delete("/mcp", async (req, res) => {
  const sessionId = req.headers["mcp-session-id"] as string | undefined;
  if (!sessionId || !transports[sessionId]) {
    res.status(400).send("Invalid or missing session ID");
    return;
  }
  await transports[sessionId].handleRequest(req, res);
});

// Health check
app.get("/health", (_req, res) => {
  res.json({
    status: "ok",
    serial: serial.getStatus(),
    sessions: Object.keys(transports).length,
  });
});

// ─── Web UI: SSE endpoint for real-time updates ───

type SSEClient = { id: string; res: express.Response };
const sseClients: SSEClient[] = [];

function broadcastSSE(event: string, data: unknown) {
  const payload = `event: ${event}\ndata: ${JSON.stringify(data)}\n\n`;
  for (let i = sseClients.length - 1; i >= 0; i--) {
    try {
      sseClients[i].res.write(payload);
    } catch {
      sseClients.splice(i, 1);
    }
  }
}

// Forward serial events to SSE clients
serial.on("log", (msg: LogMessage) => {
  broadcastSSE("log", {
    level: msg.level,
    tick: msg.tick,
    message: msg.message,
    client: (msg as any).client || null,
    timestamp: msg.timestamp,
  });
});

serial.on("heartbeat", (msg: Heartbeat) => {
  broadcastSSE("heartbeat", {
    tick: msg.tick,
    freeChip: msg.freeChip,
    freeFast: msg.freeFast,
  });
});

serial.on("var", (msg: any) => {
  broadcastSSE("var", {
    name: msg.name,
    varType: msg.varType,
    value: msg.value,
    client: msg.client || null,
  });
});

serial.on("connected", () => broadcastSSE("connected", {}));
serial.on("disconnected", () => broadcastSSE("disconnected", {}));
serial.on("clients", (msg: any) => broadcastSSE("clients", msg.names || []));
serial.on("tasks", (msg: any) => broadcastSSE("tasks", { tasks: msg.tasks || [] }));
serial.on("dir", (msg: any) => broadcastSSE("dir", { path: msg.path, entries: msg.entries || [] }));

app.get("/api/events", (req, res) => {
  res.writeHead(200, {
    "Content-Type": "text/event-stream",
    "Cache-Control": "no-cache",
    "Connection": "keep-alive",
  });
  res.write(":\n\n"); // comment to establish connection

  const client: SSEClient = { id: randomUUID(), res };
  sseClients.push(client);

  // Send current status immediately
  res.write(`event: status\ndata: ${JSON.stringify(serial.getStatus())}\n\n`);

  req.on("close", () => {
    const idx = sseClients.findIndex((c) => c.id === client.id);
    if (idx >= 0) sseClients.splice(idx, 1);
  });
});

// ─── Web UI: REST API endpoints ───

app.get("/api/status", (_req, res) => {
  res.json(serial.getStatus());
});

app.get("/api/clients", (_req, res) => {
  // If connected, try sending LISTCLIENTS and wait for response
  if (!serial.isConnected) {
    return res.json({ clients: [], error: "Not connected" });
  }
  const onClients = (msg: any) => {
    serial.removeListener("clients", onClients);
    clearTimeout(timer);
    res.json({ clients: msg.names || [] });
  };
  serial.on("clients", onClients);
  try {
    serial.send({ type: "LISTCLIENTS" });
  } catch {
    serial.removeListener("clients", onClients);
    return res.json({ clients: [], error: "Send failed" });
  }
  const timer = setTimeout(() => {
    serial.removeListener("clients", onClients);
    res.json({ clients: [], message: "No response (bridge may not support LISTCLIENTS)" });
  }, 3000);
});

app.get("/api/logs", (req, res) => {
  const count = parseInt((req.query as any).count ?? "200", 10);
  const level = (req.query as any).level as string | undefined;
  const logs = serial.getRecentLogs(count, level).map((l) => ({
    level: l.level,
    tick: l.tick,
    message: l.message,
    timestamp: l.timestamp,
    client: (l as any).client || null,
  }));
  res.json({ logs });
});

app.get("/api/tasks", (_req, res) => {
  if (!serial.isConnected) {
    return res.json({ tasks: [], error: "Not connected" });
  }
  const onTasks = (msg: any) => {
    serial.removeListener("tasks", onTasks);
    clearTimeout(timer);
    res.json({ tasks: msg.tasks || [] });
  };
  serial.on("tasks", onTasks);
  try {
    serial.send({ type: "LISTTASKS" });
  } catch {
    serial.removeListener("tasks", onTasks);
    return res.json({ tasks: [], error: "Send failed" });
  }
  const timer = setTimeout(() => {
    serial.removeListener("tasks", onTasks);
    res.json({ tasks: [], message: "No response (bridge may not support LISTTASKS)" });
  }, 3000);
});

app.get("/api/dir", (req, res) => {
  const dirPath = (req.query as any).path ?? "SYS:";
  if (!serial.isConnected) {
    return res.json({ path: dirPath, entries: [], error: "Not connected" });
  }
  const onDir = (msg: any) => {
    if (msg.path === dirPath) {
      serial.removeListener("dir", onDir);
      clearTimeout(timer);
      res.json({ path: msg.path, entries: msg.entries || [] });
    }
  };
  serial.on("dir", onDir);
  try {
    serial.send({ type: "LISTDIR", path: dirPath });
  } catch {
    serial.removeListener("dir", onDir);
    return res.json({ path: dirPath, entries: [], error: "Send failed" });
  }
  const timer = setTimeout(() => {
    serial.removeListener("dir", onDir);
    res.json({ path: dirPath, entries: [], message: "No response (bridge may not support LISTDIR)" });
  }, 5000);
});

app.get("/api/file", (req, res) => {
  const filePath = (req.query as any).path ?? "";
  const offset = parseInt((req.query as any).offset ?? "0", 10);
  const size = parseInt((req.query as any).size ?? "4096", 10);
  if (!serial.isConnected) {
    return res.json({ error: "Not connected" });
  }
  const onFile = (msg: any) => {
    if (msg.path === filePath) {
      serial.removeListener("file", onFile);
      clearTimeout(timer);
      res.json({ path: msg.path, size: msg.size, offset: msg.offset, hexData: msg.hexData });
    }
  };
  serial.on("file", onFile);
  try {
    serial.send({ type: "READFILE", path: filePath, offset, size });
  } catch {
    serial.removeListener("file", onFile);
    return res.json({ error: "Send failed" });
  }
  const timer = setTimeout(() => {
    serial.removeListener("file", onFile);
    res.json({ error: "No response (bridge may not support READFILE)" });
  }, 5000);
});

app.get("/api/memory", (req, res) => {
  const address = (req.query as any).address ?? "00000004";
  const size = Math.min(parseInt((req.query as any).size ?? "256", 10), 4096);
  if (!serial.isConnected) {
    return res.json({ error: "Not connected" });
  }
  const chunks: any[] = [];
  const onMem = (msg: any) => {
    chunks.push(msg);
    const received = chunks.reduce((sum: number, c: any) => sum + c.size, 0);
    if (received >= size) {
      serial.removeListener("mem", onMem);
      clearTimeout(timer);
      const allHex = chunks.map((c: any) => c.hexData).join("");
      res.json({ address, size: received, dump: formatHexDump(address, allHex) });
    }
  };
  serial.on("mem", onMem);
  try {
    serial.send({ type: "INSPECT", address, size });
  } catch {
    serial.removeListener("mem", onMem);
    return res.json({ error: "Send failed" });
  }
  const timer = setTimeout(() => {
    serial.removeListener("mem", onMem);
    if (chunks.length > 0) {
      const allHex = chunks.map((c: any) => c.hexData).join("");
      res.json({ address, dump: formatHexDump(address, allHex) + "\n(partial - timed out)" });
    } else {
      res.json({ error: "Timed out waiting for memory dump" });
    }
  }, 15000);
});

app.get("/api/vars", (_req, res) => {
  const vars = Array.from(serial.vars.values()).map((v) => ({
    name: v.name,
    varType: v.varType,
    value: v.value,
    client: (v as any).client || null,
  }));
  res.json({ vars });
});

app.post("/api/command", (req, res) => {
  if (!serial.isConnected) {
    return res.json({ error: "Not connected" });
  }
  const { command } = req.body ?? {};
  if (!command) {
    return res.status(400).json({ error: "Missing 'command' field" });
  }

  // Check if it's a known protocol command or a raw exec
  const parts = command.split("|");
  const cmdType = parts[0].toUpperCase();

  // Handle SETVAR specially
  if (cmdType === "SETVAR" && parts.length >= 3) {
    try {
      serial.send({ type: "SETVAR", name: parts[1], value: parts.slice(2).join("|") });
      return res.json({ message: `Set ${parts[1]} = ${parts.slice(2).join("|")}` });
    } catch (err) {
      return res.json({ error: String(err) });
    }
  }

  // For simple text commands, wrap in EXEC
  const id = Date.now() % 100000;
  const onCmd = (msg: any) => {
    if (msg.id === id) {
      serial.removeListener("cmd", onCmd);
      clearTimeout(timer);
      res.json({ response: `[${msg.status}] ${msg.data}` });
    }
  };
  serial.on("cmd", onCmd);
  try {
    serial.send({ type: "EXEC", id, expression: command });
  } catch (err) {
    serial.removeListener("cmd", onCmd);
    return res.json({ error: String(err) });
  }
  const timer = setTimeout(() => {
    serial.removeListener("cmd", onCmd);
    res.json({ message: "Command sent (no response received)" });
  }, 5000);
});

app.post("/api/ping", (_req, res) => {
  if (!serial.isConnected) {
    return res.json({ error: "Not connected" });
  }
  const onHb = (msg: Heartbeat) => {
    serial.removeListener("heartbeat", onHb);
    clearTimeout(timer);
    res.json({
      message: `Amiga alive - tick: ${msg.tick}, chip: ${msg.freeChip} bytes, fast: ${msg.freeFast} bytes`,
      heartbeat: { tick: msg.tick, freeChip: msg.freeChip, freeFast: msg.freeFast },
    });
  };
  serial.on("heartbeat", onHb);
  try {
    serial.send({ type: "PING" });
  } catch (err) {
    serial.removeListener("heartbeat", onHb);
    return res.json({ error: String(err) });
  }
  const timer = setTimeout(() => {
    serial.removeListener("heartbeat", onHb);
    res.json({ error: "No response from Amiga (timeout)" });
  }, 3000);
});

app.post("/api/connect", async (req, res) => {
  try {
    if (serial.isConnected) serial.disconnect();
    const { mode, host, port, ptyPath } = req.body ?? {};
    if (mode === "tcp") {
      serial.setTarget(host ?? SERIAL_HOST ?? "127.0.0.1", port ?? SERIAL_PORT);
    } else if (mode === "pty") {
      serial.setMode("pty", { ptyPath: ptyPath ?? PTY_PATH });
    }
    await serial.connect();
    res.json({ message: `Connected (${serial.connectionMode})`, status: serial.getStatus() });
  } catch (err) {
    res.json({ error: `Connection failed: ${err}` });
  }
});

app.post("/api/disconnect", (_req, res) => {
  serial.disconnect();
  res.json({ message: "Disconnected" });
});

// ─── Serve static Web UI (must be after API routes) ───
app.use(express.static(path.join(__dirname, "web")));

app.listen(HTTP_PORT, async () => {
  console.log(`Amiga MCP server (StreamableHTTP) on http://localhost:${HTTP_PORT}/mcp`);
  console.log(`Web UI: http://localhost:${HTTP_PORT}/`);
  console.log(`Health check: http://localhost:${HTTP_PORT}/health`);
  console.log(`Connection mode: ${USE_PTY ? "PTY" : "TCP"}`);

  if (USE_PTY) {
    // Auto-start PTY so it's ready for FS-UAE before any client connects
    console.log(`Auto-starting PTY at ${PTY_PATH}...`);
    try {
      await serial.connect();
      console.log(`PTY active: ${PTY_PATH} - configure FS-UAE serial_port=${PTY_PATH}`);
    } catch (err) {
      console.error(`PTY auto-start failed: ${err}. Use amiga_connect tool to retry.`);
    }
  }
});

for (const sig of ["SIGINT", "SIGTERM"] as const) {
  process.on(sig, async () => {
    console.log(`\n${sig} received, shutting down...`);
    serial.disconnect();
    for (const sid in transports) {
      try {
        await transports[sid].close();
      } catch {
        /* ignore */
      }
    }
    process.exit(0);
  });
}
