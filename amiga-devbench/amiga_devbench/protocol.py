"""Debug protocol message parsing and formatting.

Line-based text protocol over serial/TCP, pipe-delimited fields.

Amiga -> Host: LOG, MEM, VAR, HB, CMD, READY, CLIENTS, TASKS, LIBS,
               DIR, FILE, FILEINFO, PROC, CLOG, CVAR, HOOKS, MEMREGS,
               CINFO, DEVICES, ERR, OK
Host -> Amiga: PING, GETVAR, SETVAR, INSPECT, EXEC, LISTCLIENTS,
               LISTTASKS, LISTLIBS, LISTDEVS, LISTDIR, READFILE,
               WRITEFILE, FILEINFO, DELETEFILE, MAKEDIR, LAUNCH,
               DOSCOMMAND, RUN, BREAK, LISTHOOKS, CALLHOOK,
               LISTMEMREGS, READMEMREG, CLIENTINFO, STOP, SCRIPT,
               WRITEMEM, SHUTDOWN
"""

from __future__ import annotations

import json
from datetime import datetime, timezone
from typing import Any

LEVEL_NAMES = {"D": "DEBUG", "I": "INFO", "W": "WARN", "E": "ERROR"}


def level_name(code: str) -> str:
    return LEVEL_NAMES.get(code, code)


