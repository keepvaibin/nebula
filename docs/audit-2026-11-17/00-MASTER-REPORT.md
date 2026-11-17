# Nebula v2: full audit

Written 2026-11-17. This replaces `NEBULA-AUDIT-2026-11-16.md`, which was a first reading and contains statements this audit has since corrected (section 13 lists them).

## 0. How to read this

**The files in this folder**

| File | What it is |
|---|---|
| `00-MASTER-REPORT.md` | This file: the verdict, the results, the causes, what to rewrite, the standing of every area, the plan |
| `A-findings-mods.md` | Every finding on the mod system (M-001 to M-177), each with file and line, the trigger and the fix |
| `B-findings-port.md` | Every finding on the port and engine (P-001 to P-183), in the same form, plus the per-renderer pass tables and the render data path map |
| `C-verification.md` | My check of each finding against the code it cites, and every correction |
| `D-run-results.md` | What was run, the results, and where the logs and pictures are |
| `E-pacing-interpolation-as-built.md` | How the frame-rate unlock and the in-between pictures are built today, with file and line for every claim (I-001 to I-016) |
| `F-interpolation-and-frame-unlock.md` | My judgement of both against the modern standard, the nine gaps, the target design and how to prove it, with the 11-17 real-time measurements |
| `G-pacing-runs-digest.txt` | The raw figures of those ten PC runs |

**Evidence marks used throughout**

| Mark | Meaning |
|---|---|
| RUN | I ran it on the current build and this is what happened |
| MEASURED | A timed run, alone on the machine |
| CONFIRMED | I read the cited lines myself and they say what is claimed |
| READ | A reader agent read the code; I checked that the cited lines are the right code but did not re-trace the whole argument |
| NOT KNOWN | Nobody has evidence; said plainly |

**What was audited:** the working tree `D:\NebulaWork\MarioGalaxyRecomp\Nebula-Clean` (branch `clean/work`, 79 files uncommitted) and LumaLink at `D:\NebulaWork\lumalink-work\lumalink-sweep`. The build tested is the one made from that tree on 11-16 at 21:30 (`luma-bins/fp1`). Nothing in either repository was changed.

---

## 1. Verdict

**The game underneath is sound. It is not ready to release, and nothing needs throwing away except one data path and the test tooling.**

What is strong, by evidence:

- The mod engine passes 418 of 428 of its own tests on the current build, and nearly the same on all three Windows renderers. RUN
- All 11 documented example mods build for nine platforms and pass their 26 tests. RUN
- 101 of 102 galaxy stops load and play 600 frames of random input with no crash, on each of the three Windows renderers and on the tablet. RUN
- The current build installs, starts and passes the mod loader suite on the tablet (ARM) and the emulator (x86-64). RUN
- The game's own code costs 0.8 to 1.4 ms per step on the PC. MEASURED
- The console's libraries are gone from the game source: no file mentions any of them. CONFIRMED
- The save writer, the updater's signature and version checks, and the mod packing format were each checked and found correct. READ

What stops a release, by weight:

1. **Nothing proves itself.** There is no automatic gate, and the hand tools that existed are dead on the current build.
2. **The engine trusts whatever crosses its edges:** mod calls at the wrong moment, pointers given to services, files from strangers.
3. **The data handed to the renderer is still console-shaped.** This is the phone and high-frame-rate cost.
4. **Only the one-mod case is tested.** Much of LumaKit and several services keep one value where each mod needs its own.
5. **Platforms diverge silently:** options that do nothing, crash safety on Windows only, a Mac build nobody has run.
6. **A few traps can lose a player's data.**

360 findings sit behind those six. Section 4 takes each cause in turn; sections 5 to 9 take every area; section 11 is the order of work.

### 1.1 What needs rewriting, and what does not

Four classes. **Rewrite** means the design is wrong for the goal and is replaced. **Rework** means the part stays and its model changes. **Fix** means the design is right and has listed faults. **Keep** means it was checked and should not be touched beyond its listed faults.

**Rewrite (3)**

| What | Why | The files and functions it covers | What replaces it |
|---|---|---|---|
| The data the game hands the renderer | It is the console's data converted at draw time, on the CPU, every frame and partly every picture | `Game/Render/Material.cpp:296-424, 481-606, 731-767` (register rows); `Game/Render/TypedDraw.cpp:73-87, 193-513` and its about 109 call sites in 47 files; `Game/Render/ParticleDraw.cpp:71-200`; `Game/Render/LightFunction.cpp:150-220`; `ShadowVolumeDrawer.cpp`, `ShadowVolumeLine.cpp:89` and the other shadow shapes; `runtime/src/render/draw_walker.cpp:202-298`; `compose.cpp:170-228`; `registries.cpp:403-512`; the importer's `cook/programs.rs` (`forms_of`) and the model format | Records prepared by the importer (model format 12), sent once; per frame only what changed; matrices, skinning and particles on the GPU. Section 8.3 |
| The test and gate tooling | It cannot run on the current build and nothing runs it automatically | `tools/verify/run.sh`, `test_plugin.py`, `tour.sh` and the about 30 scripts in P-124; the two workflows; the benchmark's verdict (`benchmark.cpp:810-812, 1438-1441, 1880`); the harness's fixed-step-only start (`host_main.cpp:993-995`); the Android test start (`Harness.java:76`) and its runner; the 514 mod tests that live outside both repositories | One harness for every check, in the repositories, run on every push. Section 11, phase 0 |
| Where the reference documentation comes from | Hand-written pages drift from the code (section 5.8) | The reference pages for services, LumaKit, LumaScript errors and the game's functions | Pages generated from `luma.h`, the service headers, `lumakit/source/*.lum` and the compiler's error table; every code sample compiled in the gate |

**Rework (9)**

| What | The change | Where |
|---|---|---|
| When mod code may run | Safe handlers and a "needs" mark on every game function, checked at build | `NebulaMods::fire` and the 29 event raise sites; `instrument.py`, `gen_types.py`; the checker's `check/body.rs`. Section 5.1 |
| Who owns what a mod sets | One value per mod, undone when that mod stops | The LumaKit modules and services listed in 4.4 |
| How files from strangers are read | One shared rule: limits before allocation, prepared files re-parsed, refuse when the check cannot finish, shaders compiled out of process | `runtime/src/mod_import.cpp`, `mod_check.cpp`, `validate_c.rs:514-518`; `cook/gltf.rs`, `modern_audio.rs`, `sound.rs`, `ui_layout.rs`, `archive.rs`, `kcl.rs`, `ghost.rs`, `table.rs`, `patch.rs`, `formats.rs`; `luma-validate` `rules/patches.rs`, `schema.rs`; the game's readers (`KCollision.cpp:28`). Section 5.6 |
| After a crash | The same on every platform, with a choice per mod | `host_main.cpp:309-326, 6091-6104`; `platform/sdl/crash_log.cpp`; the Mac host |
| The frame itself. This is an overhaul; it is listed here and not under rewrite only because the frame list's format and the four back ends survive it | The game states what each pass reads and writes; a planner orders and merges the passes and chooses load and store; nothing interrupts the main scene | `GameScene::draw3D` and where the game emits its target passes and frame copies; `draw_walker.cpp` (the pass handling, :1517-1630, 1860-2060); the Vulkan and Metal pass code. Section 8.3 |
| In-between pictures (interpolation) and the frame-rate unlock | The scheduler and the blending rules stay. Three things change around them: the per-picture work moves to the GPU, each platform is asked for a display time, and the pointer and free-look are read per picture | `draw_walker.cpp:177-489`, `compose.cpp:170-228`, `render_core.cpp:730-1319, 2579-2668`, each renderer's present code. Section 8.6 and file F |
| What the benchmark measures | Game steps and shown pictures, not drawn pictures | `benchmark.cpp` |
| The game's searches | Find by position instead of walking every object | `CollisionCategorizedKeeper.cpp`, `SensorHitChecker.cpp`, `PlanetGravityManager.cpp`, `ActorLightCtrl.cpp`, `ShadowController.cpp`. Section 8.4 |
| Threads in the game step | Workers for visibility, shadows and simple animation | File B, R8.2 and P-146 (the shared state in the way) |

