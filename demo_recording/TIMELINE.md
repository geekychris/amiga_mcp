# Terra Strike demo - timeline

Prompt received 11:31:45; last event 14:41:18. `elapsed` is from the prompt; `seg @` is the offset inside that segment's video (use it to cut); `rough @` is the position in `video/rough_cut.mp4`.

## Segments

| segment | file | wall clock | length | rough-cut speed |
|---|---|---|---|---|
| 01 | `video/113434-01-recon-and-design.mp4` | 11:34:34-11:49:14 | 14.7 min | 24x |
| 02 | `video/114915-02-build-deploy-run.mp4` | 11:49:15-11:53:06 | 3.9 min | 6x |
| 03 | `video/115307-03-gameplay-key-injection.mp4` | 11:53:07-11:58:19 | 5.2 min | 6x |
| 04 | `video/115820-04-amiga-unit-tests.mp4` | 11:58:20-12:02:18 | 4.0 min | 6x |
| 05 | `video/120219-05-e2e-devbench-test.mp4` | 12:02:19-12:10:13 | 7.9 min | 6x |
| 06 | `video/121133-06-showcase-flight.mp4` | 12:11:33-12:12:31 | 1.0 min | 1.5x |
| 07 | `video/121240-07-showcase-amiga-window.mov` | 12:12:44-12:13:46 | 1.0 min | 1x |
| 08 | `video/134927-08-stage2-title-prompt.mp4` | 13:49:28-13:49:47 | 0.3 min | 2x |
| 09 | `video/135532-09-stage2-title-convert.mp4` | 13:55:32-14:10:40 | 15.1 min | 8x |
| 11 | `video/141625-11-stage2-main-screen-rerecord-amiga-window.mov` | 14:16:25-14:17:01 | 0.6 min | 1x |

## Events