def parse_message(line: str) -> dict[str, Any] | None:
    """Parse a line from the Amiga into a message dict, or None if invalid."""
    parts = line.split("|")
    if not parts:
        return None

    now = datetime.now(timezone.utc).isoformat()
    msg_type = parts[0]

    if msg_type == "LOG":
        if len(parts) < 4:
            return None
        return {
            "type": "LOG",
            "level": parts[1],
            "tick": _int(parts[2]),
            "message": "|".join(parts[3:]),
            "timestamp": now,
        }

    if msg_type == "MEM":
        if len(parts) < 4:
            return None
        return {
            "type": "MEM",
            "address": parts[1],
            "size": _int(parts[2]),
            "hexData": parts[3],
        }

    if msg_type == "VAR":
        if len(parts) < 4:
            return None
        return {
            "type": "VAR",
            "name": parts[1],
            "varType": parts[2],
            "value": "|".join(parts[3:]),
        }

    if msg_type == "HB":
        if len(parts) < 4:
            return None
        return {
            "type": "HB",
            "tick": _int(parts[1]),
            "freeChip": _int(parts[2]),
            "freeFast": _int(parts[3]),
            "timestamp": now,
        }

    if msg_type == "CMD":
        if len(parts) < 4:
            return None
        return {
            "type": "CMD",
            "id": _int(parts[1]),
            "status": parts[2],
            "data": "|".join(parts[3:]),
        }

    if msg_type == "READY":
        return {"type": "READY", "version": parts[1] if len(parts) > 1 else "unknown"}

    if msg_type == "CLIENTS":
        count = _int(parts[1]) if len(parts) > 1 else 0
        names = [s.strip() for s in parts[2].split(",") if s.strip()] if len(parts) > 2 and parts[2] else []
        return {"type": "CLIENTS", "count": count, "names": names}

    if msg_type == "TASKS":
        count = _int(parts[1]) if len(parts) > 1 else 0
        tasks: list[dict] = []
        if len(parts) > 2 and parts[2]:
            # Format: name1(pri1,state1),name2(pri2,state2),...
            raw = "|".join(parts[2:])
            tasks = _parse_task_entries(raw)
        return {"type": "TASKS", "count": count, "tasks": tasks}

    if msg_type == "LIBS":
        count = _int(parts[1]) if len(parts) > 1 else 0
        libs: list[dict] = []
        if len(parts) > 2 and parts[2]:
            # Format: name1(v1.r1),name2(v2.r2),...
            for entry in "|".join(parts[2:]).split(","):
                entry = entry.strip()
                if "(" in entry:
                    name = entry[:entry.index("(")]
                    ver = entry[entry.index("(") + 1:].rstrip(")")
                    libs.append({"name": name, "version": ver})
                elif entry:
                    libs.append({"name": entry, "version": ""})
        return {"type": "LIBS", "count": count, "libs": libs}

    if msg_type == "DIR":
        dir_path = parts[1] if len(parts) > 1 else ""
        count = _int(parts[2]) if len(parts) > 2 else 0
        entries: list[dict] = []
        if len(parts) > 3 and parts[3]:
            # Format: name1(size1,type1),name2(size2,type2),...
            # type: D=dir, F=file
            raw = "|".join(parts[3:])
            for entry in raw.split("),"):
                entry = entry.strip().rstrip(")")
                if "(" in entry:
                    name = entry[:entry.index("(")]
                    info = entry[entry.index("(") + 1:]
                    info_parts = info.split(",")
                    size = _int(info_parts[0]) if info_parts else 0
                    etype = info_parts[1].strip() if len(info_parts) > 1 else "F"
                    entries.append({
                        "name": name,
                        "size": size,
                        "type": "dir" if etype == "D" else "file",
                    })
                elif entry:
                    entries.append({"name": entry, "size": 0, "type": "file"})
        return {"type": "DIR", "path": dir_path, "count": count, "entries": entries}

    if msg_type == "FILE":
        return {
            "type": "FILE",
            "path": parts[1] if len(parts) > 1 else "",
            "size": _int(parts[2]) if len(parts) > 2 else 0,
            "offset": _int(parts[3]) if len(parts) > 3 else 0,
            "hexData": parts[4] if len(parts) > 4 else "",
        }

    if msg_type == "FILEINFO":
        return {
            "type": "FILEINFO",
            "path": parts[1] if len(parts) > 1 else "",
            "size": _int(parts[2]) if len(parts) > 2 else 0,
            "date": parts[3] if len(parts) > 3 else "",
            "prot": parts[4] if len(parts) > 4 else "",
        }

    if msg_type == "PROC":
        return {
            "type": "PROC",
            "id": _int(parts[1]) if len(parts) > 1 else 0,
            "status": parts[2] if len(parts) > 2 else "",
            "output": "|".join(parts[3:]) if len(parts) > 3 else "",
        }

    if msg_type == "CLOG":
        if len(parts) < 5:
            return None
        return {
            "type": "CLOG",
            "client": parts[1],
            "level": parts[2],
            "tick": _int(parts[3]),
            "message": "|".join(parts[4:]),
            "timestamp": now,
        }

    if msg_type == "CVAR":
        if len(parts) < 5:
            return None
        return {
            "type": "CVAR",
            "client": parts[1],
            "name": parts[2],
            "varType": parts[3],
            "value": "|".join(parts[4:]),
        }

    if msg_type == "VOLUMES":
        count = _int(parts[1]) if len(parts) > 1 else 0
        names = [s.strip() for s in parts[2].split(",") if s.strip()] if len(parts) > 2 and parts[2] else []
        return {"type": "VOLUMES", "count": count, "names": names}

    if msg_type == "PONG":
        return {
            "type": "PONG",
            "clientCount": _int(parts[1]) if len(parts) > 1 else 0,
            "freeChip": _int(parts[2]) if len(parts) > 2 else 0,
            "freeFast": _int(parts[3]) if len(parts) > 3 else 0,
            "timestamp": now,
        }

    if msg_type == "OK":
        return {
            "type": "OK",
            "context": parts[1] if len(parts) > 1 else "",
            "message": "|".join(parts[2:]) if len(parts) > 2 else "",
        }

    if msg_type == "ERR":
        return {
            "type": "ERR",
            "context": parts[1] if len(parts) > 1 else "",
            "message": "|".join(parts[2:]) if len(parts) > 2 else "",
        }

    if msg_type == "HOOKS":
        # Format: HOOKS|client|count|name1:desc1,name2:desc2,...
        client = parts[1] if len(parts) > 1 else ""
        count = _int(parts[2]) if len(parts) > 2 else 0
        hooks: list[dict] = []
        if len(parts) > 3 and parts[3]:
            for entry in parts[3].split(","):
                entry = entry.strip()
                if ":" in entry:
                    hname, hdesc = entry.split(":", 1)
                    hooks.append({"name": hname, "description": hdesc})
                elif entry:
                    hooks.append({"name": entry, "description": ""})
        return {"type": "HOOKS", "client": client, "count": count, "hooks": hooks}

    if msg_type == "MEMREGS":
        # Format: MEMREGS|client|count|name1:addr:size:desc,...
        client = parts[1] if len(parts) > 1 else ""
        count = _int(parts[2]) if len(parts) > 2 else 0
        memregs: list[dict] = []
        if len(parts) > 3 and parts[3]:
            for entry in parts[3].split(","):
                entry = entry.strip()
                mparts = entry.split(":", 3)
                if len(mparts) >= 3:
                    memregs.append({
                        "name": mparts[0],
                        "address": mparts[1],
                        "size": _int(mparts[2]),
                        "description": mparts[3] if len(mparts) > 3 else "",
                    })
                elif entry:
                    memregs.append({"name": entry, "address": "0", "size": 0, "description": ""})
        return {"type": "MEMREGS", "client": client, "count": count, "memregs": memregs}

    if msg_type == "CINFO":
        # Format: CINFO|name|id|msgs|vars:v1(type),v2(type)|hooks:h1,h2|memregs:m1(addr,sz),m2
        client = parts[1] if len(parts) > 1 else ""
        cid = _int(parts[2]) if len(parts) > 2 else 0
        msgs = _int(parts[3]) if len(parts) > 3 else 0
        info: dict[str, Any] = {"type": "CINFO", "client": client, "id": cid, "msgCount": msgs}
        # Parse remaining sections
        for i in range(4, len(parts)):
            section = parts[i]
            if section.startswith("vars:"):
                info["vars"] = [v.strip() for v in section[5:].split(",") if v.strip()]
            elif section.startswith("hooks:"):
                info["hooks"] = [h.strip() for h in section[6:].split(",") if h.strip()]
            elif section.startswith("memregs:"):
                # Split on commas NOT inside parentheses: ball_state(0036647C,20)
                raw = section[8:]
                entries: list[str] = []
                depth = 0
                start = 0
                for ci, ch in enumerate(raw):
                    if ch == '(':
                        depth += 1
                    elif ch == ')':
                        depth -= 1
                    elif ch == ',' and depth == 0:
                        entries.append(raw[start:ci].strip())
                        start = ci + 1
                if start < len(raw):
                    entries.append(raw[start:].strip())
                info["memregs"] = [e for e in entries if e]
        return info

    if msg_type == "DEVICES":
        count = _int(parts[1]) if len(parts) > 1 else 0
        devs: list[dict] = []
        if len(parts) > 2 and parts[2]:
            for entry in "|".join(parts[2:]).split(","):
                entry = entry.strip()
                if "(" in entry:
                    name = entry[:entry.index("(")]
                    ver = entry[entry.index("(") + 1:].rstrip(")")
                    devs.append({"name": name, "version": ver})
                elif entry:
                    devs.append({"name": entry, "version": ""})
        return {"type": "DEVICES", "count": count, "devices": devs}

    if msg_type == "CDBG":
        # Debug dump from client_debug_dump - pass through as LOG
        return {
            "type": "LOG",
            "level": "D",
            "tick": 0,
            "message": line,
            "timestamp": datetime.now(timezone.utc).isoformat(),
        }

    return None


