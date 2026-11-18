# Handover: Agent 3 to Agent 1 (2026-11-18 08:30)

Agent 3 stopped on the owner's order of 08:22. Every open row of Agent 3's is now Agent 1's. Detail beyond this file:
docs/plan/PROGRESS-AGENT-3.md (E7, E11, E12, E14 sections) and D:/NebulaWork/build/a3-prof/e14-notes.md.

## Branches

- app/work 7b2ca75ec (plus this file): 74+ commits never on main. Holds everything below, the workers included:
  P-040 parts 1 and 3 (b2c422f83, part 3 after it), P-146 7824db64c, P-035 ba1d9e259, N-3012 to N-3033.
- app/land1 d31424a99: the first landing's candidate, the cut of the Planner's 06:49 (2): app/work at b2c422f83^
  (70 commits; 7f85256c7 P-038 and 400e59c21 P-134 are in) plus 65a3055df N-3012, ba1d9e259 P-035, and the E14 fixes
  N-3020 to N-3033 (a0ffe5aa8, d31424a99). No workers (no P-040, no P-146). Its dispatch 44333101363 is green on
  every job, the Android start check included; only 'Mac (Intel)' was still running at 08:25.
- wip/agent-3 45717ae76 (app/work + one commit, NOT BUILT): N-3034, N-3035, N-3036 and the E7 walk-timing
  instrumentation (NEBULA_ANIM_WALK_TIMING in ActorAnimPass.cpp; never land it). Worktree D:/NebulaWork/a3-wip.

## The first landing, as it stands

1. Wait for Agent 1's candidate on main (it brings --github-wait and the gate queue). origin/main was 87b6d3db5.
2. In a worktree of app/land1: merge origin/main; resolve; build both game modules and the native tests.
3. Main gate with --move and --github-wait 180. Its 'Mac (Intel)' job is cold on a gate/<run> branch whatever
   app/work cached (over 80 min on run 44312244458); allow for it.
4. After it: the second landing brings the workers (P-040, P-146) from app/work once their measures prove them, then
   Agent 4's link/m10 5854c653b goes on main.

## Open row groups

- E7 P-040 part 1 (pool, API 161 parallel_for; frustum tests and the shadows' gravity searches on step threads),
  P-146 (the shadows' line queries), part 3 (the actors' animation batch; grain 8 is a placeholder), all on app/work.
  Proved: rows tour 20 of 20 the same at 0, 1, 2 and 29 workers with NEBULA_ANIM_WALK_VERIFY and
  NEBULA_SHADOW_LINE_VERIFY on; the replay beside the old APK's build 20 of 20; no hook ran on a worker (Agent 2's 1123).
  Owed: the tablet tours of part 3; round 2 medians (below); the tablet A/B (before, serial, after); the grain chosen
  from the pool bench on PC and tablet; Agent 1's render proof (hold C) read out. The rows stay open until the speed
  is measured. Commands: a3-prof/measure_a.sh, measure_b.sh, measure_c.sh (through hand_hold.py, below);
  rows tour per PROGRESS-AGENT-3.md E7.
