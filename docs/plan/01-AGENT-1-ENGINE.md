# 01. Agent 1: the gate, the engine and the port

Read `00-READ-FIRST.md` first. This file is your order of work. `03-LEDGER.md` lists the findings each package closes (199 are yours); the finding's full text is in the audit folder.

**Your side of the goal:** the game is crash-free and cannot lose data; pictures are even and prompt at every display rate on every platform; nothing is computed that the picture does not need; and a gate proves all of it on every push.

## The order

| # | Package | Why here |
|---|---|---|
| 1 | **E0** The gate | Nothing after it can be trusted without it. The first step (the baseline) unblocks Agent 2 |
| 2 | **E1** Data safety | Cheap, and each one can cost a player everything |
| 3 | **E2** Crashes and wrong behaviour | Stability before speed |
| 4 | **E13a** Remove ray tracing from main | Less code to carry through the rewrites that follow |
| 5 | **E8** Console limits and the gameplay rules | Small; settles the rules before the game source is changed for speed |
| 6 | **E6a** Pacing: the changes that depend on nothing | Largest effect a player feels for the least work |
| 7 | **E5** Hitches and GPU waits | The stutter suspects, now measurable |
| 8 | **E3** The render data path (rewrite) | The phone's wall, and the base for 9 and 10 |
| 9 | **E4** The frame (overhaul) | Needs E3's records |
| 10 | **E6b** In-between pictures and the frame-rate unlock (rework) | Needs E3 and E4 |
| 11 | **E7** The game step's speed | Needs the replay proof from E0; independent of 8 to 10 |
| 12 | **E9** Upscalers, HDR, option parity | |
| 13 | **E10** Audio, input, menus, text | |
| 14 | **E11** The Android app | |
| 15 | **E12** Windows, Linux, Mac | |
| 16 | **E13b** Tidy | |
| 17 | **E14** Read what the audit did not, and the last check | |

Finish a package before starting the next, with two exceptions: a crash or a data-loss fault found at any time is fixed at once; and when a package waits on a long run, start the next one.

Every package ends the same way: its ledger rows closed, its tests in the gate, the full gate green, main moved, a dated entry in `docs/plan/PROGRESS-AGENT-1.md`.

---

## E0. The gate

**Aim:** a change that breaks a galaxy, a mod test, a platform build, a picture, a replay or a timing target is caught without a person looking.

**Findings:** P-014, P-023, P-024, P-043, P-044, P-068, P-089, P-090, P-091, P-124, P-159, P-169.

