// Debug protocol message types - Amiga to Host

export interface LogMessage {
  type: "LOG";
  level: "D" | "I" | "W" | "E";
  tick: number;
  message: string;
  timestamp: Date;
}

export interface MemDump {
  type: "MEM";
  address: string;
  size: number;
  hexData: string;
}

export interface VarValue {
  type: "VAR";
  name: string;
  varType: string;
  value: string;
}

export interface Heartbeat {
  type: "HB";
  tick: number;
  freeChip: number;
  freeFast: number;
  timestamp: Date;
}

export interface CmdResponse {
  type: "CMD";
  id: number;
  status: string;
  data: string;
}

export interface ReadyMessage {
  type: "READY";
  version: string;
}

export interface ClientsMessage {
  type: "CLIENTS";
  count: number;
  names: string[];
}

export interface TasksMessage {
  type: "TASKS";
  count: number;
  tasks: Array<{ name: string; priority: number; state: string; stackSize: number }>;
}

export interface LibsMessage {
  type: "LIBS";
  count: number;
  libs: Array<{ name: string; version: number; revision: number }>;
}

export interface DirMessage {
  type: "DIR";
  path: string;
  count: number;
  entries: Array<{ name: string; type: string; size: number; date: string; prot: string; path?: string }>;
}

export interface FileMessage {
  type: "FILE";
  path: string;
  size: number;
  offset: number;
  hexData: string;
}

export interface FileInfoMessage {
  type: "FILEINFO";
  path: string;
  size: number;
  date: string;
  prot: string;
}

export interface ProcMessage {
  type: "PROC";
  id: number;
  status: string;
  output: string;
}

export interface CLogMessage {
  type: "CLOG";
  client: string;
  level: "D" | "I" | "W" | "E";
  tick: number;
  message: string;
  timestamp: Date;
}

export interface CVarMessage {
  type: "CVAR";
  client: string;
  name: string;
  varType: string;
  value: string;
}

export interface ErrMessage {
  type: "ERR";
  context: string;
  message: string;
}

export type AmigaMessage =
  | LogMessage | MemDump | VarValue | Heartbeat | CmdResponse
  | ReadyMessage | ClientsMessage | TasksMessage | LibsMessage
  | DirMessage | FileMessage | FileInfoMessage | ProcMessage
  | CLogMessage | CVarMessage | ErrMessage;

// Host to Amiga commands

export interface InspectCmd {
  type: "INSPECT";
  address: string;
  size: number;
}

export interface GetVarCmd {
  type: "GETVAR";
  name: string;
}

export interface SetVarCmd {
  type: "SETVAR";
  name: string;
  value: string;
}

export interface PingCmd {
  type: "PING";
}

export interface ExecCmd {
  type: "EXEC";
  id: number;
  expression: string;
}

export interface ListClientsCmd { type: "LISTCLIENTS"; }
export interface ListTasksCmd { type: "LISTTASKS"; }
export interface ListLibsCmd { type: "LISTLIBS"; }
export interface ListDevsCmd { type: "LISTDEVS"; }
export interface ListDirCmd { type: "LISTDIR"; path: string; }
export interface ReadFileCmd { type: "READFILE"; path: string; offset: number; size: number; }
export interface LaunchCmd { type: "LAUNCH"; id: number; command: string; }
export interface DosCommandCmd { type: "DOSCOMMAND"; id: number; command: string; }
export interface ShutdownCmd { type: "SHUTDOWN"; }

export type HostCommand =
  | InspectCmd | GetVarCmd | SetVarCmd | PingCmd | ExecCmd
  | ListClientsCmd | ListTasksCmd | ListLibsCmd | ListDevsCmd
  | ListDirCmd | ReadFileCmd | LaunchCmd | DosCommandCmd | ShutdownCmd;

const LEVEL_NAMES: Record<string, string> = {
  D: "DEBUG",
  I: "INFO",
  W: "WARN",
  E: "ERROR",
};

export function levelName(code: string): string {
  return LEVEL_NAMES[code] ?? code;
}

