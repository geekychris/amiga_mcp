#!/usr/bin/env python3
"""make_transcript.py <session.jsonl> - timestamped transcript of the Claude Code
session (prompt, Claude's messages, every tool call with a short result) as
logs/claude_transcript.md and logs/claude_transcript.jsonl (for captions)."""
import json, sys, os, re, datetime
D = os.path.dirname(os.path.abspath(__file__))
rows = []

def local(ts):
    return datetime.datetime.fromisoformat(ts.replace("Z", "+00:00")).astimezone()

def short(s, n):
    s = s.strip()
    return s if len(s) <= n else s[:n] + " ..."

for line in open(sys.argv[1]):
    try:
        e = json.loads(line)
    except ValueError:
        continue
    if e.get("type") not in ("user", "assistant") or "timestamp" not in e or e.get("isSidechain"):
        continue
    t = local(e["timestamp"])
    content = e.get("message", {}).get("content")
    if isinstance(content, str):
        content = [{"type": "text", "text": content}]
    for c in content or []:
        k = c.get("type")
        if e["type"] == "user" and k == "text" and not c["text"].startswith("<"):
            rows.append((t, "prompt", c["text"]))
        elif e["type"] == "user" and k == "text" and "<pasted_content" in c["text"]:
            rows.append((t, "prompt", c["text"].split(">", 1)[1].rsplit("</pasted_content", 1)[0]))
        elif e["type"] == "assistant" and k == "text" and c["text"].strip():
            rows.append((t, "claude", c["text"]))
        elif e["type"] == "assistant" and k == "tool_use":
            inp = c.get("input", {})
            desc = inp.get("command") or inp.get("file_path") or inp.get("skill") or json.dumps(inp)[:300]
            rows.append((t, "tool:" + c["name"], desc))
        elif e["type"] == "user" and k == "tool_result":
            r = c.get("content")
            if isinstance(r, list):
                r = "\n".join(x.get("text", "[image]") if x.get("type") == "text" else "[image]" for x in r)
            rows.append((t, "result", str(r or "")))

t0 = rows[0][0]
with open(os.path.join(D, "logs", "claude_transcript.md"), "w") as f, \
     open(os.path.join(D, "logs", "claude_transcript.jsonl"), "w") as j:
    f.write("# Claude Code session transcript - Terra Strike demo\n\n"
            "Times are local wall clock and `+mm:ss` from the prompt.\n\n")
    for t, kind, text in rows:
        rel = int((t - t0).total_seconds())
        stamp = f"{t:%H:%M:%S} +{rel // 60:02d}:{rel % 60:02d}"
        j.write(json.dumps({"t": t.timestamp(), "iso": t.isoformat(), "rel_s": rel, "kind": kind, "text": text}) + "\n")
        if kind == "prompt":
            text = re.sub(r"</?pasted_content[^>]*>", "", text)
            f.write(f"## {stamp}  USER PROMPT\n\n{text.strip()}\n\n")
        elif kind == "claude":
            f.write(f"**{stamp}  Claude:** {text.strip()}\n\n")
        elif kind == "result":
            f.write("```\n" + short(text, 900) + "\n```\n\n")
        else:
            f.write(f"`{stamp}`  **{kind[5:]}**\n```\n{short(text, 600)}\n```\n")
print(len(rows), "rows; prompt at", t0.strftime("%H:%M:%S"))