| Step | Work | Done when |
|---|---|---|
| E0.1 | **The baseline.** In `Nebula-Clean`: read `git status` and every diff. Group the 79 files into small commits by subject (the first-person camera and mouse work, the swap-queue change to pacing, the benchmark change, docs, the rest). Before pushing, run the unit tests and the audit's tour on Direct3D 12. Add `docs/plan/` (this plan, `ledger.tsv`) and `docs/audit-2026-11-17/` (the whole audit folder with its scripts). Fast-forward main, push. Write `MAIN-MOVED <commit>: baseline` on the board. Then make your work tree `D:\NebulaWork\MarioGalaxyRecomp\Nebula-A1` on branch `engine/work` | Agent 2 can branch |
| E0.2 | **One entry point:** `tools/gate/gate.py`, with `--quick` (build, unit tests, mod tests on one renderer, a 12-stop tour) and `--full` (everything in file 00, section 4.2). It writes one result file and exits non-zero on any failure. No other script starts the game for a check | Both agents use it for every change |
| E0.3 | **A workflow on every push:** builds for Windows (three renderers, both game modules), Android, Linux and Mac; unit tests on Windows and Linux; the Rust tests; a start of each built package for a few hundred frames on the runner's software renderer, failing on a crash or no first frame (P-023, P-043). What needs a real GPU or the tablet runs in the local gate, whose result file is committed under `docs/plan/gate-runs/` | A red build cannot reach main |
| E0.4 | **Unit tests build and run on Linux** (P-024): add the test target to `platform/sdl`. Add tests for what has none (P-044): the game arena, the frame-list writer, the draw walker, the display clocks, the Android layer | |
| E0.5 | **Retire the dead tools** (P-124). Every `tools/verify` script that goes through `run.sh` and the old test plug-in is either rebuilt as a harness test or deleted. Nothing reads `run.log` | No script in the tree fails silently |
| E0.6 | **The tour in the repository:** the 102 stops from `audit_make_tour.py` as harness tests, per region whose data is present. Extend each stop beyond ten seconds where a stage has an opening sequence, so the stop always reaches play | 102 of 102 or a ledger row per failing stop |
| E0.7 | **Pictures:** approved pictures per stop and per renderer, a stated tolerance, and a tool to review and approve changes. Direct3D 11 gets the reference pictures it never had | A renderer change that alters a pixel is seen |
| E0.8 | **A real-time mode for the harness.** Today a harness plan forces fixed steps (`test_extension.cpp:868`, `host_main.cpp:990-995`). Add a plan word that keeps real time, with scripted stick, buttons and pointer. Make scripted input work on Android outside fixed steps, so the tablet can play a stage with Mario moving | Pacing can be measured in any stage on PC and tablet |
| E0.9 | **The pacing recorder, always there, on every platform.** The frame timeline is Windows only; make it work everywhere. Record for every shown picture: the game time it shows, the moment it was asked for, and the time it reached the display (the swap chain's full statistics on Direct3D, including the refresh count that is missing today; presentation timing on Vulkan; the presented time on Metal). Compute in the engine: steps a second, animation error, missed and doubled refreshes, delay from step start to display, render-thread time per picture. Bring `audit_pace.sh`, `audit_pace_tablet.sh` and the digests into `tools/gate` | The numbers of file 00, section 4.3 come out of one command |
| E0.10 | **The step-hash replay.** Record input per step; after every step, hash every live object's position, velocity, gravity and state. A tool replays a recording on two builds and reports the first step and object that differ. Recordings for 20 stops go into the gate, and the gate compares PC against Android | Game-source changes can be proved identical (E7, E8) |
| E0.11 | **The benchmark tells the truth** (P-089, P-090, P-091, P-159): report game steps a second; judge "smooth" on steps and on pictures really shown; keep the GPU time of real frames apart from in-between ones; offer a recommendation only on the renderer that measured it. Give Vulkan the hidden-window skip the Direct3D renderers have | The benchmark's verdict cannot be right for the wrong reason |
| E0.12 | **GPU time by pass on a device:** the Android test start passes the plan's diagnostics instead of a fixed `0` (`Harness.java:76`). Fix the `[pacing]` line that cannot answer its own question (P-068): print each thread's figure and the window's wall time | |
| E0.13 | **The Android runner:** bring it into the repository from `D:\NebulaWork\agents\luma\d\platforms`; fix the exit record it reads 40 to 60 ms too early | No false failures on the tablet |
| E0.14 | **Sanitizers and soak:** address and undefined-behaviour builds that run the tour and the mod tests; a thread-sanitizer build on Linux; a two-hour random-input run per renderer that fails on a crash or on memory that keeps growing | |
| E0.15 | **Profiles:** the Windows release links a stored profile whose staleness warnings are off (P-014). Make profile generation a job, per platform; Android, Linux and Mac get the profile-guided step they lack | |
| E0.16 | **Collision proof in normal imports** (P-169): the check that the cooked octree reaches the same prisms runs in the gate, not only behind a feature flag | |
| E0.17 | **The measurement job:** runs the pacing kit on the PC (three renderers; 60, 120 and the display's rate; VSync on and off) and on the tablet (60 and 120 Hz; a light, a middle and the heaviest stop), and compares with the targets | A table of measured against target, in the gate's result |

After E0, record the gate's first full result as the starting line. Several targets will fail; that is expected, and the later packages turn them green.

---

## E1. Data safety

**Aim:** no path in the product can lose a player's saves, settings, achievements or mods.

| Finding | Fault | Fix |
|---|---|---|
| P-113, P-114 | Windows uninstall: "No" deletes all data; it runs on the window thread | A plain question with two named choices ("Uninstall and keep my saves", "Uninstall and delete everything"), the second asking again. Run off the window thread |
| P-079 | A save from another Nebula version is called damaged; a newer one is dropped after two saves | A version rule: older versions are upgraded; newer ones are kept untouched, never rotated out, and the player is told why they cannot be loaded |
| P-081 | Whole-save import has no journal | Write the new files beside the old, verify, then swap; recover on next start if interrupted |
| P-119 | Export and import leave out achievements and statistics | Include them |
| P-094, P-095, P-096 | Android: setting up again deletes the game first; a full device gives a raw error; mod imports never check space | Build the new one fully, then swap. Check free space first. Translated messages |
| P-093 | Android: launcher and game write one settings file from two processes | One writer, or a file per process with a merge rule |
| P-018, P-080 | Exit waits 5 s for a save, then ends; the game thread spins while a save is written | Exit waits for the save to finish, with a visible "saving" state and no upper limit that drops data. A wait, not a spin |
| P-092 | A beta is never offered the final release | A beta is offered the final of the same or a later version |
| P-067 | Crash paths flush achievements and statistics with no bound outside Windows | The same bounded flush everywhere |
| P-045, P-085 | Dump request timing; a Mii import holds a lock while reading | As the findings say |
| P-082 | Withdrawn by the reader | Prove by test, close |

Each gets a test that performs the dangerous sequence (interrupting it where that is the fault) and checks the data afterwards.

---

## E2. Crashes and wrong behaviour in the game and engine

**Findings:** P-005, P-008, P-012, P-047, P-051, P-062, P-122, P-123, P-153, P-158, P-161, P-164, P-170, P-171, P-172, P-177, P-178, P-179, P-182, P-183.

- **`FactoryGalaxy` scenario 4 (P-122, P-170, P-171).** Run it. The audit's probes showed Mario in mid-air with no surface under him after the opening flight, the same on every renderer and on the tablet. Three causes are ruled out (file D, D.3). Log the layer mask in `initAfterScenarioSelected` for scenarios 2 and 4 and compare with the stage's data; check the switches that make the landing ground appear. Fix the cause. Then give the tour a check that fails on any stop where control never arrives, and run every scenario of every galaxy, not only the 102 stops.
- **Memory order and threads (P-005, P-012, P-183).** The frame list's `released` flag needs an acquire. The game's global `new` and `delete` are not thread-safe. The creator index is built without a lock. Fix each, and let the thread sanitizer in the gate prove it.
- **Device loss (P-008, P-062).** Metal treats a command-buffer error as fatal; Direct3D 11 throws on three in-frame allocations. Both recover the way Direct3D 12 and Vulkan do.
- **Wrong reads (P-172, P-177, P-178, P-153, P-179).** Glow and water rows read at the wrong form's positions; a negative view-group number writes before a table; a flat-model shadow hands over matrices it never wrote; a light choice that depends on a removed option; effect light fixed to white.
- **Achievements (P-164).** Seven unlock a stage late: use the form that sees the star just collected.
- **Cutscene skip (P-158).** Its 15-second limit is wall time; make it steps.
- The rest as their findings say.

---

## E13a. Remove ray tracing from main

File B, "RT: what to remove or switch off to take it out of main", lists every file, member, frame-list field and call. Remove all of it from main: the sources, the renderer interface's methods, the frame-list fields the game fills for it every frame (shadow drops, world lights, point lights, clip volumes, owner lights), the settings keys and the rows. The branch `ray-tracing` is not touched. Proof: the gate's pictures are unchanged, and the game thread no longer builds that data (measure the step before and after).

---

## E8. Console limits and the gameplay rules

**The owner's rule: everything grows or shrinks as needed. No fixed console size remains anywhere.**

1. **The sweep.** Search the game source and the engine for every fixed capacity that came from the console: arrays sized by a constant, pools with a maximum, lists that drop when full, counters that wrap. The audit found several (P-148 sensor lists, P-131 the 64-hit room, P-137 pools, M-010 class rows, the eight light registers of P-142) and did not read the actor classes. List every one in the ledger as a new row. Each becomes a container that grows, or is shown by test to be a design value and not a limit (the 640 by 608 design units are an example of a value that stays).
2. **P-166:** keep the port's behaviour (every plane kept). Add a test that fixes it.
3. **P-167:** restore the console's rule for the ground pound in zero gravity, from the current decompilation (not the copy on disk from June; file D, D.9).
4. **P-168:** keep the present arithmetic unless a faster form is measured faster. If you change it, the replay must match on every platform.
5. **P-180, P-118:** leave; close as `decided`.

Every change here alters play by design, so the replay will differ. Record new replays after each, and state in the commit what changed in play and why.

---

## E6a. Pacing: the changes that depend on nothing

All figures are from file F, section 1A. Each is small. Measure before and after with E0.17.

| Change | Finding | Target |
|---|---|---|
| Start a frame's in-between pictures when the frame arrives, not at the next step. Its start time follows this step's moment plus the usual time a step takes to produce its list, with the same slow, bounded tracking as today (`render_core.cpp:2615-2631, 730-871`) | G1, I-004 | The 17 to 23 ms wait is gone: 42 ms becomes about 26 on Direct3D at 240 Hz |
| On Direct3D 12 and 11 with VSync on, stop the render thread blocking for a refresh per picture | M2 | A picture is on the display one refresh after its moment, as on Vulkan |
| With in-between pictures off and VSync on at a display faster than 60 Hz, present a finished frame at once and let the display repeat it; make the step-phase code measure the step's own work, not the present call's return | M1, I-013 | About 29 ms becomes about 5 at 240 Hz |
| At rates that are multiples of 60 with pictures on refreshes, give the sequence a lead so one refresh shows the real frame (`render_core.cpp:797-798, 848`) | I-002 | No unseen frame at 120, 240, 360 Hz |
| Offer only rates the display shows evenly on a fixed-refresh display (`settings_model.cpp:84-168`) | I-008, G6 | A 144 target is never offered on a 240 Hz display |
| After a temporal-upscaler suspend, come back (`render_core.cpp:903-924`) | I-009, P-174 | |
| The render thread no longer checks the settings file on disk or takes the settings lock inside a frame | P-002 | |
| A log line is never flushed to disk on the game or render thread | P-070 | |
| The game's speed locks to the display only within 0.2% of a multiple of 60, on every platform, and only as file F, G4 says | I-001, P-003, P-033 | First read how the music and sound follow a locked step (file F, 9); that was not read |
| Timers that count pictures count time or steps | P-041, P-147, I-011 | |
| Remove the dead `frame_rate_decouple` branch | P-001 | |
| The Android vsync thread and the Apple display link stop while the game is in the background | P-004, P-034 | |

---

## E5. Hitches and GPU waits

**Findings:** P-010, P-011, P-026, P-028, P-029, P-030, P-048, P-049, P-050, P-052, P-054, P-056, P-057, P-058, P-059, P-060, P-061, P-064, P-065, P-066, P-117, and the measured ones: the 18 ms step, M3. (P-002 and P-070 belong to this package in the ledger and are done early, in E6a.)

- **Nothing compiles on the render thread during play** (P-010, P-052, P-065). Every graphics program a stage needs is known from its data: build them while the stage loads, off the render thread, with the load screen waiting on them. The Metal shaders are built when the app is built, not from source at run time.
- **Nothing waits for the GPU in the middle of a frame** (P-028, P-030, P-048 to P-050, P-054, P-056, P-057, P-066): deferred release of old resources in place of a full wait; samplers and tables sized so they never fill in play.
- **The 18 ms step.** File B, round 4, lists 13 candidates and the five measurements that separate them. Make those measurements with E0's recorder and remove the cause.
- **The Vulkan hitch at a scene's first picture** (file F, M3): one picture 50 to 60 ms late, repeatable, outside the frame walk. Trace the render thread across a scene restart and remove the cause.
- **The tablet's first start** spends about 20 seconds on the boot screen with an empty cache; show progress and do the work off the main path, or ship a prepared cache.

Done when: the gate's "no step over 18 ms" and "no hitch over one refresh at a scene's start" hold on every renderer.

---

## E3. The render data path (rewrite)

**Aim:** the game hands the renderer records prepared when content is built. Per frame it sends what changed. Nothing the console needed remains in the path.

**Findings:** P-046, P-142, P-143, P-144, P-145, P-149, P-150, P-151, P-154, P-155, P-156, P-157, P-173, P-181. **Evidence:** file B, R13.1 (the map) and R9, R3; file F, T2.

**Before you start: the trace.** On the tablet at 120 Hz the GPU's own time fits the slot and the render thread's time from picture to picture does not (7.2 to 9.8 ms against 8.33). The audit did not separate why. Trace the render thread per picture on the tablet: CPU in the walk, waits on the GPU or on frame fences, the present call. If the render thread and the GPU work one after the other on each picture, fix that first (more than one frame in flight, with per-frame resources); it may be the largest single gain and it shapes everything below.

**The steps, in this order.** The model format goes from 11 to 12. Tell Agent 2 on the board before each format change.

| Step | The importer writes | The engine stops | Findings |
|---|---|---|---|
| 1 | Per material form: which row glows, the map rows, the glow value, the alpha cutoff and mode | Searching console register codes when a model loads (`registries.cpp:403-512`) | P-157, P-172 |
| 2 | Samplers in final form and each form's texture list | Hashing a texture's address and decoding filter codes per draw (`TypedDraw.cpp:73-87`) | P-155 |
| 3 | Per form: a block of final constant rows, and a short list of the rows that change with where each comes from | Filling register rows at draw time, and refilling a touched material every frame for the rest of the scene (`Material.cpp:296-424, 481-606, 731-767`) | P-151, P-173 |
| 4 | Prepared material records for the roughly 109 immediate-style draw sites in 47 files and for particles; fixed shapes as meshes registered once | Building vertices and indices on the CPU every frame (`TypedDraw.cpp:193-513`); three CPU matrices per particle (`ParticleDraw.cpp:71-200`); shadow volumes drawn twice with rebuilt vertices | P-154, P-149, P-150, P-143, P-181 |
| 5 | (engine only) both poses of everything sent once per step; a small GPU pass makes each picture's matrices; skinning on the GPU | Computing every draw's matrices, skin sums and material blends on the CPU for every picture (`draw_walker.cpp:202-298`, `compose.cpp:170-228`) | P-156, P-144, P-046 |
| 6 | | Loading lights per object in the eight-register form; light tables per frame | P-142 |

**Rules for this package**

- All four renderers move together at each step. Direct3D 11 is kept, so it gets every step.
- Each step is proved by the gate's pictures on every stop and renderer, and measured on the tablet (render-thread time per picture, GPU time by pass) and on the PC.
- The mod draw services (`draw.mesh`, `draw.inst`, `draw.native`, post programs) keep working; the mod tests are the proof.
- When step 5 is in, the gate's measure "render-thread work for an in-between picture does not grow with the number of objects" must hold: measure it with a synthetic scene of 500 and 5,000 objects.

---

## E4. The frame (overhaul)

**Aim:** the game states what each pass reads and writes; a planner builds the frame. The console's draw order is gone.

**Findings:** P-006, P-007, P-021, P-025, P-027, P-031, P-053, P-055, P-063. **Evidence:** file B round 3 (the pass tables per renderer, with the lines that change no pixel); master report 8.3; file F, 1A (the tablet opens the full-size scene 2.4 to 8.9 times per picture, with one full copy in every picture; bloom is a quarter of GPU time in the heaviest stop).

| Rule | Today | After |
|---|---|---|
| Render-to-texture passes run before the main scene | They interrupt it, up to 4 times a picture | The main scene opens once |
| The scene is copied only when something visible reads it, once | One copy in every picture on the tablet | No copy in most pictures |
| Load and store chosen per pass from what is read later | Vulkan and Metal store everything | Nothing stored that nobody reads |
| MSAA resolves once | At every pass end on Vulkan; an extra copy on Direct3D 12 and 11 | Once, no copy |
| Bloom's composite, the 2D layer and the final picture share a pass where no upscaler sits between | Separate | Merged on phone GPUs |
| One pass instance under parallel recording | Split on Vulkan | One |
| "The last picture" is asked for by name | Whatever was shown last, which may be an in-between picture | The last real frame |
| Bloom's size | Full chain everywhere | A phone setting for a smaller bloom (the owner allowed it); measure the gain and the difference in the picture |

The frame list's pass kinds gain what they read and write. `GameScene::draw3D` and the places that emit target passes and frame copies change; the walker's pass handling (`draw_walker.cpp:1517-1630, 1860-2060`) becomes the planner's output.

Done when: the gate's measure "the full-size scene is opened once per picture when nothing reads it back" holds on the tablet in every tour stop, pictures are unchanged, and GPU time by pass on the tablet is recorded before and after.

---

## E6b. In-between pictures and the frame-rate unlock (rework)

**Read file F whole before this.** The scheduler and the blending rules stay; file F, section 3 lists what is already right. This package builds what is missing around them.

**Findings:** I-001 to I-016, P-042, P-069, P-078, P-163, and file F's G1 to G9, M1 to M4, T1 to T7 (those not closed in E6a).

### 6.1 The scheduler as its own unit, under test (G9, I-016)

Move the scheduling out of `render_core.cpp` into a unit that takes a clock and a display model and can run against a simulated display. `present_slots_tests.cpp` is the model: every display rate in file E, A.5; steady, varying and spiking load; no uneven picture away from a hitch. Test the start time, the moments, the replacement of a sequence, the governors.

### 6.2 A display model per platform (G3)

Each platform answers two questions: when will a picture presented now be shown, and when was the last one really shown.

| Platform | Build |
|---|---|
| Windows, Direct3D 12 and 11 | Read the swap chain's statistics always. An adapting lead, as Android's controller has, so a picture starts as late as it safely can |
| Vulkan, any system | Use `VK_EXT_present_timing` where the driver offers it, checked at start; otherwise the platform's fallback |
| Android | Add `VK_EXT_present_timing` (Android 17 and later) beside the existing display timing. The newest-only queue (VSync off) gets timing or is not used for timed pictures |
| Linux | **Build the display clock that does not exist** (`display_clock.cpp:193-211`): present timing where present, otherwise a refresh grid measured from when presents complete. Until one works, in-between pictures on Linux are marked unsynchronised in settings. The owner tests on a desktop later; the code is complete and compiles |
| Mac, iOS | The newer Metal display link with a target display time per frame; present at that time; ask for a rate range; never drop an in-between picture because a drawable waited (I-006). Complete in code; the owner tests later |
| Variable refresh | Its own path: the engine sets the rhythm with evenly spaced presents, each drawn for its own moment, capped a little under the display's maximum. Detect it from the system, not from a measured rate that in-between pictures hide (I-007). Any target rate is then even |

### 6.3 The cost of a picture (G2, T2)

- **No unseen frame.** At rates that are not multiples of 60 the walk at `t = 1` exists only so later frames can read what a frame wrote. With E4's named "last real frame" it goes (`render_core.cpp:1160-1171`).
- **Blend on the GPU** (E3, step 5).
- **Record once per step, replay per picture.** With all per-picture data in buffers, a step's draw commands do not change between its pictures. Check each API's documentation for what may be replayed; where it may not, recording without per-draw arithmetic is still cheap. Measure both.
- **Do not redraw what does not depend on the picture's time.** Measure each pass first.

### 6.4 Holding the target without switching rates (T3 to T6; the owner's decision)

The owner ruled out falling to 60 automatically. The target rate is the player's (and the first-start measurement's). Holding it is the engine's job:

1. **Speed first:** 6.3, E3, E4 and the trace. The gate's target is the tablet at 120 Hz in every tour stop.
2. **Hold the phone's clocks.** The performance hint is told CPU time only, and the wrong figure (P-069). Report the real wall time of a picture's work, CPU and GPU, against the slot as the target, with the newest hint calls the system offers. The GPU must not drop its clock during a loading screen and then be judged at that clock (T4).
3. **Dynamic resolution holds the slot.** It must react to the real limit: time from picture to picture against the slot, at the held clock. On the tablet it did nothing while pictures were late (T3).
4. **The governors change.** No decision may rest on a GPU reading taken at a clock speed the decision itself caused. The picture count does not flip during play. The one automatic reduction that stays is the guard that protects the game's own 60 steps; when it acts it places pictures evenly (T5: today the capped state drifts to one-then-three refreshes) and it returns by trying, with back-off. The gate requires that it never acts on the tablet in any tour stop.
5. **If the target still cannot be held for a sustained time,** the game says so in settings and suggests what to change. It does not switch by itself.
6. **The first-start measurement** picks a target the device holds; fix what it reads (P-090).
7. **Feed the system's thermal headroom** to dynamic resolution. There is no heat rule today (file E, I.2).

### 6.5 What the hand controls is read per picture (G5)

- The pointer and the settings menu's cursor are drawn from the newest device sample at the picture's moment; the game's logic keeps its per-step value (P-078, P-163).
- A mod's free-look camera: the engine applies the newest look rotation to the picture's view. You build the host and renderer side and tell Agent 2 the call on the board; Agent 2 adds the LumaKit mark.

### 6.6 Stated cuts and teleports (G7, I-014)

The camera code states every cut; the code that warps or respawns an object states it. One call each, through `MotionSignal`. The thresholds stay as a net. Give Agent 2 the call for mods. `camera_epoch` in the frame list is either used or removed.

### 6.7 Defaults and settings

- The default stays 60 on every system and a player chooses a higher rate (P-042; decided 11-22, the owner: "should be 60fps.").
- The 59.94 lock as decided (E6a).
- Every setting on these pages is shown only where it does something (see E9).

Done when: every measure in file 00, section 4.3 is green on the PC and the tablet.

---

## E7. The game step's speed

**Findings:** P-013, P-038, P-039, P-040, P-088, P-127 to P-141, P-146, P-152, P-165. **Evidence:** file B, R8.3 and R9.9 (ranked), R8.2 (what blocks worker threads).

Every change is proved by the replay: identical before and after.

| # | Change | Where |
|---|---|---|
| 1 | Find collision by position | `CollisionCategorizedKeeper.cpp` (P-129, with P-130, P-132, P-133, P-134, P-138) |
| 2 | Find touching sensors by position | `SensorHitChecker.cpp` (P-039, P-136) |
| 3 | Skip gravity fields that cannot reach a point; index the light areas | `PlanetGravityManager.cpp`, `ActorLightCtrl.cpp` (P-139, P-152) |
| 4 | Keep a shadow's position when nothing under it changed | `ShadowController.cpp` (P-140) |
| 5 | Worker threads for visibility, shadows and simple animation, after removing the shared state in their way | P-040, P-146, file B R8.2 |
| 6 | No allocation in collision queries | `CollisionParts.cpp:296-318` (P-131) |
| 7 | Constant-time draw list insert and remove; constant-time arena lookup | `DrawBuffer.cpp` (P-038), P-013 |
| 8 | The small ones | P-127, P-128, P-135, P-137, P-141, P-165, P-088 |

Measure the step on the tablet in the five heaviest stops before and after each. Record the figures.

---

## E9. Upscalers, HDR and option parity

**Findings:** P-009, P-032, P-087, P-175, P-176.

- **The rule:** no option is shown where it does nothing. Go through every row of every settings page on every platform and renderer (P-087 has the list found so far) and make each either work or not appear.
- **Direct3D 11 stays** and reaches the others' standard: its display changes during play (P-110, in E12), its parity gaps (P-009), its reference pictures (E0.7), the same tests. Where it cannot do something (HDR), the option is not shown.
- FSR 3 is told when the render size moves (P-175). SGSR 2 and MetalFX get a correct mode for wide colour (P-176).
- Vendor frame generation stays as it is.

## E10. Audio, input, menus, text

**Findings:** P-037, P-071, P-072, P-073, P-074, P-075, P-076, P-077, P-083, P-084, P-086, P-106, P-160, P-162.

- **Input:** short presses are never lost (P-075: carry presses as events, not levels); a round dead zone on every platform, once (P-073, P-084); the HID worker never stalls (P-077, P-083); the Nunchuk's centre (P-074); the mouse-held look's host side on every platform (P-076, with Agent 2's M10).
- **Audio:** no allocation on the audio thread (P-037); movie sound and picture stay together on every platform (P-071); the Windows output does not lower its buffer back into glitching (P-072), and counts under-runs so the gate can assert there are none.
- **Text:** every host text is translated in all 14 languages by id, not by building an id from English (P-086, P-160, P-106); long translations fit at the smallest window and largest text in every language (P-162), with a test that renders each page in each language and checks nothing overflows.

## E11. The Android app

**Findings:** P-019, P-020, P-097, P-098, P-099, P-100, P-101, P-102 (and P-093 to P-096 in E1, P-069 in E6b, P-004 in E6a).

A configuration change must not recreate the game activity in a process whose native game cannot start twice (P-098). One controller press must not hide the touch controls for good (P-097). The restart keeps the launcher in the task (P-099). The rest as the findings say.

## E12. Windows, Linux, Mac

**Findings:** P-015, P-016, P-017, P-035, P-036, P-103, P-104, P-105, P-107 to P-112, P-115, P-116, P-120, P-121, P-125, P-126.

- **A crash report on every platform** (P-015, P-016, P-017): a dump or stack, the log, and a report file the player can find, on Linux and Mac as on Windows and Android. On Android, Linux and Mac the crash handler also writes the safe-start record; Agent 2 tells you on the board what it must hold (M5).
- **Linux:** windowed, borderless, full screen and window size all applied (P-103); a log file from the app menu (P-016); the file picker without a portal (P-036); the display clock (E6b); the install scripts (P-126). Complete and compiling; the owner tests later.
- **Mac:** compiles in the workflow for Apple silicon; device loss recovers (E2); the display changes, the texts, the screens and the rumble as the findings say; crash reports. Complete and compiling; the owner tests later.
- **Windows:** the window keeps its place after a hand resize (P-111); Direct3D 11 applies display changes during play (P-110); Setup lays out by the window's own DPI (P-112, P-115) and accepts install folders that are not NTFS where that is safe (P-120).
- **Updates** install themselves on Mac and Linux as they do on Windows, or the game says plainly how to update (P-125). HD movies on every platform; AV1 on Mac (P-116).
- **P-104** (iOS has no host): out of scope; close as such.

## E13b. Tidy

- The same per-renderer logic written four times moves into one place (P-022): pass timing averages, the sync choice, capture encoding, the frame's opening housekeeping.
- Split the three files over 6,000 lines (`renderer_d3d12.cpp`, `host_main.cpp`, `native_settings.cpp`) along their natural parts.
- Stale and wrong comments found by the audit (I-015) are corrected; so is every comment your own changes made untrue.

## E14. Read what the audit did not, and the last check

The audit's master report, section 10, lists what was not read. On your side:

| Not read | Do |
|---|---|
| The upscaler and frame-generation files' bodies | Read whole |
| The post-process files for Vulkan and Metal | Read whole |
| The 82 unit-test files | Read what each asserts; note what is tested only by name |
| The game's actor classes and the sound code | Read for crashes, fixed limits (E8), per-step waste and rate dependence. This is the original game's logic: do not change play, and prove every change by replay |
| The eight player files | The audit compared conditions and constants with the decompilation only. Read them whole |
| Parts of `draw_walker.cpp` and two renderers' `interpolation_mode` (file E, coverage) | Read |

Every fault becomes a ledger row and is closed like the rest.

Then the last check, before you may write the sentence in file 00, section 4.5:

1. The ledger has no open row of yours.
2. The full gate is green on main, on a clean checkout.
3. Every measure in section 4.3 is green, with the measured values written down.
4. Run the audit's own checks again on the final tree (the tour on every renderer and the tablet, the pacing kit, the benchmark) and compare with file D.
5. Read your own diff of the whole work once, start to end, as a reviewer who distrusts it.
