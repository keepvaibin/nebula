# Progress: Agent 1 (engine, port, gate)

One dated entry per package step. Newest last.

## 2026-11-17 E0.1 The baseline

- The 79 uncommitted files in Nebula-Clean went in as ten commits by subject, on top of 0b1982c6f: semver partial
  ranges; no player present; menu input (touch, wheel, key repeat); first-person look and the main camera's rig;
  the swap queue in pacing; pipelines warm; the benchmark at the player's settings; Android activity calls; a mod's
  waiting code asks at the title; the AO count.
- The headless host (nebula_native_host) had not compiled since 0b1982c6f: three headers it needs were included
  for renderer hosts only. Fixed in 4707a8625.
- Plan and audit added under docs/plan and docs/audit-2026-11-17.
- Checks on the baseline (builds: game, D3D12, Vulkan, D3D11 hosts, luma-test, nebula-import, 0 failures; pak store
  version matches):
  - Unit tests (rel2/avx2, MSVC 14.44, Release): 79 of 79 pass.
  - luma-validate Rust tests: 102 pass, 4 ignored (the semver partial-range test among them).
  - The 102-stop tour on Direct3D 12, hidden and silent: 101 of 102 pass. 030-FactoryGalaxy-4 setup-failed: Mario
    never gets control after 7,200 frames (P-122, the same as the audit; E2).

## 2026-11-17 E0.2 to E0.4 The gate, the push workflow, the missing tests (engine/work, not yet on main)

- E0.2: tools/gate/gate.py, --quick and --full, one result file, non-zero on any failure (edbce54f5). Quick checks:
  the repository audit, the Windows build, the Windows and game unit tests, a 300-frame start on D3D12, Vulkan and
  D3D11, the Rust tests, the harness's and the game description's self-tests, the mod tests, a 12-stop tour. Full
  mode names every check it cannot run yet as missing, with its plan step.