**Fix (the design is right)**

Pacing and in-between pictures (8.5); saves (4.6); the installer and updater; the Android app; the guard's listed faults (5.1); settings, input, audio (section 6); the compiler's list (5.4); LumaLink's list (5.5); the mod manager (5.7); each platform's gaps (4.5).

**Keep**

The mod kernel (loader, hooks, types, guard design); the LumaScript compiler's design; LumaLink's design and its packing format; the four renderer back ends; the save writer; the updater's signature and version checks; the game's logic.

One caution on the rewrite: it is large, and section 11 puts it after the gate exists. A rewrite of the render data path without a tour that compares pictures on every renderer would repeat the mistake this audit is about.

### 1.2 Where the build stands against the goal (as of 11-17)

The goal is a build that is stable, smooth and fast on every platform, mods included. This audit changed nothing in the code, so the build is where it was. What changed is that its standing is now known.

Four marks: **Proven** (measured or tested on the current build), **Known faults** (works, with listed faults), **Not proven** (no evidence either way), **Not built**.

**Stable**

| Goal | Standing | Evidence | What is missing |
|---|---|---|---|
| The game's stages run without crashing | Proven for a short visit | 101 of 102 stops, ten seconds of random play each, on three PC renderers and the tablet | One mission never gives control. Nothing has played a long session, a full playthrough, a boss fight or a cutscene past its first seconds |
| Mods cannot bring the game down | Known faults | 418 of 428 engine tests pass | A mod call during boot crashes the game; files from strangers can exhaust or hang it; the checker lets a mod run when it cannot finish checking |
| A player's data is safe | Known faults | Read in code | Eleven traps, among them the uninstall question that deletes every save (4.6). None is fixed |
| The game recovers after a crash | Known faults | Read and tested | Windows only; no per-mod choice anywhere |
| Every platform | Windows: proven. Android: runs. Linux, Mac: not proven. iOS: not built | Runs on the PC, the tablet and the emulator | Nobody has run the Mac build on the hardware it ships for, or the Linux build on a desktop |

**Smooth**

| Goal | Standing | Evidence | What is missing |
|---|---|---|---|
| Even pictures on the PC | Proven in one galaxy | At 240 Hz every one of 10,565 presents was exactly one refresh after the last; no long step on Direct3D | Other galaxies in real time; one repeatable hitch on Vulkan |
| Low delay on the PC | Known faults | 38 to 42 ms from input to display with in-between pictures, about 29 ms without | Both come down with small changes: to about 22 ms and about 5 ms (file F) |
| The tablet at 60 Hz | Proven even, but slow to respond | Every picture at its asked time; 50 ms from step to display | One refresh of that is lost to the GPU's low clock |
| The tablet at 120 Hz | Known faults | 105 pictures a second in a light galaxy, an uneven 72 in the heaviest; no governor reacts | The per-picture work, and a governor that judges by what is shown (file F, T1 to T6) |
| Other displays | Not proven | | Variable refresh, 144 and 165 Hz, Linux, Mac |

**Fast**

| Goal | Standing | Evidence | What is missing |
|---|---|---|---|
| The PC | Proven, with large room | The game's step costs 0.8 to 1.4 ms; a picture costs about 0.3 ms of CPU | |
| The tablet at 60 | Proven | Every one of 101 stops ran at 60 steps a second or more while drawing two pictures per step | |
| The tablet at 120 | Known faults | Above | The render data path and the per-picture work (8.3, file F G2) |
| A low-end phone | Not proven | Nobody has run one | Two reference phones |
| No wasted GPU work | Known faults | The full-size scene is opened 2.4 to 8.9 times per picture on the tablet, with one full copy in every picture | The frame overhaul (8.3) |

**Proof**

| Goal | Standing |
|---|---|
| A change that breaks something is caught without a person looking | Not built. There is no gate, the old tools are dead, and the mod tests live outside the repositories (4.1) |

**How far along.** The game exists and mostly works: that is the large part, and it is done. Of the seven phases in section 11, none has started, because this was an audit. Of about 380 findings, none is fixed. The distance left is in three places that are real engineering (the gate, the per-picture and render data path work that phones need, and the mod safety layer) and in a long list of small fixes. The first of the three decides the rest: until a gate exists, no claim of "stable" or "smooth" can be kept true from one change to the next.

---

## 2. What was done

| Work | Extent |
|---|---|
| Unit tests | Built the host from the current tree and ran all 79 |
| Rust tests | Nebula's crates and LumaLink's five crates |
| Mod conformance tests | Recovered the old suites (514 test folders, 58 suites) from the lanes' work folders, rebuilt their 300-odd test mods against the current headers, and ran them on Direct3D 12, Vulkan and Direct3D 11 |
| LumaScript test lane | 26 mods built with tonight's LumaLink, 21 tests run |
| Example mods | All 11: built for nine platforms, checked, tests run |
| Galaxy tour | The old tour is dead (section 4.1), so I rebuilt it on the test harness: 102 stops, 600 frames of seeded random play each, two pictures a stop. Run on three Windows renderers and the tablet |
| Device runs | A test build with its own package id, installed beside the installed Nebula on the tablet, and on the emulator |
| Probes | Small mods written to test specific claims by running them (wrong-time calls, the stuck mission) |
| Timed runs | The built-in benchmark, alone on the machine, at 854x480 and 2560x1440, three renderers |
| Reading | Two Opus readers, read-only, 29 rounds between them: about 360 findings with file and line |
| Checking | I checked the findings against the lines they cite (section 12 and file C) |

**What the reading covered, honestly.** The readers did not read every one of about 900,000 lines. The table in section 10 says what was read completely, partly and not at all. In short: the mod kernel, all 21 services, all 22 LumaKit modules, the four renderers, the host loop, audio, input, saves, the settings menus, the Android app, the Linux and Mac hosts, the installer, and the game's per-frame code were read completely or nearly. The importer and the checker were covered by a review of every place that allocates, loops or recurses on a value from a file. The documentation was covered by six mechanical cross-checks over all 271 pages; only two pages were read sentence by sentence.

---

## 3. Results of the runs

### 3.1 Tests

| Check | Result | Notes |
|---|---|---|
| Host unit tests | 79 of 79 | Windows only; they do not build on the other platforms (P-024) |
| Nebula Rust tests | all pass | |
| LumaLink tests | 365 pass, 1 fails, 6 skipped | The failure is a stale test: it asks for LumaKit 1.2.0, the kit is 1.0.0 (M-035) |
| Mod conformance, Direct3D 12 | 418 of 428 | Was 279 to 288 of 359 on 11-12 |
| Mod conformance, Vulkan | 417 of 428 | Same ten, plus one start-check test |
| Mod conformance, Direct3D 11 | 409 of 428 | Same, plus eight picture tests that have never had a Direct3D 11 reference picture approved |
| Harness self-tests | 23 of 24 agree | |
| LumaScript lane | 21 of 21 | |
| Example mods | 26 of 26 tests; 11 mods build for nine platforms | |
| `lumalink new`, `check`, `pack`, `add need` | work | The two tool faults from the first-person report are fixed in the current source |

**The ten conformance tests that do not pass, classified**

| Test | What it is | Verdict |
|---|---|---|
| guard 1619, 1620, 1621 | A fault in a mod's timer is not recovered; the game crashes | Real, but only for hand-built C mods without unwind data. LumaLink always includes it (M-027). The engine should not depend on that (M-028) |
| safestart 6601, 6602, 6603 | The safe-start notice is not on the Mods list | Stale tests. The safe start works; the notice was moved to the Log page (M-100). Whether it should be on the list is your call |
| h/mods 2241 | Counts 5 mods where 4 expected | Likely stale (LumaKit now ships installed); to confirm |
| modlog-host 3127, 3129 | Mod log page entries missing | To classify |
| refs 3138 | A fact present where absent expected | To classify |
| startcheck 3141 | Passes on Direct3D 12 only | Real, small: the start check does not report a missing shader compiler on Vulkan or Direct3D 11 |

