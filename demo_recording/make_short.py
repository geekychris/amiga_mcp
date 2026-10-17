#!/usr/bin/env python3
"""make_short.py - the ~2:45 cut of the Terra Strike session.

Each beat pairs a CLI panel with real footage. The CLI panel is typeset from
the session transcript (logs/claude_transcript.md): the actual commands and
their actual output, trimmed to the lines that matter, with the key line
highlighted and lines revealed step by step. The footage is the recording
(Amiga crops, the smooth FS-UAE clips), devbench bridge screenshots and the
devbench web UI captures. Output: video/terra_strike_short.mp4 (+ .srt).
"""
import os, subprocess, json
from PIL import Image, ImageDraw, ImageFont

D = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.join(D, "video", "short")
os.makedirs(OUT, exist_ok=True)
W, H = 1920, 1080
FPS = 30
MONO = ImageFont.truetype("/System/Library/Fonts/Menlo.ttc", 23)
MONO_B = ImageFont.truetype("/System/Library/Fonts/Menlo.ttc", 23, index=1)
TITLE = ImageFont.truetype("/System/Library/Fonts/Avenir Next.ttc", 50, index=0)
SUB = ImageFont.truetype("/System/Library/Fonts/Avenir Next.ttc", 30, index=0)
STEP = ImageFont.truetype("/System/Library/Fonts/Supplemental/DIN Alternate Bold.ttf", 40)
BIG = ImageFont.truetype("/System/Library/Fonts/Avenir Next.ttc", 44, index=0)

BG = (13, 17, 23)
TERM = (8, 11, 15)
COL = {"cmd": (230, 237, 243), "out": (139, 148, 158), "note": (121, 192, 255), "user": (255, 206, 120),
       "ok": (63, 185, 80), "bad": (248, 81, 73), "hl": (255, 214, 10)}

V = lambda p: os.path.join(D, p)
SEG = {"01": V("video/clips/113434-01-recon-and-design_amiga.mp4"),
       "02": V("video/clips/114915-02-build-deploy-run_amiga.mp4"),
       "03": V("video/clips/115307-03-gameplay-key-injection_amiga.mp4"),
       "05": V("video/clips/120219-05-e2e-devbench-test_amiga.mp4"),
       "07": V("video/121240-07-showcase-amiga-window.mov"),
       "11": V("video/141625-11-stage2-main-screen-rerecord-amiga-window.mov")}

