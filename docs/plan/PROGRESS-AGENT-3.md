# Progress: Agent 3 (audio, input, menus, text; Android; Windows, Linux, Mac; console limits; game step)

One dated entry per package step. Newest last.

## 2026-11-17 Start

- Took E10, E11, E12, E8, E7 and my part of E14 from Agent 1 (file 05): 71 rows, agent column 3 (4f46aabe7).
- Tree D:/NebulaWork/MarioGalaxyRecomp/Nebula-A3, branch app/work from engine/n1011 bbeeab141.

## 2026-11-17 E10 input and audio sweep (app/work)

- 66818a950 audio: P-037, P-071, P-072 fixed.
  - SDL renders in chunks from one buffer made with the device; no allocation on the audio thread.
  - SDL and Apple report the sound they still hold, so a movie's picture follows what is heard.
  - The Windows fill rises on a late wake or render and never lowers; the log's sound line gives fill and under-runs.
  - Test: galaxy_audio_fill_tests.
- 874754e3b input: P-073, P-084, P-074, P-075, P-076 fixed; P-077 and P-083 in code, open for their proof.
  - One radial dead zone per stick on every host; the game gets the direction kept (game_stick). The Nunchuk
    curve only for Nunchuk counts and the tour script. The stick test fails on the code before (family stick 0.6).
  - Nunchuk stick centre from its calibration; three checks of galaxy_native_input_tests fail before.
  - Short presses latched on SDL, Apple, Windows HID and XInput (4 ms sampler while the game reads).
  - Absolute raw mouse input gives motion (Remote Desktop, virtual machines, pen tablets); macOS uses GCMouse
    counts. galaxy_raw_mouse_win32_tests fails before.
  - The Windows HID worker waits on one completion port; light bar, motor off and the PlayStation mode request do
    not wait; stuck requests are cancelled by the timer. No controller is attached to this PC, so the device proof
    of P-077 and P-083 is owed (a pad run or a pipe-backed worker test).
- Unit tests: 87 of 88 pass in my build; galaxy_player_input_tests needs the gate's settings and dumps folders.
- Not run here: the Apple and SDL code builds in CI on app/work (no Mac SDK on this machine, file 04 decision).

## 2026-11-17 E10 text, menus and the HID worker proof

