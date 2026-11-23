# Nebula: the native plan (2026-11-20)

Everything that still has the console's shape, or that the rest of the game is built on, in the order it should be
done. Made from the two reports (`NEBULA-CONSOLE-FREE-REPORT-2026-11-20.md`, `NEBULA-REPORT-2026-11-19.md`), the
order file (`D:/NebulaWork/plan/08-CONSOLE-FREE-ORDER.md`), the ledger on main, the inventory
(`docs/engine/CONSOLE-FREE.md` on branch `docs/console-free`) and the design file (`docs/engine/FRAME-MODEL.md`).
Nothing new was searched for. No code was changed and no test was run to write it.

This file is the overview: the order and the reasons. Every step is written out, with where it changes the code,
what it needs first and how it is proved, in the step files beside it. Every ledger row is listed in plain words
in the items file (`06-NATIVE-ITEMS-PLAIN.md`, kept with the plan's author, not in the repository).

The order of the steps is `docs/plan/native/steps.tsv`; the tool `tools/plan/plan.py` reads it (`next`, `start`, `land`,
`blocked`, `status`). The repository's copy of this folder is the only one read.
The `commit` column of `steps.tsv` holds the code's commit on the step's branch; the commit main has for the landing is another one (the queue replays the branch's commits), and `landings.tsv` in the live folder, the page and `STATUS.md` show it as `main`. `step-checks.tsv` lists the extra checks a step's proof asks for (step, check, the proof item it serves); `plan.py next` and `plan.py card` print them and `plan.py land` sends them to the gate as `--also`.

## 1. Where the game stands

- **Main is at 790626cb5** (moved 2026-11-20 05:42).
- **Not 100%.** 210 open ledger rows belong to this plan. They fall into 24 packages (section 4).
- **Written, not on main (9 branches):**

| What | Branch | State |
| --- | --- | --- |
| The four renderers share one copy of their common code, parts 3 and 4 | `rows/p022c` 4a67ed3e6, `rows/p022d` ec91acc35 | pushed, on today's main, built |
| Models and materials read from typed records (render data path step 1) | `f4/e3-1a` 58900bf8b, `f4/e3-1b` b9dc99ecb, `f4/e3-1c` d32c2c573 | local, on an older main: must be moved onto today's |
| Samplers decided when the game's files are made; texture ids taken at load (step 2) | `f4/e3-2a` 49858e8bd, `f4/e3-2b` 0d73751e9, `f4/e3-2c` c7731c9ea | local, on an older main |

- **Designed, not written:** render data path steps 3 to 6, the frame planner, how a picture reaches the display,
  the game step on all cores, "every actor runs" (FRAME-MODEL 2.11 to 2.18).
- **Listed with a decided modern form, not written:** about 60 inventory items (CONSOLE-FREE.md section 3).
- **The inventory itself is not on main.** The only complete copy is on `docs/console-free` (a172aa701). It lands
  first, with the ledger upkeep the reports say is owed (rows whose code is on main and still read open).

## 2. What "done" means

One commit of main on which all of this holds, each checked then and not from memory:

1. Every row of this plan is closed with its proof. Every inventory line is closed. The inventory's whole-tree
   searches, run again, find only the listed stays.
2. FRAME-MODEL section 1.9 (the frame's faults) is empty.
3. The same recorded play gives the same game on Direct3D 12, Direct3D 11, Vulkan, the tablet and a software
   device, at 4:3, 16:9, 21:9, 48:9 and the tablet's and a tall phone's shape.
4. Pacing holds on the three Windows renderers and the tablet at every offered rate, and no figure is worse than
   today's baseline.
5. The full test set is green on that commit.

Then the statement "100%" is written at the top of the inventory with the commit, and a Windows build and an
Android build are made from it as the proof. No release.

## 3. Why this order

The earlier order put four things in places that would have forced work to be done twice. This order moves them.

1. **The game stops reading its own picture first (S0.2), before the frame planner.**
   Today the game step asks the graphics card for depths (the sun's glare, the pointer, talk balloons) and waits
   for the answers of two frames ago. That is the console's "peek at the frame buffer". It costs 16 to 18% of the
   game thread's time in the measured stages (N-1068), it makes a recorded play depend on the graphics card and the
   machine's load (P-242, P-332), and the frame planner's design carries machinery only to serve it. Done first,
   the planner is written once without it, every later proof ("the replay is identical") becomes trustworthy, and
   the Direct3D 11 fault can no longer change how the game plays.
2. **The step is made fast before "every actor runs" is judged (G4 before G5).**
   "Every actor runs" is kept only if the measures hold. The step-speed rows cut exactly the per-actor costs that
   decide that (gravity, collision, sensors, shadows searched without any index). Judged before them, the better
   choice could fail its measure for a reason that is already on the list to fix.
3. **Shader stages are rewritten with material constants (R3), not after.**
   Both are written by the same part of the importer. Done apart, that writer and every cooked program would be
   remade twice.
4. **Dead fields come off main before step 3 (S0.3).**
   Main still writes and carries data that only shelved work read (N-1053, N-1054). The new material and frame
   records should not be designed around fields nothing reads.

5. **The two surround steps come after the tablet's pacing steps (G6.2, G6.7).**
   The owner (11-22) wants 3.1, 5.1 and 7.1 in the game's options and wants to try them early. G6.7 (the surround
   law and the player's Speakers choice) needs G6.2 (the sends named as records) and nothing else; both need nothing
   of S0.2, S0.3, R1, R2 or G2 landings 1 to 5 (their cards name no file those steps change but the settings' texts, which G6.7 reads as
   they stand). The owner of 11-23 ("move all android pacing issues and any dependencies on native it might have
   to before audio ... for perfect modernization") put the tablet's pacing steps before them: G2.1 to G2.5 stand at
   orders 23 to 27 and G6.2 and G6.7 at 28 and 29 of `steps.tsv`. His reason: the tablet's uneven 120 Hz is what he
   meets when he plays. R1 and R2 stand before G2.1 because a pipeline made inside a draw and a sampler table that
   starts again would be counted by the pacing measures as late moments; G1 stands after them because only G2.6
   needs it (G1.1). The rest of G6 keeps its place and waits for the two (G6.1 at 115, G6.3 to G6.6 at 116 to 119,
   G6.8 at 120). It does not go into a second beta: the owner (11-22): "just beta 1 so far", no second beta is
   planned.

Three smaller placements with the same reason:

- **Views as records (P-254) before poses on the GPU (R5).** Step 5 computes each picture's matrices per view; it
  needs a view to be a record, not latched state read back.
- **The input record (G1) before fresh pointer sampling (G2 landing 6).** Landing 6 samples the pointer per
  picture; it should sample the final record.
- **16-bit limits inside the package that rewrites their code.** The particle index (P-188) goes with the particle
  records in R4, the layout widths (P-218) with the layout records in R10, so no record is written narrow and
  widened later.

## 4. The order

Stage 0 comes first and is short. Then two lanes run side by side. They are cut so that they do not edit the same
files (section 5 lists the few they share). The end check closes both.

How the work is done: one agent does every step, alone, in the order of `docs/plan/native/steps.tsv`, by
`docs/plan/native/AGENT.md`. A step reaches main only through its landing, and while a landing plays the agent
writes the next step on top of it. The step tool's list for a second agent is not used.

- **Lane R, the picture:** what the game hands the graphics card and how a frame is built.
- **Lane G, the game and the display:** the step, timing, input, sound, text, data.

"Design" names where the form is already written down. "Proof" is what closes the package.

### Stage 0. Solid ground

**S0.1 The written work lands.** Rows: P-022.
- The two shared-renderer branches land as they are. The six render-data branches are moved onto today's main and
  land as two groups (step 1, then step 2). The inventory and the owed ledger rows land with them.
- For a player: nothing visible. Step 2 moves each texture's first upload from its first draw to the loading
  screen, which removes a class of small hitches.
- Known corrections owed on these branches: FRAME-MODEL 2.10 must say the per-frame texture look-up stays and only
  the copy is skipped (found while writing step 2).

**S0.2 The game step reads nothing from the picture or the wall clock.** Rows: P-294, P-242, N-1068, P-249,
P-260, P-214, P-261 (and the parts of P-219, P-293 and P-295 that belong to the deleted path).
- The pointer's world point and the talk balloon's "is something in front" come from a query of the scene on the
  game thread (the owner's answer of 11-19: cast from the world).
- The sun's glare is decided on the graphics card when the flare is drawn. The game step keeps no flare state that
  depends on it.
- The whole question-and-answer path to the graphics card is deleted in all four renderers, with its book-keeping
  and the game thread's wait (N-1068).
- Drawing gets its own random stream (the owner's answer; P-249). A stage's load is cut into slices by work done,
  not by 4 ms of wall time (P-260). Draw code that does step work moves into the step (P-214, P-261).
- For a player: a lighter game thread, a pointer that picks the same thing on every machine, and on Direct3D 11 no
  more flare through walls.
- Design: inventory section 3, "The game's picture read back for a step". The scene query's exact form is written
  into the inventory line before its code.
- Proof: one recorded play gives the same step hashes on the three Windows renderers, the tablet and a software
  device, idle and under load; a search for the old path finds nothing. The replay references are recorded again
  once, and the landing says so.

**S0.3 Dead fields left by shelved work come off main.** Rows: N-1053, N-1054.
- For a player: nothing visible; slightly smaller files and frame data.
- Proof: the cook's output and the frame list no longer hold them; pictures unchanged.

**G6.2 and G6.7 stand after the tablet's pacing steps** (orders 28 and 29, after G2.1 to G2.5; their package G6 is described under Lane G). G6.2: a
note's sends and the sound settings as typed records. G6.7: the surround law, and one "Speakers" setting in the sound
options (Auto, Mono, Stereo, Encoded stereo, 3.1, 5.1, 7.1; a layout the device has no speakers for is not offered).
- For a player: real surround on a device with more speakers, chosen or automatic; two-channel sound as made.
- Proof: the law's tests (card G6.7), every choice against every device layout, the step's replay identical.

### Lane R. The picture

**R1 Pipelines made ahead.** Rows: P-010, N-1067, P-052, P-065.
- The list of programs a stage needs already comes with the game's files. What is left: they are all made on
  worker threads while the stage loads, the load screen waits for them, and a draw never compiles on the render
  thread. Showing a picture then never waits for a compile.
- For a player: no hitch the first time something new appears.
- Design: in the rows' own notes (P-010, P-052, P-065, N-1067). P-052's Direct3D 12 half is already on main.
- Proof: a tour on an emptied cache on Direct3D 12, Vulkan and the tablet: nothing is built on the render thread
  and no picture waits for a build after a stage's first picture.

**R2 Textures and samplers, the rest of step 2.** Rows: P-155, P-048, P-056, P-066, P-061, P-059, P-216, P-255.
- A picture the game draws into is read through its own slot, so no texture list is copied each frame. The sampler
  tables stop filling up and waiting for the whole graphics card. Texture records lose the console's header.
- For a player: fewer rare long frames; less work per frame.
- Design: FRAME-MODEL 2.10. Proof as written there (a counting test: each list is written once).

**R3 Material constants and shader forms written when the files are made.** Rows: P-151, P-173, P-060, P-064,
P-297, P-299, P-303, P-304, P-309, P-157.
- The importer writes each material's final numbers and each shader as plain float expressions. The game sends
  only what changed in a step. The console's register fields are deleted at the end of this step.
- For a player: the processor stops redoing every material every frame; this matters most on the tablet.
- Design: FRAME-MODEL 2.11 for the constants. The shader forms take the inventory's line ("one float expression
  per named form") and are written into 2.11 before code, because they share the importer's writer.
- Proof as 2.11, plus pictures per form group.

**R4 Draw sites made once.** Rows: P-154, P-149, P-150, P-143, P-181, P-145, P-038, P-127, P-128, P-194, P-195,
P-196, P-239, P-248, P-262, P-263, P-310, P-188.
- The 109 places that build their shapes by hand every frame (effects, shadows, particles, gauges, glows) become
  records made once. Particles are built on the graphics card. A shadow volume is one draw, not two.
- For a player: the largest single cut in per-frame processor work; particles and shadows stop costing more on
  weak devices than on strong ones.
- Design: FRAME-MODEL 2.12 and 2.12.1, one landing per group in its order. The snow floor's 192 quads (P-196) go
  in the quads group. Particle steps that depend on how often they are drawn (P-262) are fixed in the particle
  group.

**R4v Views as records.** Row: P-254.
- A view is one record per picture; draw order is per view; a draw names its camera.
- Design: the inventory line. Written into FRAME-MODEL before R5.

**R5 Poses and matrices on the graphics card.** Rows: P-156, P-144, P-046, I-003.
- The game writes each pose once a step. One pass on the graphics card makes every picture's matrices.
- For a player: an in-between picture costs the processor almost nothing, so high frame rates stop loading it.
- Design: FRAME-MODEL 2.13.

**R6 Lights as world records.** Rows: P-142, P-140, P-152, P-190.
- Lights are kept in world space; the graphics card applies the view. The view is applied in R5's pass, in one
  place.
- Design: FRAME-MODEL 2.14.

**R7 The frame planned from what each pass reads and writes.** Rows: P-006, P-007, P-021, P-025, P-027, P-031,
P-053, P-055, P-063, P-290, P-296, P-319, P-026, P-028, P-030, P-049, P-050, P-054, P-057, P-058, N-1081,
N-1082, P-293, P-295, P-298, P-305.
- A planner orders the passes, chooses what each keeps and throws away, and pools the targets. Size changes and
  upscaler changes stop waiting for the whole graphics card.
- For a player: fewer graphics-card waits, less memory traffic on phones, no long frame at a resolution or
  window change.
- Design: FRAME-MODEL 2.15, seven landings. The planner has no part for the depth questions (S0.2 removed them).
  N-1081 goes in landing 7.

**R8 Effects in real units.** Rows: P-300, P-198, P-213, P-185, P-301, P-207, P-289.
- Bloom, blur, depth of field and the underwater warp work in fractions of the picture and in floats, and read the
  depth directly. Bloom draws into its own target (HDR bloom from the real brightness, the owner's answer).
- For a player: the effects look the same at every resolution and screen shape.
- After R7 landing 2 (the passes read depth by name).

**R9 Texture and movie containers.** Rows: P-291, P-292. Any time; touches no other package's files.

**R10 Layouts and the 2D screens.** Rows: P-218, P-229, P-230, P-184, P-199, P-200, P-210, P-197.
- Square layout units instead of the stretched 4:3 canvas; layout records at modern widths; layout pictures by id.
- For a player: menus and the HUD laid out correctly on every screen shape.
- Any time; touches no other package's files.

### Lane G. The game and the display

**G1 The input record and its reads.** Rows: P-282, P-279, P-244, P-234, P-236, P-073, P-084, P-074, P-075,
P-077, P-083, P-220, P-288.
- The game's input record stops being a Wii Remote's (the owner's answer: tilt as a record in the game's own
  terms). Pointer speed is measured in one fixed space. Short presses are no longer lost. One dead zone.
- For a player: the same feel on every pad and screen; no dropped presses; no input stalls on a busy radio.

**G2 How a picture reaches the display.** Rows: I-002, I-004, I-005, I-006, I-007, I-010, I-012, I-013, I-014,
N-1084, P-034, P-042, P-078, P-163, P-274, P-311 to P-318, P-019, P-281, P-363, P-364.
- For a player: even frame pacing at every refresh rate, less delay between a step and its picture, one governor
  that lowers resolution before it drops pictures, a pointer that moves at the display's rate.
- Design: FRAME-MODEL 2.16, nine landings. Landings 1 to 5 (the tablet's pacing, orders 23 to 27) stand directly
  after R1 and R2, before G1 and the surround steps; landings 6 and 7 stand at 35 and 36, after G1. Landing 8 comes
  with R5 and R7 landing 2. Landing 9 (the default stays 60, the owner 11-22; the settings show only where they act)
  is the last, after the measures hold.

**G3 The game step on the cores.** Rows: P-040, P-146, N-1083, P-283, P-265, P-241.
- One worker pool; results applied in list order, so the game is the same on one core or many. Loads are read
  ahead of the step, so a drive read never happens inside a step.
- For a player: headroom on weak PCs and the tablet; no hitch when a stage streams in.
- Design: FRAME-MODEL 2.17, seven landings. Landing 4 (animation) waits for R5, which changes the same file.

**G4 The step's speed.** Rows: P-013, P-039, P-088, P-129 to P-139, P-141, P-165, P-238, P-240, P-247, N-1076, N-1077.
- Searches that were cheap only because the console's tables were small get an index: collision parts, gravity,
  sensor pairs, light areas, arenas, pools.
- For a player: a faster game step where a scene is busy, most on weak devices.
- Step-speed commits already written on the frozen branch `app/land1` are taken first, one row a landing, each
  re-checked against G3's model and measured. N-1077 (a sparkle picture that differed there) is checked then.

**G5 Every actor runs, the same on every screen.** Rows: P-191, P-243, P-253, P-256.
- Design: FRAME-MODEL 2.18, five landings. After G3 landing 2 and G4; landing 3 needs R4's records.
- The owner's rule decides at landing 5: every actor always runs if the measures hold, else one run volume that is
  the same on every screen.
- For a player: the game plays the same on a wide monitor, a 16:9 screen and a phone.

**G6 Sound.** Rows: P-186, P-221, P-222, P-223, P-224, P-227, P-228, P-225, P-226, P-271, P-219, P-193, P-037,
P-071, P-072, P-286.
- In this order: sends and settings as typed records (G6.2) and then the surround law with the player's Speakers
  choice (G6.7), both at orders 28 and 29, after the tablet's pacing steps; then sequence data in host order; look-ups by map;
  bounds checked at load; streams filled by a reader thread; a stream's end told by the game clock; then the
  device threads.
- For a player: no sound hitch from a drive read, no crackle on machines that need a larger buffer, real surround
  on surround devices, chosen in the options (3.1, 5.1, 7.1) or automatic. Two-channel sound stays as made.

**G7 Text.** Rows: P-202, P-215, P-237, P-275, P-187, P-201.
- Design: the inventory's five landings (messages parsed once into runs and tag records, then 32-bit characters).

**G8 Save, tables, limits and waits.** Rows: P-205, P-232, P-272, P-189, P-233, P-192, P-148, P-204, P-235.
- The save as named, sized records with a version (an old file is read once and converted). Table columns resolved
  when the files are made. Shared counts and id widths that could cap what is added grow. No wait on the game
  thread without a bound.

**G9 Timing and small items.** Rows: P-203, P-206, P-268, P-306, P-208, P-211.
- Movie cues fire when the movie passes them, so a hitch cannot skip one. The staff roll follows its music.

G6 to G9 touch no other package's files, except the settings files named in section 5 (G6.7's new row). They are taken whenever the lane's main line waits for Lane R. (not used: one agent does every step)

### The end

**Z Profiles and the end check.** Rows: P-014, P-269.
- The speed profiles the compiler uses are taken again on the finished code, for Windows and for the tablet's
  build (about 7% of the game's speed on phones is not used today).
- Then section 2's checks, the statement and the two builds.

## 5. Files both lanes touch

Everything else belongs to one lane. A shared file is changed by one lane at a time; the other moves its work
onto the result. (not used: one agent does every step)

| File | Lane R | Lane G | Who goes first |
| --- | --- | --- | --- |
| The four renderers' frame and present code | R1, R2.1, R2.2, R7 | G2 landings 1 to 5 | R1 and R2 (orders 14 to 22), then G2 landings 1 to 5 (23 to 27), then R7 |
| `render_core.cpp` | R1.2, R3, R7 | S0.2, G2 | S0.2, then R1.2 (15), then G2 (23 to 27, 35, 36), then R3, R7 |
| `Model.cpp` | R1, R5 | G3 landing 4 | R5 |
| `FrameList.cpp`, `nebula_frame_list.h` | R3.2, R4, R4v.1, R5.1, R6.1, R7.1 | G2 landings 6 and 7, G3 landing 6 | G2.6 and G2.7 (35, 36) first; different parts; whoever lands second moves |
| `host_main.cpp` | R7 landing 7 | G2 landings 1 and 3, G6.7, G1.1 and G1.4, G3 landing 7 | G, in the order of `steps.tsv` (23, 25, 29, 30, 33, 83), then R7.7 (100) |
| `host_window_sdl.cpp` | none | G2.3 (event pump, surface hand-over), G1.1 and G1.4, G2.9 (the vote) | G2.3 (25), then G1 (30, 33), then G2.9 (135) |
| The clipping files (`ClippingJudge`, `ClippingActorHolder`) | none | G3 landing 2, G5 | G3 |
| `LensFlare.cpp`, the probe code in the renderers | none after S0.2 | S0.2 | S0.2, before R touches the renderers |
| `native_settings.cpp`, `runtime_settings.*`, `settings_table.tsv`, `NebulaText.tsv`, `NebulaSettingsMenu.cpp`, `settings_graphics_page.cpp` | none | G2.3 and G2.5 (texts for a Linux mark and a missed target; their cards name no file), G6.7 (the Speakers row), G1.2 (the language list), G7.3, G9.5, G2.9 (P-364's text) | G2.3 and G2.5 (orders 25, 27), then G6.7 (29), then G1.2 (31); G7.3, G9.5 and G2.9 read their rows as they stand |

Checked against `steps.tsv` on 11-23 (the order of the tablet's pacing before the surround steps): G2 landings 1 to 5 stand at 23 to 27, before R7 (94 to 101); `render_core.cpp` goes S0.2 (6 to 11), R1.2 (15), G2 (23 to 27, 35, 36), then R3 and R7; G1.1 (30) stands before G2.6 (35); no need of any step is placed after the step itself (`plan.py` read all 138), and places 37 to 138 are the same as before.

## 6. Decisions settled here, so the design has no open ends

The design file disagrees with itself in a few places. These are the answers the plan uses.

| Question | Answer |
| --- | --- |
| Where does P-157 (the console's register layout in the material file) close? | At the last landing of R3, when the console field records are deleted. |
| When do the material form descriptions get written? | Each by the landing that adds its first reader. |
| Does step 1 have an old path beside the new one? | No. The first such switch is step 2's. |
| Where is "pipelines made ahead" designed? | In the rows' own notes (P-010, P-052, P-065, N-1067); R1 follows them. |
| Where does N-1081 (the game waits for the whole queue at a window change) go? | R7 landing 7. |
| Where is the view applied to lights? | In R5's pass. |
| How many tour stops? | 102. |
| When two sections describe the same record differently | The later section (2.8, 2.10, 2.11) is the one the code follows. |
| Parts the design leaves "to be read in the landing" (28 of the 42 reads of an actor's clipped state; the 22 "band along points" shapes; the player's swapped images) | Read whole first in that landing; the finding is written into the design before the code. |
| A ledger row that holds several faults (many of the 101 rows from the whole-tree read do) | Each landing names the parts it closes; the row closes with its last part. A part that belongs to no package of this plan is given its own row first. |

## 7. For the owner

1. **One reading to confirm or overrule (from the last agent):** "sound stays as it was made" was taken to mean
   the two-channel output is left unchanged, including the console's surround fold on stereo devices, and only the
   real surround output is reworked. This plan uses that reading.
2. **The default frame rate** (P-042): decided (you, 11-22: "should be 60fps."). It is 60 on every system and
   a player chooses a higher rate. G2 landing 9 keeps the Android fall-back and the surface vote on the step rate
   and makes each setting show only where it acts.
3. **S0.2 changes two answers the game gets:** what the pointer is over and whether a talk balloon is hidden are
   computed from the world instead of from the picture. You decided this on 11-19. Recorded plays are recorded
   again once after it.
4. **Surround comes early (the owner, 11-22), after the tablet's pacing (11-23).** G6.2 and G6.7 stand at orders 28
   and 29, directly after G2.1 to G2.5, so the Speakers setting and real 3.1, 5.1 and 7.1 output can be tried before
   the rest of the sound work. Not in a second beta (his words
   of 11-22: "just beta 1 so far"). The setting replaces "Sound output" (Mono, Stereo, Surround): Surround becomes
   "Encoded stereo", the old key is read once, and a file that said `stereo` (the default every file wrote) becomes
   Auto: a player on a surround device who never touched the row gets the device's speakers.

## 8. What this plan does not know

- No code was read to check the rows again; the plan rests on the reports, the ledger and the design as written.
- Some packages have no section in the design file: R1, R8, R9, R10, G1, G4, G6, G8 and G9 rest on the rows' own
  notes and the inventory's lines. Their step cards say where the sources leave something to be read first.
- The design gives no count of landings for render data path steps 3 to 6. From its group lists it is about 20.
  The whole plan is roughly 100 landings: about 45 in Lane R and 55 in Lane G.
