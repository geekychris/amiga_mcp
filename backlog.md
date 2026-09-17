## Backlog
- [ ] {id:0d61} add support to develop for the atari 8 bit
  add atari 8 bit support.  This should include installers to install an emulator and tooling to make sure you can control that emulator (keyboard, joystick input, screen snapshots, single step / debugging).  Should be a platform you can turn on and off from the workbench and the workbench should have a persona for atari 8 bit vs amiga.

### New game examples (clean-room clones inspired by 8-bit / 16-bit classics)

Every entry below is a clean-room reimplementation — reproduce **mechanics**, never
copyrighted assets, level data, code or trademarked names. Each ticket must start
with a research phase covering gameplay, graphics, and sound before code lands.

- [ ] {id:b3e2} jet-lifter — collect-and-launch single-screen shooter inspired by Jetpac
  Assemble a rocket from parts falling into a single-screen arena, fuel it, blast off, repeat with escalating aliens.
  - Gameplay research: sprite-physics with jetpack thrust, part-carry state, wave scheduler + alien behaviour trees.
  - Graphics research: hardware sprite budget for ship + parts + aliens + laser bolts on classic, RTG chunky path on OS4.
  - Sound research: layered SFX (thrust drone, laser, explosion), MOD victory jingle, procedural alien squawks.

- [ ] {id:5f88} filmation-castle — isometric room adventure inspired by Knight Lore / Head over Heels
  Explore a castle of interconnected rooms in isometric projection; solve environmental puzzles with a day/night or dual-character shape shift.
  - Gameplay research: room graph, object push / pickup, hitbox reconciliation on tilted axes, deterministic puzzle solvability.
  - Graphics research: isometric sprite sort, depth cueing / occlusion, hand-drawn 32×32 room tile prefabs, foreground transparency.
  - Sound research: sparse atmospheric SFX (footsteps, door creak), optional MOD ambience, period-correct restraint (no continuous music).

- [ ] {id:c40a} sentry-ridge — solid-3D landscape strategy inspired by The Sentinel
  A rotating sentry stares across a low-poly hilly landscape; absorb objects to gain energy and teleport upward, avoid its gaze.
  - Gameplay research: line-of-sight computation over discrete tile heights, sentry rotation timing, energy economy, world generation.
  - Graphics research: flat-shaded 3D landscape renderer (span-buffer or line-fill); PPC path uses CGX for fast fill, 68k uses Blitter fills.
  - Sound research: iconic single-note synth "hyperspace" cue, ticking sentry sweep, low-freq drones on Paula / AHI.

- [ ] {id:9d17} bubble-trap — trap-and-pop platformer inspired by Bubble Bobble / Snow Bros
  Two-player co-op single-screen platformer; encapsulate enemies in projectiles, kick to pop for fruit + points.
  - Gameplay research: bubble physics + drift, chain-pop scoring, enemy AI (patrol → panic → hunter), 100+ hand-designed levels.
  - Graphics research: chunky character sprites with squash-and-stretch animation, bubble refraction FX, per-level palette cycles.
  - Sound research: catchy MOD loop for the level theme (composer-tier tune), layered squeaky SFX, boss stinger.

- [ ] {id:2eab} iron-league — armoured future-sport handball inspired by Speedball / Bitmap Brothers
  Top-down two-team arena ball game with body-check, ramps, and power-up pickups.
  - Gameplay research: ball physics with wall + player bounce, team AI positioning, power-up token spawn timing, foul rules.
  - Graphics research: shaded metallic sprites (Bitmap Brothers house style), scrolling top-down pitch, crowd sprites, scoreboard.
  - Sound research: metallic clang SFX, sampled commentator one-liners, aggressive MOD title theme.

- [ ] {id:6c5d} micro-rescuers — assign-skills-to-crowd puzzle inspired by Lemmings
  Save N of M marching creatures; assign a limited pool of skills (dig / build / climb / block / bomb) to reshape the terrain.
  - Gameplay research: destructible pixel-mask terrain, skill state machine per creature, level solvability constraints, cursor UX.
  - Graphics research: pixel-perfect terrain mutation (bitplane XOR or chunky mask), tiny 8×10 creature sprite animation, HUD panel.
  - Sound research: nursery-rhyme MOD arrangements per level, "oh no!" sample stinger, click SFX for skill selection.

- [ ] {id:e309} polar-ascent — rotating tower climber inspired by Nebulus
  Climb a cylindrical tower that scrolls horizontally as it rotates; avoid patrols, jump gaps, reach the top.
  - Gameplay research: platform layout mapped to cylindrical coords, enemy patrols on curved surface, jump physics with wraparound.
  - Graphics research: horizontally scrolling tower texture with visible curvature (per-column shading trick), reflection water bonus stage.
  - Sound research: Rob Hubbard-style bass-lead MOD loop, jump/shoot SFX, splash for water bonus.

- [ ] {id:8b74} deity-shape — god-game terraforming inspired by Populous
  Terraform an isometric world to guide your followers to overrun the enemy's followers.
  - Gameplay research: terrain height-brush actions (raise / lower), settlement growth AI, mana economy, divine intervention effects.
  - Graphics research: isometric tile deformation preserving neighbour heights, animated water shorelines, glowing divine-effect overlays.
  - Sound research: ambient MOD score with per-region variation (desert / grass / snow), tribal drum for combat.

- [ ] {id:41f5} highway-grid — isometric ramp-and-shoot inspired by Highway Encounter / Costa Panayi
  Push a mystical device up an isometric road while fending off waves of alien vehicles.
  - Gameplay research: pathing for the device (auto-push if unmolested), enemy spawn scheduling, obstacle placement fairness.
  - Graphics research: fixed-camera isometric road tiles, 32×32 vehicle sprites with 4-way rotation, muzzle-flash overlays.
  - Sound research: engine drone SFX, ricochet + explosion samples, MOD title tune only (period-correct minimalism).
