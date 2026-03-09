import { Socket } from "net";
import { EventEmitter } from "events";
import { ChildProcess, spawn } from "child_process";
import path from "path";
import { fileURLToPath } from "url";
import {
  parseMessage,
  formatCommand,
  type AmigaMessage,
  type HostCommand,
  type LogMessage,
  type VarValue,
  type Heartbeat,
} from "./protocol.js";

const __dirname = path.dirname(fileURLToPath(import.meta.url));

const MAX_LOG_BUFFER = 1000;
const RECONNECT_INTERVAL = 5000;
const PTY_RESTART_DELAY = 2000;

export type ConnectionMode = "tcp" | "pty";

export class SerialConnection extends EventEmitter {
  // TCP mode
  private socket: Socket | null = null;
  private host: string;
  private port: number;

  // PTY mode
  private ptyProcess: ChildProcess | null = null;
  private ptyPath: string;
  private ptyReady = false;

  // Common
  private mode: ConnectionMode;
  private lineBuf = "";
  private connected = false;
  private reconnectTimer: ReturnType<typeof setTimeout> | null = null;
  private autoReconnect = true;

  readonly logs: LogMessage[] = [];
  readonly vars: Map<string, VarValue> = new Map();
  lastHeartbeat: Heartbeat | null = null;

  constructor(host = "127.0.0.1", port = 1234, ptyPath = "/tmp/amiga-serial") {
    super();
    this.host = host;
    this.port = port;
    this.ptyPath = ptyPath;
    // Default mode: PTY unless a host is explicitly configured
    this.mode = "tcp";
  }

  get isConnected(): boolean {
    return this.connected;
  }

  get connectionMode(): ConnectionMode {
    return this.mode;
  }

  get ptyDevicePath(): string {
    return this.ptyPath;
  }

  setTarget(host: string, port: number): void {
    this.host = host;
    this.port = port;
    this.mode = "tcp";
  }

  setMode(mode: ConnectionMode, options?: { ptyPath?: string; host?: string; port?: number }): void {
    this.mode = mode;
    if (options?.ptyPath) this.ptyPath = options.ptyPath;
    if (options?.host) this.host = options.host;
    if (options?.port) this.port = options.port;
  }

  connect(): Promise<void> {
    if (this.mode === "pty") {
      return this.connectPty();
    }
    return this.connectTcp();
  }

  // ─── TCP Mode ───

  private connectTcp(): Promise<void> {
    return new Promise((resolve, reject) => {
      if (this.connected) {
        resolve();
        return;
      }

      this.autoReconnect = true;
      this.socket = new Socket();

      this.socket.on("connect", () => {
        this.connected = true;
        this.lineBuf = "";
        this.emit("connected");
        resolve();
      });

      this.socket.on("data", (data: Buffer) => {
        this.handleData(data.toString());
      });

      this.socket.on("close", () => {
        const wasConnected = this.connected;
        this.connected = false;
        if (wasConnected) {
          this.emit("disconnected");
        }
        if (this.autoReconnect) {
          this.scheduleReconnect();
        }
      });

      this.socket.on("error", (err: Error) => {
        if (!this.connected) {
          reject(err);
        }
        this.emit("error", err);
      });

      this.socket.connect(this.port, this.host);
    });
  }

  // ─── PTY Mode ───

  private connectPty(): Promise<void> {
    return new Promise((resolve, reject) => {
      if (this.connected) {
        resolve();
        return;
      }

      this.autoReconnect = true;
      this.ptyReady = false;

      const helperScript = path.join(__dirname, "pty-helper.py");

      console.log(`[serial] Starting PTY helper: python3 ${helperScript} ${this.ptyPath}`);

      this.ptyProcess = spawn("python3", [helperScript, this.ptyPath], {
        stdio: ["pipe", "pipe", "pipe"],
      });

      let resolved = false;

      // Read stderr for ready signal and status messages
      this.ptyProcess.stderr?.on("data", (data: Buffer) => {
        const lines = data.toString().split("\n");
        for (const line of lines) {
          const trimmed = line.trim();
          if (!trimmed) continue;

          if (trimmed.startsWith("PTY_READY:")) {
            const actualPath = trimmed.slice("PTY_READY:".length);
            this.ptyReady = true;
            this.connected = true;
            this.lineBuf = "";
            console.log(`[serial] PTY ready: ${actualPath}`);
            this.emit("connected");
            if (!resolved) {
              resolved = true;
              resolve();
            }
          } else if (trimmed === "PTY_CLOSED") {
            console.log("[serial] PTY helper reported closed");
          } else {
            console.log(`[serial] PTY helper: ${trimmed}`);
          }
        }
      });

      // Read stdout for data from FS-UAE (via PTY)
      this.ptyProcess.stdout?.on("data", (data: Buffer) => {
        this.handleData(data.toString());
      });

      this.ptyProcess.on("error", (err: Error) => {
        console.error(`[serial] PTY helper error: ${err.message}`);
        if (!resolved) {
          resolved = true;
          reject(err);
        }
        this.emit("error", err);
      });

      this.ptyProcess.on("exit", (code, signal) => {
        const wasConnected = this.connected;
        this.connected = false;
        this.ptyReady = false;
        this.ptyProcess = null;

        console.log(`[serial] PTY helper exited (code=${code}, signal=${signal})`);

        if (!resolved) {
          resolved = true;
          reject(new Error(`PTY helper exited with code ${code}`));
        }

        if (wasConnected) {
          this.emit("disconnected");
        }

        if (this.autoReconnect) {
          console.log(`[serial] Will restart PTY helper in ${PTY_RESTART_DELAY}ms...`);
          this.scheduleReconnect();
        }
      });

      // Timeout if PTY helper doesn't signal ready
      setTimeout(() => {
        if (!resolved) {
          resolved = true;
          if (!this.ptyReady) {
            reject(new Error("PTY helper did not become ready within 10s"));
          }
        }
      }, 10000);
    });
  }

