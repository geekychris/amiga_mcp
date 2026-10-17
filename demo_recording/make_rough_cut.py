#!/usr/bin/env python3
"""make_rough_cut.py - build the editing package from the recording.

  video/rough_cut.mp4      1920x1080 draft: CLI pane | FS-UAE window, long
                           stretches sped up, smooth Amiga-only clip at the end
  video/rough_cut.srt      captions from the timeline (phases, bugs, fixes, ...)
  video/clips/*_cli.mp4    per-segment crops of the Claude CLI pane (real time)
  video/clips/*_amiga.mp4  per-segment crops of the FS-UAE window (real time)
  TIMELINE.md              every event: wall clock, elapsed, segment + offset

Screen geometry: desktop 2056x1329 pt captured at 1728 px wide (x0.8405).
Claude CLI pane (cmux, sidebar excluded) and FS-UAE window positions are the
ones set at the start of the session.
"""
import json, os, subprocess, datetime
D = os.path.dirname(os.path.abspath(__file__))
V = os.path.join(D, "video")
os.makedirs(os.path.join(V, "clips"), exist_ok=True)

CLI = "crop=392:908:196:25"            # cmux terminal pane
AMIGA = "crop=860:648:592:57"          # FS-UAE window content
SPEED = {"01": 24, "02": 6, "03": 6, "04": 6, "05": 6, "06": 1.5, "07": 1, "08": 2, "09": 8, "10": 1, "11": 1}
SKIP = {"10"}          # superseded by the 11 re-record (FS-UAE frontmost, final prompt)
CAPTION_KINDS = {"handoff", "showcase2", "phase", "milestone", "bug", "fix", "perf", "test", "showcase", "e2e",
                 "devbench-fix", "devbench-note"}

ev = sorted((json.loads(l) for l in open(os.path.join(D, "timeline.jsonl"))), key=lambda e: e["t"])
t_prompt = datetime.datetime(2026, 10, 16, 18, 31, 45, tzinfo=datetime.timezone.utc).timestamp()  # 11:31:45 PDT
segs = []
for e in ev:
    if e["kind"] == "rec_start":
        name = e["label"]
        ext = ".mov" if name.endswith("amiga-window") else ".mp4"
        path = os.path.join(V, name + ext)
        if os.path.exists(path) and name.split("-")[1] not in SKIP:
            segs.append({"name": name, "path": path, "t0": e["t"], "id": name.split("-")[1]})
for s in segs:
    s["dur"] = float(subprocess.check_output(["ffprobe", "-v", "error", "-show_entries", "format=duration",
                                              "-of", "csv=p=0", s["path"]]).decode())
    s["t1"] = s["t0"] + s["dur"]

def run(args):
    if os.path.exists(args[-1]) and "rough_cut.mp4" not in args[-1]:
        return                                   # already encoded
    subprocess.run(["ffmpeg", "-v", "error", "-y"] + args, check=True)

parts, srt, pos = [], [], 0.0
for s in segs:
    sp = SPEED[s["id"]]
    out = os.path.join(V, "clips", f"{s['name']}_rough.mp4")
    if s["path"].endswith(".mov"):
        vf = (f"scale=1433:1080,pad=1920:1080:(ow-iw)/2:0,setpts=PTS/{sp},fps=30")
        run(["-i", s["path"], "-vf", vf, "-an", "-c:v", "libx264", "-preset", "fast", "-crf", "22", out])
    else:
        fc = (f"[0:v]setpts=PTS/{sp},fps=30,split=2[a][b];[a]{CLI},scale=466:1080[c];"
              f"[b]{AMIGA},scale=1432:1080[g];[c][g]hstack=inputs=2,"
              f"pad=1920:1080:11:0:color=0x101418[v]")
        run(["-i", s["path"], "-filter_complex", fc, "-map", "[v]", "-an",
             "-c:v", "libx264", "-preset", "fast", "-crf", "23", out])
        run(["-i", s["path"], "-vf", f"{CLI},scale=784:-2", "-an", "-c:v", "libx264", "-crf", "23",
             os.path.join(V, "clips", f"{s['name']}_cli.mp4")])
        run(["-i", s["path"], "-vf", f"{AMIGA}", "-an", "-c:v", "libx264", "-crf", "20",
             os.path.join(V, "clips", f"{s['name']}_amiga.mp4")])
    s["rough_start"] = pos
    for e in ev:
        if e["kind"] in CAPTION_KINDS and s["t0"] <= e["t"] < s["t1"]:
            srt.append((pos + (e["t"] - s["t0"]) / sp, e))
    pos += s["dur"] / sp
    parts.append(out)

