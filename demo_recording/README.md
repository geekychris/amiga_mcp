# Terra Strike: recorded demo of agent coding with Amiga DevBench

Raw material for a short narrated video: Claude Code builds a Zarch-style
3D landscape shooter for the 68k Amiga, testing and debugging it live
through devbench (MCP over HTTP to the classic-68k devbench on :3001).

Session: 2026-10-16, prompt at 11:31:45, recording 11:34:34 to 12:13:45;
stage 2 (main screen) 13:49 to 14:11.
The game is in `examples/terra_strike/`.

## The short cut (start here to watch)

`video/terra_strike_short.mp4`: **2:57, 1920×1080, 16 beats, narrated**
(`make_short.py`; captions in `terra_strike_short.srt`, beat times in
`terra_strike_short_beats.json` for the narration). Each beat puts a CLI
panel next to real footage. The CLI panels are typeset from the session
transcript: the real commands and real output, trimmed to the lines that
matter, key line highlighted, revealed step by step. The raw CLI capture is
mostly bookkeeping diffs at an unreadable size. The footage is the
recording itself: Amiga crops, the smooth FS-UAE clips, devbench bridge
screenshots and the devbench web UI.

`shots/webui/` holds the devbench web UI, captured from a headless Chrome
during a live session (`webui_shot.py`): dashboard, Logs (MCP calls, bridge
`[VAR]`/`[HOOK]` replies and the game's events interleaved), game events
only, the test stream ending "67/67 passed", the Traffic list, and a
Traffic entry opened to its MCP request/response. Beat 11 also shows the
wire protocol of the first kill, verbatim from `logs/bridge_protocol.log`.

**Narration**: `narration.py` writes one line per beat and synthesises it with
the local Kokoro runtime (`/Users/chris/code/claude_world/kokoro_runtime`,
voice `bf_emma`, British English, speed 1.08). `make_short.py` stretches any
beat whose line runs long, starts each line 0.35 s into its beat, and
loudness-normalises the voice track (-16 LUFS). The script text is in
`video/terra_strike_short_beats.json`; edit `narration.py`, run it, then
run `make_short.py`.

## What's here

Everything under `video/` except the three `terra_strike_short.*` files is
**local only** (gitignored): the raw full-screen segments show the whole
desktop, so they're not in this public repo. `TIMELINE.md` and
`make_rough_cut.py` refer to them by name. To re-cut, use the recording Mac's
copy of `demo_recording/video/` or record a new session with `rec.sh`.
The tooling is macOS-specific: avfoundation and `screencapture` for
capture, macOS fonts in `make_short.py`, and Chrome's macOS path in
`webui_shot.py`.

| | |
|---|---|
| `TIMELINE.md` | **start here for the edit**: every event with wall clock, elapsed time, offset inside its segment video, and position in the rough cut |
| `timeline.jsonl` | the same events, machine-readable (`kind`: phase, milestone, bug, fix, perf, test, e2e, showcase, devbench-fix, devbench-note, mcp, rec_start/stop) |
| `video/NN-*.mp4` | full-screen segments (15 fps, 1728 px wide). The desktop shows other windows too, so use the crops |
| `video/121240-07-showcase-amiga-window.mov` | smooth (~40 fps, Retina) capture of just the FS-UAE window during the showcase flight |
| `video/clips/*_cli.mp4` | per-segment crop of the Claude Code CLI pane, real time |
| `video/clips/*_amiga.mp4` | per-segment crop of the FS-UAE window, real time |
| `video/rough_cut.mp4` + `.srt` | 1920×1080 draft: CLI pane next to FS-UAE, long stretches sped up (coding 24×, testing 6×), smooth showcase at the end, captions from the timeline |
| `logs/claude_transcript.md` / `.jsonl` | the Claude Code session: prompt, Claude's messages, every tool call and a short result, timestamped |
| `logs/mcp_calls.jsonl` | every devbench MCP call made through `mcp.py`: tool, args, duration, result |
| `logs/bridge_protocol.log` | the AmigaBridge wire protocol, `HOST -> AMIGA` / `AMIGA -> HOST`, heartbeats and screenshot rows removed |
| `logs/bridge_protocol_highlights.md` | first exchange of each of the 23 command types, plus the game's remote log events |
| `logs/devbench_session_full.log` | raw slice of devbench's persistent log for the session |
| `logs/e2e_run*.txt` | the five end-to-end test runs, from 2 failures to all passing |
| `shots/` | screenshots: macOS captures of FS-UAE plus devbench bridge screenshots (`shots/e2e/`) |

Tools: `rec.sh` (screen recording plus timeline marks), `mark.sh`,
`mcp.py` (call any devbench MCP tool), `key.sh` (hold an Amiga key),
`showcase.sh` (the scripted highlight flight), `make_bridge_log.py`,
`make_transcript.py`, `make_rough_cut.py`.

## Story beats (see TIMELINE.md for exact times)

1. **Prompt and recon.** Claude reads the examples and reuses `ace_pilot`'s
   ptplayer and sound; devbench MCP `ping`, `list_clients`, `list_tasks`.
2. **Design and code.** Portable core (maths, clipping, terrain, world)
   with 67 unit tests that run on the Mac, the Amiga and inside the game.
   The first host run finds a real bug (enemies firing every frame).
3. **First cross-compile** through `amiga_build`, clean apart from a name
   clash with intuition's `FINE`. `amiga_run` builds, deploys and launches;
   the title flyover renders the island.
4. **Performance.** 5 fps. Stage timers exposed as bridge vars show 159 ms
   in `graphics.library` area fills; a CPU bitplane span filler gets 27,
   then 32 fps.
5. **Gameplay via key injection.** Return to start, hold Up to lift off;
   `TAKEOFF` arrives in the remote log; the `scene` hook sets up a tank
   fight; aim assist; first kill; a ship sunk.
6. **Bugs found on screen.** Black cracks (clip T-junctions); colours
   flickering, diagnosed as the blitter clearing the visible buffer (a
   double-buffer bug inherited from the sample code); verified with a
   burst capture and `amiga_get_palette`.
7. **On-Amiga tests.** `amiga_run_tests` exposes two devbench bugs (a
   missing import kills the TCP read loop; a call to a nonexistent builder
   function). The wire log shows `TEST_BEGIN` and then `disconnected`.
   Fixed, then 67/67 pass on the 68060.
8. **E2E through devbench**: 32 checks covering self-test hook, key
   injection, scenes, kills, landing and refuel, live retuning with
   `set_var`, memregion, level clear, game over, stop. Wrong `score`
   values traced to a second bridge client (miner-meteor), started by the
   Amiga's launcher, whose global input handler had caught our injected
   Return. Found with `amiga_dos_command Status`; fixed; all passed.
9. **Showcase flight**, played by Claude through key injection and scene
   hooks: building, tank, ship, landing, level cleared.

## Stage 2: a main screen through a multi-modal hand-off (13:49-14:11)

10. **Claude writes a prompt for ChatGPT** (`stage2/chatgpt_prompt.md`)
    tuned for the conversion: flat polygons, ~24 colours, 5:4, the
    game's own palette and models, a dark bottom band for the prompt
    text. Segment 08.
11. **The user generates the art in ChatGPT** and hands it back
    (`stage2/chatgpt_title_source.png`, 1402x1122). Timeline kind
    `handoff`.
12. **Conversion** (`examples/terra_strike/tools/make_title.py`), segment
    09. Median cut washed out the logo and craft (on video in segment
    09; the preview files now hold the k-means result); k-means weighted by
    sqrt(pixel count) in the 12-bit gamut keeps them. No dithering beats
    Floyd-Steinberg on flat art (`stage2/preview_fs.png`). The output is
    five 40-byte-per-row bitplanes as C data plus a title palette.
13. **Into the game**: the artwork and the 3D attract flyover alternate,
    each loading its own palette, with a `title` hook and `title_phase`
    var. Checked with devbench screenshots and `amiga_get_palette` ($017
    title vs $FFF game), and key injection for Return and Esc.
14. **Two false leads, then a real limitation.** "PRESS FIRE" seemed
    missing. macOS colour management shows yellow as (255,255,84), and the
    FS-UAE window stops repainting while you use other apps, so the Mac
    captures were wrong. Then the devbench screenshot (row by row over
    ~5 s) never caught the on/off blink. The fix is to alternate
    yellow/white so the prompt is always drawn; the e2e test checks its
    pixels. All checks pass. Segment 11 is the smooth FS-UAE capture of the
    final main screen (re-recorded with FS-UAE frontmost; segment 10 is an
    earlier take with the old blinking prompt, left out of the rough cut).

Stage 2 artifacts: `stage2/` (prompt, source, previews),
`shots/13-18*`, `shots/e2e_stage2/`, `logs/e2e_run6_stage2.txt`,
`logs/e2e_run7_stage2.txt`.

## Narration later

The speak-content skill (Kokoro TTS at 127.0.0.1:8770) can voice a
script written against `TIMELINE.md`'s `rough @` column, or against a
re-cut using the per-segment clips.
