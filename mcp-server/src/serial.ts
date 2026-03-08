import { Socket } from "net";
import { EventEmitter } from "events";
import {
  parseMessage,
  formatCommand,
  type AmigaMessage,
  type HostCommand,
  type LogMessage,
  type VarValue,
  type Heartbeat,
} from "./protocol.js";

const MAX_LOG_BUFFER = 1000;
const RECONNECT_INTERVAL = 5000;

export class SerialConnection extends EventEmitter {
  private socket: Socket | null = null;
  private host: string;
  private port: number;
  private lineBuf = "";
  private connected = false;
  private reconnectTimer: ReturnType<typeof setTimeout> | null = null;
  private autoReconnect = true;

  readonly logs: LogMessage[] = [];
  readonly vars: Map<string, VarValue> = new Map();
  lastHeartbeat: Heartbeat | null = null;

  constructor(host = "127.0.0.1", port = 1234) {
    super();
    this.host = host;
    this.port = port;
  }

  get isConnected(): boolean {
    return this.connected;
  }

  setTarget(host: string, port: number): void {
    this.host = host;
    this.port = port;
  }

  connect(): Promise<void> {
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

  disconnect(): void {
    this.autoReconnect = false;
    if (this.reconnectTimer) {
      clearTimeout(this.reconnectTimer);
      this.reconnectTimer = null;
    }
    if (this.socket) {
      this.socket.destroy();
      this.socket = null;
    }
    this.connected = false;
  }

  send(cmd: HostCommand): void {
    if (!this.connected || !this.socket) {
      throw new Error("Not connected to Amiga serial port");
    }
    const line = formatCommand(cmd);
    this.socket.write(line + "\n");
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
    }
  }

  private scheduleReconnect(): void {
    if (this.reconnectTimer) return;
    this.reconnectTimer = setTimeout(() => {
      this.reconnectTimer = null;
      if (!this.connected && this.autoReconnect) {
        this.connect().catch(() => {
          this.scheduleReconnect();
        });
      }
    }, RECONNECT_INTERVAL);
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
      host: this.host,
      port: this.port,
      logCount: this.logs.length,
      varCount: this.vars.size,
      lastHeartbeat: this.lastHeartbeat,
    };
  }
}