# ---------------------------------------------------------------- the beats
# CLI lines: (kind, text, reveal_group). kinds: user, cmd, out, ok, bad, hl, note
# media: list of ("video", key, start, dur) | ("image", path, dur, nearest)
BEATS = [
 dict(title="Terra Strike", sub="A Zarch-style Amiga game, agent-coded with Claude Code + Amiga DevBench",
      dur=5, layout="full", media=[("image", "shots/18_main_screen_final.png", 5, True)]),

 dict(title="The prompt", sub="One request: build it, test it through devbench, record it", dur=9, layout="cli",
      cli=[("user", "> Create a game ... some kind of 3d landscape akin to", 0),
           ("user", "  Zarch or Carrier Command.", 0),
           ("user", "> A craft that takes off from a landing pad, flies", 1),
           ("user", "  around and shoots tanks, buildings, ships (on water).", 1),
           ("user", "> Build this for the amiga 68k. Include unit tests.", 2),
           ("user", "> Test the game with key injection, screenshots,", 3),
           ("user", "  remote logging and variable setting.", 3)]),

 dict(title="Recon over MCP", sub="Devbench exposes 129 tools; the Amiga answers over the bridge", dur=8,
      cli=[("cmd", "● Skill(amiga-devbench-workflow)", 0),
           ("cmd", "● mcp amiga_ping", 1),
           ("hl", "  └ Amiga alive - clients: 0, chip: 1658856 bytes", 1),
           ("cmd", "● mcp amiga_list_tasks", 2),
           ("out", "  └ Tasks (28): Background CLI, Workbench,", 2),
           ("out", "      input.device, trackdisk.device, ...", 2),
           ("cmd", "● Read examples/ace_pilot/*.c", 3),
           ("note", "  reuse: fixed-point 3D, ptplayer, double buffering", 3)],
      media=[("video", "01", 2, 8)]),

 dict(title="Code + unit tests first", sub="Portable core, one test suite for three runners (Mac · Amiga · in-game)",
      dur=10, layout="cli",
      cli=[("cmd", "● Write ts.h · ts_math.c · terrain.c · world.c   (no Amiga headers)", 0),
           ("cmd", "● Write models.c · render.c · main.c · tests/test_cases.c", 0),
           ("cmd", "● Bash(make host-test)", 1),
           ("bad", "  └ FAIL  shots destroy the tank ahead", 1),
           ("bad", "  └ FAIL  ship shells damage the craft", 1),
           ("hl", "  └ 58 checks, 4 failed", 1),
           ("note", "  the tests caught a real bug: enemies fired every frame", 2),
           ("note", "  (fire timer reset backwards) - fixed, regression test added", 2),
           ("cmd", "● Bash(make host-test)", 3),
           ("ok", "  └ 59 checks, 0 failed", 3)]),

 dict(title="Cross-compile, deploy, launch", sub="Docker m68k toolchain behind devbench's amiga_build / amiga_run",
      dur=10,
      cli=[("cmd", "● mcp amiga_build(project=\"examples/terra_strike\")", 0),
           ("out", "  └ m68k-amigaos-gcc -noixemul -O2 -m68020 ...", 0),
           ("ok", "  └ Build SUCCEEDED (692ms)", 0),
           ("cmd", "● mcp amiga_run(project=\"examples/terra_strike\")", 1),
           ("out", "  └ Deploy: OK - Deployed 1 file(s) to .../Dropbox/Dev", 1),
           ("ok", "  └ Launch: [OK] Started: Dropbox:Dev/terra_strike", 1),
           ("note", "  first frame on the Amiga - but 5 fps", 2)],
      media=[("video", "02", 8, 10)]),

 dict(title="Profiling through bridge variables", sub="Stage timers exposed as vars, read live with amiga_get_var",
      dur=11,
      cli=[("cmd", "● mcp amiga_get_var(\"fps\")", 0),
           ("bad", "  └ fps (i32) = 5", 0),
           ("cmd", "● mcp amiga_get_var(\"us_draw\" / \"us_build\" / \"us_hud\")", 1),
           ("hl", "  └ us_draw (i32) = 159046", 1),
           ("out", "  └ us_build (i32) = 2241    us_hud (i32) = 13765", 1),
           ("note", "  graphics.library area fill ~0.94 ms per polygon", 2),
           ("note", "  -> CPU span filler writing the bitplanes directly", 2),
           ("cmd", "● mcp amiga_get_var(...)", 3),
           ("ok", "  └ fps (i32) = 27     us_draw (i32) = 18696", 3)],
      media=[("video", "02", 8, 4), ("video", "02", 200, 7)]),

 dict(title="Playing it with key injection", sub="Raw Amiga key codes into input.device; state checked over the bridge",
      dur=12,
      cli=[("cmd", "● mcp amiga_input_key(rawkey=\"44\")          # Return", 0),
           ("out", "  └ state (i32) = 1    pstate (i32) = 0", 0),
           ("cmd", "● mcp amiga_input_key(rawkey=\"4C\") hold 1.5 s # cursor up", 1),
           ("out", "  └ pstate (i32) = 1   alt (i32) = 210   fuel (i32) = 912", 1),
           ("cmd", "● remote log  (CLOG from the Amiga)", 2),
           ("out", "  └ [terra_strike] STATE PLAY level 1", 2),
           ("hl", "  └ [terra_strike] TAKEOFF fuel=1000", 2)],
      media=[("video", "03", 14, 12)]),

 dict(title="Bugs found on screen", sub="Screenshots + palette reads pin down what the eye sees", dur=10,
      cli=[("note", "  black cracks at the screen edges:", 0),
           ("out", "  └ clip T-junction - shared edges rounded differently", 0),
           ("ok", "  └ fix + regression test (66/66)", 0),
           ("cmd", "● mcp amiga_get_palette", 1),
           ("out", "  └ palette correct ... so pens are written wrong", 1),
           ("hl", "  shadow pen 31 shown as pen 30 = bitplane 0 cleared", 2),
           ("note", "  -> the blitter clears the buffer still on screen", 2),
           ("ok", "  └ fix: wait for the SafeMessage of the shown buffer", 3)],
      media=[("image", "shots/03_flying.png", 3, False), ("image", "shots/10_sea_scene.png", 4, False),
             ("image", "shots/11b_doublebuffer_fix_burst.png", 3, False)]),

 dict(title="Scene hooks + first kill", sub="A bridge hook drops the craft into a tank battle", dur=10,
      cli=[("cmd", "● mcp amiga_call_hook(client=\"terra_strike\",", 0),
           ("cmd", "                      hook=\"scene\", args=\"tanks\")", 0),
           ("out", "  └ [ok] scene tanks: craft at 6632,8193 alt 542", 0),
           ("cmd", "● mcp amiga_input_key(rawkey=\"40\")  # hold fire", 1),
           ("cmd", "● mcp amiga_get_var(\"score\"), (\"targets\")", 2),
           ("ok", "  └ score (i32) = 100   targets (i32) = 8", 2),
           ("hl", "  └ [terra_strike] KILL tank +100 score=100", 3)],
      media=[("video", "03", 234, 10)]),

 dict(title="Unit tests on the Amiga", sub="The same 67 tests on the 68060 - after fixing two devbench bugs",
      dur=10,
      cli=[("cmd", "● mcp amiga_run_tests(command=\"terra_strike_test\")", 0),
           ("bad", "  └ module 'amiga_devbench.builder' has no", 0),
           ("bad", "    attribute 'build_project'", 0),
           ("out", "  wire log: CLOG TEST_BEGIN -> disconnected", 1),
           ("bad", "  └ TCP read error: name 'protocol' is not defined", 1),
           ("note", "  two devbench bugs: fixed + regression test", 2),
           ("cmd", "● mcp amiga_run_tests(...)", 3),
           ("ok", "  └ Results: 67 passed, 0 failed, 67 total", 3),
           ("ok", "  └ ALL TESTS PASSED", 3)],
      media=[("image", "shots/webui/04c_logs_tests_2x.png", 10, False)]),

 dict(title="How devbench talks to the bridge", sub="Devbench web UI: live log, MCP traffic, and the wire protocol",
      dur=12, layout="full",
      media=[("image", "shots/webui/02_logs_all.png", 4, False), ("image", "shots/webui/07_traffic_detail_call_hook.png", 4, False),
             ("image", "WIRE", 4, False)]),

 dict(title="End-to-end test - and a mystery", sub="32 checks through MCP; the wrong score came from another game",
      dur=11,
      cli=[("cmd", "● Bash(python3 tests/e2e_devbench.py)", 0),
           ("bad", "  └ FAIL  tank destroyed (score 0)", 0),
           ("cmd", "● mcp amiga_client_info(\"miner-meteor\")", 1),
           ("hl", "  └ Variables: mode, level, lives, score, ...", 1),
           ("cmd", "● mcp amiga_dos_command(\"Status\")", 2),
           ("hl", "  └ Process 4: Loaded as command: DH2:Dev/launcher", 2),
           ("note", "  the launcher's input handler saw our injected Return", 2),
           ("cmd", "● Bash(python3 tests/e2e_devbench.py)", 3),
           ("ok", "  └ ALL PASSED", 3)],
      media=[("video", "05", 194, 11)]),

 dict(title="Stage 2: a main screen from ChatGPT", sub="Claude to ChatGPT and back",
      dur=13,
      cli=[("cmd", "● Write stage2/chatgpt_prompt.md", 0),
           ("out", "  └ \"1280x1024, flat-shaded low-poly, <=24 colours,", 0),
           ("out", "     Zarch checkerboard island, TERRA STRIKE logo\"", 0),
           ("user", "  user -> ChatGPT -> image  (multi-modal hand-off)", 1),
           ("cmd", "● Bash(tools/make_title.py --dither none)", 2),
           ("note", "  median cut washed out the logo -> k-means in the", 2),
           ("note", "  12-bit OCS gamut, weighted by sqrt(pixel count)", 2),
           ("hl", "  └ title_image.c: 320x256, 32/32 pens, text pen 30", 3)],
      media=[("image", "stage2/chatgpt_title_source.png", 4, False), ("image", "stage2/preview_final.png", 5, False),
             ("image", "shots/13_main_screen_bridge.png", 4, True)]),

 dict(title="The main screen, live", sub="Own palette per screen; checked with palette reads and key injection",
      dur=12,
      cli=[("cmd", "● mcp amiga_call_hook(hook=\"title\", args=\"image\")", 0),
           ("cmd", "● mcp amiga_get_palette", 0),
           ("hl", "  └ 1: $017            (title palette)", 0),
           ("cmd", "● mcp amiga_input_key(rawkey=\"44\")   # Return", 1),
           ("out", "  └ 1: $FFF            (game palette back)", 1),
           ("cmd", "● Bash(python3 tests/e2e_devbench.py)", 2),
           ("ok", "  └ PASS  PRESS FIRE TO START drawn (390 px)", 2),
           ("ok", "  └ ALL PASSED", 3)],
      media=[("video", "11", 1, 12)]),

 dict(title="Terra Strike on the Amiga", sub="Played by Claude through devbench key injection and scene hooks",
      dur=13, layout="full", media=[("video", "07", 9, 13)]),

 dict(title="One session", sub="", dur=7, layout="end", media=[("image", "shots/18_main_screen_final.png", 7, True)]),
]

