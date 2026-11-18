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

## 2026-11-18, early morning (engine/e13a)

- The main gate of a8796ab68 did not start: GitHub's 'Linux sanitizers (thread)' job was red. N-1066 (9916b2544): P-070's
  log writer thread was stopped at the process's end but never joined, a thread leak the sanitizer reported into the
  test's own stderr (/dev/null by then). LineWriter::finish joins it (lets it go when its output is stuck). The job is
  green on f89429c9b.
- P-029 (9ce65eef0): not a fault; a ring test holds that a heap handed back leaves the ring empty.
- The step hash names a difference below 1/1000 (f89429c9b): detail facts carry the 15 floats' bits.
- M-068, the renderer's side (40ce46953): a turn is never a camera cut; the game and a mod's camera say their own cuts.
  frame_interpolation_tests: 2 failures before, none after.
- P-004 fixed: on the tablet the vsync thread wakes 0 times in 10 s in the background (848 on main 87b6d3db5).
- N-1064: the tablet's replay of HeavenlyBeachGalaxy-2 left the computer's at step 356 (Mario's rotation, a few ulps).
  Not char signedness (a -funsigned-char module replays the same), not an out-of-range float conversion or a shift (a
  module with those checks trapping ran all 600 steps), not long's width, the game's libm, sorts or hash containers.
  The host on Android and Apple was built with clang's default -ffp-contract=on: nunchuk_stick_counts' x*x + y*y fused
  into one rounding, so the stick of the step at 354 differed by one ulp on each axis. All hosts now build with
  -ffp-contract=off (as the game module already did); native_input_tests pins the bits.

## 2026-11-18, 02:40 to 03:30 (engine/e13a)

- Quick gate of fc23ce733 passed 24 of 24 (986 s); its main gate with --move took gate.lock at 03:09, after Agent 4
  stopped a main gate whose candidate was already red.
- Closed by main's 00:01 move (run 44263531077, 12 of 12 jobs green): N-1010, N-1011, N-1047, N-1048.
- Settled by reading: P-021 decided (the camera's cover freezes the picture the player sees). Confirmed with a design
  and its proof in the row: P-010 and P-052 (pipelines made while a stage loads, a draw never compiles), P-020, P-026,
  P-034 (code in, a Mac check owed), P-108, P-117, I-006, I-010, I-013, I-014, N-1046.
- P-098 (fbdf60a64): GameActivity declares every configuration change; ConfigChangesTest fails before. The JVM tests
  now take src/main as an input: before, Gradle left them up to date after a manifest change and they did not run.
- P-058 (7f4a07281): VMA is told the Vulkan version both the instance and the device have (1.1 before).
- P-005 (fbf316b8a): a two-thread test of the frame list's release and acquire, for the Linux thread sanitizer job.
  No thread sanitizer tour exists; the rows that named one now name that job.
- N-1041 (2d6f62121): the long run plays every mod test on the AddressSanitizer build.
- From Agent 4's 03:09 report: a test whose asked picture was never written passed. The harness now fails it
  (314dc3185, harness_pictures_written_test fails before). Gates sharing the machine is a question to the Planner.

## 2026-11-18, 03:30 to 04:00 (engine/e13a)

- The main gate of fc23ce733 failed on pictures alone: stop 030-FactoryGalaxy-4 runs since P-122's fed save, and its
  pictures had never been approved (two new pictures a renderer and region). Looked at, the same pixels on Vulkan,
  Direct3D 12 and 11 and PAL, the Japanese banner aside; approved in b7328ccb5, pushed with N-1041, P-098, P-058, the
  P-005 test and the harness picture check, so the next quick and main gate prove them together. E5 (0b2e023bf)
  stays after it, local, until its suite run.
- N-4036 (Agent 4's pictures not written): 33 runs of that gate had the GPU's answers late, 30 during play, all while
  my quick gates ran beside it; the lone gate of fc23ce733 had one, the D3D12 present gate during a test's pipeline
  warm-up (N-1067, a deterministic run's title does not wait for the start set).
- Designs written from reading: P-030, P-049, P-050, P-057 (one retire a frame slot in place of mid-frame drains;
  DLSS and SGSR 2 made on a worker), P-059 (the cook judges authored levels once), P-065 (Metal's own shaders as
  bytecode), I-002 and I-004 (E6a: a lead at multiples of 60 on refreshes; a shown list starts its in-between pictures
  when it arrives). P-053: Direct3D already skips a resolve with no draw since the last; Vulkan and Metal remain.
- E5 reviewed again: a mod's paint_level(uv, level) is now exactly that level on every renderer; Vulkan added the
  sampler's frame bias to an explicit level before.

## 2026-11-18, 04:00 to 04:20 (engine/e13a, E3 opening trace)

- Candidate b4b11bbe1: gate lock at every level with a queue (Agents 3 and 4 first), pre-push checks under the lock,
  the long run part by part, --github-wait (Planner 03:44, 03:49; N-1069). Its quick gate waits for Agent 3's slot.
- E3 trace step 0: the frame timeline recorded nothing outside Windows (clock 0, no file). Ported (053586a0d): the
  runtime clock, the system's thread ids, a file through open_path; the Android harness already passes --timeline
  and ends by --seconds, so the dump runs there. New events: Vulkan and Metal slot waits, swap image waits, and the
  Direct3D 12 present gate's wait (N-1067's interim). Unit test on every desktop system (88c3607a7); it fails before
  outside Windows. Unproven until the Linux build and a tablet run.
- Quick gate length (fc23ce733, 986 s): 497 s of CPU-only checks (LumaLink 362 s, of it 128 s rebuilding its 5
  crates) run in series before the game lanes start; the mod suite, 358 s, is the critical lane. Design waits for
  the N-1052 suite times of b4b11bbe1's quick gate.

## 2026-11-18, 04:20 to 05:15 (Nebula-A1h, beside the gates of b4b11bbe1)

- Quick gate of b4b11bbe1 passed, 24 of 24, 804 s (fc23ce733: 986 s; the mod suite 285 s from 358 s, N-1052). Its
  main gate holds gate.lock since 05:00.
- N-1070 (quick gate length): the CPU-only checks run in a lane of their own at below-normal priority beside the game
  lanes (a475a4d02). LumaLink's rebuilds at every gate had two causes, measured with cargo's fingerprint log: the
  junction (git rewrites file times) and two feature sets (-p lumalink, then --workspace). nebula/ is now a copy
  synced by content and one build covers both (1608278e7): with nothing changed, 69 s + 218 s became 1 s + 3 s.
  Shared with Agents 2 and 4 (Planner 04:39).
- N-1046 (5677376c9): a move starts the long run in the gate's own tree Nebula-long when none started in 6 hours
  and long.lock is free (Planner 04:39). Test fails before.
- P-043 (1f17962c7): the console census, cast_check and byte_order_check run as the gate's repo/code and in
  check.yml's Linux unit job. cast_check's one hit (CameraAnim cutCount, the NCAM count as cooked) is reviewed.
- P-023 (0a2c4a65b): release.yml builds only from a commit main holds whose Check run passed, and each package job
  starts its package (Windows through a start-check action check.yml now shares, Linux under xvfb, Mac on Metal).
  The dry run on GitHub is owed.
- P-012 (85325d957): the arena test of a new inside another new. Timeline unit test runs on Linux (gcc 14, fails
  before the port) and Windows. texture_pack_tests built again on the new sampler key (67b6198ac).
- N-1045 read: candidates' C/C++ cache hits are 9 to 18 %, main's own first run had none; the next candidate after
  b4b11bbe1 tells E13a's one-time cost from a cache fault. P-044: the display clock half was done on 11-17.
- Reviewed Agent 3's shared worker pool (P-040) render half: correct; asked that the chunk count follow the pool's
  threads, so its replays with 0 or 1 worker test the path players take.

## 2026-11-18, 05:15 to 06:00 (Nebula-A1h, beside the main gate of b4b11bbe1)

- The main gate of b4b11bbe1 does not move main: android/tablet's walk test failed on its own pattern, not on the
  game. It asked for a pacing line of exactly 600 frames; since P-041 the line is a 10 s window (594 to 600 frames
  at 60 Hz), so it passed only by chance (fc23ce733). N-1071 (dbc7ec863): any frame count; the new pattern matches
  the failed run's log, the old one does not. Every tour, picture and replay of that gate passed.
- N-1042 (10db9cfc6, ea927861f): coverage of the game modules in the long run. NEBULA_COVERAGE builds both modules
  counting what they run; build_windows.ps1 -Coverage; gate.py's coverage part plays the engine tests, the tour and
  every mod test on that build, writes coverage.tsv and never-run.txt with the results, and fails when the share of
  lines run falls. coverage_test.py builds a real clang coverage program (fails before). The engine half (the MSVC
  programs) is a clang-cl build, to be tried in a build root of its own.
- P-114 closed: installer/tests/run-setup-uninstall-window.ps1 removes a portable copy of 40,000 files through
  Setup's window and asks the window WM_NULL every 50 ms meanwhile: Setup from before 86f5b2316 missed 2 of 4
  probes, the current one none of 57.
- N-1040: pictures/tablet wired in gate.py (main level, after the tablet's tour, --place tablet), uncommitted until
  the tablet's pictures are approved from two tours of one commit; the first tour (b4b11bbe1's main gate, 24
  pictures, Adreno 740 driver 512.676.1) is kept in D:/NebulaWork/build/n1040.

## 2026-11-18, 06:00 to 07:00 (Nebula-A1h; the quick gate of fc36296a4)

- The quick gate of fc36296a4: 21 pass, 4 failed, all pictures (engine, tour and both region tours on D3D12). Cause:
  4eea5fc6d moves the frame's part of the level bias from the sampler to the programs (SampleBias), the same value
  rounded at another point. Of 25 changed pictures nearly every changed pixel is 1 off; at most 74 pixels of a picture
  are more, single and scattered (Honeyhive's pond, Surfing): level and alpha-test flips. Not a fault: approved after a
  main-level run of pictures only (b865c09e8, which adds pictures/tablet, N-1040) on every renderer and the tablet.
- N-1042 engine half (49a2127c4): Nebula.exe counted with clang-cl. The coverage program did not start at first:
  lld-link appends manifests, and the host's trustInfo (asm.v2 prefixes) and a second longPathAware made Windows refuse
  it (14001). nebula_host.manifest keeps the identity and OS; Microsoft's link gives the same manifest as before. The
  boot test on the coverage bins: 12.52 % of 511955 lines, engine and game paths as the tree's.
- 5985d9f35: renderer code clang names unused (pairing hashes, kGpuBudgetBusy, an unread config and request, two FSR
  counters), each without a reader in the tree. 4f11d3b15: M-168 for Agent 2 (level bytes in 64 bits; test fails before).
- A1h after these: 92 of 92 engine and 10 of 10 game unit tests, boot test on the normal bins.
- P-052 second half written for Direct3D 12 and Vulkan (scratch scripts), to apply once the first half is proved. On
  the way: Vulkan's scene camera keeps its pipelines by kind and samples while its pass changes with HDR output (a
  pipeline of an incompatible render pass); keyed by pass in the second half.