### 3.2 Galaxy tour (102 stops, 600 frames of random play each)

| Where | Result |
|---|---|
| Direct3D 12 | 101 of 102 |
| Vulkan | 101 of 102 |
| Direct3D 11 | 101 of 102 |
| Tablet (Adreno 740, Vulkan) | 101 of 102. The test runner printed 99: on two stops the game finished and quit cleanly, and the runner asked Android for the exit record before Android had written it (file D, D.8) |
| Emulator (x86-64, Vulkan) | Stopped at your request once the tablet's tour was complete; the tablet is the device result. Of the 61 stops the emulator ran cleanly, 60 passed (one misreported by the same runner fault) and one was the stuck mission. After that the emulator's own Android system hung and restarted, so its later stops are not valid |

The one stop that really fails is the same everywhere: **Toy Time Galaxy, the Fast Foes comet mission** (stage `FactoryGalaxy`, scenario 4). The stage loads and frames keep coming, but the player never gets control. Probes show Mario touching no surface at all, at a fixed spot, his height repeating exactly every 280 frames. The opening flight ran; the ground where it ends is missing. Three theories were tested and ruled out (a bouncy floor, the collision limit, a skipped flight). What is left is which layers of the stage are loaded for that comet mission (P-122, P-170, P-171). **It needs one check by you in normal play:** if it happens there too, that star cannot be played.

### 3.3 Wrong-time probes

Each probe makes one call from `on frame`, once on the first boot frame and once on the title screen.

| Call | First boot frame | Title screen |
|---|---|---|
| No call (control) | fine | fine |
| Raw game call needing a player (`MarioAccess::validateSensor`) | game crashes | fine |
| LumaKit `item.give(Coins)` | game crashes | fine |
| LumaKit `item.give(Health)` | game crashes | fine |
| LumaKit `camera.now()` | game crashes | fine |
| LumaKit `pointer.world()` | fine | fine |

The title screen runs inside a game scene with a player, so the unsafe window is the boot frames and, by the same reasoning, scene changes. LumaKit does not protect against it. On Windows the crash report names the right mod and tells the player to turn it off.

### 3.4 Timed runs (PC: Ryzen 9950X3D, RTX 5090; benchmark, uncapped, in-between pictures on)

| Renderer | Output | Pictures a second | The game's own step | Stutters recorded |
|---|---|---|---|---|
| Direct3D 12 | 854x480 | 2,658 to 5,219 | 0.78 to 1.28 ms | none |
| Direct3D 12 | 2560x1440 | 2,676 to 4,258 | 0.78 to 1.34 ms | none |
| Direct3D 11 | 2560x1440 | 1,415 to 2,569 | 0.77 to 1.39 ms | none |
| Vulkan | 2560x1440 | 719 to 862 | 0.86 to 1.28 ms | none |

Three cautions about this table:

- **The renderers cannot be ranked from it.** The test window was hidden. On a hidden window the two Direct3D renderers draw each picture but skip showing it; Vulkan shows every one (P-159). The Vulkan row is an effect of that.
- **The benchmark's own summary is misleading.** It times picture to picture and averages the game's time over every picture, in-between ones included (P-089, P-090). The "game step" column above is my own calculation from its raw numbers.
- **Nothing here shows stutter,** because nothing on this PC gets near a 16.7 ms step. Stutter has to be measured capped, in play, on a visible screen, and no tool can do that today (section 4.1).

### 3.5 Tablet figures

The tour on the tablet also gives the first per-galaxy figures from a real device. Conditions: SM-X810 (Adreno 740), Vulkan, 2800x1752 inside and out, fixed steps run as fast as the device goes, two pictures drawn per step, 600 frames of random play per stop. The device's heat state was not recorded. The full table, one row per stop, is in file D (D.11).

| Measure | Figure |
|---|---|
| Game steps a second, middle value over 101 stops | 164 |
| The stops' own middle values | 65 to 229 |
| Lowest single second in any stop | 60 (`OceanPhantomCaveGalaxy` 4) |
| Stops whose lowest second was under 120 steps | 26 of 101 |
| GPU time a picture, middle value over 86 stops | 5.13 ms |
| GPU time a picture, the five highest | 14.47 (`OceanPhantomCaveGalaxy` 4), 11.75 and 10.28 (`OceanRingGalaxy` 2 and 1), 9.72 (`KoopaBattleVs1Galaxy`), 9.30 (`KoopaBattleVs3Galaxy`) |
| Times the full-size scene is opened per picture | 2.4 to 8.9, middle 2.8; plus one full-size copy in every picture |
| Longest single picture on the GPU | up to 77 ms; over 16.7 ms at least once in 36 of 86 stops |

What these do and do not say:

- **On this tablet, 60 Hz is covered everywhere.** No stop fell under 60 steps a second while also drawing two pictures per step at full resolution.
- **120 pictures a second is not covered at full resolution.** That needs every picture under 8.3 ms, and five stops are over it on GPU time alone.
- **This is the top of the range.** The figures bound a weaker phone from above and say nothing more about it. A low-end phone has to be measured.
- **The scene is reopened far more than it needs to be.** In the three stops with 8 to 9 openings (`FlipPanelExGalaxy`, `DarkRoomGalaxy`, `KoopaBattleVs2Galaxy`) the log names the cause: a switch to another target and back, 3.4 to 4.4 times a picture, each one reopening the full-size scene. This is direct evidence for planning the passes (section 8.3).
- **Pass count alone does not predict GPU time here.** 8.8 openings cost 6.08 ms; 2.5 openings cost 5.12 ms. The earlier report's claim that GPU time follows the pass count stays unproven.
- **The long single pictures cannot be read as stutter.** Each stop takes two full-size pictures for the test itself, and the first pictures of a stage are in the same window. They are a reason to measure, not a measurement.

From the earlier loader run on the same tablet: at the title, one pass costs 2.0 to 2.4 ms of GPU time; "frame work" averaged 1.11 ms with one step of 18.68 ms in a 600-frame window. That long step is the one recorded as open on 11-15. Its cause is not known (file B, round 4, lists 13 candidates and the five measurements that separate them).

---

## 4. The six causes

### 4.1 Nothing proves itself

**What a modern project does.** Every change is built and tested automatically before it is accepted: unit tests, an in-game run through every level, the release packages started on each platform, and performance compared against a budget.

**What Nebula does.**

- There are two workflows: release packaging and the website. Neither runs a test, and no built package is ever started (P-023, P-043). CONFIRMED
- The galaxy tour, the random-input tester and every other hand-run check go through a test plug-in installed as a mod. The current checker refuses that plug-in because it needs a system library. RUN: I ran the tour as written; the game sat on the title for 50 minutes and the script printed "0 crash lines, 0 dumps".
- The same scripts read their results from a log file the current build no longer writes to. About 30 scripts are affected (P-124).
- The mod engine's conformance tests were left in the lanes' work folders when main became one commit. The repositories hold 26 mod tests. CONFIRMED
- The benchmark judges smoothness on pictures drawn, not game steps completed (P-089). A device that cannot hold 60 steps a second can be called smooth.
- The test harness always runs on fixed steps, so it cannot measure pacing in play. Nothing can today.
- Unit tests build on Windows only, so the Linux, Mac and Android branches of tested code are never tested (P-024).
- The Android test runner reports a false failure in about 2 runs in 100. RUN: it reads the exit record 40 to 60 ms before Android writes it; the game's own log shows a clean exit each time.
- The arena, the frame-list writer, the draw walker, the display clocks and the Android layer have no unit tests at all (P-044).
- Main is one commit that is rewritten, so there is no history to search when something breaks.

**The gap: rewrite the tooling.** It cannot be patched back; section 11, phase 0.

### 4.2 The engine trusts whatever crosses its edges

**What a modern engine does.** Anything that comes from outside the engine's own code is treated as possibly wrong: checked, limited, and when it fails, contained.