- 592dff84e P-086, P-160 fixed: every host text has its NebulaText row; the gate's text step fails a missing one.
- b74a6ffe9, 3e9cf913a P-162 fixed: one rule for a line of text in its room (NebulaUi), the achievement board
  included; N-3001 not a fault (the board's narrowing never passes 0.6 on shipped text; tests/engine/board-text-fit-*).
- abafe4418 P-106 fixed: the host texts of every host in 14 languages from one table and one language rule.
- a49c06767 P-077, P-083 fixed: galaxy_controller_hid_tests, a pipe that takes no writes stands in for a pad.
- 6e80c1589 P-165 fixed: NebulaText finds a row by binary search; the gate's text step keeps the table sorted.
- b16544f9e, 264a0ac4a N-3003 (found here): the importer names each refusal; Windows Setup, the Mac setup window and
  Android show it in 14 languages. Open until the Android jobs of my main gate are green (the Mac job is).
- e4c264889 N-3009 fixed: the benchmark results table grows.

## 2026-11-17 E11 Android app

- f0b41e472 P-097, P-098, P-101, P-102 fixed; P-099, P-100 fixed after 17b4cfecc (the tests start the game again;
  Android passes --started-again); P-020 fixed with them. b16544f9e N-3005 fixed (the launcher's refusal text).
- a1fdf2726 P-019: the hosts read the system's heat (Android thermal status and headroom, Apple thermal state),
  galaxy_thermal_state_tests. Open until a tablet run shows the heat (with the E7 tablet A/B).

## 2026-11-17 E12 Windows, Linux, Mac

- c768c72a5 P-121, P-115; a24031b5d P-120; 56a8fbeae P-113, P-114 (Setup).
- 7fd212f08, 40f41526f, 703dd4363 P-015, P-016, P-017 (crash reports on Linux and Apple, a stack for every thread).
- e2e545fe3, ce1432772 P-036; 69e71ed83 P-126; 5197842af P-125 (Linux); N-3002 out of scope (the Mac half of P-125).
- 495c7f1dc P-110, P-111, P-112; 5721bb234 P-103; 37b342f7f P-105, P-107, P-108; 5b2328af3 P-109.
- ca7d85851, 46b6f6e4c P-116 and N-3004 (replacement movies on every platform through the system's decoder).
- 49325b93b P-104 out of scope (the owner: no iOS host until the game builds there).
- b61e80c09 P-035: the Mac app ships universal; the 'Mac (Intel)' job builds, starts and tests its Intel half. Its
  first run (44267379242) failed in the unit tests: Homebrew's OpenSSL headers in /usr/local/include were read before
  BoringSSL's. f113fe503 puts BoringSSL's folder first. Open until 'Mac (Intel)' is green.
- Rows proved by a Linux or Mac job stay open until that job is green (12cdff03b); 13 closed on run 44267379242.

## 2026-11-17 E8 console limits and gameplay rules

- 857af1b4c P-118, P-168, P-180 decided; ebf5801ad P-166 decided with its test (the owner's decisions, file 01 E8).
- 9dd53e94d P-167 fixed; 5bf26c718 P-148 fixed (a sensor keeps every sensor it touches).
- e4c264889 N-3006, N-3007 fixed (Mario's messages, the spider thread's hangers grow); 0139f4174 N-3011 fixed
  (Mario's state timers stop at their top). N-3010 decided: the other fixed arrays the sweep read are sizes of the
  design, not limits on what a player makes.
- Proof: the gate's step-hash replays (466a52e30).

## 2026-11-18 E7 game step speed

- The step's own fixes (P-013, P-039, P-088, P-127 to P-141, P-152): every one proved by its unit test and the rows
  tour (the module with hook points built with the plain paths and every verify on, 20 stops). The first PC A/B
  (before 264a0ac4a, tip) gave +2.7% game time a step; its profiles named two causes, both fixed:
  7f85256c7 P-038 (the draw walks a dense copy of the packet list, made at the first draw after a change) and
  400e59c21 P-134 (decided: corners made at each use; the table measured slower).
- b2c422f83 P-040 part 1: one worker pool for the process (galaxy::WorkerPool, the host's; two lanes: the renderer's
  recording urgent, the game's passes normal; a pass wakes at most half as many threads as it has ranges). Game API
  161 parallel_for; the plain module's frustum tests and the shadows' gravity searches on the step threads.
- 7824db64c P-146: the shadows' line queries on the step threads, into hit rooms the caller owns.
- Proof so far: the rows tour 20 of 20 the same at every step with 0, 1, 2 and 29 workers, with the clipping pass
  check and NEBULA_SHADOW_LINE_VERIFY on. Owed: the replay beside a full build, the tablet tours, the PC and tablet
  A/B (before, serial, after), Agent 1's render timeline proof, the pool bench on both for the thresholds.
- 65a3055df N-3012 fixed: the red sanitizer job of f113fe503 (the sensor test's null scene holder).
- ba1d9e259 P-035: instrument.py's copies cache (NEBULA_LUMA_CACHE) and one CI cache folder; the warm 'Mac (Intel)'
  run is owed.
- The replay beside a full build (the old APK's): 20 of 20 the same. Condition (3) on the real hook module (Agent 2's
  1123 test): no hook point and no callback ran on a worker.
- 816b2e617 N-3013: the headless host's include of the pool (red 'Windows game modules' on ba1d9e259).
- N-3014: the pool and galaxy::logical_cores() counted the machine's processors, not those the process may use (red
  'Windows (three renderers, unit tests)' on ba1d9e259). The count now follows the process's affinity; the pool test
  holds itself to four processors: 20 of 20 failed before the fix, none of the urgent lane after.
- PC round 1 (hold A): game ms a step, all stops before 1.275, after 1.251 (-1.9%); the five heaviest 1.362, 1.310
  (-3.8%); the two after runs differ by up to 28% at one stop, so round 2 completes the medians.
- P-040 part 3, the actors' animation (plain module): a run of actors whose exact class animates as LiveActor does
  (lumacore::exactClassOf, kAnimatesAsLiveActor) is one batch: base matrices, frames and visibility on the game thread
  in list order, the models' joint walks on the step threads (the root's calculator passed in, each track read at its
  player's own frame, the walk's stack per thread), then draw lists and collision matrices in list order. An actor
  whose model or data holds a callback or another calculator, or that is taken, runs its calcAnim as before.
  NEBULA_ANIM_WALK_VERIFY makes each walk again on the game thread and compares it to the bit.
  Its first build did not link: an extern thread_local read from another file makes clang call the C runtime's
  on-demand initialiser on Windows (__dyn_tls_on_demand_init, __tls_guard), which the game modules do not link; the
  walk state is now declared constant-initialised (NEBULA_CONSTANT_TLS, Model.hpp). Proof: the rows tour 20 of 20 the
  same with 0, 1, 2 and the host's workers, NEBULA_ANIM_WALK_VERIFY on, no walk mismatch; native tests 23 of 23 (the
  class test: an object whose constructor never names its class gets none). Owed: the tablet tours, round 2 and the
  tablet A/B, the grain (8 a placeholder).
- E14 (my part of file 01) found four loops and one bound, each a row: N-3015 (a Life Up's change of the most health
  looped for ever when a step was refused), N-3016 (a hanging Mario's ceiling probe repeated for ever), N-3017 (the
  sequence check did not bound port numbers; the game indexes 16 ports by them), N-3018 (the running rabbit's turn up
  from the map). Notes of everything read: a3-prof/e14-notes.md.
- The red 'Android start check (x86_64 emulator)' on ba1d9e259 (run 44312244458): the game process ends with status 3,
  the start check's 'settings or renderer failed'; its reason line was lost. Android's stdout and stderr reach logcat
  through a pipe that a thread reads, and std::_Exit came before the thread had read it: the log stops after the first
  of five [vulkan] lines written together. N-3019 (4b52a1205): host_window::finish_output drains the pipe before every
  std::_Exit. The earlier failure of that job on app/work (f6cdc4f58) was the emulator's AVD, not the game.
- N-3003 closed: the Mac and both Android jobs are green on run 44312244458. P-019 owes only the tablet run (the test
  binary and a game run showing the heat).
- The first landing (the Planner's 06:49): app/work cut before b2c422f83, with N-3012, P-035's copies cache, N-3015 to
  N-3019 and these notes. P-040 parts 1 to 3, P-146, N-3013 and N-3014 come with the second landing, once proved.
