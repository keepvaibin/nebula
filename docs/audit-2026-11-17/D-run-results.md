# D. What was run, the results, and where the evidence is

All runs were made on 2026-11-16 and 11-17 on the build `D:\NebulaWork\build\luma-bins\fp1` (the working tree `Nebula-Clean` as of 11-16 21:30; game data `D:\NebulaWork\build\all1-pak-34\game.pak`). PC runs used a hidden window and sound written to a file. Nothing in either repository was changed.

The scripts that made each run are in `scripts\` beside this file.

## D.1 Summary

| # | Run | Result | Evidence |
|---|---|---|---|
| 1 | Host unit tests (ctest, `build\rel2\avx2`) | 79 of 79 | Windows only |
| 2 | Nebula Rust crates (`cargo test --workspace --locked`) | all pass | |
| 3 | LumaLink (`cargo test --workspace --locked --no-fail-fast`) | 365 pass, 1 fail, 6 skipped | Fail: `lumalink-core add::tests::each_manifest_part_is_written_in_the_format_order_and_checked` |
| 4 | Mod conformance, Direct3D 12 | 418 of 428 | `build\audit-conf\out` |
| 5 | Mod conformance, Vulkan | 417 of 428 | `build\audit-conf\out-vk` |
| 6 | Mod conformance, Direct3D 11 | 409 of 428 | `build\audit-conf\out-d11` |
| 7 | Harness self-tests (`g/harness-r6`, 24 tests that expect refusals) | 23 agree; `19-settings-step` does not | `build\audit-conf\out` |
| 8 | LumaScript lane (k): 26 mods built with tonight's LumaLink | 21 of 21 tests | `build\audit-conf\out-k` |
| 9 | Example mods from the documentation (11) | all build for nine platforms; 26 of 26 tests | `build\audit-examples` |
| 10 | `lumalink new`, `check`, `pack`, `add need lumakit` | all work | `build\audit-newpack` |
| 11 | Galaxy tour, Direct3D 12 | 101 of 102 | `build\audit-tourh\tour-d3d12.txt` |
| 12 | Galaxy tour, Vulkan | 101 of 102 | `build\audit-tourh\tour-vulkan.txt` |
| 13 | Galaxy tour, Direct3D 11 | 101 of 102 | `build\audit-tourh\tour-d3d11.txt` |
| 14 | Galaxy tour, tablet | 101 of 102 (the runner printed 99; two are false failures, D.8) | `build\audit-tab-tour-R52WA0FXE5J` |
| 15 | Galaxy tour, emulator | Stopped at the owner's request after the tablet's tour: 61 valid stops, 60 pass (one misreported by the runner), 1 the stuck mission | `build\audit-tab-tour-emulator-5554` |
| 16 | Tablet: install, start, mod loader suite | 23 of 23 | `build\audit-tab-phone` (title picture `game.png`) |
| 17 | Emulator: install, start, mod loader suite | 23 of 23 | `build\audit-emu-phone` |
| 18 | Wrong-time probes | 4 of 5 calls crash the game at boot; none on the title | `build\audit-probes`, `build\audit-probes-work` |
| 19 | Stuck-mission probes | Mario in mid-air, no surface, never on ground | `build\audit-probes-work` (p9, p10) |
| 20 | Benchmark, 854x480, three renderers | section D.6 | `build\audit-bench` |
| 21 | Benchmark, 2560x1440, three renderers | section D.6 | `build\audit-bench-1440` |
| 22 | The old galaxy tour as written (`tools/verify/tour.sh`) | Dead: 50 minutes on the title, "stops 0, 0 crash lines" | section D.8 |

## D.2 Mod conformance tests

The suites are the lanes' own (`D:\NebulaWork\agents\luma\{f,fb,g,h,j,k}`), copied to `build\audit-conf\luma` and rebuilt against the current headers: 514 test folders in 58 suites. Two suites are the harness's own self-tests; without them, 428.

**On 11-12 the same suites stood at 279 to 288 of 359.** Suites that were at zero then and pass now: anim 6 of 6, sound 3 of 3, draw 4 of 4, net 10 of 10 (was 2 of 9).

Tests that do not pass:

| Test | Direct3D 12 | Vulkan | Direct3D 11 | What happens |
|---|---|---|---|---|
| f/guard 1619 timer-access | crash | crash | crash | A fault in a mod's time-service event is not recovered: "not resumed: the game's or the host's code is between the boundary and the fault". The test mods were built by the old lane script without unwind data; LumaLink always includes it (M-027) |
| f/guard 1620 timer-illegal | crash | crash | crash | Same |
| f/guard 1621 timer-divide | crash | crash | crash | Same |
| h/mods 2241-list | fail | fail | fail | Counts 5 mods where 4 expected |
| j/modlog-host 3127-page-keys | fail | fail | fail | Mods-page log entry facts missing |
| j/modlog-host 3129-entry-whole | fail | fail | fail | Same |
| j/refs 3138-no-mod | fail | fail | fail | A fact present where absent expected |
| j/safestart 6601-asked | fail | fail | fail | The Mods screen's notice label is null. The safe start itself works; the notice moved to the Log page (M-100) |
| j/safestart 6602-crash | fail | fail | fail | Same |
| j/safestart 6603 | setup failed | setup failed | setup failed | Follows from 6602 |
| j/startcheck 3141-no-compiler | pass | fail | fail | The start check does not report the missing compiler part |
| fb/world 2454, 2471; modlog-host 3122-ja; modlog 3124; ui 3813, 3815, 3816, 3817 | pass | pass | harness error | The Direct3D 11 reference picture does not exist |

The other 32 of 35 guard tests pass on every renderer.

## D.3 Galaxy tour

`scripts\audit_make_tour.py` writes one harness test per stop: 102 stops (every galaxy's missions as the old tour listed them), each starting the stage the way a save file does, then 600 frames of seeded random play, with a picture at arrival and one at the end. A stop passes when the stage is ready, the run reaches its last frame, there is no crash dump, frames never stop, and the game quits with exit 0.

**PC, three renderers: 101 of 102 each.** The one stop that fails is `030-FactoryGalaxy-4` on all three: "ready: MarioModule::isInputDisable still true (after 7,200 frames)".

**Tablet (Vulkan, Adreno 740): 101 of 102.** The runner printed 99 pass and 3 not:

| Stop | Runner's verdict | What happened |
|---|---|---|
| `030-FactoryGalaxy-4` | setup failed | The same stuck mission as on the PC |
| `024-EggStarGalaxy-2` | fail: "the game quit by itself with exit 0 (actual: null)" | False. The run reached its last frame, both pictures were taken, and the device log has "Process 12172 exited cleanly (0)". The runner asked for the exit record at 01:39:45.040; Android wrote it at 01:39:45.080 |
| `095-SurfingLv1Galaxy-1` | the same | False in the same way: asked at 02:04:30.452, written at 02:04:30.514 |

**Emulator (Vulkan, x86-64): stopped at the owner's request; the tablet is the device result.** Of the first 61 stops, 59 pass by the runner; `030-FactoryGalaxy-4` is the stuck mission; `040-HeavensDoorGalaxy-1` is a false failure of the same kind (the log has "Process 4141 exited cleanly (0)"). At stop 62 the game's log stops right after the Vulkan device is made, the run timed out at 240 s, and 26 s later Android's own watchdog restarted the emulator's system (02:04:15). The stops after that failed at setup while the system was coming back and are not results. The same stop passes on the tablet and on all three PC renderers.

**The stuck stop, by probe.** A probe mod logged the player every 60 frames in `FactoryGalaxy` scenario 4 and, as a control, scenario 2:

| | Scenario 2 (passes) | Scenario 4 (stuck) |
|---|---|---|
| Control given | by about frame 1,000 | never in 7,200 frames |
| Position | moves | x = -65691.99, z = -39761.16, fixed |
| Height | | cycles between about 790 and 1,096, repeating exactly every 280 frames |
| On ground | yes | never |
| Collision planes touched | | 0 at every sample |
| Floor code | | 0 |
| Bind, rush, flying, dead, off flags | | all false |

Ruled out: a bouncing floor (no floor is touched); the collision plane limit (no planes); a skipped opening flight (the log line that the guard writes is absent, so the flight ran); a reversed test in the jump code (it matches the decompilation's current version). Left: the ground where the flight ends is not there, which points at the set of layers loaded for that comet mission (`StageDataHolder.cpp:116-123`, `ScenarioDataParser.cpp:226-233`, P-171) or a switch that should make it appear.

## D.4 Tablet and emulator

A test build with its own package id (`dev.nebula.galaxy.test`) was installed beside the installed Nebula. The installed game and its data were not touched. Tests that record from the microphone or camera were left out on purpose.

| | Tablet (SM-X810, Adreno 740, Android 16) | Emulator (Android 16, x86-64) |
|---|---|---|
| Install and first start | fine; title screen correct at 2800x1752 | fine |
| Mod loader suite | 23 of 23 | 23 of 23 |
| One `.luma` holding every platform's code | loads | loads |

Figures from the tablet's tour are in D.11. From the loader run: at the title one pass costs 2.0 to 2.4 ms of GPU time at 2800x1752; in the suite's scenes 2.2 to 2.6 passes and one copy cost 6.6 to 7.0 ms; "frame work 1.11 ms average, 18.68 ms longest"; "600 frames, 600 on the display's vsync (60.0 Hz)". On the emulator the game's work averaged 17 to 20 ms a frame; that is the emulator's CPU, not a device figure.

The test app was removed from the tablet when its runs ended (`pm uninstall --user 0 dev.nebula.galaxy.test`); the installed Nebula is still there. It was removed from the emulator as well.

## D.5 Wrong-time probes

`scripts\audit_make_probes.py` writes one small LumaScript mod per call. Each calls once from `on frame`, on the first frame (boot) in one run and on frame 90 (title) in another.

| Probe | Boot | Title |
|---|---|---|
| control (no call) | pass | pass |
| `MarioAccess::validateSensor` | crash | pass |
| `item.give(Coins(1))` | crash | pass |
| `item.give(Health(1))` | crash | pass |
| `camera.now()` | crash | pass |
| `pointer.world()` | pass | pass |

The crash report on Windows: "NEB-M03 A mod's code crashed the game... in the code of the mod audit-p2... Turn this mod off in Settings > MODS"; exception 0xC0000005 at `smg_game.dll+0x8a628d`, address 0x18.

Writing the probes also showed two documented forms the compiler refuses: `MarioAccess::getPlayerActor` without `()` (S0083) and `&&` in place of `and` (S0012).

## D.6 Benchmark

The built-in benchmark, alone on the machine, hidden window, no frame limit, VSync off, in-between pictures on. PC: Ryzen 9 9950X3D, RTX 5090. Eight scenes: AstroGalaxy, CosmosGarden, HeavenlyBeach, SandClock, OceanFloaterLand, IceVolcano, BattleShip, HoneyBeeKingdom.

"Game step" is computed as the game's time per picture times pictures per second, divided by 60.

| Output | Renderer | Pictures a second | Lowest 1% | GPU ms a picture | Game step, ms | Render thread, ms a picture | Worst frame, ms | Stutters |
|---|---|---|---|---|---|---|---|---|
| 854x480 | Direct3D 12 | 2,658 to 5,219 | 1,591 to 2,099 | 0.05 to 0.12 | 0.78 to 1.28 | 0.19 to 0.37 | 1.0 to 3.6 | 0 |
| 854x480 | Vulkan | 1,361 to 2,329 | 906 to 1,366 | 0.05 to 0.09 | 0.82 to 1.36 | 0.31 to 0.57 | 1.4 to 5.3 | 0 |
| 854x480 | Direct3D 11 | 1,338 to 2,397 | 840 to 1,492 | 0.05 to 0.15 | 0.80 to 1.43 | 0.34 to 0.66 | 1.3 to 3.9 | 0 |
| 2560x1440 | Direct3D 12 | 2,676 to 4,258 | 1,552 to 1,961 | 0.11 to 0.17 | 0.78 to 1.34 | 0.23 to 0.37 | 1.1 to 4.6 | 0 |
| 2560x1440 | Vulkan | 719 to 862 | 563 to 676 | 0.12 to 0.17 | 0.86 to 1.28 | 0.34 to 0.58 | 2.0 to 8.0 | 0 |
| 2560x1440 | Direct3D 11 | 1,415 to 2,569 | 923 to 1,536 | 0.12 to 0.22 | 0.77 to 1.39 | 0.31 to 0.62 | 1.2 to 4.0 | 0 |

What this table can and cannot show:

- The game step figures are valid and agree across renderers.
- The picture rates **cannot rank the renderers.** On a hidden window Direct3D 12 and 11 draw every picture and show none; Vulkan has no such test and shows every one (P-159). The Vulkan rows are that effect.
- The render thread's time per picture is higher on Vulkan and Direct3D 11 than on Direct3D 12 by about 1.5 to 1.8 times. That figure does not depend on showing.
- The benchmark's own summary names the render thread as the limit on Direct3D 12 (P-156 fits).
- The benchmark's result file gives the game's time per picture (0.024 ms in AstroGalaxy at 2,785 pictures a second). That is the 1.1 ms step spread over 46 pictures. Its "smooth at 60" verdict is taken from the picture rate (P-089, P-090).

## D.7 LumaLink and the example mods

- LumaLink was rebuilt from `lumalink-sweep` on 11-16 night; the release program on disk was from 11-14.
- All 11 example mods: nine libraries each, `check` with 0 errors, 26 of 26 tests on Direct3D 12.
- `balloon-world` gave 9 errors (L505, "the game has no service input at version 1.4") under plain `lumalink check`, and none with the names of the build under test. `check` compares against the installed game, which was the older 2.0.0b with input 1.3 (M-116, M-147).
- `lumalink new` then `check` then `pack` works; `add need lumakit` writes `*` and `check` accepts it. The two tool faults in the first-person report are fixed in this source. Which LumaLink build is published was not checked.

## D.8 Tools found dead by running them

| Tool | What happened | Cause |
|---|---|---|
| `tools/verify/tour.sh` (the 102-stop tour) | Sat on the title for 3,000 seconds, then printed "stops 0, 0 done, 0 crash lines" | `test_plugin.py` installs the test plug-in as the mod `nebula-tests`; the checker refuses it (L504: its library needs MSVCP140.dll). The script also reads `run.log`, which the current build leaves empty; the log is at `settings\logs\latest.log` |
| Every script that starts the game through `run.sh` | Same cause | Listed in P-124 |
| Harness setup `save.stars` | "save.stars 120: not built yet" | Documented in `test-files.md`, not built |
| `lumalink pack` with a second names file under the build folder | Refused | Two `luma_functions.json` found |
| The Android suite runner (`agents\luma\d\platforms\android\suite_android.py`) | Reports "the game quit by itself with exit 0 (actual: null)" on a run that ended cleanly: 2 of 102 on the tablet, 1 of 61 on the emulator | It reads `dumpsys activity exit-info` 40 to 60 ms before Android records the exit |
| Pacing in play | No tool | The test start always sets fixed steps (`host_main.cpp:993-995`); the benchmark is uncapped; a hidden window cannot measure display pacing |
| GPU time per pass on a device | Off | `Harness.java:76` passes `--diagnostics 0` |

## D.9 Incidents during the audit

- **The work drive filled.** `D:` had about 30 GB free when the audit began. Each test copies the programs, and my runs filled the drive; six suites and the Android build failed with "not enough space". I deleted my own copies, changed the script to delete them per suite, and re-ran. The drive holds 284 older work folders that I did not touch.
- **The tablet locked during the first tour.** The run was stopped and restarted after it was unlocked.
- **The emulator's Android system hung and restarted** at 02:04 during its tour (above). Whether the game's renderer start set it off is not known; the emulator had been up for 39 hours.
- **The emulator had too little room** for a second copy of the game data. With permission, the older test install's data on the emulator was cleared and the data was sent in 1 GB pieces.
- **The decompilation copy on disk is from 6 June.** One of my comparisons against it was wrong for that reason. The current version was fetched to `build\audit-petari-upstream` and used after that.

## D.10 Folders

Under `D:\NebulaWork\build\`:

| Folder | Holds |
|---|---|
| `audit-conf` | The copied suites (`luma`), results per renderer (`out`, `out-vk`, `out-d11`, `out-k`) |
| `audit-tourh` | The tour's tests (`tests\tour`) and results per renderer |
| `audit-bench`, `audit-bench-1440` | Benchmark results and logs per renderer |
| `audit-examples` | The example mods, built and tested |
| `audit-probes`, `audit-probes-work` | The probe mods and their runs |
| `audit-apk`, `audit-android-bin`, `audit-android-bin-arm64` | The Android test build |
| `audit-tab-phone`, `audit-emu-phone` | Device loader suites |
| `audit-tab-tour-R52WA0FXE5J`, `audit-tab-tour-emulator-5554` | Device tours, with the devices' logs |
| `audit-petari-upstream` | The decompilation's current version |
| `audit-newpack` | The `lumalink new` check |

These can be deleted once the report is accepted; together they are the largest thing this audit left on the drive.

## D.11 Tablet figures per stop

From the game's own log lines during the tablet tour (`scripts\audit_tablet_figures.py`). SM-X810, Adreno 740, Vulkan, 2800x1752 inside and out. The harness runs fixed steps as fast as the device goes and the renderer draws two pictures per step, so "steps a second" is how fast the whole game and renderer ran, counted over the seconds that lie inside the 600 played frames. GPU figures are the renderer's last logged window of 600 pictures in that stop; a stop with no such line is blank. "Passes" is how many times the full-size scene was opened per picture; "copies" is full-size copies per picture.

The longest-picture column includes the two full-size pictures each stop takes for the test itself and the first pictures of a stage. It is not a stutter count.

Stops with figures: 101 (steps), 86 (GPU).
Steps a second, lowest second of any stop: 60 (066-OceanPhantomCaveGalaxy-4); five slowest stops: 066-OceanPhantomCaveGalaxy-4 60, 060-KoopaBattleVs3Galaxy-1 65, 069-OceanRingGalaxy-2 71, 037-HeavenlyBeachGalaxy-2 74, 036-HeavenlyBeachGalaxy-3 77.
Steps a second, middle value over all stops: 164; range of the stops' middle values 65 to 229.
Stops whose lowest second fell under 60 steps: 0; under 120: 26.
GPU ms a picture: middle 5.13, highest 14.47 (066-OceanPhantomCaveGalaxy-4); five highest: 066-OceanPhantomCaveGalaxy-4 14.47, 069-OceanRingGalaxy-2 11.75, 068-OceanRingGalaxy-1 10.28, 058-KoopaBattleVs1Galaxy-1 9.72, 060-KoopaBattleVs3Galaxy-1 9.30.
Longest single picture on the GPU: 77.43 ms (079-ReverseKingdomGalaxy-2); stops with a picture over 16.7 ms: 36 of 86.
Scene passes a picture: 2.4 to 8.9, middle 2.8.

| Stop | Steps a second, lowest | Steps a second, middle | GPU ms a picture | Longest picture, ms | Passes | Copies |
|---|---|---|---|---|---|---|
| 001-BattleShipGalaxy-1 | 169 | 172 | 4.35 | 10.89 | 3.1 | 1.0 |
| 002-BattleShipGalaxy-2 | 174 | 174 | 4.34 | 12.67 | 2.6 | 1.0 |
| 003-BattleShipGalaxy-3 | 185 | 185 | 4.11 | 11.64 | 2.6 | 1.0 |
| 004-BattleShipGalaxy-5 | 129 | 133 | 5.14 | 16.22 | 3.5 | 1.0 |
| 005-BattleShipGalaxy-4 | 185 | 191 |  |  |  |  |
| 006-BeltConveyerExGalaxy-1 | 175 | 190 |  |  |  |  |
| 007-BreakDownPlanetGalaxy-1 | 149 | 162 |  |  |  |  |
| 008-CannonFleetGalaxy-3 | 167 | 182 | 5.84 | 15.15 | 2.9 | 1.0 |
| 009-CannonFleetGalaxy-2 | 176 | 179 | 4.70 | 14.74 | 2.6 | 1.0 |
| 010-CannonFleetGalaxy-1 | 141 | 150 | 4.93 | 15.75 | 3.1 | 1.0 |
| 011-CannonFleetGalaxy-4 | 173 | 185 | 4.65 | 15.30 | 3.2 | 1.0 |
| 012-CannonFleetGalaxy-5 | 164 | 200 |  |  |  |  |
| 013-CocoonExGalaxy-1 | 194 | 202 |  |  |  |  |
| 014-CosmosGardenGalaxy-2 | 161 | 195 | 4.21 | 11.13 | 2.6 | 1.0 |
| 015-CosmosGardenGalaxy-3 | 184 | 190 | 5.12 | 74.56 | 2.5 | 1.0 |
| 016-CosmosGardenGalaxy-1 | 139 | 179 | 4.47 | 13.94 | 2.6 | 1.0 |
| 017-CosmosGardenGalaxy-5 | 156 | 159 | 4.54 | 11.34 | 2.6 | 1.0 |
| 018-CosmosGardenGalaxy-4 | 148 | 164 | 4.43 | 12.09 | 3.6 | 1.0 |
| 019-CubeBubbleExLv1Galaxy-1 | 135 | 160 | 4.52 | 12.54 | 2.6 | 1.0 |
| 020-CubeBubbleExLv2Galaxy-1 | 207 | 210 |  |  |  |  |
| 021-DarkRoomGalaxy-1 | 130 | 158 | 5.72 | 16.84 | 8.2 | 1.0 |
| 022-EggStarGalaxy-1 | 160 | 173 | 4.67 | 14.75 | 3.0 | 1.0 |
| 023-EggStarGalaxy-3 | 147 | 171 | 4.91 | 9.74 | 2.8 | 1.0 |
| 024-EggStarGalaxy-2 | 159 | 182 | 4.90 | 11.74 | 2.7 | 1.0 |
| 025-EggStarGalaxy-4 | 166 | 168 | 5.00 | 15.44 | 3.0 | 1.0 |
| 026-EggStarGalaxy-5 | 167 | 168 | 4.48 | 29.36 | 2.6 | 1.0 |
| 027-FactoryGalaxy-1 | 128 | 133 | 5.61 | 16.46 | 2.5 | 1.0 |
| 028-FactoryGalaxy-2 | 159 | 161 | 6.38 | 17.03 | 2.4 | 1.0 |
| 029-FactoryGalaxy-3 | 152 | 185 | 4.96 | 18.59 | 2.5 | 1.0 |
| 030-FactoryGalaxy-4 |  |  | 4.28 | 4.47 | 3.0 | 1.0 |
| 031-FactoryGalaxy-5 | 107 | 109 | 5.76 | 13.77 | 2.6 | 1.0 |
| 032-FishTunnelGalaxy-1 | 154 | 163 | 5.97 | 16.91 | 4.1 | 1.0 |
| 033-FlipPanelExGalaxy-1 | 114 | 151 | 6.08 | 12.23 | 8.8 | 1.0 |
| 034-FloaterOtaKingGalaxy-1 | 164 | 179 |  |  |  |  |
| 035-HeavenlyBeachGalaxy-1 | 146 | 149 | 6.00 | 21.07 | 3.5 | 1.0 |
| 036-HeavenlyBeachGalaxy-3 | 77 | 92 | 6.67 | 22.28 | 3.4 | 1.0 |
| 037-HeavenlyBeachGalaxy-2 | 74 | 80 | 7.52 | 21.72 | 3.3 | 1.0 |
| 038-HeavenlyBeachGalaxy-4 | 163 | 164 | 5.07 | 17.04 | 2.6 | 1.0 |
| 039-HeavenlyBeachGalaxy-5 | 90 | 98 | 8.60 | 19.44 | 3.0 | 1.0 |
| 040-HeavensDoorGalaxy-1 | 178 | 185 |  |  |  |  |
| 041-HeavensDoorGalaxy-2 | 182 | 184 |  |  |  |  |
| 042-HellProminenceGalaxy-1 | 152 | 161 | 4.62 | 11.71 | 2.6 | 1.0 |
| 043-HellProminenceGalaxy-2 | 140 | 155 | 5.05 | 13.66 | 2.6 | 1.0 |
| 044-HellProminenceGalaxy-3 | 118 | 137 | 5.06 | 12.09 | 2.6 | 1.0 |
| 045-HellProminenceGalaxy-4 | 128 | 140 | 4.94 | 12.14 | 2.6 | 1.0 |
| 046-HellProminenceGalaxy-5 | 137 | 148 | 5.24 | 14.55 | 2.5 | 1.0 |
| 047-HoneyBeeExGalaxy-1 | 210 | 229 |  |  |  |  |
| 048-HoneyBeeKingdomGalaxy-1 | 138 | 169 | 5.36 | 28.48 | 2.5 | 1.0 |
| 049-HoneyBeeKingdomGalaxy-2 | 185 | 185 | 4.64 | 15.53 | 2.7 | 1.0 |
| 050-HoneyBeeKingdomGalaxy-3 | 155 | 167 | 4.99 | 20.58 | 3.0 | 1.0 |
| 051-HoneyBeeKingdomGalaxy-4 | 178 | 184 |  |  |  |  |
| 052-HoneyBeeKingdomGalaxy-5 | 172 | 176 | 4.55 | 13.59 | 2.6 | 1.0 |
| 053-IceVolcanoGalaxy-2 | 103 | 103 | 6.54 | 18.38 | 2.4 | 1.0 |
| 054-IceVolcanoGalaxy-1 | 88 | 104 | 6.58 | 24.37 | 2.4 | 1.0 |
| 055-IceVolcanoGalaxy-3 | 92 | 118 | 6.20 | 18.23 | 2.5 | 1.0 |
| 056-IceVolcanoGalaxy-4 | 212 | 212 |  |  |  |  |
| 057-IceVolcanoGalaxy-5 | 192 | 193 | 5.48 | 24.91 | 2.6 | 1.0 |
| 058-KoopaBattleVs1Galaxy-1 | 83 | 89 | 9.72 | 23.88 | 4.1 | 1.0 |
| 059-KoopaBattleVs2Galaxy-1 | 107 | 126 | 8.04 | 19.01 | 8.9 | 1.0 |
| 060-KoopaBattleVs3Galaxy-1 | 65 | 72 | 9.30 | 33.10 | 4.1 | 1.0 |
| 061-KoopaJrShipLv1Galaxy-1 | 181 | 219 | 5.06 | 13.24 | 2.8 | 1.0 |
| 062-OceanFloaterLandGalaxy-1 | 90 | 103 | 6.57 | 17.41 | 2.8 | 1.0 |
| 063-OceanPhantomCaveGalaxy-1 | 97 | 139 | 6.73 | 23.76 | 3.2 | 1.0 |
| 064-OceanPhantomCaveGalaxy-2 | 93 | 100 | 6.39 | 23.50 | 2.7 | 1.0 |
| 065-OceanPhantomCaveGalaxy-3 | 103 | 133 | 7.95 | 22.76 | 3.2 | 1.0 |
| 066-OceanPhantomCaveGalaxy-4 | 60 | 65 | 14.47 | 17.58 | 5.0 | 1.0 |
| 067-OceanPhantomCaveGalaxy-5 | 109 | 134 | 5.69 | 22.48 | 3.2 | 1.0 |
| 068-OceanRingGalaxy-1 | 85 | 87 | 10.28 | 13.96 | 4.0 | 1.0 |
| 069-OceanRingGalaxy-2 | 71 | 81 | 11.75 | 16.40 | 4.0 | 1.0 |
| 070-OceanRingGalaxy-3 | 88 | 91 | 7.47 | 15.39 | 3.3 | 1.0 |
| 071-OceanRingGalaxy-4 | 172 | 180 | 4.05 | 13.37 | 2.6 | 1.0 |
| 072-OceanRingGalaxy-5 | 160 | 170 | 4.46 | 13.14 | 2.6 | 1.0 |
| 073-PhantomGalaxy-1 | 132 | 158 | 5.57 | 17.19 | 2.9 | 1.0 |
| 074-PhantomGalaxy-3 | 118 | 128 | 5.17 | 15.67 | 2.9 | 1.0 |
| 075-PhantomGalaxy-2 | 113 | 137 | 6.08 | 21.64 | 3.0 | 1.0 |
| 076-PhantomGalaxy-4 | 162 | 168 | 4.39 | 10.86 | 2.9 | 1.0 |
| 077-PhantomGalaxy-5 | 180 | 182 | 4.75 | 18.16 | 3.2 | 1.0 |
| 078-ReverseKingdomGalaxy-1 | 149 | 153 | 4.91 | 14.33 | 2.5 | 1.0 |
| 079-ReverseKingdomGalaxy-2 | 163 | 183 | 4.60 | 77.43 | 2.5 | 1.0 |
| 080-ReverseKingdomGalaxy-3 | 165 | 185 | 5.32 | 20.19 | 2.9 | 1.0 |
| 081-ReverseKingdomGalaxy-4 | 152 | 168 | 4.45 | 13.29 | 2.6 | 1.0 |
| 082-ReverseKingdomGalaxy-5 | 183 | 185 | 4.66 | 14.41 | 2.5 | 1.0 |
| 083-SandClockGalaxy-2 | 143 | 151 | 5.15 | 13.11 | 2.9 | 1.0 |
| 084-SandClockGalaxy-3 | 143 | 163 | 4.86 | 16.92 | 2.8 | 1.0 |
| 085-SandClockGalaxy-1 | 139 | 160 | 5.57 | 16.86 | 2.9 | 1.0 |
| 086-SandClockGalaxy-4 | 160 | 181 | 5.30 | 14.11 | 2.8 | 1.0 |
| 087-SandClockGalaxy-5 | 145 | 155 | 6.18 | 16.35 | 2.5 | 1.0 |
| 088-SkullSharkGalaxy-1 | 141 | 156 | 5.77 | 14.14 | 2.7 | 1.0 |
| 089-SnowCapsuleGalaxy-1 | 161 | 169 | 4.86 | 11.25 | 2.6 | 1.0 |
| 090-StarDustGalaxy-1 | 178 | 195 | 5.67 | 12.34 | 3.0 | 1.0 |
| 091-StarDustGalaxy-2 | 118 | 135 | 5.65 | 18.71 | 2.8 | 1.0 |
| 092-StarDustGalaxy-3 | 138 | 158 | 4.89 | 15.08 | 2.8 | 1.0 |
| 093-StarDustGalaxy-4 | 120 | 152 | 5.93 | 13.41 | 3.1 | 1.0 |
| 094-StarDustGalaxy-5 | 174 | 180 | 4.37 | 27.48 | 2.6 | 1.0 |
| 095-SurfingLv1Galaxy-1 | 181 | 183 |  |  |  |  |
| 096-SurfingLv2Galaxy-1 | 184 | 187 |  |  |  |  |
| 097-TamakoroExLv1Galaxy-1 | 180 | 181 |  |  |  |  |
| 098-TamakoroExLv2Galaxy-1 | 174 | 191 | 4.23 | 13.78 | 2.6 | 1.0 |
| 099-TearDropGalaxy-1 | 118 | 153 | 5.07 | 13.51 | 2.8 | 1.0 |
| 100-TeresaMario2DGalaxy-1 | 217 | 217 |  |  |  |  |
| 101-TransformationExGalaxy-1 | 89 | 90 | 6.40 | 20.33 | 3.3 | 1.0 |
| 102-TriLegLv1Galaxy-1 | 174 | 174 | 4.34 | 10.80 | 2.7 | 1.0 |