with open(os.path.join(V, "concat.txt"), "w") as f:
    f.write("".join(f"file '{p}'\n" for p in parts))
run(["-f", "concat", "-safe", "0", "-i", os.path.join(V, "concat.txt"), "-c", "copy",
     os.path.join(V, "rough_cut.mp4")])

def stamp(x):
    ms = int(round(x * 1000))
    return f"{ms // 3600000:02d}:{ms // 60000 % 60:02d}:{ms // 1000 % 60:02d},{ms % 1000:03d}"
with open(os.path.join(V, "rough_cut.srt"), "w") as f:
    for i, (t, e) in enumerate(srt):
        end = srt[i + 1][0] if i + 1 < len(srt) else t + 4
        end = min(max(end, t + 1.5), t + 5)
        f.write(f"{i + 1}\n{stamp(t)} --> {stamp(end)}\n[{e['kind']}] {e['label']}\n\n")

def seg_of(t):
    for s in segs:
        if s["t0"] <= t < s["t1"]:
            return s
    return None
with open(os.path.join(D, "TIMELINE.md"), "w") as f:
    f.write("# Terra Strike demo - timeline\n\n"
            f"Prompt received 11:31:45; last event {datetime.datetime.fromtimestamp(ev[-1]['t']):%H:%M:%S}. "
            "`elapsed` is from the prompt; `seg @` is the offset inside that segment's video "
            "(use it to cut); `rough @` is the position in `video/rough_cut.mp4`.\n\n"
            "## Segments\n\n| segment | file | wall clock | length | rough-cut speed |\n|---|---|---|---|---|\n")
    for s in segs:
        f.write(f"| {s['id']} | `video/{os.path.basename(s['path'])}` | "
                f"{datetime.datetime.fromtimestamp(s['t0']):%H:%M:%S}-{datetime.datetime.fromtimestamp(s['t1']):%H:%M:%S} | "
                f"{s['dur'] / 60:.1f} min | {SPEED[s['id']]}x |\n")
    f.write("\n## Events\n\n| wall | elapsed | seg @ | rough @ | kind | event |\n|---|---|---|---|---|---|\n")
    for e in ev:
        s = seg_of(e["t"])
        el = int(e["t"] - t_prompt)
        sat = f"{s['id']} {int(e['t'] - s['t0']) // 60}:{int(e['t'] - s['t0']) % 60:02d}" if s else ""
        rat = ""
        if s:
            r = s["rough_start"] + (e["t"] - s["t0"]) / SPEED[s["id"]]
            rat = f"{int(r) // 60}:{int(r) % 60:02d}"
        label = (e["label"] + (" - " + e["note"] if e.get("note") else "")).replace("|", "/")
        if e["kind"] == "mcp":
            label = e["label"] + " " + e.get("note", "").replace("|", "/")
        f.write(f"| {datetime.datetime.fromtimestamp(e['t']):%H:%M:%S} | {el // 60}:{el % 60:02d} | {sat} | {rat} | {e['kind']} | {label} |\n")
print(f"rough cut {pos / 60:.1f} min from {len(segs)} segments, {len(srt)} captions")