END_LINES = ["39 min from prompt to a tested, playable game  ·  +22 min for the main screen",
             "67 unit tests × 3 runners (Mac · Amiga · in-game)  ·  33 end-to-end checks",
             "Bugs found through devbench: 6 in the game, 2 in devbench itself",
             "~750 bridge commands  ·  129 MCP tools  ·  68060 FS-UAE"]

# ---------------------------------------------------------------- drawing
def header(d, i, n, b):
    d.rectangle([0, 0, W, 112], fill=(22, 27, 34))
    d.rectangle([0, 112, W, 115], fill=(255, 140, 0))
    d.text((40, 26), f"{i:02d}", font=STEP, fill=(255, 140, 0))
    d.text((120, 14), b["title"], font=TITLE, fill=(240, 246, 252))
    d.text((122 + d.textlength(b["title"], font=TITLE) + 30, 32), b["sub"], font=SUB, fill=(139, 148, 158))


def term_fonts(box, lines):
    """Largest Menlo size (<= 30) at which every line fits the panel."""
    x0, y0, x1, y1 = box
    for size in range(30, 17, -1):
        f = ImageFont.truetype("/System/Library/Fonts/Menlo.ttc", size)
        fb = ImageFont.truetype("/System/Library/Fonts/Menlo.ttc", size, index=1)
        wide = max(fb.getlength(t) for _, t, _ in lines)
        if wide + 50 <= x1 - x0 and len(lines) * size * 1.65 + 80 <= y1 - y0:
            return f, fb, int(size * 1.65)
    return f, fb, int(size * 1.65)