def format_command(cmd: dict[str, Any]) -> str:
    """Format a host command dict into a protocol line string."""
    t = cmd["type"]
    if t == "PING":
        return "PING"
    if t == "GETVAR":
        return f"GETVAR|{cmd['name']}"
    if t == "SETVAR":
        return f"SETVAR|{cmd['name']}|{cmd['value']}"
    if t == "INSPECT":
        return f"INSPECT|{cmd['address']}|{cmd['size']}"
    if t == "EXEC":
        return f"EXEC|{cmd['id']}|{cmd['expression']}"
    if t == "LISTCLIENTS":
        return "LISTCLIENTS"
    if t == "LISTTASKS":
        return "LISTTASKS"
    if t == "LISTLIBS":
        return "LISTLIBS"
    if t == "LISTDEVS":
        return "LISTDEVS"
    if t == "LISTVOLUMES":
        return "LISTVOLUMES"
    if t == "LISTDIR":
        return f"LISTDIR|{cmd['path']}"
    if t == "READFILE":
        return f"READFILE|{cmd['path']}|{cmd['offset']}|{cmd['size']}"
    if t == "WRITEFILE":
        return f"WRITEFILE|{cmd['path']}|{cmd['offset']}|{cmd['hexData']}"
    if t == "FILEINFO":
        return f"FILEINFO|{cmd['path']}"
    if t == "DELETEFILE":
        return f"DELETEFILE|{cmd['path']}"
    if t == "MAKEDIR":
        return f"MAKEDIR|{cmd['path']}"
    if t == "LAUNCH":
        return f"LAUNCH|{cmd['id']}|{cmd['command']}"
    if t == "DOSCOMMAND":
        return f"DOSCOMMAND|{cmd['id']}|{cmd['command']}"
    if t == "RUN":
        return f"RUN|{cmd['id']}|{cmd['command']}"
    if t == "BREAK":
        return f"BREAK|{cmd['name']}"
    if t == "LISTHOOKS":
        return f"LISTHOOKS|{cmd.get('client', '')}"
    if t == "CALLHOOK":
        return f"CALLHOOK|{cmd['id']}|{cmd['client']}|{cmd['hook']}|{cmd.get('args', '')}"
    if t == "LISTMEMREGS":
        return f"LISTMEMREGS|{cmd.get('client', '')}"
    if t == "READMEMREG":
        return f"READMEMREG|{cmd['client']}|{cmd['region']}"
    if t == "CLIENTINFO":
        return f"CLIENTINFO|{cmd['client']}"
    if t == "STOP":
        return f"STOP|{cmd['name']}"
    if t == "SCRIPT":
        return f"SCRIPT|{cmd['id']}|{cmd['script']}"
    if t == "WRITEMEM":
        return f"WRITEMEM|{cmd['address']}|{cmd['hexData']}"
    if t == "SHUTDOWN":
        return "SHUTDOWN"
    raise ValueError(f"Unknown command type: {t}")