- E0.3: .github/workflows/check.yml on every push (2b5fee5be, 3f96f8861): Windows (three renderers, unit tests, a
  start check of D3D12 and D3D11; the runner has no Vulkan driver, N-1011), the Windows game modules, Linux (build,
  unit tests, a Vulkan start on Mesa's software renderer), Mac (a Metal start), Android (the APK), the Rust tests on
  Linux and Windows. The Android
  start is not in it yet (N-1010, E0.13).
- E0.4: the unit tests build on Linux (2530d7ff2); the game's unit tests build with the game's own flags
  (e7bcd827e). New tests, each failing before its fix:
  - the game arena (c3cfd99c4): a new before the first arena wrote through null; a non-power-of-two alignment gave a
    misaligned block (N-1004, N-1005);
  - the frame-list writer (6c07f9028): an offset aligned in place of the address, endless pages for a block larger
    than a page (the test hung), writes through a null reallocation (N-1006 to N-1008);
  - the draw walker's plans, now in a header of their own (585cedcd6);
  - the display clocks (0aa8e3a6f): Android and Apple now share one vsync grid; the old Apple rule fails 8 of its
    checks (N-1009);
  - the Android launcher's update rules, signature check and bounded save read (c8cdf58fe), run by the push
    workflow's Android job; JUnit is not on this computer (N-1012, board ask).
- A mistake, caught before any push: my arena change first refused alignment 0, without a whole-tree grep. 76 callers
  use it as "any"; the tour crashed at all 12 stops. fde4f52c6 allows it again and the arena test now checks it.
- The game-source changes (arena, frame list) still wait for the step-hash replay (E0.10).
- Also: the harness's real-time mode (c9f931174) and a test's "disc" (c93921de3, Agent 2's ask).
- Gate quick on 0aa8e3a6f plus the Android changes: 10 of 12 pass. description/self waits for Agent 2's
  functions_fixture fix (on mods/work); mods waits for tools/lumacore/mod_tests.py (mods M0).

## 2026-11-17 E0.5 The dead tools retired (engine/work, not yet on main)

- run.sh and every script built on it are gone (f8746bc45, P-124). Where each went:
  - to tests/engine, run by the gate on each renderer (25459b87d): boots.sh to boot; gpulost.sh to device-lost (two
    losses); sizes.sh and resize.sh to window-small, -wide, -tall, -4k and window-resize; luigi.sh and luigi2.sh to
    luigi-stages; save.sh to save-files and save-files-again; home_det.sh to home-menu;
  - to later steps: tour.sh, all_stops.sh, galaxy.sh, stopcross, look, validate.sh and extended.sh to the gate's tour
    and E0.6, E0.7, E0.14; fs_det.sh, window.sh, mii.sh and movie.sh to E0.7 (N-1017); interp_real.sh to E0.9;
    perf.sh to E0.17; pgo_profile.sh to E0.15; monkey.py to E0.14; rt_inputs.sh goes with ray tracing;
  - losscheck.py to a check every harness test now makes (3f7d2a257, N-1016); the other run.log readers (gaps,
    loads, matchframes, align_*, compare_stops, sheet, water_summary, perf_summary) had nothing left to read.
- The test extension now runs only a harness's plan (357d25a18). Its environment paths did nothing in a harness run,
  so the first window-resize test passed without resizing anything (N-1015). Window sizes and device loss are now
  plan steps; a step the host cannot do is a harness error.
- A real fault found by the new device-lost test: the host never offered extensions lose_gpu_device (N-1013).
- A test's settings names are checked: a setting the game reads or one of four test hooks (N-1014).
- docs/using-nebula/diagnostics.md describes the harness, the hooks and the file tools that stay.
- Gate quick on these changes: 11 of 13 pass; engine 11 of 11, tour 12 of 12. description/self and mods wait for
  Agent 2 as before.

## 2026-11-17 E0.10 and E0.11 The step-hash replay, the benchmark's truth (engine/next, not yet on main)

- E0.10, the tool (a6b483875): a test's "hash": "on" step has the game write a hash of every live actor after every
  step; tools/verify/step_hash.py replays tour stops on two builds and names the first step and actor that differ.
- The proof it was made for: the game arena and frame-list changes of E0.4 (N-1004 to N-1008) against the files before
  them, on Direct3D 12: 101 of 102 tour stops the same over 600 steps. 030-FactoryGalaxy-4 fails setup on both sides
  (P-122, E2), so it has no hashes.
- E0.10 still open: the 20 recordings in the gate and the PC-against-Android comparison.
- E0.11 (70309e4b1), on Direct3D 12, Vulkan and Direct3D 11 with the built-in stages, hidden:
  - P-089: the benchmark counts the game's own steps. Each scene ran about 60 steps a second beside 800 to 4,950
    pictures a second. "Fits" now needs both 1% lows at 58 or more. The run beside a build said cpu_bound: its steps'
    1% low was 42.5 while the pictures' was 420. The old rule would have said fits.
  - P-090: GPU times are one per game step (AstroGalaxy: 779 steps, 780 GPU times, 30,020 pictures).
  - P-091: a result from another renderer gives no recommendation and says so.
  - P-159: Vulkan stops acquiring and presenting while its window cannot be seen, as Direct3D does; Metal has the same
    rule (its build is the push workflow's Mac job, not yet run).
  - The result is schema 3; the telemetry fixture is now a real run's result.
- Store version 35 (8fd1a5799) for the placeholder model's program (e3a2026ed); paks of all three discs at 35.
- N-1032 (465dbbe23), from Agent 2's report: on Direct3D 11 and Metal a draw table kept its old samplers when a
  painted replacement arrived, so the menu's key pictures were drawn aliased. New test tests/engine/menu-key-icons:
  Direct3D 11 against the Direct3D 12 picture, rmse 5.83 before, 0.00 after.
- N-1012 (3465976af): the gate's Android build runs the launcher's JVM tests (8 of 8), with JUnit and org.json
  downloaded under the owner's permission.
- Unit tests 85 of 85, nebula-import 334 pass, telemetry 6 of 6.