def terminal(d, box, lines, upto):
    MONO, MONO_B, LH = term_fonts(box, lines)
    x0, y0, x1, y1 = box
    d.rounded_rectangle(box, 14, fill=TERM, outline=(48, 54, 61), width=2)
    d.rounded_rectangle([x0, y0, x1, y0 + 40], 14, fill=(30, 35, 42))
    d.rectangle([x0, y0 + 26, x1, y0 + 40], fill=(30, 35, 42))
    for k, c in enumerate(((255, 95, 86), (255, 189, 46), (39, 201, 63))):
        d.ellipse([x0 + 16 + k * 24, y0 + 13, x0 + 30 + k * 24, y0 + 27], fill=c)
    d.text((x0 + 100, y0 + 8), "claude  —  amiga_mcp/examples/terra_strike",
           font=ImageFont.truetype("/System/Library/Fonts/Menlo.ttc", 20), fill=(139, 148, 158))
    y = y0 + 66
    for kind, text, grp in lines:
        if grp > upto:
            continue
        col = COL.get(kind, COL["out"])
        if kind == "hl":
            tw = d.textlength(text, font=MONO)      # Menlo Bold has no box-drawing glyphs
            d.rectangle([x0 + 14, y - 5, x0 + 26 + tw, y + LH - 10], fill=(60, 50, 0))
            d.text((x0 + 20, y), text, font=MONO, fill=COL["hl"])
        elif kind == "cmd" and text.startswith("●"):
            d.text((x0 + 20, y), "●", font=MONO_B, fill=(63, 185, 80))
            d.text((x0 + 20 + d.textlength("● ", font=MONO), y), text[2:], font=MONO_B, fill=col)
        else:
            d.text((x0 + 20, y), text, font=MONO, fill=col)
        y += LH