**What Nebula does.** The mod kernel is carefully built for mods that cooperate. Its defences stop at four edges.

*Calls at the wrong time.* A mod can call any of 27,095 game functions at any moment; the game's code assumes its own calling order. Rows carry no statement of what a function needs (M-014). `on frame` and waiting tasks run during boot and loading (M-014, round 8 note). RUN: four of five probes crash at boot.

*Faults the guard cannot recover.*
- A fault inside a game function the mod called ends the game. That is the right decision, but it makes the first edge matter more.
- A fault inside LumaKit, reached from another mod, ends the game, because the guard only recovers inside the calling mod's own code (M-001). CONFIRMED
- Every pointer a mod hands a service is read where the guard does not apply (M-011, M-029, with the full list).
- A mod that writes over the header in front of its own memory block stops the whole game (M-003). CONFIRMED

*Files from strangers.* Readers that trust a number written in the file:

| Input | Effect | Finding |
|---|---|---|
| Any archive | Unpacked up to 256 GiB and a million entries before it is checked | M-143 CONFIRMED |
| A small model file | The importer allocates gigabytes, inside the game | M-145 |
| A sound stream with block size 0 | Loops billions of times | M-146 |
| One command in a sound archive | A buffer sized up to 64 GiB | M-156 |
| A layout animation file | About 4.3 billion curves from a few hundred kilobytes | M-157 |
| Archives nested 8 deep | The 4 GiB limit multiplies | M-158 |
| An Ogg given as a stream | Decoded with no cap | M-159 |
| FLAC | Decoded whole before its length is checked | M-151 |
| A library mod's data schema | Exponential checking time at every game start | M-160 |
| A patch pattern with thousands of `*` | Stack overflow at every game start | M-167 CONFIRMED |
| A table file's header | About 100 GB reserved before any row is read | M-169 CONFIRMED |
| A file already in Nebula's prepared format | Passed through unchecked; the game uses its offsets after checking four bytes | M-168 CONFIRMED at the pass-through |
| A mod's screen-effect shader | May loop without bound; compiled inside the game with no time limit except on Direct3D 12 | M-152, M-162 |

The importer is built so an ordinary internal error becomes a clean refusal. Only three things end the process: running out of memory, a stack overflow, a loop that never ends. Every row above is one of those.

*The checker failing open.* If the checker cannot finish (an internal error, or its library missing), the mod is treated as not refused and its code runs (M-150). CONFIRMED in pieces. When it fails using the game's description, the game checks again without it, which skips the rules about the mod's code (M-161). CONFIRMED

*After a crash.* On Windows the next start runs with every mod off, once, then all on again; there is no per-mod choice (M-032). On Android, Linux and Mac nothing arms that at all, so a mod that crashes at start crashes every start (M-031). CONFIRMED

*Permissions.* A mod is native code in the game's process. Permissions are consent for honest mods, not a wall (M-024). The blog says a mod "can't" reach saves or the internet unless allowed.

**The gap: rework.** Not the kernel. A safety layer above it (section 5.1) and one shared rule for untrusted files (section 5.6).

### 4.3 The data handed to the renderer is still console-shaped

**What a modern engine does.** Models, materials and textures are turned into the renderer's own records once, when content is built. Per frame, the engine sends what changed. Matrices, skinning and particles are computed on the GPU or once per simulation step. A frame's passes are planned before drawing so that nothing is loaded or stored without need.

**What Nebula does.** The console's libraries are gone, but the shape of the data is the console's, converted at draw time.

| Still console-shaped | Where | Finding |
|---|---|---|
| Materials filled in as register rows at draw time: texture matrices, ambient pair, fog, alpha test, colour registers | `Material.cpp:296-424, 481-606` | P-151 |
| The cooked material format keeps the register layout; the engine finds glow colour and alpha cutoff by searching register codes at load | `registries.cpp:403-512` | P-157 |
| About 109 draw sites in 47 files work like immediate mode: copy constants, look up a texture, write vertices, build indices, every frame | `TypedDraw.cpp:193-513` | P-154 |
| Particles built as eye-space matrices on the CPU, three per particle | `ParticleDraw.cpp:71-200` | P-149 |
| Textures identified by the address of their bytes and the console's header, hashed per textured draw | `TypedDraw.cpp:73-87` | P-155 |
| Lights loaded per object draw in the eight-register model | `LightFunction.cpp:150-220` | P-142 |
| Shadow volumes drawn in two passes with vertices rebuilt, because the console had no two-sided stencil | `ShadowVolumeDrawer.cpp` | P-143 |
| A material touched once is refilled every frame for the rest of the scene | `Material.cpp:731-767` | P-173 |
| For every picture shown, every draw's matrices, skin sums and material blends are recomputed on the CPU | `draw_walker.cpp:202-298`, `compose.cpp:170-228` | P-156 |
| The frame is drawn in the console's order (draw, copy the screen, draw, bloom); the list of passes is played as written | `GameScene::draw3D` | section 8.3 |

On the PC the render thread is the named limit in the benchmark, which fits P-156. On phones the pass structure adds full reloads and stores of the picture (P-006, P-007, P-031).