def hex_to_ascii(hex_str: str) -> str:
    """Convert hex string to ASCII, replacing non-printable bytes with '.'."""
    ascii_chars = []
    for i in range(0, len(hex_str), 2):
        byte = int(hex_str[i:i + 2], 16)
        ascii_chars.append(chr(byte) if 32 <= byte < 127 else ".")
    return "".join(ascii_chars)


def format_hex_dump(address: str, hex_data: str) -> str:
    """Format a hex dump with address, hex bytes, and ASCII columns."""
    lines = []
    addr = int(address, 16)
    for i in range(0, len(hex_data), 32):
        chunk = hex_data[i:i + 32]
        offset = addr + i // 2
        # Format hex bytes with spaces
        hex_bytes = " ".join(chunk[j:j + 2] for j in range(0, len(chunk), 2))
        ascii_part = hex_to_ascii(chunk)
        lines.append(f"{offset:08x}  {hex_bytes:<48}  {ascii_part}")
    return "\n".join(lines)


def _parse_task_entries(raw: str) -> list[dict[str, Any]]:
    """Parse task list format: name1(pri1,state1,type1),name2(pri2,state2,type2),..."""
    tasks = []
    for entry in raw.split("),"):
        entry = entry.strip().rstrip(")")
        if "(" in entry:
            name = entry[:entry.index("(")]
            info = entry[entry.index("(") + 1:]
            info_parts = info.split(",")
            pri = _int(info_parts[0]) if info_parts else 0
            task_state = info_parts[1].strip() if len(info_parts) > 1 else "?"
            task_type = info_parts[2].strip() if len(info_parts) > 2 else "task"
            tasks.append({"name": name, "priority": pri, "state": task_state, "type": task_type})
        elif entry:
            tasks.append({"name": entry, "priority": 0, "state": "?", "type": "task"})
    return tasks


def _int(s: str) -> int:
    try:
        return int(s, 10)
    except (ValueError, TypeError):
        return 0