export function parseMessage(line: string): AmigaMessage | null {
  const parts = line.split("|");
  if (parts.length < 1) return null;

  const now = new Date();

  switch (parts[0]) {
    case "LOG":
      if (parts.length < 4) return null;
      return {
        type: "LOG",
        level: parts[1] as LogMessage["level"],
        tick: parseInt(parts[2], 10),
        message: parts.slice(3).join("|"),
        timestamp: now,
      };

    case "MEM":
      if (parts.length < 4) return null;
      return {
        type: "MEM",
        address: parts[1],
        size: parseInt(parts[2], 10),
        hexData: parts[3],
      };

    case "VAR":
      if (parts.length < 4) return null;
      return {
        type: "VAR",
        name: parts[1],
        varType: parts[2],
        value: parts.slice(3).join("|"),
      };

    case "HB":
      if (parts.length < 4) return null;
      return {
        type: "HB",
        tick: parseInt(parts[1], 10),
        freeChip: parseInt(parts[2], 10),
        freeFast: parseInt(parts[3], 10),
        timestamp: now,
      };

    case "CMD":
      if (parts.length < 4) return null;
      return {
        type: "CMD",
        id: parseInt(parts[1], 10),
        status: parts[2],
        data: parts.slice(3).join("|"),
      };

    case "READY":
      return { type: "READY", version: parts[1] ?? "unknown" };

    case "CLIENTS": {
      const count = parseInt(parts[1] ?? "0", 10);
      const names = parts[2] ? parts[2].split(",").map(s => s.trim()).filter(Boolean) : [];
      return { type: "CLIENTS", count, names };
    }

    case "TASKS": {
      const count = parseInt(parts[1] ?? "0", 10);
      let tasks: TasksMessage["tasks"] = [];
      try { tasks = JSON.parse(parts.slice(2).join("|")); } catch { /* ignore */ }
      return { type: "TASKS", count, tasks };
    }

    case "LIBS": {
      const count = parseInt(parts[1] ?? "0", 10);
      let libs: LibsMessage["libs"] = [];
      try { libs = JSON.parse(parts.slice(2).join("|")); } catch { /* ignore */ }
      return { type: "LIBS", count, libs };
    }

    case "DIR": {
      const dirPath = parts[1] ?? "";
      const count = parseInt(parts[2] ?? "0", 10);
      let entries: DirMessage["entries"] = [];
      try { entries = JSON.parse(parts.slice(3).join("|")); } catch { /* ignore */ }
      return { type: "DIR", path: dirPath, count, entries };
    }

    case "FILE": {
      const filePath = parts[1] ?? "";
      const size = parseInt(parts[2] ?? "0", 10);
      const offset = parseInt(parts[3] ?? "0", 10);
      const hexData = parts[4] ?? "";
      return { type: "FILE", path: filePath, size, offset, hexData };
    }

    case "FILEINFO":
      return {
        type: "FILEINFO",
        path: parts[1] ?? "",
        size: parseInt(parts[2] ?? "0", 10),
        date: parts[3] ?? "",
        prot: parts[4] ?? "",
      };

    case "PROC":
      return {
        type: "PROC",
        id: parseInt(parts[1] ?? "0", 10),
        status: parts[2] ?? "",
        output: parts.slice(3).join("|"),
      };

    case "CLOG":
      if (parts.length < 5) return null;
      return {
        type: "CLOG",
        client: parts[1],
        level: parts[2] as CLogMessage["level"],
        tick: parseInt(parts[3], 10),
        message: parts.slice(4).join("|"),
        timestamp: now,
      };

    case "CVAR":
      if (parts.length < 5) return null;
      return {
        type: "CVAR",
        client: parts[1],
        name: parts[2],
        varType: parts[3],
        value: parts.slice(4).join("|"),
      };

    case "ERR":
      return {
        type: "ERR",
        context: parts[1] ?? "",
        message: parts.slice(2).join("|"),
      };

    default:
      return null;
  }
}

export function formatCommand(cmd: HostCommand): string {
  switch (cmd.type) {
    case "PING":
      return "PING";
    case "GETVAR":
      return `GETVAR|${cmd.name}`;
    case "SETVAR":
      return `SETVAR|${cmd.name}|${cmd.value}`;
    case "INSPECT":
      return `INSPECT|${cmd.address}|${cmd.size}`;
    case "EXEC":
      return `EXEC|${cmd.id}|${cmd.expression}`;
    case "LISTCLIENTS":
      return "LISTCLIENTS";
    case "LISTTASKS":
      return "LISTTASKS";
    case "LISTLIBS":
      return "LISTLIBS";
    case "LISTDEVS":
      return "LISTDEVS";
    case "LISTDIR":
      return `LISTDIR|${cmd.path}`;
    case "READFILE":
      return `READFILE|${cmd.path}|${cmd.offset}|${cmd.size}`;
    case "LAUNCH":
      return `LAUNCH|${cmd.id}|${cmd.command}`;
    case "DOSCOMMAND":
      return `DOSCOMMAND|${cmd.id}|${cmd.command}`;
    case "SHUTDOWN":
      return "SHUTDOWN";
  }
}

export function hexToAscii(hex: string): string {
  let ascii = "";
  for (let i = 0; i < hex.length; i += 2) {
    const byte = parseInt(hex.substring(i, i + 2), 16);
    ascii += byte >= 32 && byte < 127 ? String.fromCharCode(byte) : ".";
  }
  return ascii;
}

export function formatHexDump(address: string, hexData: string): string {
  const lines: string[] = [];
  const addr = parseInt(address, 16);

  for (let i = 0; i < hexData.length; i += 32) {
    const chunk = hexData.substring(i, i + 32);
    const offset = addr + i / 2;
    const hexPart = chunk.match(/.{1,2}/g)?.join(" ") ?? "";
    const asciiPart = hexToAscii(chunk);
    lines.push(
      `${offset.toString(16).padStart(8, "0")}  ${hexPart.padEnd(48)}  ${asciiPart}`
    );
  }

  return lines.join("\n");
}