This is also why I expect ray tracing to be hard: a tracer handed register recipes has to guess what each surface is. One finding already shows it (P-172: glow and water rows read from the wrong form's order).

**The gap: rewrite this path.** The four renderers stay. Section 8 has the map.

### 4.4 Only the one-mod case is tested

**What a modern mod system does.** State a mod sets is that mod's. When a mod stops, exactly its changes are undone and other mods' stand.

**What Nebula does.** The kernel does this well for hooks, numbers and tables. Above it, one value is kept where each mod needs its own:

| One value for all mods | Finding |
|---|---|
| LumaKit camera: field of view, mirror, shake | M-021 |
| Mouse look | M-059 |
| LumaKit loading tips | M-033 |
| LumaKit talk line | M-041 |
| LumaKit replay seed | M-050 |
| LumaKit galaxies and domes, by name | M-047 |
| LumaKit area, gravity and ride kind names | M-043 |
| Stage music against cutscene music | M-054 |
| HUD handles: bare slot numbers, no owner | M-042 |
| Game sound slots reused across mods | M-080 |
| Pools shared behind per-mod limits: draw textures and fonts, post programs, anim plays and events, effects, models | M-071, M-087 |
| Two mods colouring one object drift it to black | M-070 |
| A reused model part keeps another mod's colours | M-089 |
| A library mod's struct growing a field overruns an older caller | M-135 |
| A library keeps another mod's callback after that mod stops | M-136 |

No test anywhere has two mods holding the same thing and one stopping (M-036, M-174).

**The gap: rework** of the ownership pattern across LumaKit and the listed services.

### 4.5 Platforms diverge silently

**What a modern port does.** One feature list; where a platform cannot do something, the option is not offered and the player is told.

**What Nebula does.**

| Divergence | Finding |
|---|---|
| Options offered where they do nothing: HDR on Direct3D 11 and Metal, Wii Remote modes on Linux, Mac and iOS, surround outside Windows, MSAA 8x above a device's limit | P-032, P-087 |
| Linux: windowed or borderless and window size are offered and never applied; no full screen | P-103 |
| Direct3D 11: display changes during play never apply | P-110 |
| Crash reports: Windows writes a dump and a report; Android writes the log; Linux writes to a stream nobody keeps; Mac and iOS write nothing of Nebula's | P-015, P-016, P-017 |
| In-game update installs itself on Windows only | P-125 |
| HD replacement movies on Windows only; AV1 movies missing on Mac | P-116 |
| Metal treats any GPU error as fatal where the others rebuild the device | P-008 |
| Metal drops mod screen effects and mod native draws without saying so | P-009, M-074 |
| Mac: the released build is Apple-silicon only; the only recorded Mac runs were on Intel | P-035 |
| iOS: no host that can build | P-104 |
| Windows on ARM64: no fault guard and no game build, while mods ship code for it | M-006 |
| Mod camera and microphone services on Windows and Android only | M-012 |
| Android has no thermal handling; its performance hint reports the wrong figure | P-019, P-069 |
| Game speed locks to the display when within 3% of 60 Hz on Android and Mac, 1% on Windows | P-003, P-033 CONFIRMED |

**The gap: fix,** item by item, plus the rule that no option is shown where it does nothing.

### 4.6 Traps that can lose a player's data

| Trap | Finding |
|---|---|
| **Windows uninstall.** "Uninstall Nebula?" with Yes, No, Cancel. Yes uninstalls and keeps saves. **No uninstalls and permanently deletes saves, settings, achievements and mods.** | P-113 CONFIRMED |
| A save written by any other Nebula version is read as "damaged"; a newer one is dropped after two saves | P-079 CONFIRMED |
| Whole-save import moves the old files out before the new ones are in, with no recovery if interrupted | P-081 CONFIRMED |
| Save export and import leave out achievements and play statistics | P-119 CONFIRMED |
| Android: setting up again deletes the installed game before the new one is built | P-094 CONFIRMED |
| Android: the launcher and the game write one settings file from two processes | P-093 |
| Exit waits at most 5 seconds for a save still being written | P-018 CONFIRMED |
| A beta install is never offered the final release | P-092 CONFIRMED |
| Importing an older version of a mod replaces the newer one (the message does say "Older version") | M-106 CORRECTED |
| On Linux, Mac and Android, importing an update of a mod in use replaces its files while the game reads them | M-107 |
| LumaLink uninstall deletes whatever folder its data setting names | M-153 |

**The gap: fix.** Each is small. They come first in the plan because they are cheap and the cost of one happening is a player's whole save.

---

## 5. The mod system, part by part

### 5.1 Kernel (loader, hooks, types, classes, guard): keep

Read completely. The design matches how professional native mod systems work: binding by name and signature, hook chains that can change while calls run, field offsets resolved at load, a guard that recovers only when it is safe.

Measured cost, counted from the code: an unhooked game function costs one load and one branch. One `before` hook call costs 2 clock reads, about 12 calls, and no allocation by the engine.

Faults to fix: M-001 (faults inside a library mod), M-002, M-003, M-005, M-007, M-010, M-013, M-027 and M-028 (the timer path must not depend on how the compiler arranges a call), M-129 (a text format and a scan of all mods per event call).

**To add (the safety layer):**

1. *Safe handlers.* A frame of play, any stage ready, stage ended, player ready. The game's 29 events delivered to mod code, each at a safe point. The reader classified them: 6 are safe to act in today, 1 mostly, 7 read-only, 15 not safe as raised (file A, round 7).
2. *What each function needs.* The build already reads every game function with a compiler front end. The design (file A, round 7): a pass in `instrument.py` and a fixed point in `gen_types.py` give each row `needs`, `null_when`, `thread` and the reason; the LumaScript checker refuses a call whose need is not proven at that place. Traced by hand, the four calls my probes crashed on all come out marked. One catch: today's call lists leave out the template calls that reach scene objects, so the pass must add them (M-113).
3. *After a crash.* Arm the safe start on every platform; offer to turn off the named mod; keep the notice where the player will see it.

### 5.2 Services (21): fix, with a rework of shared state

Every service file was read completely. They are well specified: arguments, thread and state are checked and errors are named.

Per-service verdicts are in file A (the Area 2 closing table). The pattern faults are in 4.2 (pointers) and 4.4 (shared state). Specific ones worth naming: `save.game.export_file` during boot ends the game (M-076); a mod id with a dot gets no data folder (M-083 CONFIRMED); `assets.provide`'s limit resets on every reload and its memory is never freed (M-086); name lookups start threads without limit (M-095); a WebSocket peer can grow memory with pings (M-055); the clipboard decodes pictures from other programs inside the game (M-090); extra players leak until the scene ends (M-030).

Only one service costs anything when no mod runs: the audio mixer clears a mod buffer every block.

### 5.3 LumaKit (22 modules, 11,587 lines): rework the state model, fix the rest

Every module was read line by line. Signatures match the reference pages exactly. The faults are 4.4's list plus:

- It is not safe at the wrong time (section 3.3), and `shop.open`, `camera.now`, `pointer.world` and `cutscene.play` read scene objects with no test (M-038, M-049).
- Data schemas accept values the code cannot take; a data-only item can hold the game thread for billions of calls (M-053).
- A UI callback can run after its mod was taken out (M-051).
- The network helper authenticates no one (M-052).
- Five modules have no proving test; two are used by no test mod (M-174).

### 5.4 LumaScript compiler: keep, fix the list

The checker and most of the emitter and runtime were read. Arithmetic, division, shifts and conversions are all checked. A script error ends only that handler and the mod stays on.

Faults: a script may write `none` into a game object's pointer field (M-123); a `T?` cannot be passed on to a game parameter that accepts none (M-118); errors in a chained expression point at the chain's first word (M-121); `skip <text>` hands the game an unkept pointer (M-126); texts and callbacks are kept until the mod unloads, so a per-frame use grows without limit (M-114, M-137); any live `ref` hooks every free in the game (M-132); a mod asks for the newest version of every service it touches, so a mod rebuilt with newer tools is refused by any older game (M-116, RUN).

### 5.5 LumaLink: keep, fix the list

365 of 366 tests pass and it has its own CI. Packing is repeatable. Its updater is correct. `convert` keeps paths inside the pack and converted cheat lists never become running code.

Faults: `check` and `pack` compare against the installed game while `build` can use another, and nothing records which (M-147, RUN); the linux-arm64 build differs by host (M-142); uninstall faults (M-153, M-163); `test` passes when only one renderer left a result (M-164). About half of its internals were not read (section 10).

### 5.6 Importer and checker: rework how untrusted files are read

Section 4.2 has the list. The rework is one shared rule instead of a dozen separate readers: limits on size, count, nesting and time applied before anything is allocated or looped; already-prepared files parsed and re-written instead of passed through (`camera.rs:236-242` already does this correctly); the checker refuses when it cannot finish; shaders compiled out of process with a time limit on every renderer.

### 5.7 Mod manager and screens: fix

Read completely. Faults: M-105 (Android: a zip mod reported imported, never runs), M-107, M-106, quadratic work on the Mods page while it shows (M-101, M-103, M-104), a reload asked during another is dropped (M-154), a mod that cannot be taken out is loaded twice (M-155).

### 5.8 Documentation: regenerate the reference, correct the prose

Six mechanical checks over all 271 pages found: commands, flags, service function names, LumaKit signatures and tutorial code all match the code. The errors are:

- Three features documented as working that the compiler refuses: `hook ... every override of`, `on layers`, `c struct` (M-125, M-171).
- Code samples the compiler refuses: calls written without `()`, `vec3` alone as a value (M-115, M-120; RUN).
- About 20 other unbuilt constructs not marked as such; 80 compiler error codes on no page (M-171, M-172).
- No page warns about the boot-frame crash; one page points modders at the first frame (M-173).
- The camera page promises the body stays hidden (M-175).
- Promises the code does not keep: "cannot read a game object after it is gone", "a call from another thread is a script error", the events' "return to cancel" (M-022, M-025, M-111).

Only two pages were read sentence by sentence, and both had false statements. Expect more in the rest.

### 5.9 The first-person mod: what actually happened

1. The mod was the first real use of camera control from a mod. It found two engine gaps (the camera applied after the stick was read; no mouse movement or capture) and a LumaKit design fault.
2. Last night's uncommitted work addresses those. Read from the code: the view is now applied before the player's movement reads it; the mouse is gathered per event and used once per game step with the camera smoothed between.
3. **It has not been played, and it still has the fault you reported.** LumaKit hides the body once and never again; the game resets that flag whenever it sets up the player. After any arrival or scene change the body is drawn again (M-175). CONFIRMED
4. Other faults in that work: mirrors show the game's camera, not the mod's (M-056); one look shared by all mods (M-059); the player can stay invisible after a mod ends (M-065); a carried object may stay invisible (M-064); a fast flick is treated as a camera cut (M-068); the Mac uses accelerated mouse movement and freezes the cursor without moving it into the picture (M-057, M-058); a camera set late in the frame leaves the stick one frame old.

"Done" for this mod is: you play it through an arrival, a death, a cutscene and a mirror stage on PC and tablet, and a test replays the same.

---

## 6. The port, area by area

| Area | Standing | Verdict | Evidence and main findings |
|---|---|---|---|
| Game logic | Free of the console's libraries; 101 of 102 stops | Keep; rework four searches | Section 8.4 |
| Direct3D 12 | Read completely | Keep; fix in-frame GPU waits, first-use compiles, MSAA copies | P-025, P-028, P-048 to P-053 |
| Vulkan | Read completely | Keep; fix store and load choices, parallel recording split, mid-frame waits, hidden-window presenting | P-006, P-007, P-054, P-056, P-159 |
| Direct3D 11 | Read completely | Decide if it stays; if so fix | P-060 to P-064, P-110; no HDR, no picture references |
| Metal | Read completely; never run on the hardware it ships for | Fix, then a real run before any Mac release | P-008, P-031, P-035, P-065, P-066 |
| Pacing | Specific faults | Fix | P-002, P-003, P-041, P-174; section 8.5 |
| In-between pictures and the frame-rate unlock | The scheduler and blending are at modern standard; the delay, the per-picture cost, the display timing outside Android and the proof are not | Keep the scheduler; rework three parts; fix the rest | Section 8.6, files E and F |
| Upscalers and frame generation | Partly read | Fix | P-049, P-050, P-175, P-176 |
| HDR | Windows Direct3D 12 and Vulkan only | Fix the offering | P-027, P-032 |
| Audio | Mixer read | Fix | P-037, P-071, P-072 |
| Input | Read | Fix | P-073 CONFIRMED, P-074, P-075, P-076, P-083, P-084 |
| Saves | Read | Fix | Section 4.6 |
| Settings menus | Both read completely | Fix; split the files | P-086, P-088, P-160 to P-163 |
| Achievements | Read | Fix | P-164: seven unlock at the next stage start |
| Benchmark | Read | Rework what it measures | P-089, P-090, P-091 |
| Updater | Read | Keep the checks; fix the rest | P-092, P-125 |
| Windows Setup | Read | Fix | P-113 to P-115, P-120, P-121 |
| Android app | Every file read | Fix | P-093 to P-102 |
| Linux host | Read | Fix | P-016, P-036, P-103, P-126 |
| Mac host | Read | Fix | P-105 to P-109 |
| Crash reporting | Read | Fix on three platforms | P-015 to P-017, P-045, P-067 |
| Movies | Read | Fix | P-116, P-117 |
| Ray tracing leftovers | Listed exactly | Remove from main | File B, "RT: what to remove"; the game still builds ray-tracing data every frame with it off |
| Code health | Measured | Split and share | Three files over 6,000 lines; the same logic written four times (P-022); only 12 leftover markers in our own code |

---

## 7. Faults a player would meet

Ranked by how bad and how likely. All are in the files with their lines.

1. Answering "No" to the uninstall question deletes every save (P-113).
2. The Fast Foes comet mission in Toy Time never gives control (P-122). RUN in the harness; to confirm in play.
3. A mod calling the game or LumaKit during boot crashes the game (section 3.3). RUN
4. On Android, Linux and Mac a mod that crashes at start crashes every start (M-031).
5. Android: setting up again, then cancelling or running out of space, leaves no game (P-094).
6. A save from another Nebula version is called damaged (P-079).
7. Seven achievements unlock a stage late (P-164).
8. The first-person body reappears after any scene change (M-175).
9. Short controller presses can be lost (P-075).
10. Small diagonal stick pushes straighten on Windows, and on every platform through a second dead zone (P-073, P-084).
11. Android: one controller press in Phone mode hides the touch controls for good (P-097).
12. Options that do nothing (4.5).
13. Sound on Windows can glitch again after ten quiet seconds (P-072).
14. Movie sound and picture drift on Linux, Mac and Android (P-071).
15. Skipping a cutscene stops after 15 seconds of real time, which differs by device (P-158).
16. Long translations squeezed or overflowing in the settings menu (P-162); host messages that stay in English (P-086, P-160).
17. Windows: the window jumps back to the centre after a hand resize (P-111).
18. First entry to a galaxy can hitch while a graphics program is built on the render thread (P-010, P-052, P-065).
19. A beta is never offered the final release (P-092).
20. Importing a whole save and being interrupted leaves no save (P-081).

---

## 8. Performance

### 8.1 What is measured

- The game's own step: 0.8 to 1.4 ms on the PC, every scene, every renderer. MEASURED
- No stutter in an uncapped benchmark on the PC. MEASURED
- Tablet, in play at 2800x1752: 65 to 229 game steps a second with two pictures per step; GPU time 5.13 ms a picture in the middle stop and 14.47 ms in the worst (section 3.5). MEASURED
- Tablet, at the title: about 2 ms of GPU time for one pass; an 18.68 ms longest step in one window. From the device's log.

### 8.2 What is not known

- **Stutter in play, beyond one stage on one PC.** No tool in the repositories can measure pacing in a stage (4.1). For this audit I built a kit that can (`scriptsudit_pace.sh`) and measured Honeyhive on the PC: even pacing, no long step on Direct3D, one repeatable hitch on Vulkan (file F, section 1A). On the tablet at 120 Hz the same kit measured real unevenness (file F, section 1A, T1 to T6). Not measured: other galaxies, a low-end device, the tablet with Mario moving.
- **Anything on a low-end phone.** Nobody has run one.
- **GPU time per pass on a phone.** The total per picture is now measured on the tablet (3.5). The split by pass is not: the game can record it, and the Android test start hard-codes recording off (one line, `Harness.java:76`).
- **Whether "120 to 140 fps" on your phone meant pictures or steps.**

### 8.3 The frame and the render data path (the rewrite)

The pass tables for each renderer are in file B (round 3). For a typical frame with bloom and no MSAA: Direct3D 12 and 11 make 3 full-size draws and no copies; Vulkan makes 4 pass instances and stores depth up to three times where nothing reads it; Metal makes 4 and stores depth and stencil the same way.

**The map, in the order to do it** (file B, round 13; the model format goes from version 11 to 12):

| Step | What the importer writes | What the engine stops doing | Finding |
|---|---|---|---|
| 1 | Per material form: glow row, map rows, glow value, alpha cutoff, alpha mode | Searching register codes at load | P-157, P-172 |
| 2 | Samplers in final form; each form's texture list | Hashing a texture's address and decoding filter codes per draw | P-155 |
| 3 | Per form: a block of final constant rows, plus a short list of the rows that change | Filling register rows per draw; refilling a touched material forever | P-151, P-173 |
| 4 | Prepared material records for the 109 draw sites and for particles; fixed shapes as meshes registered once | Building vertices and indices on the CPU every frame | P-154, P-149, P-150 |
| 5 | (engine only) matrices composed once per game step; skinning and in-between blending on the GPU | Recomputing every draw's matrices for every picture | P-156 |

Then the frame's passes: choose load and store per pass (P-006, P-031), keep one pass instance under parallel recording (P-007), merge what can merge. The reader marked which lines of each renderer's table change no pixel.

**The frame itself, and why it needs an overhaul**

*What it is today.* The game writes each frame as a list of passes in the order the console drew them: clear, draw the scene, copy the screen for effects that read it, draw more, bloom, then the 2D layer (`GameScene::draw3D`). Render-to-texture passes sit in that list wherever the console did them. The renderer plays the list as written. One console habit is still reproduced on purpose: a frame copy asked for before the frame's first draw returns the last shown picture, as the console's frame buffer would have (P-021).

*What that costs, by renderer (file B, round 3, read from the code).*

| Renderer | A typical frame with bloom, no MSAA | Waste the reader marked as changing no pixel |
|---|---|---|
| Direct3D 12 | 3 full-size draws, no copies | Little: it lends the scene image instead of copying |
| Direct3D 11 | 3 full-size draws | Little |
| Vulkan | 4 pass instances, plus 1 for each render-to-texture pass, plus 1 for each frame copy | Every pass stores everything; up to 3 depth stores nothing reads; with MSAA a resolve at every pass end |
| Metal | 4, on the same pattern | Depth and stencil stored the same way |

On a PC GPU an extra pass costs little. On a phone GPU each end and reopen of the scene is a full store and load of the picture.

*What the tablet measured (RUN).* The full-size scene was opened 2.4 to 8.9 times per picture, with one full-size copy in every picture. In the stages at 8 to 9, the log names the cause: the list leaves the scene for another target and comes back 3.4 to 4.4 times per picture. With in-between pictures on, all of it is done again for each one.

*What a modern engine does.* The game does not give an order; it states what each pass reads and writes. A planner then builds the frame:

| Rule | Today | After |
|---|---|---|
| Render-to-texture passes run before the main scene | They interrupt it, up to 4 times a picture | The main scene opens once |
| The scene is copied only when something visible reads it, once, at one agreed point | One copy in every picture on the tablet | No copy in most pictures |
| Load and store are chosen per pass from what is read later | Vulkan and Metal store everything | Nothing stored that nobody reads |
| MSAA resolves once | At every pass end on Vulkan | Once |
| Bloom's composite, the 2D layer and the final picture share one pass where no upscaler sits between | Separate | Merged on phone GPUs |
| "The last picture" is asked for by name | Whatever was shown last, which can be an in-between picture (P-021) | The last real game frame |

*What changes in the code.* `GameScene::draw3D` and the places where the game emits target passes and frame copies; the walker's pass handling (`draw_walker.cpp:1517-1630, 1860-2060`); the pass code of the Vulkan and Metal renderers. The frame list's 15 pass kinds mostly stay; each gains what it reads and writes.

*How it is proved.* The tour compares every stop's pictures before and after on each renderer, and the tablet reports GPU time per pass (one line to turn on, `Harness.java:76`). Without both, this change cannot be trusted. That is why it comes after phase 0.

### 8.4 The game's own code, ranked (read, not timed)

| # | Change | Where | Expected | Risk to identical play |
|---|---|---|---|---|
| 1 | Look up collision by position | `CollisionCategorizedKeeper.cpp` | Medium to large on phones | Low |
| 2 | Find touching objects by position | `SensorHitChecker.cpp` | Medium on phones, crowded scenes | Medium |
| 3 | Skip gravity fields that cannot reach a point; index light areas | `PlanetGravityManager.cpp`, `ActorLightCtrl.cpp` | Medium in large galaxies | Medium |
| 4 | Keep a shadow's position when nothing under it changed | `ShadowController.cpp` | Medium on phones | Medium |
| 5 | Worker threads for visibility, shadows, simple animation | file B, R8.2 | Medium to large on multi-core phones | Low to medium |
| 6 | No allocation in collision queries after a buffer grows | `CollisionParts.cpp:296-318` | Medium once triggered | None |
| 7 | Constant-time draw list insert and remove | `DrawBuffer.cpp` | Small to medium | Low |
| 8 | Lights per frame, not per object | `LightFunction.cpp` | Small | Low |

**Proof for each:** replay the same recorded inputs on the old and new build and compare every object's position, velocity, gravity and state after every step. The reader calls this a step-hash replay. It must run automatically, or these changes will keep being avoided.

Also free: the Android, Linux and Mac builds lack the profile-guided step that cuts the game's CPU by about 17% on Windows (the profile is Windows-only).

### 8.5 Stutter suspects, unranked until measured

| Suspect | Finding |
|---|---|
| The settings file checked on disk, and the settings lock taken, inside the frame twice a second | P-002 |
| A graphics program built on the render thread at first use | P-010, P-052, P-065 |
| Waits for the GPU in the middle of a frame | P-028, P-030, P-048 to P-050, P-054, P-056, P-066 |
| Timers that count pictures, running 2 to 4 times too often at 120 to 240 Hz | P-041 |
| A log line flushed to disk on the game thread | P-070 |
| The 18 ms step: 13 candidates and 5 separating measurements | file B, round 4 |
| Android's performance hint told CPU time instead of frame time, so the system lowers the clock | P-069 |
| Pointer and look sampled once per game step, not when the picture is shown | P-078, P-163 |
| The game thread spinning while a save is written | P-080 |

### 8.6 In-between pictures (interpolation) and the frame-rate unlock

**Files E and F hold the full treatment; this is the summary.** An earlier version of this section, written before the code was read closely, undersold the scheduling and overstated one fault. File F states both corrections.

*What is already at modern standard (keep).* A fixed 60-step game with drawn in-between pictures. Each picture shows the game at the moment it reaches the display, at any display rate. Pictures land on real display refreshes. A stall skips what it missed. Two governors lower the picture count under load and bring it back. Rotations are blended as rotations, still objects stay still under a moving camera, teleports are caught. Android asks the display for a time per picture.

*The nine gaps (file F, section 4).*

| # | Gap | Class |
|---|---|---|
| G1 | A finished frame waits for the next game step before its pictures start: about one step of delay that is not needed (`render_core.cpp:2615-2631`, CONFIRMED). MEASURED at 240 Hz on Direct3D 12 with VSync on: the wait is 18.3 ms and a frame is whole on the display 42.4 ms after its step starts; about 22 to 26 ms after the fix | Fix, small |
| G2 | Every picture is a whole second frame on the CPU and GPU, and with VSync on one more unseen frame is drawn per step (I-002, I-003) | Rework, with the render data path rewrite and the frame overhaul |
| G3 | Only Android with Vulkan asks the display for a time; Linux has no display clock at all; variable-refresh displays are not really handled (I-005, I-007, I-012) | Rework, per platform |
| G4 | The game's speed follows the display within 1% (Windows) or 3% (Android, Apple) of 60 (I-001) | Fix |
| G5 | The pointer and a mod's camera look are as old as the frame; nothing is read at picture time (I-004) | Rework |
| G6 | Two governors that do not know of each other; rates offered that the display cannot show evenly, so 120 on a 144 Hz display gives 72; off for good after one slow stretch with a temporal upscaler (I-008 to I-010) | Fix |
| G7 | Camera cuts and teleports are mostly guessed from speed; the game states cuts from two places only (I-014) | Fix |
| G8 | A few counters count pictures (file E, section H) | Fix, small |
| G9 | The scheduler has no unit tests and nothing measures display times in play (I-016) | Phase 0 |

*Measured on 11-17* (file F, section 1A; ten real-time runs on the PC with a visible window, three on the tablet). The motion is even: at 240 Hz every one of 10,565 presents was exactly one refresh after the one before, and the game ran 60.000 steps a second with no step over 18 ms on Direct3D. The measurements also found four things the reading had not: with in-between pictures off and VSync on, a frame's present call blocks for 16.4 ms on a fast display (M1); Direct3D shows a picture two refreshes after its moment where Vulkan takes one (M2); Vulkan had one repeatable 50 to 60 ms hitch when a restarted scene first showed (M3); the render thread spins while it waits (M4). On the tablet at 120 Hz (seven runs): it does not hold 120. It showed 105 pictures a second in a light galaxy with one in six a refresh late, and an uneven 72 in the heaviest stop. The GPU's own time fits (5.6 to 6.9 ms); the time from one picture to the next on the render thread does not (7.2 to 9.8 ms against an 8.33 ms slot). No governor reacts; dynamic resolution did nothing; and in-between pictures switch off after a loading screen because the GPU's clock had dropped, sometimes for good. At 60 Hz the tablet is perfectly even but shows each frame 50 ms after its step starts.

*Verdict.* Keep the scheduler and the blending rules. Rebuild three things around them: where the per-picture work is done (G2), how each platform is asked to show a picture (G3), and what is read fresh (G5). Fix the rest. File F, section 5 has the target design and the code each change touches; section 6 has the measures that prove it.

---

## 9. What changed in gameplay behaviour (your decisions)

The port differs from the current decompilation in ways that change play. These are not bugs to fix quietly; they are yours to decide.

| Difference | Finding |
|---|---|
| Mario's collision keeps every touching surface; the console stopped at 8 | P-166 |
| A ground-pound rule for zero gravity was dropped | P-167 |
| Multiply-add rounding differs from the console's, so edge cases in collision can fall the other way | P-168 (suspected) |
| A sensor's list of touching sensors silently drops past its group size (same as the console) | P-148 |
| A rail gravity has no point at the rail's end (same as the console) | P-180 |
| The movie decoder keeps the console SDK's column-swap bug on purpose | P-118 |

---

## 10. What was not read or measured

Stated so nobody assumes otherwise.

| Area | State |
|---|---|
| LumaLink internals | About half read; `new`, `add`, `test`, `convert`, `verify`, the editor server, the GUI and Setup only in part |
| LumaScript compiler | Checker, emitter and runtime read; the parser, lexer and parts of the game-description reader not |
| Importer and checker | Every allocation, loop and recursion site reviewed; not read line by line |
| Documentation prose | 2 of 271 pages read sentence by sentence; all 271 mechanically cross-checked |
| Old mod tests | One per suite read; all were run |
| Upscaler and frame-generation files | Read for HDR and dynamic resolution; bodies mostly not |
| Post-process files for Vulkan and Metal | Not read |
| The 82 unit test files' contents | Not read; all were run |
| The game's actor classes and sound code | Not read (they are the original game) |
| The eight player files | Compared with the decompilation for conditions and constants only |
| Capped pacing in play | Not measurable today |
| Low-end phones, a real Mac, a real Linux desktop | Not available |
| Ray tracing | Not audited; a separate audit follows |

---

## 11. The order of work

Seven phases. Phase 0 comes before everything and has one owner. After it, the two agents work in parallel.

### Phase 0: make the project able to prove things

1. Commit the 79 pending files in small commits. From here main is never rewritten.
2. A workflow on every push: build, unit tests, Rust tests.
3. Bring the 514 conformance tests into the repositories; fix the stale ones (safe-start notice, guard mods built with unwind data, the mod count).
4. Replace the dead tools: the harness tour (the generator from this audit is in file D), a random-input run, the Android release build started on the emulator.
5. A real-time mode for the harness, so pacing can be measured in a stage.
6. The benchmark reports and judges game steps, and records whether pictures were shown.
7. Per-pass GPU timing available on a device.
8. The step-hash replay, automatic.
9. Unit tests build on Linux.

**Done when:** a change that breaks a galaxy, a mod test or a platform build is caught without a person looking.

### Phase 1: stop the data loss and the repeat crashes

Section 4.6 in full; the checker fails closed (M-150, M-161); safe start armed on every platform with a per-mod choice (M-031, M-032); the Toy Time mission (P-122); late achievements (P-164); the first-person body (M-175).

**Done when:** each has a test in the gate.

### Phase 2 (mods agent): the safety layer and ownership

Section 5.1's three additions; LumaKit guarded at the wrong time; section 5.6's shared rule for untrusted files; section 4.4's ownership rework; library layout checks (M-135, M-136); service versions per function (M-116); the compiler faults in 5.4.

**Done when:** the wrong-time probes pass; a set of hostile files is refused cleanly; a two-mod test exists for every shared value; six reference mods (first person, a new object, a HUD change, a ghost replay, a stage edit, a character swap) are built, tested and played by you.

### Phase 3 (port agent): the render data path and phone performance

Section 8.3's five steps, then the frame overhaul (8.3), then the in-between pictures rework (8.6 and file F), then 8.4's list. Budgets per device class in the gate. Two low-end phones as the reference.

**Done when:** every step shows identical pictures on the tour, and the budgets hold on the reference phones.

### Phase 4 (port agent): pacing

Section 8.5, measured with phase 0's real-time mode, fixed in the order the measurements give. File F, section 7 gives the order for in-between pictures and the frame-rate unlock; its first four changes depend on nothing else.

**Done when:** a capped run in three galaxies on the PC and the tablet shows every picture on time over ten minutes.

### Phase 5 (port agent): platforms

Section 4.5. No option shown where it does nothing. Crash reports and logs on every platform. A real run on Apple silicon and on a Linux desktop before either ships.

### Phase 6 (mods agent): documentation from the build

Reference pages generated; every code sample compiled in the gate; unbuilt features marked or built; the prose read against the code page by page, starting with the pages a first mod uses.

### Phase 7: tidy

Remove ray tracing from main by the list in file B. Split the three largest files. Move the logic written four times into one place. Clear the work drive (284 work folders; it was 100% full during this audit).

### Rules that would have prevented most of this

- A written finish line for every feature before work starts: its tests, and what you will play.
- "Done" means a test in the gate or a capture you have played. Never "compiled and its own tests pass".
- No rewrite without a failing test that shows why a fix is not enough.
- One priority list, changed at set times.
- Small commits on a history that is never rewritten.

---

## 12. How far to trust the findings

I checked the findings against the lines they cite (file C). Of about 355 checked:

- In every case the tool could locate, the cited code exists and is the code the finding describes.
- About two thirds are CONFIRMED from the cited lines alone. The rest are READ: the right code, the reader's argument not re-traced.
- About 25 are RUN.

Nine were wrong or withdrawn:

| Finding | What changed |
|---|---|
| P-019, Android has no performance hint | It has one; corrected |
| M-014's example, a raw call crashes on the title | It crashes at boot, not on the title |
| M-106, an older mod import says "Updated" | It says "Older version"; the replacement does happen |
| The port inverted the bounce test | Withdrawn: it matches the decompilation's current version. The copy on disk is from 6 June and is out of date |
| P-170 as the cause of the stuck mission | Ruled out by the mission's own log |
| M-166, texture size overflow | Withdrawn by the reader |
| P-082, save import races the save worker | Withdrawn by the reader |
| M-102, texture mods need a restart | Withdrawn by the reader |
| My own: Vulkan four times slower at 1440p | Withdrawn: an effect of the hidden test window |

That rate, about one in forty, is the reason every finding carries a mark.

---

## 13. Corrections to the 11-16 report

| It said | The audit found |
|---|---|
| The mod engine's state is unknown; about 80% on 11-12 | 418 of 428 on the current build |
| Nothing offers to turn a mod off after a crash | On Windows the next start runs with every mod off, once. Nothing on the other platforms; no per-mod choice anywhere |
| Crash reports are Windows only | Android writes its log; Linux writes to a stream nobody keeps; Mac writes nothing |
| GPU time follows the number of passes | Not shown. On the tablet 8.8 openings of the scene cost 6.08 ms and 2.5 cost 5.12 ms (section 3.5) |
| LumaLink's `new` and `add need` produce what `pack` and `check` refuse | Fixed in the current source |

---

## 14. Decisions I need from you

1. **Play the Fast Foes comet mission in Toy Time** and tell me whether Mario gets control.
2. The six gameplay differences in section 9: keep the port's behaviour or the console's, one by one.
3. Should the safe-start notice be on the Mods list, where a player will see it, or stay on the Log page?
4. Does Direct3D 11 stay in v2? It is a fourth copy of every graphics feature and has the least proof.
5. Remove ray tracing code from main now (it stays on its branch)?
6. The Mac: hold it until someone has run it on Apple silicon, or ship it marked untested?
7. iOS and Windows on ARM64 have no working game build. Drop them from what mods are built for until they do?
8. The default frame-rate setting is 60 on every platform. Is that what you want on a 120 Hz phone?
9. Two low-end phones for phase 3 (one Mali, one low-end Adreno).
10. May the blog's two lines be reworded (mods can do "anything"; a mod "can't" reach saves or the internet)?
11. Bloom at a lower size on phones changes the look slightly. Allowed as a phone setting?
12. The work drive: may the old work folders and ray tracing builds be cleared in phase 7?
13. The two-agent split in section 11, and who does phase 0.