def wire_card(path):
    """The bridge wire protocol, from logs/bridge_protocol.log (this session)."""
    # verbatim: the first kill, 11:57:02.912-11:57:06.800 (heartbeats already filtered out)
    log = open(os.path.join(D, "logs", "bridge_protocol.log")).read().splitlines()
    start = next(k for k, l in enumerate(log) if l.startswith("11:57:02.912"))
    rows = [l for l in log[start:start + 20] if "[VAR] send_and_wait" not in l][:12]
    im = Image.new("RGB", (1600, 900), TERM)
    d = ImageDraw.Draw(im)
    d.text((40, 30), "AmigaBridge wire protocol: the first kill, verbatim from the devbench log",
           font=SUB, fill=(255, 140, 0))
    d.text((40, 76), "756 commands sent / 39,327 lines received this session  (logs/bridge_protocol.log)",
           font=MONO, fill=(139, 148, 158))
    y = 140
    for row in rows:
        stamp, arrow, msg = row[:12], row[14:28], row[30:][:78]
        col = (121, 192, 255) if arrow.startswith("HOST") else (63, 185, 80)
        d.text((40, y), stamp, font=MONO, fill=(110, 118, 129))
        d.text((240, y), arrow, font=MONO_B, fill=col)
        d.text((470, y), msg, font=MONO_B if "KILL" in msg else MONO,
               fill=COL["hl"] if "KILL" in msg else (230, 237, 243))
        y += 60
    im.save(path)


def media_img(path, box_w, box_h, nearest):
    im = Image.open(path).convert("RGB")
    s = min(box_w / im.width, box_h / im.height)
    return im.resize((int(im.width * s), int(im.height * s)), Image.NEAREST if nearest else Image.LANCZOS)


def run(args):
    subprocess.run(["ffmpeg", "-v", "error", "-y"] + args, check=True)


# ---------------------------------------------------------------- narration fit
# narration.py writes video/short/narration.json; stretch any beat whose line
# runs long (plus a beat of breathing room) and stretch its media to match.
NARR_LEAD, NARR_PAD = 0.35, 1.0
NARR = os.path.join(OUT, "narration.json")
narr = json.load(open(NARR)) if os.path.exists(NARR) else []
for b, n in zip(BEATS, narr):
    need = n["dur"] + NARR_PAD
    if need > b["dur"]:
        k = need / b["dur"]
        b["media"] = [(m[0], m[1], m[2], round(m[3] * k, 2)) if m[0] == "video"
                      else (m[0], m[1], round(m[2] * k, 2), m[3]) for m in b.get("media", [])]
        b["dur"] = round(need, 2)
    b["narr"] = n["file"]

import sys
if '--wire-only' in sys.argv:
    wire_card(sys.argv[-1]); sys.exit()

