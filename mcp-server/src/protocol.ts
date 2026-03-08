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

export type AmigaMessage = LogMessage | MemDump | VarValue | Heartbeat | CmdResponse;

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

export type HostCommand = InspectCmd | GetVarCmd | SetVarCmd | PingCmd | ExecCmd;

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
