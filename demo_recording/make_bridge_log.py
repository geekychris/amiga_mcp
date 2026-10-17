#!/usr/bin/env python3
"""make_bridge_log.py - turn devbench's persistent log slice into a readable
bridge wire transcript for the demo.

  logs/bridge_protocol.log            every host<->Amiga line (minus heartbeats,
                                      bulk screenshot rows and fps chatter)
  logs/bridge_protocol_highlights.md  the first exchange of each command type,
                                      plus every game event line, for narration
"""
import re, datetime, collections, sys, os
D = os.path.dirname(os.path.abspath(__file__))
src = os.path.join(D, "logs", "devbench_session_full.log")
rx_tx = re.compile(r'^ts=(\S+) event=(tx|rx) (.*)$')
SKIP_RX = ("msg_type=HB ", "msg_type=SCRDATA", "msg_type=SCRRLE", "msg_type=SCRRGB", "fps=")

def local(ts):
    t = datetime.datetime.fromisoformat(ts.replace("Z", "+00:00")).astimezone()
    return t.strftime("%H:%M:%S.") + f"{t.microsecond // 1000:03d}"

def wire(kind, rest):
    if kind == "tx":
        m = re.match(r'cmd=(\S+)(?: payload=(.*))?', rest)
        cmd, payload = (m.group(1), m.group(2) or "") if m else (rest, "")
        return cmd, f"{cmd}|{payload.strip(chr(34))}" if payload else cmd
    m = re.match(r'msg_type=(\S+)(?: payload=(.*))?', rest)
    typ, payload = (m.group(1), m.group(2) or "") if m else (rest, "")
    return typ, f"{typ}|{payload.strip(chr(34))}" if payload else typ

out, first = [], collections.OrderedDict()
events = []
pending = None
for line in open(src, encoding="utf-8", errors="replace"):
    m = rx_tx.match(line.rstrip("\n"))
    if not m:
        continue
    ts, kind, rest = m.groups()
    if kind == "rx" and any(s in rest for s in SKIP_RX):
        continue
    typ, text = wire(kind, rest)
    arrow = "HOST  -> AMIGA" if kind == "tx" else "AMIGA -> HOST "
    row = f"{local(ts)}  {arrow}  {text}"
    out.append(row)
    if kind == "tx":
        if typ not in first:
            first[typ] = [row]
            pending = typ
        else:
            pending = None
    elif pending and len(first[pending]) < 4:
        first[pending].append(row)
    if kind == "rx" and typ == "CLOG" and "terra_strike|" in text and any(
            k in text for k in ("TAKEOFF", "LANDED", "KILL", "HIT", "CRASH", "LEVEL", "GAME OVER",
                                "SCENE", "SELFTEST", "STATE", "starting", "TEST_BEGIN", "TEST_END")):
        events.append(row)

open(os.path.join(D, "logs", "bridge_protocol.log"), "w").write("\n".join(out) + "\n")
with open(os.path.join(D, "logs", "bridge_protocol_highlights.md"), "w") as f:
    f.write("# AmigaBridge wire protocol - Terra Strike demo session\n\n")
    f.write("Line-based, pipe-delimited protocol between devbench (host) and the\n"
            "amiga-bridge daemon (FS-UAE serial port tunnelled over TCP :2345).\n"
            "Source: `logs/devbench.log` (devbench persistent log), this session only.\n\n")
    f.write(f"{sum(1 for r in out if 'HOST  ->' in r)} commands sent, "
            f"{sum(1 for r in out if 'AMIGA ->' in r)} replies/events received (heartbeats excluded).\n\n")
    f.write("## First exchange of each command\n\n```\n")
    for typ, rows in first.items():
        f.write("\n".join(r[:200] for r in rows) + "\n\n")
    f.write("```\n\n## Game events streamed from the Amiga (remote logging)\n\n```\n")
    f.write("\n".join(r[:200] for r in events) + "\n```\n")
print(len(out), "wire lines;", len(first), "command types;", len(events), "game events")