| wall | elapsed | seg @ | rough @ | kind | event |
|---|---|---|---|---|---|
| 11:32:57 | 1:12 |  |  | session_start | Demo session start - user prompt received ~11:30 |
| 11:34:34 | 2:49 | 01 0:00 | 0:00 | rec_start | 113434-01-recon-and-design |
| 11:34:34 | 2:49 | 01 0:00 | 0:00 | phase | Recon - devbench :3001 up, bridge heartbeat green, MCP 129 tools; studied ace_pilot 3D engine + ptplayer |
| 11:34:39 | 2:54 | 01 0:05 | 0:00 | mcp | amiga_ping {} |
| 11:34:39 | 2:54 | 01 0:05 | 0:00 | mcp | amiga_list_clients {} |
| 11:34:39 | 2:54 | 01 0:05 | 0:00 | mcp | amiga_proc_list {} |
| 11:34:40 | 2:55 | 01 0:06 | 0:00 | mcp | amiga_list_tasks {} |
| 11:36:56 | 5:11 | 01 2:21 | 0:05 | phase | Design - Terra Strike: Zarch-like; portable logic (ts_*.c) + Amiga render/main; tests shared host+Amiga |
| 11:42:13 | 10:28 | 01 7:38 | 0:19 | test | Host unit tests run 1: 54/58 pass - combat: shots fall short of the tank; ship shells don't reach the craft |
| 11:42:40 | 10:55 | 01 8:06 | 0:20 | test | Host unit tests run 2: 60/60 pass - fixed enemy fire timer bug found by tests |
| 11:42:46 | 11:01 | 01 8:12 | 0:20 | test | CORRECTION: run 2 had 1 fail (new fire-rate test, craft respawned out of range); run 3: 60/60 pass |
| 11:49:10 | 17:25 | 01 14:36 | 0:36 | phase | Code written - ts_math/terrain/world (portable), models, render, main, sound, tests; first cross-compile next |
| 11:49:15 | 17:30 |  |  | rec_stop | 113434-01-recon-and-design |
| 11:49:15 | 17:30 | 02 0:00 | 0:36 | rec_start | 114915-02-build-deploy-run |
| 11:49:15 | 17:30 | 02 0:00 | 0:36 | mcp | amiga_build {"project": "examples/terra_strike"} |
| 11:49:22 | 17:37 | 02 0:07 | 0:37 | mcp | amiga_run {"project": "examples/terra_strike"} |
| 11:49:30 | 17:45 | 02 0:15 | 0:39 | mcp | amiga_list_clients {} |
| 11:49:43 | 17:58 | 02 0:28 | 0:41 | milestone | FIRST LAUNCH works: title flyover renders - fps=5 - too slow; text JAM2 boxes |
| 11:49:43 | 17:58 | 02 0:28 | 0:41 | mcp | amiga_perf_report {} |
| 11:49:43 | 17:58 | 02 0:28 | 0:41 | mcp | amiga_get_var {"name": "fps"} |
| 11:49:44 | 17:59 | 02 0:28 | 0:41 | mcp | amiga_get_var {"name": "polys"} |
| 11:49:46 | 18:01 | 02 0:31 | 0:41 | mcp | amiga_perf_report {"client": "terra_strike"} |
| 11:50:16 | 18:31 | 02 1:01 | 0:46 | mcp | amiga_run {"project": "examples/terra_strike"} |
| 11:50:27 | 18:42 | 02 1:12 | 0:48 | mcp | amiga_get_var {"name": "fps"} |
| 11:50:28 | 18:43 | 02 1:12 | 0:48 | mcp | amiga_get_var {"name": "us_update"} |
| 11:50:28 | 18:43 | 02 1:13 | 0:48 | mcp | amiga_get_var {"name": "us_build"} |
| 11:50:28 | 18:43 | 02 1:13 | 0:48 | mcp | amiga_get_var {"name": "us_draw"} |
| 11:50:29 | 18:44 | 02 1:14 | 0:49 | mcp | amiga_get_var {"name": "us_hud"} |
| 11:50:29 | 18:44 | 02 1:14 | 0:49 | mcp | amiga_get_var {"name": "polys"} |
| 11:51:07 | 19:22 | 02 1:52 | 0:55 | perf | Bridge perf vars: us_draw=159046 us_build=2241 us_hud=13765 fps=5 - AreaFill ~0.94ms/poly -> replace with CPU planar span filler |
| 11:51:26 | 19:41 | 02 2:11 | 0:58 | mcp | amiga_run {"project": "examples/terra_strike"} |
| 11:51:35 | 19:50 | 02 2:20 | 1:00 | mcp | amiga_get_var {"name": "fps"} |
| 11:51:40 | 19:55 | 02 2:25 | 1:00 | mcp | amiga_get_var {"name": "us_build"} |
| 11:51:45 | 20:00 | 02 2:30 | 1:01 | mcp | amiga_get_var {"name": "us_draw"} |
| 11:51:50 | 20:05 | 02 2:35 | 1:02 | mcp | amiga_get_var {"name": "us_hud"} |
| 11:51:59 | 20:14 | 02 2:44 | 1:04 | mcp | amiga_list_clients {} |
| 11:51:59 | 20:14 | 02 2:44 | 1:04 | mcp | amiga_last_crash {} |
| 11:52:18 | 20:33 | 02 3:02 | 1:07 | perf | CPU span filler: fps 5 -> 27 (screen) - bridge registration raced old instance shutdown -> add ab_init retry |
| 11:52:18 | 20:33 | 02 3:02 | 1:07 | mcp | amiga_run {"project": "examples/terra_strike"} |
| 11:52:26 | 20:41 | 02 3:10 | 1:08 | mcp | amiga_list_clients {} |
| 11:52:26 | 20:41 | 02 3:10 | 1:08 | mcp | amiga_get_var {"name": "fps"} |
| 11:52:26 | 20:41 | 02 3:11 | 1:08 | mcp | amiga_get_var {"name": "us_update"} |
| 11:52:26 | 20:41 | 02 3:11 | 1:08 | mcp | amiga_get_var {"name": "us_build"} |
| 11:52:26 | 20:41 | 02 3:11 | 1:08 | mcp | amiga_get_var {"name": "us_draw"} |
| 11:52:27 | 20:42 | 02 3:11 | 1:08 | mcp | amiga_get_var {"name": "us_hud"} |
| 11:52:27 | 20:42 | 02 3:12 | 1:08 | mcp | amiga_get_var {"name": "polys"} |
| 11:52:51 | 21:06 | 02 3:36 | 1:12 | mcp | amiga_run {"project": "examples/terra_strike"} |
| 11:53:00 | 21:15 | 02 3:45 | 1:14 | mcp | amiga_get_var {"name": "fps"} |
| 11:53:00 | 21:15 | 02 3:45 | 1:14 | mcp | amiga_get_var {"name": "us_build"} |
| 11:53:01 | 21:16 | 02 3:45 | 1:14 | mcp | amiga_get_var {"name": "us_draw"} |
| 11:53:01 | 21:16 | 02 3:46 | 1:14 | mcp | amiga_get_var {"name": "us_hud"} |
| 11:53:07 | 21:22 |  |  | perf | HUD line cache: fps 32 on title, us_draw 18.5ms us_hud 9.9ms |
| 11:53:07 | 21:22 |  |  | rec_stop | 114915-02-build-deploy-run |
| 11:53:07 | 21:22 | 03 0:00 | 1:15 | rec_start | 115307-03-gameplay-key-injection |
| 11:53:07 | 21:22 | 03 0:00 | 1:15 | phase | Gameplay test via key injection |
| 11:53:07 | 21:22 | 03 0:00 | 1:15 | mcp | amiga_screenshot {} |
| 11:53:23 | 21:38 | 03 0:15 | 1:17 | mcp | amiga_input_key {"rawkey": "44", "direction": "down"} |
| 11:53:23 | 21:38 | 03 0:15 | 1:17 | mcp | amiga_input_key {"rawkey": "44", "direction": "up"} |
| 11:53:24 | 21:39 | 03 0:16 | 1:18 | mcp | amiga_get_var {"name": "state"} |
| 11:53:24 | 21:39 | 03 0:16 | 1:18 | mcp | amiga_get_var {"name": "pstate"} |
| 11:53:25 | 21:40 | 03 0:17 | 1:18 | mcp | amiga_input_key {"rawkey": "4C", "direction": "down"} |
| 11:53:26 | 21:41 | 03 0:18 | 1:18 | mcp | amiga_input_key {"rawkey": "4C", "direction": "up"} |
| 11:53:27 | 21:42 | 03 0:19 | 1:18 | mcp | amiga_get_var {"name": "pstate"} |
| 11:53:27 | 21:42 | 03 0:19 | 1:18 | mcp | amiga_get_var {"name": "alt"} |
| 11:53:27 | 21:42 | 03 0:19 | 1:18 | mcp | amiga_get_var {"name": "fuel"} |
| 11:53:34 | 21:49 | 03 0:26 | 1:19 | mcp | amiga_input_key {"rawkey": "20", "direction": "down"} |
| 11:53:35 | 21:50 | 03 0:27 | 1:19 | mcp | amiga_input_key {"rawkey": "20", "direction": "up"} |
| 11:53:36 | 21:51 | 03 0:28 | 1:20 | mcp | amiga_get_var {"name": "speed"} |
| 11:53:36 | 21:51 | 03 0:28 | 1:20 | mcp | amiga_get_var {"name": "alt"} |
| 11:53:36 | 21:51 | 03 0:29 | 1:20 | mcp | amiga_get_var {"name": "fps"} |
| 11:53:37 | 21:52 | 03 0:29 | 1:20 | mcp | amiga_get_var {"name": "us_draw"} |
| 11:53:37 | 21:52 | 03 0:29 | 1:20 | mcp | amiga_get_var {"name": "us_hud"} |
| 11:54:04 | 22:19 | 03 0:56 | 1:24 | bug | Black dashed cracks at screen edges: clip T-junction (opposite winding rounds differently) - fix: interpolate from inside endpoint; regression test added |
| 11:54:10 | 22:25 | 03 1:02 | 1:25 | mcp | amiga_run {"project": "examples/terra_strike"} |
| 11:54:22 | 22:37 | 03 1:14 | 1:27 | mcp | amiga_call_hook {"client": "terra_strike", "hook": "scene", "args": "tanks"} |
| 11:54:29 | 22:44 | 03 1:22 | 1:28 | mcp | amiga_list_clients {} |
| 11:54:33 | 22:48 | 03 1:25 | 1:29 | mcp | amiga_proc_list {} |
| 11:54:33 | 22:48 | 03 1:25 | 1:29 | mcp | amiga_launch {"command": "Dropbox:Dev/terra_strike"} |
| 11:54:36 | 22:51 | 03 1:28 | 1:30 | mcp | amiga_list_clients {} |
| 11:54:41 | 22:56 | 03 1:33 | 1:30 | devbench-note | amiga_run LAUNCH got no response once (client still shutting down); manual amiga_launch fixed it |
| 11:54:41 | 22:56 | 03 1:33 | 1:30 | mcp | amiga_call_hook {"client": "terra_strike", "hook": "scene", "args": "tanks"} |
| 11:54:54 | 23:09 | 03 1:46 | 1:33 | mcp | amiga_input_key {"rawkey": "40", "direction": "down"} |
| 11:54:54 | 23:09 | 03 1:46 | 1:33 | mcp | amiga_input_key {"rawkey": "20", "direction": "down"} |
| 11:54:55 | 23:10 | 03 1:47 | 1:33 | mcp | amiga_input_key {"rawkey": "20", "direction": "up"} |
| 11:54:58 | 23:13 | 03 1:50 | 1:33 | mcp | amiga_input_key {"rawkey": "40", "direction": "up"} |
| 11:54:58 | 23:13 | 03 1:50 | 1:33 | mcp | amiga_get_var {"name": "score"} |
| 11:54:58 | 23:13 | 03 1:51 | 1:33 | mcp | amiga_get_var {"name": "targets"} |
| 11:54:59 | 23:14 | 03 1:51 | 1:33 | mcp | amiga_get_var {"name": "speed"} |
| 11:54:59 | 23:14 | 03 1:51 | 1:33 | mcp | amiga_get_var {"name": "alt"} |
| 11:54:59 | 23:14 | 03 1:51 | 1:33 | mcp | amiga_get_var {"name": "shield"} |
| 11:55:24 | 23:39 | 03 2:16 | 1:38 | mcp | amiga_get_palette {} |
| 11:55:43 | 23:58 | 03 2:35 | 1:41 | mcp | amiga_set_var {"name": "god_mode", "value": "1"} |
| 11:55:43 | 23:58 | 03 2:36 | 1:41 | mcp | amiga_call_hook {"client": "terra_strike", "hook": "scene", "args": "city"} |
| 11:56:53 | 25:08 | 03 3:45 | 1:52 | fix | Aim assist + debris pens + red tanks; host tests 67/67 |
| 11:56:53 | 25:08 | 03 3:45 | 1:52 | mcp | amiga_run {"project": "examples/terra_strike"} |
| 11:57:00 | 25:15 | 03 3:52 | 1:54 | mcp | amiga_list_clients {} |
| 11:57:02 | 25:17 | 03 3:55 | 1:54 | mcp | amiga_call_hook {"client": "terra_strike", "hook": "scene", "args": "tanks"} |
| 11:57:03 | 25:18 | 03 3:56 | 1:54 | mcp | amiga_input_key {"rawkey": "40", "direction": "down"} |
| 11:57:06 | 25:21 | 03 3:58 | 1:55 | mcp | amiga_input_key {"rawkey": "40", "direction": "up"} |
| 11:57:06 | 25:21 | 03 3:58 | 1:55 | mcp | amiga_get_var {"name": "score"} |
| 11:57:06 | 25:21 | 03 3:58 | 1:55 | mcp | amiga_get_var {"name": "targets"} |
| 11:57:07 | 25:22 | 03 3:59 | 1:55 | mcp | amiga_get_var {"name": "shield"} |
| 11:57:16 | 25:31 | 03 4:08 | 1:56 | milestone | FIRST KILL via key injection: tank destroyed, score 100 |
| 11:57:16 | 25:31 | 03 4:08 | 1:56 | mcp | amiga_call_hook {"client": "terra_strike", "hook": "scene", "args": "sea"} |
| 11:57:18 | 25:33 | 03 4:10 | 1:56 | mcp | amiga_input_key {"rawkey": "40", "direction": "down"} |
| 11:57:20 | 25:35 | 03 4:12 | 1:57 | mcp | amiga_input_key {"rawkey": "40", "direction": "up"} |
| 11:57:21 | 25:36 | 03 4:13 | 1:57 | mcp | amiga_call_hook {"client": "terra_strike", "hook": "status"} |
| 11:57:48 | 26:03 | 03 4:40 | 2:02 | bug | Transient wrong colours = blitter clearing the DISPLAYED buffer (double-buffer SafeMessage wait off by one, inherited from ace_pilot) - evidence: shadow pen31 shown as pen30 = plane0 cleared; fixed swap_buffers |
| 11:57:48 | 26:03 | 03 4:40 | 2:02 | mcp | amiga_run {"project": "examples/terra_strike"} |
| 11:57:55 | 26:10 | 03 4:47 | 2:03 | mcp | amiga_list_clients {} |
| 11:57:58 | 26:13 | 03 4:50 | 2:03 | mcp | amiga_get_var {"name": "fps"} |
| 11:58:06 | 26:21 | 03 4:59 | 2:05 | mcp | amiga_call_hook {"client": "terra_strike", "hook": "scene", "args": "sea"} |
| 11:58:07 | 26:22 | 03 4:59 | 2:05 | mcp | amiga_input_key {"rawkey": "4F", "direction": "down"} |
| 11:58:10 | 26:25 | 03 5:02 | 2:05 | mcp | amiga_input_key {"rawkey": "4F", "direction": "up"} |
| 11:58:19 | 26:34 |  |  | milestone | Double buffer fix verified: 6-frame burst clean, 22-25 fps vsync |
| 11:58:20 | 26:35 |  |  | rec_stop | 115307-03-gameplay-key-injection |
| 11:58:20 | 26:35 | 04 0:00 | 2:07 | rec_start | 115820-04-amiga-unit-tests |
| 11:58:20 | 26:35 | 04 0:00 | 2:07 | mcp | amiga_run_tests {"project": "examples/terra_strike", "command": "terra_strike_test"} |
| 11:59:05 | 27:20 | 04 0:45 | 2:14 | devbench-fix | amiga_run_tests called nonexistent builder.build_project -> fixed in mcp_tools.py; devbench restart also restarted FS-UAE (child process) |
| 11:59:06 | 27:21 | 04 0:46 | 2:14 | mcp | amiga_run_tests {"project": "examples/terra_strike", "command": "terra_strike_test"} |
| 11:59:53 | 28:08 | 04 1:32 | 2:22 | mcp | amiga_list_clients {} |
| 11:59:53 | 28:08 | 04 1:32 | 2:22 | mcp | amiga_ping {} |
| 12:00:11 | 28:26 | 04 1:51 | 2:25 | bug | On-Amiga tests: devbench wire log shows TEST_BEGIN then 'disconnected' - assertion burst overruns serial/TCP - pace check() with Delay(2) |
| 12:00:12 | 28:27 | 04 1:51 | 2:25 | mcp | amiga_run_tests {"project": "examples/terra_strike", "command": "terra_strike_test"} |
| 12:01:14 | 29:29 | 04 2:54 | 2:36 | devbench-fix | serial_conn.py: protocol.parse_message NameError on TEST_ lines killed TCP read loop -> parse_message; restarted devbench with SIGKILL (FS-UAE survives) |
| 12:01:14 | 29:29 | 04 2:54 | 2:36 | mcp | amiga_run_tests {"project": "examples/terra_strike", "command": "terra_strike_test"} |
| 12:01:25 | 29:40 | 04 3:05 | 2:38 | milestone | ON-AMIGA UNIT TESTS: 67/67 pass via amiga_run_tests (68060 FS-UAE) - pacing wasn't the cause; comment corrected |
| 12:02:19 | 30:34 |  |  | rec_stop | 115820-04-amiga-unit-tests |
| 12:02:19 | 30:34 | 05 0:00 | 2:46 | rec_start | 120219-05-e2e-devbench-test |
| 12:02:19 | 30:34 | 05 0:00 | 2:46 | phase | E2E devbench test (build->selftest->keys->scenes->tuning->memregion->level clear->game over->stop) |
| 12:02:19 | 30:34 | 05 0:00 | 2:46 | e2e | build, deploy, launch |
| 12:02:28 | 30:43 | 05 0:08 | 2:48 | e2e | build, deploy, launch |
| 12:02:28 | 30:43 | 05 0:08 | 2:48 | e2e | in-game self-test hook |
| 12:02:33 | 30:48 | 05 0:14 | 2:49 | e2e | title screen and music |
| 12:02:38 | 30:53 | 05 0:18 | 2:50 | e2e | key injection: start and lift off |
| 12:02:41 | 30:56 | 05 0:22 | 2:50 | e2e | scene hook: tank battle, fire with aim assist |
| 12:02:45 | 31:00 | 05 0:26 | 2:51 | e2e | scene hook: ships at sea |
| 12:02:49 | 31:04 | 05 0:30 | 2:52 | e2e | landing on the pad |
| 12:02:55 | 31:10 | 05 0:36 | 2:52 | e2e | live tuning through bridge variables |
| 12:02:57 | 31:12 | 05 0:37 | 2:53 | e2e | memory region: heightmap |
| 12:02:57 | 31:12 | 05 0:37 | 2:53 | e2e | level clear |
| 12:03:03 | 31:18 | 05 0:43 | 2:54 | e2e | game over |
| 12:03:12 | 31:27 | 05 0:53 | 2:55 | e2e | status hook, process list, stop |
| 12:03:44 | 31:59 | 05 1:24 | 3:01 | fix | e2e run 2: 28/30. selftest -> async (hook queues, main loop runs); memregion retry (VAR err cross-talk; bridge caps reads at 256B) |
| 12:03:44 | 31:59 | 05 1:24 | 3:01 | e2e | build, deploy, launch |
| 12:03:48 | 32:03 | 05 1:29 | 3:01 | e2e | in-game self-test hook |
| 12:03:52 | 32:07 | 05 1:32 | 3:02 | e2e | title screen and music |
| 12:03:56 | 32:11 | 05 1:37 | 3:03 | e2e | key injection: start and lift off |
| 12:04:05 | 32:20 | 05 1:46 | 3:04 | e2e | scene hook: tank battle, fire with aim assist |
| 12:04:20 | 32:35 | 05 2:01 | 3:07 | e2e | scene hook: ships at sea |
| 12:04:36 | 32:51 | 05 2:16 | 3:09 | e2e | landing on the pad |
| 12:04:45 | 33:00 | 05 2:26 | 3:11 | e2e | live tuning through bridge variables |
| 12:04:48 | 33:03 | 05 2:28 | 3:11 | e2e | memory region: heightmap |
| 12:04:48 | 33:03 | 05 2:28 | 3:11 | e2e | level clear |
| 12:05:03 | 33:18 | 05 2:43 | 3:14 | e2e | game over |
| 12:05:24 | 33:39 | 05 3:04 | 3:17 | e2e | status hook, process list, stop |
| 12:06:16 | 34:31 | 05 3:56 | 3:26 | mcp | amiga_list_clients {} |
| 12:06:16 | 34:31 | 05 3:56 | 3:26 | mcp | amiga_client_info {"client": "miner-meteor"} |
| 12:06:25 | 34:40 | 05 4:05 | 3:27 | mcp | amiga_stop_client {"name": "miner-meteor"} |
| 12:06:25 | 34:40 | 05 4:05 | 3:27 | bug | Wrong var values: a 2nd bridge client (miner-meteor, auto-launched by the Amiga's Launcher) registered score/level/lives; GETVAR/SETVAR have no client field so they hit it - stopped it via amiga_stop_client; e2e now stops foreign clients first |
| 12:06:25 | 34:40 | 05 4:05 | 3:27 | e2e | clean slate: stop other bridge clients |
| 12:06:25 | 34:40 | 05 4:05 | 3:27 | e2e | build, deploy, launch |
| 12:06:29 | 34:44 | 05 4:09 | 3:28 | e2e | in-game self-test hook |
| 12:06:32 | 34:47 | 05 4:13 | 3:29 | e2e | title screen and music |
| 12:06:37 | 34:52 | 05 4:17 | 3:29 | e2e | key injection: start and lift off |
| 12:06:46 | 35:01 | 05 4:26 | 3:31 | e2e | scene hook: tank battle, fire with aim assist |
| 12:07:01 | 35:16 | 05 4:41 | 3:33 | e2e | scene hook: ships at sea |
| 12:07:17 | 35:32 | 05 4:57 | 3:36 | e2e | landing on the pad |
| 12:07:26 | 35:41 | 05 5:07 | 3:38 | e2e | live tuning through bridge variables |
| 12:07:29 | 35:44 | 05 5:09 | 3:38 | e2e | memory region: heightmap |
| 12:07:29 | 35:44 | 05 5:09 | 3:38 | e2e | level clear |
| 12:07:44 | 35:59 | 05 5:25 | 3:41 | e2e | game over |
| 12:08:05 | 36:20 | 05 5:46 | 3:44 | e2e | status hook, process list, stop |
| 12:08:12 | 36:27 | 05 5:52 | 3:45 | mcp | amiga_launch {"command": "Dropbox:Dev/terra_strike"} |
| 12:08:15 | 36:30 | 05 5:55 | 3:46 | mcp | amiga_list_clients {} |
| 12:08:15 | 36:30 | 05 5:55 | 3:46 | mcp | amiga_call_hook {"client": "terra_strike", "hook": "start"} |
| 12:08:15 | 36:30 | 05 5:56 | 3:46 | mcp | amiga_set_var {"name": "score", "value": "4242"} |
| 12:08:16 | 36:31 | 05 5:56 | 3:46 | mcp | amiga_get_var {"name": "score"} |
| 12:08:16 | 36:31 | 05 5:56 | 3:46 | mcp | amiga_call_hook {"client": "terra_strike", "hook": "status"} |
| 12:08:25 | 36:40 | 05 6:05 | 3:47 | mcp | amiga_list_tasks {} |
| 12:08:25 | 36:40 | 05 6:05 | 3:47 | mcp | amiga_list_screens {} |
| 12:08:25 | 36:40 | 05 6:05 | 3:47 | mcp | amiga_client_info {"client": "miner-meteor"} |
| 12:08:30 | 36:45 | 05 6:10 | 3:48 | mcp | amiga_read_file {"path": "S:User-Startup"} |
| 12:08:41 | 36:56 | 05 6:21 | 3:50 | mcp | amiga_dos_command {"command": "Status"} |
| 12:08:45 | 37:00 | 05 6:25 | 3:51 | mcp | amiga_dos_command {"command": "Break 4 C"} |
| 12:08:45 | 37:00 | 05 6:26 | 3:51 | mcp | amiga_stop_client {"name": "miner-meteor"} |
| 12:08:47 | 37:02 | 05 6:28 | 3:51 | mcp | amiga_dos_command {"command": "Status"} |
| 12:08:48 | 37:03 | 05 6:28 | 3:51 | mcp | amiga_list_clients {} |
| 12:09:02 | 37:17 | 05 6:42 | 3:54 | mcp | amiga_stop_client {"name": "terra_strike"} |
| 12:09:04 | 37:19 | 05 6:44 | 3:54 | mcp | amiga_input_key {"rawkey": "45", "direction": "down"} |
| 12:09:04 | 37:19 | 05 6:44 | 3:54 | mcp | amiga_input_key {"rawkey": "45", "direction": "up"} |
| 12:09:06 | 37:21 | 05 6:46 | 3:54 | mcp | amiga_dos_command {"command": "Status"} |
| 12:09:06 | 37:21 | 05 6:47 | 3:54 | mcp | amiga_list_clients {} |
| 12:09:15 | 37:30 | 05 6:55 | 3:56 | bug | ROOT CAUSE: examples/launcher (from User-Startup) has a global input handler; it queued our injected Return and launched miner_meteor when our game closed - found via amiga_dos_command Status; quit it with injected Esc |
| 12:09:15 | 37:30 | 05 6:55 | 3:56 | e2e | clean slate: stop other bridge clients |
| 12:09:16 | 37:31 | 05 6:56 | 3:56 | e2e | build, deploy, launch |
| 12:09:19 | 37:34 | 05 7:00 | 3:57 | e2e | in-game self-test hook |
| 12:09:21 | 37:36 | 05 7:01 | 3:57 | e2e | title screen and music |
| 12:09:22 | 37:37 | 05 7:03 | 3:57 | e2e | key injection: start and lift off |
| 12:09:25 | 37:40 | 05 7:06 | 3:57 | e2e | scene hook: tank battle, fire with aim assist |
| 12:09:29 | 37:44 | 05 7:10 | 3:58 | e2e | scene hook: ships at sea |
| 12:09:33 | 37:48 | 05 7:14 | 3:59 | e2e | landing on the pad |
| 12:09:39 | 37:54 | 05 7:20 | 4:00 | e2e | live tuning through bridge variables |
| 12:09:41 | 37:56 | 05 7:21 | 4:00 | e2e | memory region: heightmap |
| 12:09:41 | 37:56 | 05 7:22 | 4:00 | e2e | level clear |
| 12:09:46 | 38:01 | 05 7:27 | 4:01 | e2e | game over |
| 12:09:56 | 38:11 | 05 7:36 | 4:03 | e2e | status hook, process list, stop |
| 12:10:14 | 38:29 |  |  | milestone | E2E ALL PASSED (32 checks) - run 5 |
| 12:10:14 | 38:29 |  |  | rec_stop | 120219-05-e2e-devbench-test |
| 12:10:14 | 38:29 |  |  | rec_start | 121014-06-showcase-flight |
| 12:10:14 | 38:29 |  |  | phase | Showcase flight: Claude plays via key injection |
| 12:10:14 | 38:29 |  |  | mcp | amiga_launch {"command": "Dropbox:Dev/terra_strike"} |
| 12:10:20 | 38:35 |  |  | showcase | title flyover |
| 12:10:20 | 38:35 |  |  | mcp | amiga_input_key {"rawkey": "44", "direction": "down"} |
| 12:10:21 | 38:36 |  |  | mcp | amiga_input_key {"rawkey": "44", "direction": "up"} |
| 12:10:22 | 38:37 |  |  | showcase | lift off |
| 12:10:22 | 38:37 |  |  | mcp | amiga_input_key {"rawkey": "4C", "direction": "down"} |
| 12:10:23 | 38:38 |  |  | mcp | amiga_input_key {"rawkey": "4C", "direction": "up"} |
| 12:10:23 | 38:38 |  |  | showcase | throttle + bank toward targets |
| 12:10:23 | 38:38 |  |  | mcp | amiga_input_key {"rawkey": "20", "direction": "down"} |
| 12:10:23 | 38:38 |  |  | mcp | amiga_input_key {"rawkey": "4E", "direction": "down"} |
| 12:10:25 | 38:40 |  |  | mcp | amiga_input_key {"rawkey": "4E", "direction": "up"} |
| 12:10:25 | 38:40 |  |  | mcp | amiga_input_key {"rawkey": "20", "direction": "up"} |
| 12:10:25 | 38:40 |  |  | showcase | cruise + fire |
| 12:10:25 | 38:40 |  |  | mcp | amiga_input_key {"rawkey": "40", "direction": "down"} |
| 12:10:29 | 38:44 |  |  | mcp | amiga_input_key {"rawkey": "4F", "direction": "down"} |
| 12:10:30 | 38:45 |  |  | mcp | amiga_input_key {"rawkey": "4F", "direction": "up"} |
| 12:10:32 | 38:47 |  |  | mcp | amiga_input_key {"rawkey": "40", "direction": "up"} |
| 12:10:32 | 38:47 |  |  | mcp | amiga_call_hook {"client": "terra_strike", "hook": "status"} |
| 12:10:38 | 38:53 |  |  | showcase | scene hook: tanks |
| 12:10:38 | 38:53 |  |  | mcp | amiga_call_hook {"client": "terra_strike", "hook": "scene", "args": "tanks"} |
| 12:10:40 | 38:55 |  |  | mcp | amiga_input_key {"rawkey": "40", "direction": "down"} |
| 12:10:42 | 38:57 |  |  | mcp | amiga_input_key {"rawkey": "40", "direction": "up"} |
| 12:10:44 | 38:59 |  |  | showcase | scene hook: sea |
| 12:10:44 | 38:59 |  |  | mcp | amiga_call_hook {"client": "terra_strike", "hook": "scene", "args": "sea"} |
| 12:10:45 | 39:00 |  |  | mcp | amiga_input_key {"rawkey": "40", "direction": "down"} |
| 12:10:48 | 39:03 |  |  | mcp | amiga_input_key {"rawkey": "40", "direction": "up"} |
| 12:10:50 | 39:05 |  |  | showcase | scene hook: landing, descend onto pad |
| 12:10:50 | 39:05 |  |  | mcp | amiga_call_hook {"client": "terra_strike", "hook": "scene", "args": "landing"} |
| 12:10:51 | 39:06 |  |  | mcp | amiga_input_key {"rawkey": "4D", "direction": "down"} |
| 12:10:54 | 39:09 |  |  | mcp | amiga_input_key {"rawkey": "4D", "direction": "up"} |
| 12:10:57 | 39:12 |  |  | mcp | amiga_call_hook {"client": "terra_strike", "hook": "status"} |
| 12:10:57 | 39:12 |  |  | showcase | landed + refuelling |
| 12:11:03 | 39:18 |  |  | mcp | amiga_input_key {"rawkey": "4C", "direction": "down"} |
| 12:11:04 | 39:19 |  |  | mcp | amiga_input_key {"rawkey": "4C", "direction": "up"} |
| 12:11:04 | 39:19 |  |  | showcase | clear hook -> LEVEL 1 CLEARED |
| 12:11:04 | 39:19 |  |  | mcp | amiga_call_hook {"client": "terra_strike", "hook": "clear"} |
| 12:11:10 | 39:25 |  |  | mcp | amiga_get_var {"name": "level"} |
| 12:11:12 | 39:27 |  |  | showcase | stop client |
| 12:11:12 | 39:27 |  |  | mcp | amiga_stop_client {"name": "terra_strike"} |
| 12:11:13 | 39:28 |  |  | rec_stop | 121014-06-showcase-flight |
| 12:11:13 | 39:28 |  |  | session_end | Demo recording complete |
| 12:11:33 | 39:48 | 06 0:00 | 4:05 | rec_start | 121133-06-showcase-flight |
| 12:11:40 | 39:55 | 06 0:07 | 4:10 | phase | Showcase flight (re-recorded; first attempt lost to camera device index shift) |
| 12:11:41 | 39:56 | 06 0:07 | 4:10 | mcp | amiga_launch {"command": "Dropbox:Dev/terra_strike"} |
| 12:11:47 | 40:02 | 06 0:13 | 4:15 | showcase | title flyover |
| 12:11:49 | 40:04 | 06 0:15 | 4:16 | showcase | Return: start |
| 12:11:49 | 40:04 | 06 0:15 | 4:16 | mcp | amiga_input_key {"rawkey": "44", "direction": "down"} |
| 12:11:49 | 40:04 | 06 0:15 | 4:16 | mcp | amiga_input_key {"rawkey": "44", "direction": "up"} |
| 12:11:51 | 40:06 | 06 0:17 | 4:17 | showcase | lift off |
| 12:11:51 | 40:06 | 06 0:17 | 4:17 | mcp | amiga_input_key {"rawkey": "4C", "direction": "down"} |
| 12:11:52 | 40:07 | 06 0:18 | 4:18 | mcp | amiga_input_key {"rawkey": "4C", "direction": "up"} |
| 12:11:53 | 40:08 | 06 0:19 | 4:19 | showcase | throttle + bank |
| 12:11:53 | 40:08 | 06 0:19 | 4:19 | mcp | amiga_input_key {"rawkey": "20", "direction": "down"} |
| 12:11:53 | 40:08 | 06 0:19 | 4:19 | mcp | amiga_input_key {"rawkey": "4E", "direction": "down"} |
| 12:11:54 | 40:09 | 06 0:21 | 4:20 | mcp | amiga_input_key {"rawkey": "4E", "direction": "up"} |
| 12:11:55 | 40:10 | 06 0:21 | 4:20 | mcp | amiga_input_key {"rawkey": "20", "direction": "up"} |
| 12:11:55 | 40:10 | 06 0:21 | 4:20 | showcase | cruise + fire |
| 12:11:55 | 40:10 | 06 0:21 | 4:20 | mcp | amiga_input_key {"rawkey": "40", "direction": "down"} |
| 12:11:58 | 40:13 | 06 0:24 | 4:22 | mcp | amiga_input_key {"rawkey": "4F", "direction": "down"} |
| 12:11:59 | 40:14 | 06 0:25 | 4:23 | mcp | amiga_input_key {"rawkey": "4F", "direction": "up"} |
| 12:12:01 | 40:16 | 06 0:27 | 4:24 | mcp | amiga_input_key {"rawkey": "40", "direction": "up"} |
| 12:12:01 | 40:16 | 06 0:28 | 4:24 | showcase | scene hook: tanks |
| 12:12:01 | 40:16 | 06 0:28 | 4:24 | mcp | amiga_call_hook {"client": "terra_strike", "hook": "scene", "args": "tanks"} |
| 12:12:02 | 40:17 | 06 0:29 | 4:25 | mcp | amiga_input_key {"rawkey": "40", "direction": "down"} |
| 12:12:05 | 40:20 | 06 0:32 | 4:27 | mcp | amiga_input_key {"rawkey": "40", "direction": "up"} |
| 12:12:07 | 40:22 | 06 0:33 | 4:28 | showcase | scene hook: sea |
| 12:12:07 | 40:22 | 06 0:33 | 4:28 | mcp | amiga_call_hook {"client": "terra_strike", "hook": "scene", "args": "sea"} |
| 12:12:08 | 40:23 | 06 0:34 | 4:29 | mcp | amiga_input_key {"rawkey": "40", "direction": "down"} |
| 12:12:11 | 40:26 | 06 0:37 | 4:31 | mcp | amiga_input_key {"rawkey": "40", "direction": "up"} |
| 12:12:12 | 40:27 | 06 0:39 | 4:32 | showcase | scene hook: landing |
| 12:12:12 | 40:27 | 06 0:39 | 4:32 | mcp | amiga_call_hook {"client": "terra_strike", "hook": "scene", "args": "landing"} |
| 12:12:13 | 40:28 | 06 0:40 | 4:32 | mcp | amiga_input_key {"rawkey": "4D", "direction": "down"} |
| 12:12:17 | 40:32 | 06 0:43 | 4:35 | mcp | amiga_input_key {"rawkey": "4D", "direction": "up"} |
| 12:12:20 | 40:35 | 06 0:46 | 4:37 | showcase | landed + refuelling |
| 12:12:20 | 40:35 | 06 0:46 | 4:37 | mcp | amiga_call_hook {"client": "terra_strike", "hook": "status", "args": ""} |
| 12:12:22 | 40:37 | 06 0:48 | 4:38 | showcase | lift off again |
| 12:12:22 | 40:37 | 06 0:48 | 4:38 | mcp | amiga_input_key {"rawkey": "4C", "direction": "down"} |
| 12:12:23 | 40:38 | 06 0:49 | 4:39 | mcp | amiga_input_key {"rawkey": "4C", "direction": "up"} |
| 12:12:23 | 40:38 | 06 0:50 | 4:39 | showcase | clear hook: level cleared |
| 12:12:23 | 40:38 | 06 0:50 | 4:39 | mcp | amiga_call_hook {"client": "terra_strike", "hook": "clear", "args": ""} |
| 12:12:29 | 40:44 | 06 0:56 | 4:43 | mcp | amiga_get_var {"name": "level"} |
| 12:12:29 | 40:44 | 06 0:56 | 4:43 | showcase | stop client |
| 12:12:30 | 40:45 | 06 0:56 | 4:43 | mcp | amiga_stop_client {"name": "terra_strike"} |
| 12:12:31 | 40:46 |  |  | rec_stop | 121133-06-showcase-flight |
| 12:12:31 | 40:46 |  |  | session_end | Demo recording complete |
| 12:12:43 | 40:58 |  |  | phase | Showcase flight #2: FS-UAE window only, native screencapture -V (smooth) |
| 12:12:44 | 40:59 | 07 0:00 | 4:44 | rec_start | 121240-07-showcase-amiga-window - native screencapture -V, FS-UAE window only, .mov |
| 12:12:44 | 40:59 | 07 0:00 | 4:44 | mcp | amiga_launch {"command": "Dropbox:Dev/terra_strike"} |
| 12:12:50 | 41:05 | 07 0:06 | 4:50 | showcase | title flyover |
| 12:12:52 | 41:07 | 07 0:08 | 4:52 | showcase | Return: start |
| 12:12:52 | 41:07 | 07 0:08 | 4:52 | mcp | amiga_input_key {"rawkey": "44", "direction": "down"} |
| 12:12:52 | 41:07 | 07 0:08 | 4:52 | mcp | amiga_input_key {"rawkey": "44", "direction": "up"} |
| 12:12:54 | 41:09 | 07 0:10 | 4:54 | showcase | lift off |
| 12:12:54 | 41:09 | 07 0:10 | 4:54 | mcp | amiga_input_key {"rawkey": "4C", "direction": "down"} |
| 12:12:55 | 41:10 | 07 0:11 | 4:55 | mcp | amiga_input_key {"rawkey": "4C", "direction": "up"} |
| 12:12:56 | 41:11 | 07 0:12 | 4:56 | showcase | throttle + bank |
| 12:12:56 | 41:11 | 07 0:12 | 4:56 | mcp | amiga_input_key {"rawkey": "20", "direction": "down"} |
| 12:12:56 | 41:11 | 07 0:12 | 4:56 | mcp | amiga_input_key {"rawkey": "4E", "direction": "down"} |
| 12:12:57 | 41:12 | 07 0:13 | 4:57 | mcp | amiga_input_key {"rawkey": "4E", "direction": "up"} |
| 12:12:58 | 41:13 | 07 0:14 | 4:58 | mcp | amiga_input_key {"rawkey": "20", "direction": "up"} |
| 12:12:58 | 41:13 | 07 0:14 | 4:58 | showcase | cruise + fire |
| 12:12:58 | 41:13 | 07 0:14 | 4:58 | mcp | amiga_input_key {"rawkey": "40", "direction": "down"} |
| 12:13:01 | 41:16 | 07 0:17 | 5:01 | mcp | amiga_input_key {"rawkey": "4F", "direction": "down"} |
| 12:13:02 | 41:17 | 07 0:18 | 5:02 | mcp | amiga_input_key {"rawkey": "4F", "direction": "up"} |
| 12:13:04 | 41:19 | 07 0:20 | 5:04 | mcp | amiga_input_key {"rawkey": "40", "direction": "up"} |
| 12:13:04 | 41:19 | 07 0:20 | 5:04 | showcase | scene hook: tanks |
| 12:13:04 | 41:19 | 07 0:20 | 5:04 | mcp | amiga_call_hook {"client": "terra_strike", "hook": "scene", "args": "tanks"} |
| 12:13:05 | 41:20 | 07 0:21 | 5:06 | mcp | amiga_input_key {"rawkey": "40", "direction": "down"} |
| 12:13:08 | 41:23 | 07 0:24 | 5:08 | mcp | amiga_input_key {"rawkey": "40", "direction": "up"} |
| 12:13:10 | 41:25 | 07 0:26 | 5:10 | showcase | scene hook: sea |
| 12:13:10 | 41:25 | 07 0:26 | 5:10 | mcp | amiga_call_hook {"client": "terra_strike", "hook": "scene", "args": "sea"} |
| 12:13:11 | 41:26 | 07 0:27 | 5:11 | mcp | amiga_input_key {"rawkey": "40", "direction": "down"} |
| 12:13:14 | 41:29 | 07 0:30 | 5:14 | mcp | amiga_input_key {"rawkey": "40", "direction": "up"} |
| 12:13:15 | 41:30 | 07 0:31 | 5:16 | showcase | scene hook: landing |
| 12:13:15 | 41:30 | 07 0:31 | 5:16 | mcp | amiga_call_hook {"client": "terra_strike", "hook": "scene", "args": "landing"} |
| 12:13:16 | 41:31 | 07 0:32 | 5:17 | mcp | amiga_input_key {"rawkey": "4D", "direction": "down"} |
| 12:13:20 | 41:35 | 07 0:36 | 5:20 | mcp | amiga_input_key {"rawkey": "4D", "direction": "up"} |
| 12:13:23 | 41:38 | 07 0:39 | 5:23 | showcase | landed + refuelling |
| 12:13:23 | 41:38 | 07 0:39 | 5:23 | mcp | amiga_call_hook {"client": "terra_strike", "hook": "status", "args": ""} |
| 12:13:25 | 41:40 | 07 0:41 | 5:25 | showcase | lift off again |
| 12:13:25 | 41:40 | 07 0:41 | 5:25 | mcp | amiga_input_key {"rawkey": "4C", "direction": "down"} |
| 12:13:26 | 41:41 | 07 0:42 | 5:26 | mcp | amiga_input_key {"rawkey": "4C", "direction": "up"} |
| 12:13:26 | 41:41 | 07 0:42 | 5:26 | showcase | clear hook: level cleared |
| 12:13:26 | 41:41 | 07 0:42 | 5:26 | mcp | amiga_call_hook {"client": "terra_strike", "hook": "clear", "args": ""} |
| 12:13:32 | 41:47 | 07 0:48 | 5:33 | mcp | amiga_get_var {"name": "level"} |
| 12:13:32 | 41:47 | 07 0:48 | 5:33 | showcase | stop client |
| 12:13:32 | 41:47 | 07 0:48 | 5:33 | mcp | amiga_stop_client {"name": "terra_strike"} |
| 12:13:46 | 42:01 |  |  | rec_stop | 121240-07-showcase-amiga-window - native screencapture -V, FS-UAE window only, .mov |
| 13:49:28 | 137:43 | 08 0:00 | 5:46 | rec_start | 134927-08-stage2-title-prompt |
| 13:49:28 | 137:43 | 08 0:00 | 5:46 | phase | STAGE 2: main screen via multi-modal hand-off (Claude writes ChatGPT prompt -> user generates image -> Claude converts for Amiga) |
| 13:49:48 | 138:03 |  |  | milestone | ChatGPT prompt written: stage2/chatgpt_prompt.md - hand-off to user for image generation |
| 13:49:48 | 138:03 |  |  | rec_stop | 134927-08-stage2-title-prompt |
| 13:49:49 | 138:04 |  |  | handoff | Waiting for user's ChatGPT image |
| 13:55:32 | 143:47 | 09 0:00 | 5:55 | rec_start | 135532-09-stage2-title-convert |
| 13:55:32 | 143:47 | 09 0:00 | 5:55 | handoff | Image received from ChatGPT (user hand-off back) - 1402x1122 PNG, logo spelled correctly |
| 13:57:59 | 146:14 | 09 2:27 | 6:14 | test | make_title.py round-trip test 6/6 (after fixing the test's own regex) |
| 13:57:59 | 146:14 | 09 2:27 | 6:14 | mcp | amiga_list_clients {} |
| 13:58:00 | 146:15 | 09 2:27 | 6:14 | mcp | amiga_run {"project": "examples/terra_strike"} |
| 13:58:08 | 146:23 | 09 2:35 | 6:15 | mcp | amiga_list_clients {} |
| 13:58:11 | 146:26 | 09 2:38 | 6:15 | mcp | amiga_get_var {"name": "title_phase"} |
| 13:58:21 | 146:36 | 09 2:48 | 6:17 | mcp | amiga_screenshot {} |
| 13:58:24 | 146:39 | 09 2:51 | 6:17 | mcp | amiga_get_palette {} |
| 13:58:32 | 146:47 | 09 2:59 | 6:18 | mcp | amiga_call_hook {"client": "terra_strike", "hook": "title", "args": "image"} |
| 13:58:32 | 146:47 | 09 3:00 | 6:18 | mcp | amiga_get_var {"name": "title_phase"} |
| 13:58:33 | 146:48 | 09 3:00 | 6:18 | mcp | amiga_screenshot {} |
| 13:58:34 | 146:49 | 09 3:01 | 6:18 | mcp | amiga_get_palette {} |
| 13:58:34 | 146:49 | 09 3:01 | 6:18 | mcp | amiga_get_var {"name": "us_draw"} |
| 13:58:45 | 147:00 | 09 3:12 | 6:20 | milestone | MAIN SCREEN on the Amiga: ChatGPT art -> 32-colour planar, title palette loaded - bridge screenshot shots/13_main_screen_bridge.png, us_draw 3.7 ms |
| 13:58:45 | 147:00 | 09 3:12 | 6:20 | mcp | amiga_input_key {"rawkey": "44", "direction": "down"} |
| 13:58:45 | 147:00 | 09 3:13 | 6:20 | mcp | amiga_input_key {"rawkey": "44", "direction": "up"} |
| 13:58:47 | 147:02 | 09 3:14 | 6:20 | mcp | amiga_get_var {"name": "state"} |
| 13:58:47 | 147:02 | 09 3:14 | 6:20 | mcp | amiga_get_palette {} |
| 13:58:47 | 147:02 | 09 3:14 | 6:20 | mcp | amiga_screenshot {} |
| 13:58:54 | 147:09 | 09 3:21 | 6:21 | mcp | amiga_input_key {"rawkey": "45", "direction": "down"} |
| 13:58:54 | 147:09 | 09 3:21 | 6:21 | mcp | amiga_input_key {"rawkey": "45", "direction": "up"} |
| 13:58:55 | 147:10 | 09 3:22 | 6:21 | mcp | amiga_get_var {"name": "state"} |
| 13:58:55 | 147:10 | 09 3:23 | 6:21 | mcp | amiga_get_var {"name": "title_phase"} |
| 13:58:56 | 147:11 | 09 3:23 | 6:21 | mcp | amiga_get_palette {} |
| 13:58:56 | 147:11 | 09 3:23 | 6:21 | mcp | amiga_screenshot {} |
| 13:59:11 | 147:26 | 09 3:38 | 6:23 | e2e | Re-run E2E with main-screen checks |
| 13:59:11 | 147:26 | 09 3:38 | 6:23 | e2e | clean slate: stop other bridge clients |
| 13:59:12 | 147:27 | 09 3:40 | 6:23 | e2e | build, deploy, launch |
| 13:59:13 | 147:28 | 09 3:40 | 6:23 | e2e | in-game self-test hook |
| 13:59:14 | 147:29 | 09 3:41 | 6:23 | e2e | main screen (ChatGPT artwork) and music |
| 13:59:16 | 147:31 | 09 3:43 | 6:23 | e2e | key injection: start and lift off |
| 13:59:23 | 147:38 | 09 3:50 | 6:24 | e2e | scene hook: tank battle, fire with aim assist |
| 13:59:28 | 147:43 | 09 3:55 | 6:25 | e2e | scene hook: ships at sea |
| 13:59:35 | 147:50 | 09 4:02 | 6:26 | e2e | landing on the pad |
| 13:59:45 | 148:00 | 09 4:12 | 6:27 | e2e | live tuning through bridge variables |
| 13:59:47 | 148:02 | 09 4:14 | 6:27 | e2e | memory region: heightmap |
| 13:59:47 | 148:02 | 09 4:14 | 6:27 | e2e | level clear |
| 13:59:55 | 148:10 | 09 4:22 | 6:28 | e2e | game over |
| 14:00:05 | 148:20 | 09 4:32 | 6:30 | e2e | status hook, process list, stop |
| 14:00:31 | 148:46 | 09 4:59 | 6:33 | phase | Stage 2 showcase: smooth FS-UAE capture of the main screen |
| 14:00:31 | 148:46 | 09 4:59 | 6:33 | rec_start | 140031-10-stage2-main-screen-amiga-window - native screencapture -V, FS-UAE window only |
| 14:00:32 | 148:47 | 09 4:59 | 6:33 | showcase2 | launch: main screen |
| 14:00:32 | 148:47 | 09 4:59 | 6:33 | mcp | amiga_launch {"command": "Dropbox:Dev/terra_strike"} |
| 14:00:41 | 148:56 | 09 5:08 | 6:34 | showcase2 | Return: palette switch into the game |
| 14:00:41 | 148:56 | 09 5:08 | 6:34 | mcp | amiga_input_key {"rawkey": "44", "direction": "down"} |
| 14:00:41 | 148:56 | 09 5:09 | 6:34 | mcp | amiga_input_key {"rawkey": "44", "direction": "up"} |
| 14:00:43 | 148:58 | 09 5:10 | 6:34 | showcase2 | lift off |
| 14:00:43 | 148:58 | 09 5:10 | 6:34 | mcp | amiga_input_key {"rawkey": "4C", "direction": "down"} |
| 14:00:45 | 149:00 | 09 5:12 | 6:35 | mcp | amiga_input_key {"rawkey": "4C", "direction": "up"} |
| 14:00:45 | 149:00 | 09 5:12 | 6:35 | showcase2 | scene tanks + fire |
| 14:00:45 | 149:00 | 09 5:12 | 6:35 | mcp | amiga_call_hook {"client": "terra_strike", "hook": "scene", "args": "tanks"} |
| 14:00:46 | 149:01 | 09 5:13 | 6:35 | mcp | amiga_input_key {"rawkey": "40", "direction": "down"} |
| 14:00:49 | 149:04 | 09 5:16 | 6:35 | mcp | amiga_input_key {"rawkey": "40", "direction": "up"} |
| 14:00:50 | 149:05 | 09 5:18 | 6:35 | showcase2 | Esc: back to the main screen |
| 14:00:51 | 149:06 | 09 5:18 | 6:35 | mcp | amiga_input_key {"rawkey": "45", "direction": "down"} |
| 14:00:51 | 149:06 | 09 5:18 | 6:35 | mcp | amiga_input_key {"rawkey": "45", "direction": "up"} |
| 14:00:57 | 149:12 | 09 5:24 | 6:36 | showcase2 | stop |
| 14:00:57 | 149:12 | 09 5:24 | 6:36 | mcp | amiga_stop_client {"name": "terra_strike"} |
| 14:01:01 | 149:16 | 09 5:28 | 6:37 | rec_stop | 140031-10-stage2-main-screen-amiga-window |
| 14:02:05 | 150:20 | 09 6:32 | 6:45 | mcp | amiga_launch {"command": "Dropbox:Dev/terra_strike"} |
| 14:02:08 | 150:23 | 09 6:35 | 6:45 | mcp | amiga_get_var {"name": "frame"} |
| 14:02:08 | 150:23 | 09 6:36 | 6:45 | mcp | amiga_get_var {"name": "frame"} |
| 14:02:09 | 150:24 | 09 6:36 | 6:45 | mcp | amiga_get_var {"name": "frame"} |
| 14:02:22 | 150:37 | 09 6:49 | 6:47 | mcp | amiga_call_hook {"client": "terra_strike", "hook": "title", "args": "image"} |
| 14:02:22 | 150:37 | 09 6:49 | 6:47 | mcp | amiga_screenshot {} |
| 14:02:23 | 150:38 | 09 6:51 | 6:47 | mcp | amiga_screenshot {} |
| 14:03:06 | 151:21 | 09 7:34 | 6:52 | bug | PRESS FIRE never appears on the main screen (26 video samples + exact-pixel bridge screenshots) - experiment: draw unconditionally + count blink branch |
| 14:03:07 | 151:22 | 09 7:34 | 6:52 | mcp | amiga_run {"project": "examples/terra_strike"} |
| 14:03:14 | 151:29 | 09 7:41 | 6:53 | mcp | amiga_list_clients {} |
| 14:03:16 | 151:31 | 09 7:43 | 6:53 | mcp | amiga_call_hook {"client": "terra_strike", "hook": "title", "args": "image"} |
| 14:03:16 | 151:31 | 09 7:43 | 6:53 | mcp | amiga_get_var {"name": "title_blinks"} |
| 14:03:17 | 151:32 | 09 7:44 | 6:54 | mcp | amiga_get_var {"name": "title_blinks"} |
| 14:03:17 | 151:32 | 09 7:45 | 6:54 | mcp | amiga_get_var {"name": "frame"} |
| 14:03:18 | 151:33 | 09 7:45 | 6:54 | mcp | amiga_screenshot {} |
| 14:03:55 | 152:10 | 09 8:22 | 6:58 | mcp | amiga_run {"project": "examples/terra_strike"} |
| 14:04:02 | 152:17 | 09 8:29 | 6:59 | mcp | amiga_list_clients {} |
| 14:04:04 | 152:19 | 09 8:31 | 6:59 | mcp | amiga_call_hook {"client": "terra_strike", "hook": "title", "args": "image"} |
| 14:04:04 | 152:19 | 09 8:31 | 6:59 | mcp | amiga_get_var {"name": "dbg_frame_at_draw"} |
| 14:04:04 | 152:19 | 09 8:31 | 6:59 | mcp | amiga_get_var {"name": "dbg_draws"} |
| 14:04:04 | 152:19 | 09 8:32 | 7:00 | mcp | amiga_get_var {"name": "dbg_swapfail"} |
| 14:04:05 | 152:20 | 09 8:32 | 7:00 | mcp | amiga_get_var {"name": "title_blinks"} |
| 14:04:05 | 152:20 | 09 8:32 | 7:00 | mcp | amiga_get_var {"name": "dbg_frame_at_draw"} |
| 14:04:05 | 152:20 | 09 8:32 | 7:00 | mcp | amiga_get_var {"name": "dbg_draws"} |
| 14:04:05 | 152:20 | 09 8:33 | 7:00 | mcp | amiga_get_var {"name": "dbg_swapfail"} |
| 14:04:06 | 152:21 | 09 8:33 | 7:00 | mcp | amiga_get_var {"name": "title_blinks"} |
| 14:04:06 | 152:21 | 09 8:33 | 7:00 | mcp | amiga_get_var {"name": "dbg_frame_at_draw"} |
| 14:04:06 | 152:21 | 09 8:33 | 7:00 | mcp | amiga_get_var {"name": "dbg_draws"} |
| 14:04:06 | 152:21 | 09 8:34 | 7:00 | mcp | amiga_get_var {"name": "dbg_swapfail"} |
| 14:04:07 | 152:22 | 09 8:34 | 7:00 | mcp | amiga_get_var {"name": "title_blinks"} |
| 14:04:07 | 152:22 | 09 8:34 | 7:00 | mcp | amiga_get_var {"name": "dbg_frame_at_draw"} |
| 14:04:07 | 152:22 | 09 8:34 | 7:00 | mcp | amiga_get_var {"name": "dbg_draws"} |
| 14:04:07 | 152:22 | 09 8:34 | 7:00 | mcp | amiga_get_var {"name": "dbg_swapfail"} |
| 14:04:07 | 152:22 | 09 8:35 | 7:00 | mcp | amiga_get_var {"name": "title_blinks"} |
| 14:04:08 | 152:23 | 09 8:35 | 7:00 | mcp | amiga_get_var {"name": "dbg_frame_at_draw"} |
| 14:04:08 | 152:23 | 09 8:35 | 7:00 | mcp | amiga_get_var {"name": "dbg_draws"} |
| 14:04:08 | 152:23 | 09 8:35 | 7:00 | mcp | amiga_get_var {"name": "dbg_swapfail"} |
| 14:04:08 | 152:23 | 09 8:36 | 7:00 | mcp | amiga_get_var {"name": "title_blinks"} |
| 14:04:09 | 152:24 | 09 8:36 | 7:00 | mcp | amiga_get_var {"name": "dbg_frame_at_draw"} |
| 14:04:09 | 152:24 | 09 8:36 | 7:00 | mcp | amiga_get_var {"name": "dbg_draws"} |
| 14:04:09 | 152:24 | 09 8:36 | 7:00 | mcp | amiga_get_var {"name": "dbg_swapfail"} |
| 14:04:09 | 152:24 | 09 8:37 | 7:00 | mcp | amiga_get_var {"name": "title_blinks"} |
| 14:04:16 | 152:31 | 09 8:44 | 7:01 | mcp | amiga_screenshot {} |
| 14:04:20 | 152:35 | 09 8:47 | 7:01 | mcp | amiga_screenshot {} |
| 14:04:24 | 152:39 | 09 8:52 | 7:02 | mcp | amiga_screenshot {} |
| 14:04:44 | 152:59 | 09 9:12 | 7:04 | mcp | amiga_call_hook {"client": "terra_strike", "hook": "title", "args": "image"} |
| 14:04:51 | 153:06 | 09 9:19 | 7:05 | mcp | amiga_get_var {"name": "title_phase"} |
| 14:05:18 | 153:33 | 09 9:45 | 7:09 | mcp | amiga_run {"project": "examples/terra_strike"} |
| 14:05:25 | 153:40 | 09 9:52 | 7:10 | mcp | amiga_list_clients {} |
| 14:05:27 | 153:42 | 09 9:54 | 7:10 | mcp | amiga_call_hook {"client": "terra_strike", "hook": "title", "args": "image"} |
| 14:05:39 | 153:54 | 09 10:06 | 7:11 | mcp | amiga_list_clients {} |
| 14:05:39 | 153:54 | 09 10:06 | 7:11 | mcp | amiga_dos_command {"command": "Status"} |
| 14:05:51 | 154:06 | 09 10:19 | 7:13 | mcp | amiga_call_hook {"client": "terra_strike", "hook": "title", "args": "image"} |
| 14:05:52 | 154:07 | 09 10:19 | 7:13 | mcp | amiga_get_var {"name": "title_phase"} |
| 14:05:53 | 154:08 | 09 10:20 | 7:13 | mcp | amiga_get_var {"name": "title_phase"} |
| 14:05:53 | 154:08 | 09 10:20 | 7:13 | mcp | amiga_get_var {"name": "title_phase"} |
| 14:05:54 | 154:09 | 09 10:21 | 7:13 | mcp | amiga_get_var {"name": "title_phase"} |
| 14:05:55 | 154:10 | 09 10:22 | 7:13 | mcp | amiga_get_var {"name": "title_phase"} |
| 14:05:56 | 154:11 | 09 10:23 | 7:13 | mcp | amiga_get_var {"name": "title_phase"} |
| 14:05:56 | 154:11 | 09 10:24 | 7:13 | mcp | amiga_get_var {"name": "title_phase"} |
| 14:05:57 | 154:12 | 09 10:24 | 7:14 | mcp | amiga_get_var {"name": "title_phase"} |
| 14:06:03 | 154:18 | 09 10:30 | 7:14 | mcp | amiga_call_hook {"client": "terra_strike", "hook": "title", "args": "image"} |
| 14:06:37 | 154:52 | 09 11:04 | 7:19 | mcp | amiga_run {"project": "examples/terra_strike"} |
| 14:06:44 | 154:59 | 09 11:11 | 7:19 | mcp | amiga_list_clients {} |
| 14:06:46 | 155:01 | 09 11:13 | 7:20 | mcp | amiga_call_hook {"client": "terra_strike", "hook": "title", "args": "image"} |
| 14:07:22 | 155:37 | 09 11:49 | 7:24 | bug | FALSE ALARM: PRESS FIRE was there all along - macOS colour management shows pen 30 as (255,255,84), detector wanted blue<80; bridge samples hit the blink off-phase - experiments reverted |
| 14:07:22 | 155:37 | 09 11:49 | 7:24 | mcp | amiga_run {"project": "examples/terra_strike"} |
| 14:07:29 | 155:44 | 09 11:56 | 7:25 | mcp | amiga_list_clients {} |
| 14:07:31 | 155:46 | 09 11:59 | 7:25 | mcp | amiga_call_hook {"client": "terra_strike", "hook": "title", "args": "image"} |
| 14:07:47 | 156:02 | 09 12:14 | 7:27 | mcp | amiga_dos_command {"command": "Status"} |
| 14:07:47 | 156:02 | 09 12:14 | 7:27 | mcp | amiga_get_var {"name": "title_phase"} |
| 14:07:54 | 156:09 | 09 12:21 | 7:28 | mcp | amiga_list_screens {} |
| 14:07:54 | 156:09 | 09 12:22 | 7:28 | mcp | amiga_get_var {"name": "fps"} |
| 14:07:55 | 156:10 | 09 12:22 | 7:28 | mcp | amiga_get_var {"name": "frame"} |
| 14:07:56 | 156:11 | 09 12:23 | 7:28 | mcp | amiga_get_var {"name": "frame"} |
| 14:08:11 | 156:26 | 09 12:38 | 7:30 | mcp | amiga_get_var {"name": "title_phase"} |
| 14:08:11 | 156:26 | 09 12:38 | 7:30 | mcp | amiga_screenshot {} |
| 14:08:24 | 156:39 | 09 12:51 | 7:32 | bug | 2nd false lead: macOS FS-UAE window stops repainting when the user works in other apps (frontmost: Activity Monitor) - mac captures stale; bridge screenshot = ground truth |
| 14:08:24 | 156:39 | 09 12:51 | 7:32 | mcp | amiga_call_hook {"client": "terra_strike", "hook": "title", "args": "image"} |
| 14:08:24 | 156:39 | 09 12:51 | 7:32 | mcp | amiga_screenshot {} |
| 14:08:25 | 156:40 | 09 12:53 | 7:32 | mcp | amiga_call_hook {"client": "terra_strike", "hook": "title", "args": "image"} |
| 14:08:26 | 156:41 | 09 12:53 | 7:32 | mcp | amiga_screenshot {} |
| 14:08:27 | 156:42 | 09 12:54 | 7:32 | mcp | amiga_call_hook {"client": "terra_strike", "hook": "title", "args": "image"} |
| 14:08:28 | 156:43 | 09 12:55 | 7:32 | mcp | amiga_screenshot {} |
| 14:08:29 | 156:44 | 09 12:56 | 7:33 | mcp | amiga_call_hook {"client": "terra_strike", "hook": "title", "args": "image"} |
| 14:08:30 | 156:45 | 09 12:57 | 7:33 | mcp | amiga_screenshot {} |
| 14:09:33 | 157:48 | 09 14:01 | 7:41 | fix | PRESS FIRE now alternates yellow/white (always present) - robust for row-by-row bridge screenshots; e2e checks the pixels |
| 14:09:34 | 157:49 | 09 14:01 | 7:41 | e2e | clean slate: stop other bridge clients |
| 14:09:35 | 157:50 | 09 14:02 | 7:41 | e2e | build, deploy, launch |
| 14:09:39 | 157:54 | 09 14:06 | 7:41 | e2e | in-game self-test hook |
| 14:09:40 | 157:55 | 09 14:07 | 7:41 | e2e | main screen (ChatGPT artwork) and music |
| 14:09:43 | 157:58 | 09 14:10 | 7:42 | e2e | key injection: start and lift off |
| 14:09:46 | 158:01 | 09 14:14 | 7:42 | e2e | scene hook: tank battle, fire with aim assist |
| 14:09:52 | 158:07 | 09 14:19 | 7:43 | e2e | scene hook: ships at sea |
| 14:09:57 | 158:12 | 09 14:24 | 7:44 | e2e | landing on the pad |
| 14:10:06 | 158:21 | 09 14:34 | 7:45 | e2e | live tuning through bridge variables |
| 14:10:08 | 158:23 | 09 14:36 | 7:45 | e2e | memory region: heightmap |
| 14:10:09 | 158:24 | 09 14:36 | 7:45 | e2e | level clear |
| 14:10:17 | 158:32 | 09 14:44 | 7:46 | e2e | game over |
| 14:10:27 | 158:42 | 09 14:54 | 7:47 | e2e | status hook, process list, stop |
| 14:10:41 | 158:56 |  |  | milestone | STAGE 2 DONE: main screen on the Amiga, e2e all passed |
| 14:10:42 | 158:57 |  |  | rec_stop | 135532-09-stage2-title-convert |
| 14:16:14 | 164:29 |  |  | mcp | amiga_list_clients {} |
| 14:16:14 | 164:29 |  |  | mcp | amiga_dos_command {"command": "Status"} |
| 14:16:25 | 164:40 |  |  | phase | Stage 2 showcase RE-RECORD: FS-UAE frontmost, final yellow/white PRESS FIRE |
| 14:16:25 | 164:40 | 11 0:00 | 7:49 | rec_start | 141625-11-stage2-main-screen-rerecord-amiga-window - native screencapture -V, FS-UAE window only, re-record |
| 14:16:25 | 164:40 | 11 0:00 | 7:49 | showcase2 | launch: main screen |
| 14:16:25 | 164:40 | 11 0:00 | 7:49 | mcp | amiga_launch {"command": "Dropbox:Dev/terra_strike"} |
| 14:16:35 | 164:50 | 11 0:09 | 7:58 | showcase2 | Return: palette switch into the game |
| 14:16:35 | 164:50 | 11 0:09 | 7:58 | mcp | amiga_input_key {"rawkey": "44", "direction": "down"} |
| 14:16:35 | 164:50 | 11 0:09 | 7:59 | mcp | amiga_input_key {"rawkey": "44", "direction": "up"} |
| 14:16:37 | 164:52 | 11 0:11 | 8:00 | showcase2 | lift off |
| 14:16:37 | 164:52 | 11 0:11 | 8:00 | mcp | amiga_input_key {"rawkey": "4C", "direction": "down"} |
| 14:16:38 | 164:53 | 11 0:12 | 8:02 | mcp | amiga_input_key {"rawkey": "4C", "direction": "up"} |
| 14:16:38 | 164:53 | 11 0:13 | 8:02 | showcase2 | scene tanks + fire |
| 14:16:39 | 164:54 | 11 0:13 | 8:02 | mcp | amiga_call_hook {"client": "terra_strike", "hook": "scene", "args": "tanks"} |
| 14:16:40 | 164:55 | 11 0:14 | 8:03 | mcp | amiga_input_key {"rawkey": "40", "direction": "down"} |
| 14:16:42 | 164:57 | 11 0:17 | 8:06 | mcp | amiga_input_key {"rawkey": "40", "direction": "up"} |
| 14:16:44 | 164:59 | 11 0:18 | 8:08 | showcase2 | Esc: back to the main screen |
| 14:16:44 | 164:59 | 11 0:18 | 8:08 | mcp | amiga_input_key {"rawkey": "45", "direction": "down"} |
| 14:16:45 | 165:00 | 11 0:19 | 8:08 | mcp | amiga_input_key {"rawkey": "45", "direction": "up"} |
| 14:16:51 | 165:06 | 11 0:25 | 8:14 | showcase2 | stop |
| 14:16:51 | 165:06 | 11 0:25 | 8:14 | mcp | amiga_stop_client {"name": "terra_strike"} |
| 14:17:02 | 165:17 |  |  | rec_stop | 141625-11-stage2-main-screen-rerecord-amiga-window |
| 14:20:38 | 168:53 |  |  | phase | Web UI capture session (headless Chrome on devbench UI) |
| 14:20:38 | 168:53 |  |  | mcp | amiga_launch {"command": "Dropbox:Dev/terra_strike"} |
| 14:20:42 | 168:57 |  |  | mcp | amiga_call_hook {"client": "terra_strike", "hook": "selftest"} |
| 14:20:45 | 169:00 |  |  | mcp | amiga_input_key {"rawkey": "44", "direction": "down"} |
| 14:20:45 | 169:00 |  |  | mcp | amiga_input_key {"rawkey": "44", "direction": "up"} |
| 14:20:46 | 169:01 |  |  | mcp | amiga_input_key {"rawkey": "4C", "direction": "down"} |
| 14:20:48 | 169:03 |  |  | mcp | amiga_input_key {"rawkey": "4C", "direction": "up"} |
| 14:20:48 | 169:03 |  |  | mcp | amiga_call_hook {"client": "terra_strike", "hook": "scene", "args": "tanks"} |
| 14:20:48 | 169:03 |  |  | mcp | amiga_input_key {"rawkey": "40", "direction": "down"} |
| 14:20:51 | 169:06 |  |  | mcp | amiga_input_key {"rawkey": "40", "direction": "up"} |
| 14:20:51 | 169:06 |  |  | mcp | amiga_set_var {"name": "turn_rate", "value": "4"} |
| 14:20:51 | 169:06 |  |  | mcp | amiga_get_var {"name": "score"} |
| 14:20:52 | 169:07 |  |  | mcp | amiga_get_var {"name": "targets"} |
| 14:20:52 | 169:07 |  |  | mcp | amiga_get_var {"name": "fps"} |
| 14:20:52 | 169:07 |  |  | mcp | amiga_get_var {"name": "alt"} |
| 14:20:52 | 169:07 |  |  | mcp | amiga_call_hook {"client": "terra_strike", "hook": "status"} |
| 14:21:29 | 169:44 |  |  | mcp | amiga_stop_client {"name": "terra_strike"} |
| 14:21:29 | 169:44 |  |  | milestone | Web UI screenshots: shots/webui/01-07 (dashboard, logs, game events, tests, traffic, call detail) |
| 14:28:56 | 177:11 |  |  | mcp | amiga_launch {"command": "Dropbox:Dev/terra_strike"} |
| 14:29:00 | 177:15 |  |  | mcp | amiga_call_hook {"client": "terra_strike", "hook": "selftest"} |
| 14:29:07 | 177:22 |  |  | mcp | amiga_stop_client {"name": "terra_strike"} |
| 14:29:15 | 177:30 |  |  | milestone | Web UI test-log recaptured at 2x for the short |
| 14:31:13 | 179:28 |  |  | milestone | Short cut rendered: video/terra_strike_short.mp4 (2:46, 16 beats) |
| 14:41:18 | 189:33 |  |  | milestone | Narrated short: bf_emma (en-gb) via local Kokoro, 2:57 |