- E7 N-3034, N-3035, N-3036 (wip/agent-3): each has a native test whose before/after proof ran (old fails, new passes:
  scratchpad proof.py, old overlays); nothing else ran. Owed: build both modules
  (a3-prof/n3034_build.sh, n3036_build.sh as templates), native tests, `cargo test -p nebula-import` (the two
  check_flow tests), and for N-3036 the disc test: `NEBULA_DISC_FILES=<disc dir> cargo test -p nebula-import
  disc_messages_read_out_and_cook_back -- --ignored` (proves the game's own flows pass the new cook check), then the
  step-hash replays at the gate. Ledger: N-3034/N-3035 rows are on wip; the N-3036 row is on wip too. Next id N-3037.
- E11 P-019: owes only the tablet run (galaxy_thermal_state_tests binary and a game run showing the heat).
- E12 P-035: owes the warm 'Mac (Intel)' run: dispatch 44344374865 on app/work 7b2ca75ec, still running at 08:30.
  The same run proves N-3013 (Windows game modules) and N-3014 (worker pool test) on Windows.
- E14: table below. Rows N-3015 to N-3033 fixed (app/work; N-3020 to N-3033 also on land1); each test fails before.

## Holds A, B, C (PC, 5090; scripts and outputs in D:/NebulaWork/build/a3-prof)

- A (13.6 min, measure_a.sh, runs/m-*/out/benchmark.json): game ms a step, all stops: before 1.275, after 1.251
  (-1.9%); the five heaviest 1.362 vs 1.310 (-3.8%). The two after runs differ by up to 28% at one stop.
- B (7.4 min, measure_b.sh): round 2, m-serial-2 and m-before-2 (not yet put into the medians), the pool --bench
  (pool-bench-pc.txt: 32 processors, 29 workers; per count/grain the alone/awake/asleep times; asleep wakes cost
  10-250 us), and the N-3014 quiet run 0 of 20 failed.
- C (10.1 min, measure_c.sh, pc-{old,new}-{match,unl}-N.out): pace-match and pace-unlimited, d3d12, old RecordPool vs
  new pool: all six pass. pc-new-unl-1 does NOT count: Agent 3 compiled a proof during it (board NOTICE 08:18).
  measure_c2.sh reruns that pair; never ran. The figures beyond pass/fail are not read out yet.
- hand_hold.py "<title>" <script> <minutes>: queues for gate.lock, takes measure.lock, runs, frees both, writes the
  board lines. luma_test.py needs --lock (measure_c.sh passes a3-prof/<name>.run.lock).

## E14 (Agent 3's folders); notes: a3-prof/e14-notes.md

| Folder | Files | Read whole | Swept | Left |
|---|---|---|---|---|
| Player, file 01's eight | 8 | 8 | all | done |
| Player, other | 99 | 0 | loops, rate, computed indices of tables | 99 whole reads |
| Sound | 82 | 6 | all: loops, data indices, divisions | 76 whole reads |
| Enemy | 109 | 0 | loops, modulos, args as indices/divisors, appends, fixed arrays, narrow arg members | 109 |
| Boss | 166 | 0 | as Enemy (without the last two) | 166 |
| Ride | 19 | 0 | as Enemy; every point count | 19 |
| NPC | 76 | 0 | as Enemy | 76 (TalkNodeCtrl partly read: N-3036) |
| MapObj | 314 | 0 | as Enemy | 314 |
| LiveActor | 62 | 0 | as Enemy | 62 |

Swept with no fault: the 54 fixed-array candidates, Ride point counts, Player divisions, per-step allocations, joint
and material lookups. Rate: no clock or display rate outside System/LumaTime.cpp. Fur faults in Game/Util were
posted to Agent 2 (board 08:13); not edited.

## The red Android start check (x86_64 emulator)

- Red on app/work ba1d9e259 (run 44312244458): the game ends with status 3 (host fatal path: die() at startup or
  NEBULA_PANIC, host_main.cpp) after '[vulkan] device=SwiftShader'. Its reason line was lost: N-3019 (4b52a1205)
  drains the stdout/stderr pipe to logcat before every std::_Exit.
- Green on land1 (44333101363) and on Agent 1's gate branch; the part 3 test APK passes it on the tablet (status 0).
  So the cause is in the workers (P-040/P-146), app/work only, on SwiftShader. The reason line will show in
  44344374865's job log. The earlier red on f6cdc4f58 was the emulator's AVD.

## Traps written nowhere else

- A push to app/work cancels a running dispatch on app/work (concurrency check-<ref>, cancel-in-progress off main and
  gate/*). This file's commit carries [skip ci] for that reason. Push runs skip Windows, Android and the start check;
  caches save only on main or a dispatch: dispatch to see those jobs.
- An extern thread_local read across files makes clang on Windows call __dyn_tls_on_demand_init, which the game
  modules do not link: declare it constant-initialised (NEBULA_CONSTANT_TLS, Model.hpp).
- Placement arguments are read by MR::getPlacementArgNNoInit, not getJMapInfoArg: sweep that name.
- proof.py: pass the compile command as one string; shlex mangles -D_ACRTIMP="".
- After a header change: `python -I tools/lumacore/gen_lumacore.py .`, then `--check`, then
  `git checkout docs/modding/reference/classes.md`.
- Never compile or build while measure.lock is held, yours included.

## Folders on D:

- Keep: D:/NebulaWork/MarioGalaxyRecomp/Nebula-A3 (app/work), D:/NebulaWork/a3-land (app/land1),
  D:/NebulaWork/build/a3-prof (scripts, runs, notes, p146bins, after-game), D:/NebulaWork/build/a3-runs.
- Keep, not Agent 3's to clear: D:/NebulaWork/a3-apksrc (its staged changes are not Agent 3's).
- May delete: D:/NebulaWork/a3-wip once wip/agent-3 is taken (`git worktree remove`), D:/NebulaWork/a3-apkold
  (old APK source worktree), D:/NebulaWork/build/a3-apk-old, a3-apk, a3-android-movie, a3-logs, a3-work (build and
  log output only).