# ---------------------------------------------------------------- build
parts, srt, t_acc = [], [], 0.0
for i, b in enumerate(BEATS, 1):
    layout = b.get("layout", "split")
    groups = sorted({g for _, _, g in b.get("cli", [])}) or [0]
    # background states (header + terminal with progressive reveal)
    states = []
    step = b["dur"] / len(groups)
    for gi, g in enumerate(groups):
        im = Image.new("RGB", (W, H), BG)
        d = ImageDraw.Draw(im)
        header(d, i, len(BEATS), b)
        if layout == "cli":
            terminal(d, (110, 160, 1810, 1040), b["cli"], g)
        elif layout == "split":
            terminal(d, (30, 150, 1010, 1050), b["cli"], g)
        elif layout == "end":
            y = 600
            for line in END_LINES:
                tw = d.textlength(line, font=BIG)
                d.text(((W - tw) / 2, y), line, font=BIG, fill=(240, 246, 252))
                y += 82
        p = os.path.join(OUT, f"b{i:02d}_s{gi}.png")
        im.save(p)
        states.append((p, step))
    with open(os.path.join(OUT, f"b{i:02d}.txt"), "w") as f:
        for p, dur in states:
            f.write(f"file '{p}'\nduration {dur:.3f}\n")
        f.write(f"file '{states[-1][0]}'\n")
    # media box
    if layout == "split":
        bx, by, bw, bh = 1030, 150, 870, 900
    elif layout == "full":
        bx, by, bw, bh = 60, 140, 1800, 920
    elif layout == "end":
        bx, by, bw, bh = 660, 150, 600, 430
    else:
        bx = by = bw = bh = 0
    out = os.path.join(OUT, f"b{i:02d}.mp4")
    inputs = ["-f", "concat", "-safe", "0", "-i", os.path.join(OUT, f"b{i:02d}.txt")]
    chains, labels = [], []
    for k, m in enumerate(b.get("media", [])):
        if m[0] == "video":
            _, key, st, du = m
            inputs += ["-ss", str(st), "-t", str(du), "-i", SEG[key]]
            chains.append(f"[{k + 1}:v]fps={FPS},scale={bw}:{bh}:force_original_aspect_ratio=decrease,"
                          f"pad={bw}:{bh}:(ow-iw)/2:(oh-ih)/2:color=0x0d1117,setsar=1[m{k}]")
        else:
            _, path, du, nearest = m
            src = os.path.join(OUT, "wire.png") if path == "WIRE" else V(path)
            if path == "WIRE":
                wire_card(src)
            img = media_img(src, bw, bh, nearest)
            canvas = Image.new("RGB", (bw, bh), BG)
            canvas.paste(img, ((bw - img.width) // 2, (bh - img.height) // 2))
            pp = os.path.join(OUT, f"b{i:02d}_m{k}.png")
            canvas.save(pp)
            inputs += ["-loop", "1", "-t", str(du), "-framerate", str(FPS), "-i", pp]
            chains.append(f"[{k + 1}:v]fps={FPS},setsar=1[m{k}]")
        labels.append(f"[m{k}]")
    fc = f"[0:v]fps={FPS},scale={W}:{H},setsar=1[bg]"
    if labels:
        fc += ";" + ";".join(chains) + ";" + "".join(labels) + f"concat=n={len(labels)}:v=1:a=0[m];" \
              f"[bg][m]overlay={bx}:{by}:shortest=0:eof_action=repeat[v]"
    else:
        fc += ";[bg]null[v]"
    run(inputs + ["-filter_complex", fc, "-map", "[v]", "-t", str(b["dur"]), "-r", str(FPS),
                  "-c:v", "libx264", "-preset", "medium", "-crf", "20", "-pix_fmt", "yuv420p", out])
    parts.append(out)
    srt.append((t_acc, t_acc + b["dur"], f"{b['title']} - {b['sub']}" if b["sub"] else b["title"]))
    t_acc += b["dur"]
    print(f"beat {i:02d} {b['title']:<38s} {b['dur']:>4.0f}s  ({t_acc:5.0f}s)")

with open(os.path.join(OUT, "concat.txt"), "w") as f:
    f.write("".join(f"file '{p}'\n" for p in parts))
final = os.path.join(D, "video", "terra_strike_short.mp4")
silent = os.path.join(OUT, "picture.mp4")
run(["-f", "concat", "-safe", "0", "-i", os.path.join(OUT, "concat.txt"), "-c", "copy", silent])
if narr:
    # one voice track: each line delayed to its beat start (+lead), mixed without level drop
    ins, fil, t0 = [], [], 0.0
    for k, b in enumerate(BEATS):
        ins += ["-i", b["narr"]]
        ms = int((t0 + NARR_LEAD) * 1000)
        fil.append(f"[{k + 1}:a]aresample=48000,adelay={ms}|{ms}[a{k}]")
        t0 += b["dur"]
    mix = ";".join(fil) + ";" + "".join(f"[a{k}]" for k in range(len(BEATS))) + \
          f"amix=inputs={len(BEATS)}:normalize=0,loudnorm=I=-16:TP=-1.5:LRA=11,apad[aout]"
    run(["-i", silent] + ins + ["-filter_complex", mix, "-map", "0:v", "-map", "[aout]",
         "-c:v", "copy", "-c:a", "aac", "-b:a", "192k", "-t", f"{t0:.3f}", final])
else:
    os.replace(silent, final)


def ts(x):
    ms = int(x * 1000)
    return f"{ms // 3600000:02d}:{ms // 60000 % 60:02d}:{ms // 1000 % 60:02d},{ms % 1000:03d}"
with open(final.replace(".mp4", ".srt"), "w") as f:
    for k, (a, z, txt) in enumerate(srt, 1):
        f.write(f"{k}\n{ts(a)} --> {ts(z)}\n{txt}\n\n")
json.dump([{"beat": k + 1, "start": a, "end": z, "title": t,
            "narration": narr[k]["text"] if k < len(narr) else ""} for k, (a, z, t) in enumerate(srt)],
          open(final.replace(".mp4", "_beats.json"), "w"), indent=1)
print(f"{final}: {t_acc:.0f} s")
