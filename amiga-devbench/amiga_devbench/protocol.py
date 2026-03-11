"""Debug protocol message parsing and formatting.

Line-based text protocol over serial/TCP, pipe-delimited fields.

Amiga -> Host: LOG, MEM, VAR, HB, CMD, READY, CLIENTS, TASKS, LIBS,
               DIR, FILE, FILEINFO, PROC, CLOG, CVAR, HOOKS, MEMREGS,
               CINFO, DEVICES, ERR, OK, SCRINFO, SCRDATA, PALETTE,
               COPPER, SPRITE, CRASH, RESOURCES, PERF, MEMMAP,
               STACKINFO, CHIPREGS, REGS, SEARCH, LIBINFO, DEVINFO,
               SNOOP, SNOOPSTATE, LIBFUNCS
Host -> Amiga: PING, GETVAR, SETVAR, INSPECT, EXEC, LISTCLIENTS,
               LISTTASKS, LISTLIBS, LISTDEVS, LISTDIR, READFILE,
               WRITEFILE, FILEINFO, DELETEFILE, MAKEDIR, LAUNCH,
               DOSCOMMAND, RUN, BREAK, LISTHOOKS, CALLHOOK,
               LISTMEMREGS, READMEMREG, CLIENTINFO, STOP, SCRIPT,
               WRITEMEM, SHUTDOWN, SCREENSHOT, PALETTE, SETPALETTE,
               COPPERLIST, SPRITES, LISTRESOURCES, GETPERF, LASTCRASH,
               LISTWINDOWS, MEMMAP, STACKINFO, CHIPREGS, READREGS,
               SEARCH, LIBINFO, DEVINFO, LIBFUNCS, SNOOPSTART,
               SNOOPSTOP, SNOOPSTATUS
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

    if msg_type == "SCRINFO":
        # Format: SCRINFO|width|height|depth|r0g0b0,r1g1b1,...
        if len(parts) < 5:
            return None
        return {
            "type": "SCRINFO",
            "width": _int(parts[1]),
            "height": _int(parts[2]),
            "depth": _int(parts[3]),
            "palette": parts[4],
        }

    if msg_type == "SCRDATA":
        # Format: SCRDATA|row|plane|hex_data
        if len(parts) < 4:
            return None
        return {
            "type": "SCRDATA",
            "row": _int(parts[1]),
            "plane": _int(parts[2]),
            "hexData": parts[3],
        }

    if msg_type == "PALETTE":
        # Format: PALETTE|depth|r0g0b0,r1g1b1,...
        if len(parts) < 3:
            return None
        return {
            "type": "PALETTE",
            "depth": _int(parts[1]),
            "palette": parts[2],
        }

    if msg_type == "COPPER":
        # Format: COPPER|addr_hex|count|hex_data
        if len(parts) < 4:
            return None
        return {
            "type": "COPPER",
            "address": parts[1],
            "count": _int(parts[2]),
            "hexData": parts[3],
        }

    if msg_type == "SPRITE":
        # Format: SPRITE|id|vstart|vstop|hstart|attached|hex_data
        if len(parts) < 7:
            return None
        return {
            "type": "SPRITE",
            "id": _int(parts[1]),
            "vstart": _int(parts[2]),
            "vstop": _int(parts[3]),
            "hstart": _int(parts[4]),
            "attached": _int(parts[5]) != 0,
            "hexData": parts[6],
        }

    if msg_type == "CRASH":
        # Format: CRASH|alert_hex|alert_name|D0:D1:...:D7|A0:A1:...:A7|SP|stack_hex
        return {
            "type": "CRASH",
            "alertNum": parts[1] if len(parts) > 1 else "00000000",
            "alertName": parts[2] if len(parts) > 2 else "Unknown",
            "dataRegs": parts[3].split(":") if len(parts) > 3 else [],
            "addrRegs": parts[4].split(":") if len(parts) > 4 else [],
            "sp": parts[5] if len(parts) > 5 else "00000000",
            "stackHex": parts[6] if len(parts) > 6 else "",
            "timestamp": now,
        }

    if msg_type == "RESOURCES":
        # Format: RESOURCES|client|count|type:tag:ptr:size:state,...
        client = parts[1] if len(parts) > 1 else ""
        raw_data = parts[2] if len(parts) > 2 else ""
        # The client data format is "count|entries"
        res_parts = raw_data.split("|", 1)
        count = _int(res_parts[0]) if res_parts else 0
        resources: list[dict] = []
        entries_raw = res_parts[1] if len(res_parts) > 1 else ""
        if entries_raw:
            for entry in entries_raw.split(","):
                entry = entry.strip()
                if not entry:
                    continue
                rp = entry.split(":", 4)
                if len(rp) >= 5:
                    resources.append({
                        "type": rp[0],
                        "tag": rp[1],
                        "ptr": rp[2],
                        "size": _int(rp[3]),
                        "state": rp[4],
                    })
                elif entry:
                    resources.append({"type": "?", "tag": entry, "ptr": "0", "size": 0, "state": "?"})
        return {"type": "RESOURCES", "client": client, "count": count, "resources": resources}

    if msg_type == "PERF":
        # Format: PERF|client|frame_avg|frame_min|frame_max|frame_count|section1:avg:min:max:count,...
        client = parts[1] if len(parts) > 1 else ""
        raw_data = parts[2] if len(parts) > 2 else ""
        # Client data format: frame_avg|frame_min|frame_max|frame_count|sections...
        perf_parts = raw_data.split("|")
        frame_avg = _int(perf_parts[0]) if len(perf_parts) > 0 else 0
        frame_min = _int(perf_parts[1]) if len(perf_parts) > 1 else 0
        frame_max = _int(perf_parts[2]) if len(perf_parts) > 2 else 0
        frame_count = _int(perf_parts[3]) if len(perf_parts) > 3 else 0
        sections: list[dict] = []
        if len(perf_parts) > 4 and perf_parts[4]:
            for entry in perf_parts[4].split(","):
                entry = entry.strip()
                if not entry:
                    continue
                sp = entry.split(":", 4)
                if len(sp) >= 5:
                    sections.append({
                        "label": sp[0],
                        "avg": _int(sp[1]),
                        "min": _int(sp[2]),
                        "max": _int(sp[3]),
                        "count": _int(sp[4]),
                    })
        return {
            "type": "PERF",
            "client": client,
            "frameAvg": frame_avg,
            "frameMin": frame_min,
            "frameMax": frame_max,
            "frameCount": frame_count,
            "sections": sections,
        }

    if msg_type == "WINLIST":
        # Format: WINLIST|title1|title2|...
        windows = parts[1:] if len(parts) > 1 else []
        return {"type": "WINLIST", "windows": windows}

    if msg_type == "CDBG":
        # Debug dump from client_debug_dump - pass through as LOG
        return {
            "type": "LOG",
            "level": "D",
            "tick": 0,
            "message": line,
            "timestamp": datetime.now(timezone.utc).isoformat(),
        }

    if msg_type == "MEMMAP":
        # Format: MEMMAP|count|name:attr:lower:upper:free:largest|name:attr:...
        count = _int(parts[1]) if len(parts) > 1 else 0
        regions: list[dict] = []
        for i in range(2, len(parts)):
            entry = parts[i].strip()
            if not entry:
                continue
            rp = entry.split(":", 5)
            if len(rp) >= 6:
                regions.append({
                    "name": rp[0],
                    "attr": rp[1],
                    "lower": rp[2],
                    "upper": rp[3],
                    "free": _int(rp[4]),
                    "largest": _int(rp[5]),
                })
            elif entry:
                regions.append({"name": entry, "attr": "", "lower": "0", "upper": "0", "free": 0, "largest": 0})
        return {"type": "MEMMAP", "count": count, "regions": regions}

    if msg_type == "STACKINFO":
        # Format: STACKINFO|taskname|spLower|spUpper|spReg|stackSize|stackUsed|stackFree
        if len(parts) < 8:
            return None
        return {
            "type": "STACKINFO",
            "task": parts[1],
            "spLower": parts[2],
            "spUpper": parts[3],
            "spReg": parts[4],
            "size": _int(parts[5]),
            "used": _int(parts[6]),
            "free": _int(parts[7]),
        }

    if msg_type == "CHIPREGS":
        # Format: CHIPREGS|count|name:addr:value|name:addr:value|...
        count = _int(parts[1]) if len(parts) > 1 else 0
        chipreg_list: list[dict[str, str]] = []
        for i in range(2, len(parts)):
            entry = parts[i].strip()
            if ":" in entry:
                fields = entry.split(":", 2)
                if len(fields) == 3:
                    chipreg_list.append({
                        "name": fields[0],
                        "addr": fields[1],
                        "value": fields[2],
                    })
        return {"type": "CHIPREGS", "count": count, "registers": chipreg_list}

    if msg_type == "REGS":
        # Format: REGS|D0=xxxxxxxx|D1=xxxxxxxx|...|SP=xxxxxxxx|SR=xxxx
        registers = {}
        for i in range(1, len(parts)):
            entry = parts[i].strip()
            if "=" in entry:
                rname, rval = entry.split("=", 1)
                registers[rname] = rval
        return {"type": "REGS", "registers": registers}

    if msg_type == "SEARCH":
        # Format: SEARCH|count|addr1,addr2,...
        count = _int(parts[1]) if len(parts) > 1 else 0
        addresses: list[str] = []
        if len(parts) > 2 and parts[2]:
            addresses = [a.strip() for a in parts[2].split(",") if a.strip()]
        return {"type": "SEARCH", "count": count, "addresses": addresses}

    if msg_type == "LIBINFO":
        # Format: LIBINFO|name|version|revision|openCnt|flags|negSize|posSize|baseAddr|idString
        if len(parts) < 10:
            return None
        return {
            "type": "LIBINFO",
            "name": parts[1],
            "version": _int(parts[2]),
            "revision": _int(parts[3]),
            "openCnt": _int(parts[4]),
            "flags": _int(parts[5]),
            "negSize": _int(parts[6]),
            "posSize": _int(parts[7]),
            "baseAddr": parts[8],
            "idString": "|".join(parts[9:]),  # idString may contain pipes
        }

    if msg_type == "DEVINFO":
        # Format: DEVINFO|name|version|revision|openCnt|flags|negSize|posSize|baseAddr|idString
        if len(parts) < 10:
            return None
        return {
            "type": "DEVINFO",
            "name": parts[1],
            "version": _int(parts[2]),
            "revision": _int(parts[3]),
            "openCnt": _int(parts[4]),
            "flags": _int(parts[5]),
            "negSize": _int(parts[6]),
            "posSize": _int(parts[7]),
            "baseAddr": parts[8],
            "idString": "|".join(parts[9:]),
        }

    if msg_type == "SNOOP":
        # Format: SNOOP|func|caller_hex|arg1|arg2|result|timestamp_tick
        return {
            "type": "SNOOP",
            "func": parts[1] if len(parts) > 1 else "",
            "caller": parts[2] if len(parts) > 2 else "",
            "arg1": parts[3] if len(parts) > 3 else "",
            "arg2": parts[4] if len(parts) > 4 else "",
            "result": parts[5] if len(parts) > 5 else "",
            "tick": _int(parts[6]) if len(parts) > 6 else 0,
            "timestamp": now,
        }

    if msg_type == "SNOOPSTATE":
        # Format: SNOOPSTATE|ON/OFF|eventCount|dropCount|buffered
        return {
            "type": "SNOOPSTATE",
            "active": parts[1] == "ON" if len(parts) > 1 else False,
            "eventCount": _int(parts[2]) if len(parts) > 2 else 0,
            "dropCount": _int(parts[3]) if len(parts) > 3 else 0,
            "buffered": _int(parts[4]) if len(parts) > 4 else 0,
        }

    if msg_type == "LIBFUNCS":
        # Format: LIBFUNCS|name|totalFuncs|startIdx|count|lvo1:addr1,lvo2:addr2,...
        name = parts[1] if len(parts) > 1 else ""
        total = _int(parts[2]) if len(parts) > 2 else 0
        start = _int(parts[3]) if len(parts) > 3 else 0
        count = _int(parts[4]) if len(parts) > 4 else 0
        funcs = []
        if len(parts) > 5 and parts[5]:
            for entry in parts[5].split(","):
                entry = entry.strip()
                if ":" in entry:
                    lvo, addr = entry.split(":", 1)
                    funcs.append({"lvo": _int(lvo), "addr": addr})
        return {"type": "LIBFUNCS", "name": name, "totalFuncs": total, "startIdx": start, "count": count, "funcs": funcs}

    if msg_type == "AUDIOCHANNELS":
        # Format: AUDIOCHANNELS|dmaEnabled|intReq|intEna
        return {
            "type": "AUDIOCHANNELS",
            "dmaEnabled": parts[1] if len(parts) > 1 else "0",
            "intReq": parts[2] if len(parts) > 2 else "0",
            "intEna": parts[3] if len(parts) > 3 else "0",
        }

    if msg_type == "AUDIOSAMPLE":
        # Format: AUDIOSAMPLE|addr|size|hexdata
        return {
            "type": "AUDIOSAMPLE",
            "address": parts[1] if len(parts) > 1 else "0",
            "size": _int(parts[2]) if len(parts) > 2 else 0,
            "hexData": parts[3] if len(parts) > 3 else "",
        }

    if msg_type == "SCREENS":
        # Format: SCREENS|count|title:w:h:depth:viewmodes:flags:addr,...
        count = _int(parts[1]) if len(parts) > 1 else 0
        screens = []
        if len(parts) > 2 and parts[2]:
            for entry in parts[2].split(","):
                fields = entry.split(":")
                if len(fields) >= 7:
                    screens.append({
                        "title": fields[0],
                        "width": _int(fields[1]),
                        "height": _int(fields[2]),
                        "depth": _int(fields[3]),
                        "viewModes": fields[4],
                        "flags": fields[5],
                        "addr": fields[6],
                    })
        return {"type": "SCREENS", "count": count, "screens": screens}

    if msg_type == "WINDOWS":
        # Format: WINDOWS|screenAddr|count|title:l:t:w:h:flags:idcmp:addr,...
        scrAddr = parts[1] if len(parts) > 1 else "0"
        count = _int(parts[2]) if len(parts) > 2 else 0
        windows = []
        if len(parts) > 3 and parts[3]:
            for entry in parts[3].split(","):
                fields = entry.split(":")
                if len(fields) >= 8:
                    windows.append({
                        "title": fields[0],
                        "left": _int(fields[1]),
                        "top": _int(fields[2]),
                        "width": _int(fields[3]),
                        "height": _int(fields[4]),
                        "flags": fields[5],
                        "idcmp": fields[6],
                        "addr": fields[7],
                    })
        return {"type": "WINDOWS", "screenAddr": scrAddr, "count": count, "windows": windows}

    if msg_type == "GADGETS":
        # Format: GADGETS|windowAddr|count|id:l:t:w:h:type:flags:text:addr,...
        winAddr = parts[1] if len(parts) > 1 else "0"
        count = _int(parts[2]) if len(parts) > 2 else 0
        gadgets = []
        if len(parts) > 3 and parts[3]:
            for entry in parts[3].split(","):
                fields = entry.split(":")
                if len(fields) >= 9:
                    gadgets.append({
                        "id": _int(fields[0]),
                        "left": _int(fields[1]),
                        "top": _int(fields[2]),
                        "width": _int(fields[3]),
                        "height": _int(fields[4]),
                        "gadgetType": _int(fields[5]),
                        "flags": _int(fields[6]),
                        "text": fields[7],
                        "addr": fields[8],
                    })
        return {"type": "GADGETS", "windowAddr": winAddr, "count": count, "gadgets": gadgets}

    if msg_type == "TEST_BEGIN":
        return {"type": "TEST_BEGIN", "suite": parts[1] if len(parts) > 1 else "unknown"}

    if msg_type == "TEST_PASS":
        return {
            "type": "TEST_PASS",
            "testName": parts[1] if len(parts) > 1 else "?",
            "file": parts[2] if len(parts) > 2 else "?",
            "line": _int(parts[3]) if len(parts) > 3 else 0,
        }

    if msg_type == "TEST_FAIL":
        return {
            "type": "TEST_FAIL",
            "testName": parts[1] if len(parts) > 1 else "?",
            "file": parts[2] if len(parts) > 2 else "?",
            "line": _int(parts[3]) if len(parts) > 3 else 0,
        }

    if msg_type == "TEST_END":
        return {
            "type": "TEST_END",
            "suite": parts[1] if len(parts) > 1 else "unknown",
            "passed": _int(parts[2]) if len(parts) > 2 else 0,
            "failed": _int(parts[3]) if len(parts) > 3 else 0,
            "total": _int(parts[4]) if len(parts) > 4 else 0,
        }

    # Font browser
    if msg_type == "FONTS":
        fonts = []
        count = _int(parts[1]) if len(parts) > 1 else 0
        for i in range(2, len(parts)):
            entry = parts[i]
            if ":" in entry:
                name, sizes_str = entry.split(":", 1)
                sizes = [int(s) for s in sizes_str.split(",") if s.isdigit()]
                fonts.append({"name": name, "sizes": sizes})
            else:
                fonts.append({"name": entry, "sizes": []})
        return {"type": "FONTS", "count": count, "fonts": fonts}

    if msg_type == "FONTINFO":
        return {
            "type": "FONTINFO",
            "name": parts[1] if len(parts) > 1 else "",
            "size": _int(parts[2]) if len(parts) > 2 else 0,
            "ysize": _int(parts[3]) if len(parts) > 3 else 0,
            "xsize": _int(parts[4]) if len(parts) > 4 else 0,
            "style": _int(parts[5]) if len(parts) > 5 else 0,
            "flags": _int(parts[6]) if len(parts) > 6 else 0,
            "baseline": _int(parts[7]) if len(parts) > 7 else 0,
        }

    # Custom chip write logger
    if msg_type == "CHIPLOG":
        regs = {}
        for i in range(1, len(parts)):
            if ":" in parts[i]:
                kv = parts[i].split(":")
                if len(kv) >= 2:
                    regs[kv[0]] = kv[1]
        return {"type": "CHIPLOG", "registers": regs}

    if msg_type == "CHIPLOGCHANGE":
        tick = _int(parts[1]) if len(parts) > 1 else 0
        changes = []
        for i in range(2, len(parts)):
            ch = parts[i].split(":")
            if len(ch) >= 3:
                changes.append({"reg": ch[0], "old": ch[1], "new": ch[2]})
        return {"type": "CHIPLOGCHANGE", "tick": tick, "changes": changes}

    # Memory pool tracker
    if msg_type == "POOLS":
        pools = []
        count = _int(parts[1]) if len(parts) > 1 else 0
        for i in range(2, len(parts)):
            fields = parts[i].split(":")
            if len(fields) >= 5:
                pools.append({
                    "address": fields[0],
                    "puddleSize": _int(fields[1]),
                    "threshSize": _int(fields[2]),
                    "allocCount": _int(fields[3]),
                    "totalAlloc": _int(fields[4]),
                })
        return {"type": "POOLS", "count": count, "pools": pools}

    # Clipboard bridge
    if msg_type == "CLIPBOARD":
        length = _int(parts[1]) if len(parts) > 1 else 0
        text = parts[2] if len(parts) > 2 else ""
        return {"type": "CLIPBOARD", "length": length, "text": text}

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
    if t == "SCREENSHOT":
        window = cmd.get("window", "")
        return f"SCREENSHOT|{window}" if window else "SCREENSHOT"
    if t == "PALETTE":
        return "PALETTE"
    if t == "SETPALETTE":
        return f"SETPALETTE|{cmd['index']}|{cmd['rgb']}"
    if t == "COPPERLIST":
        return "COPPERLIST"
    if t == "SPRITES":
        return "SPRITES"
    if t == "LISTRESOURCES":
        return f"LISTRESOURCES|{cmd['client']}"
    if t == "GETPERF":
        return f"GETPERF|{cmd['client']}"
    if t == "LASTCRASH":
        return "LASTCRASH"
    if t == "LISTWINDOWS":
        return "LISTWINDOWS"
    if t == "CRASHINIT":
        return "CRASHINIT"
    if t == "CRASHREMOVE":
        return "CRASHREMOVE"
    if t == "CRASHTEST":
        return "CRASHTEST"
    if t == "MEMMAP":
        return "MEMMAP"
    if t == "STACKINFO":
        return f"STACKINFO|{cmd['task']}"
    if t == "CHIPREGS":
        return "CHIPREGS"
    if t == "READREGS":
        return "READREGS"
    if t == "SEARCH":
        return f"SEARCH|{cmd['address']}|{cmd['size']}|{cmd['pattern']}"
    if t == "LIBINFO":
        return f"LIBINFO|{cmd['name']}"
    if t == "DEVINFO":
        return f"DEVINFO|{cmd['name']}"
    if t == "LIBFUNCS":
        start = cmd.get('start', 0)
        return f"LIBFUNCS|{cmd['name']}|{cmd['libtype']}|{start}"
    if t == "SNOOPSTART":
        return "SNOOPSTART"
    if t == "SNOOPSTOP":
        return "SNOOPSTOP"
    if t == "SNOOPSTATUS":
        return "SNOOPSTATUS"
    if t == "AUDIOCHANNELS":
        return "AUDIOCHANNELS"
    if t == "AUDIOSAMPLE":
        return f"AUDIOSAMPLE|{cmd['address']}|{cmd['size']}"
    if t == "LISTSCREENS":
        return "LISTSCREENS"
    if t == "LISTWINDOWS2":
        return f"LISTWINDOWS2|{cmd.get('screen', '')}"
    if t == "LISTGADGETS":
        return f"LISTGADGETS|{cmd['window']}"
    if t == "INPUTKEY":
        return f"INPUTKEY|{cmd['rawkey']}|{cmd['direction']}"
    if t == "INPUTMOVE":
        return f"INPUTMOVE|{cmd['dx']}|{cmd['dy']}"
    if t == "INPUTCLICK":
        return f"INPUTCLICK|{cmd['button']}|{cmd['direction']}"
    if t == "WINACTIVATE":
        return f"WINACTIVATE|{cmd['window']}"
    if t == "WINTOFRONT":
        return f"WINTOFRONT|{cmd['window']}"
    if t == "WINTOBACK":
        return f"WINTOBACK|{cmd['window']}"
    if t == "WINZIP":
        return f"WINZIP|{cmd['window']}"
    if t == "WINMOVE":
        return f"WINMOVE|{cmd['window']}|{cmd['x']}|{cmd['y']}"
    if t == "WINSIZE":
        return f"WINSIZE|{cmd['window']}|{cmd['width']}|{cmd['height']}"
    if t == "SCRTOFRONT":
        return f"SCRTOFRONT|{cmd['screen']}"
    if t == "SCRTOBACK":
        return f"SCRTOBACK|{cmd['screen']}"
    # Font browser
    if t == "LISTFONTS":
        return "LISTFONTS"
    if t == "FONTINFO":
        return f"FONTINFO|{cmd['name']}|{cmd['size']}"
    # Custom chip write logger
    if t == "CHIPLOGSTART":
        return "CHIPLOGSTART"
    if t == "CHIPLOGSTOP":
        return "CHIPLOGSTOP"
    if t == "CHIPLOGSNAPSHOT":
        return "CHIPLOGSNAPSHOT"
    # Memory pool tracker
    if t == "POOLSTART":
        return "POOLSTART"
    if t == "POOLSTOP":
        return "POOLSTOP"
    if t == "POOLS":
        return "POOLS"
    # Clipboard bridge
    if t == "CLIPGET":
        return "CLIPGET"
    if t == "CLIPSET":
        return f"CLIPSET|{cmd['text']}"
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
