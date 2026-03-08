import { randomUUID } from "node:crypto";
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

const SERIAL_HOST = process.env.AMIGA_SERIAL_HOST ?? "127.0.0.1";
const SERIAL_PORT = parseInt(process.env.AMIGA_SERIAL_PORT ?? "1234", 10);
const HTTP_PORT = parseInt(process.env.MCP_PORT ?? "3000", 10);
const PROJECT_ROOT = process.env.AMIGA_PROJECT_ROOT;

// Shared state
const serial = new SerialConnection(SERIAL_HOST, SERIAL_PORT);
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
    "Connect to the Amiga emulator serial port (TCP).",
    {
      host: z.string().optional().describe("TCP host (default: 127.0.0.1)"),
      port: z.number().optional().describe("TCP port (default: 1234)"),
    },
    async ({ host, port }, extra) => {
      try {
        const h = host ?? SERIAL_HOST;
        const p = port ?? SERIAL_PORT;

        if (serial.isConnected) {
          serial.disconnect();
        }
        serial.setTarget(h, p);
        await serial.connect();

        await server.sendLoggingMessage(
          { level: "info", data: `Connected to Amiga at ${h}:${p}` },
          extra.sessionId
        );

        return {
          content: [{ type: "text", text: `Connected to ${h}:${p}` }],
        };
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
        }, 5000);
      });
    }
  );

  // ─── Variable Tools ───

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

app.listen(HTTP_PORT, () => {
  console.log(`Amiga MCP server (StreamableHTTP) on http://localhost:${HTTP_PORT}/mcp`);
  console.log(`Health check: http://localhost:${HTTP_PORT}/health`);
});

process.on("SIGINT", async () => {
  console.log("Shutting down...");
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