  disconnect(): void {
    this.autoReconnect = false;
    if (this.reconnectTimer) {
      clearTimeout(this.reconnectTimer);
      this.reconnectTimer = null;
    }

    if (this.mode === "tcp") {
      if (this.socket) {
        this.socket.destroy();
        this.socket = null;
      }
    } else {
      if (this.ptyProcess) {
        this.ptyProcess.kill("SIGTERM");
        // Force kill after 2s if still alive
        const proc = this.ptyProcess;
        setTimeout(() => {
          try { proc.kill("SIGKILL"); } catch { /* already dead */ }
        }, 2000);
        this.ptyProcess = null;
      }
    }

    this.connected = false;
    this.ptyReady = false;
  }

  send(cmd: HostCommand): void {
    if (!this.connected) {
      throw new Error("Not connected to Amiga serial port");
    }
    const line = formatCommand(cmd);
    this.writeRaw(line + "\n");
  }

  sendRaw(line: string): void {
    if (!this.connected) {
      throw new Error("Not connected to Amiga serial port");
    }
    this.writeRaw(line + "\n");
  }

  private writeRaw(data: string): void {
    if (this.mode === "tcp") {
      if (!this.socket) throw new Error("Not connected (TCP)");
      this.socket.write(data);
    } else {
      if (!this.ptyProcess?.stdin?.writable) throw new Error("Not connected (PTY)");
      this.ptyProcess.stdin.write(data);
    }
  }

  private handleData(data: string): void {
    this.lineBuf += data;
    const lines = this.lineBuf.split("\n");
    this.lineBuf = lines.pop() ?? "";

    for (const line of lines) {
      const trimmed = line.trim();
      if (!trimmed) continue;

      const msg = parseMessage(trimmed);
      if (!msg) continue;

      this.processMessage(msg);
      this.emit("message", msg);
    }
  }

  private processMessage(msg: AmigaMessage): void {
    switch (msg.type) {
      case "LOG":
        this.logs.push(msg);
        while (this.logs.length > MAX_LOG_BUFFER) {
          this.logs.shift();
        }
        this.emit("log", msg);
        break;

      case "VAR":
        this.vars.set(msg.name, msg);
        this.emit("var", msg);
        break;

      case "HB":
        this.lastHeartbeat = msg;
        this.emit("heartbeat", msg);
        break;

      case "MEM":
        this.emit("mem", msg);
        break;

      case "CMD":
        this.emit("cmd", msg);
        break;

      case "CLOG":
        // Client log - store like a regular log but with client info
        this.logs.push({
          type: "LOG",
          level: msg.level,
          tick: msg.tick,
          message: `[${msg.client}] ${msg.message}`,
          timestamp: msg.timestamp,
        } as LogMessage);
        while (this.logs.length > MAX_LOG_BUFFER) this.logs.shift();
        this.emit("log", { ...msg, type: "LOG" });
        this.emit("clog", msg);
        break;

      case "CVAR":
        this.vars.set(msg.name, {
          type: "VAR",
          name: msg.name,
          varType: msg.varType,
          value: msg.value,
        } as VarValue);
        this.emit("var", { type: "VAR", name: msg.name, varType: msg.varType, value: msg.value, client: msg.client });
        this.emit("cvar", msg);
        break;

      case "READY":
        this.emit("ready", msg);
        break;

      case "CLIENTS":
        this.emit("clients", msg);
        break;

      case "TASKS":
        this.emit("tasks", msg);
        break;

      case "LIBS":
        this.emit("libs", msg);
        break;

      case "DIR":
        this.emit("dir", msg);
        break;

      case "FILE":
        this.emit("file", msg);
        break;

      case "FILEINFO":
        this.emit("fileinfo", msg);
        break;

      case "PROC":
        this.emit("proc", msg);
        break;

      case "ERR":
        this.emit("err", msg);
        break;
    }
  }

  private scheduleReconnect(): void {
    if (this.reconnectTimer) return;
    const delay = this.mode === "pty" ? PTY_RESTART_DELAY : RECONNECT_INTERVAL;
    this.reconnectTimer = setTimeout(() => {
      this.reconnectTimer = null;
      if (!this.connected && this.autoReconnect) {
        console.log(`[serial] Attempting ${this.mode} reconnect...`);
        this.connect().catch((err) => {
          console.error(`[serial] Reconnect failed: ${err.message}`);
          this.scheduleReconnect();
        });
      }
    }, delay);
  }

  getRecentLogs(count = 100, level?: string): LogMessage[] {
    let filtered: LogMessage[] = this.logs;
    if (level) {
      filtered = this.logs.filter(
        (l) => l.level === level.toUpperCase().charAt(0)
      );
    }
    return filtered.slice(-count);
  }

  getStatus(): object {
    return {
      connected: this.connected,
      mode: this.mode,
      ...(this.mode === "tcp"
        ? { host: this.host, port: this.port }
        : { ptyPath: this.ptyPath, ptyReady: this.ptyReady }),
      logCount: this.logs.length,
      varCount: this.vars.size,
      lastHeartbeat: this.lastHeartbeat,
    };
  }
}
