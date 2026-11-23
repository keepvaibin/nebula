# Planted faults: how a mended check is shown to refuse again

A mend that changes how a check judges (`docs/plan/native/TOOL-MEND.md` section 7) shows two things by the real gate: a
clean main plus the mend passes the check, and the check's planted fault is refused again. This file holds the recipes.
Seven were proved on 2026-11-22 by helper T11-B, each on a clean main, each refused; an eighth (the soak's memory) by helper W on 11-22. They are recipes, not patch
files: the code a plant sat in will be rewritten by the plan. Where a recipe names a function or a file, find the same
place by its meaning in the tree you have; the commit that held each plant at that time is named so that
`git show <commit>` shows it while the object store still has it.

## How a plant is kept from ever landing

1. It lives on a branch named `plant/...` (a mend uses `plant/M-00N-<n>`), made from your mend's tip in your own folder.
   Its commit message begins `planted fault (not for landing):`.
2. It is judged only by a gate command without `--move`. `gate.py` refuses `--move` unless it is the landing runner's
   own call (`main moves only through gate.py land <branch>`), so a plain `gate.py --main ...` cannot move main.
3. It is never pushed. The push guard (`tools/gate/main_push_guard.py`, called by the pre-push hook) refuses any push of
   a ref under `refs/heads/plant/`.
4. It cannot be handed to the landing: `gate.py land plant/...` refuses a branch whose name starts with `plant/`, in
   seconds, before anything is queued (and so does the landing runner's own entry). `plan.py land` and `plan.py mend
   land` hand over only `native/` and `mend/` branches. `tools/gate/tests/unattended_gate_test.py` pins the gate's
   refusal and the push guard's.
5. Its helper deletes it when the report is written (the commit and the run folder stay in the report).

## The command that judges a plant

From the folder the plant branch is checked out in, as a background command with no time limit:

- `python -X utf8 -I tools/gate/gate.py --main --scope auto [--also <names>] --tablet R52WA0FXE5J --tablet-wait 30`
  runs the checks the plant's changed files ask for, as a landing would, and moves nothing. Leave `--tablet` out when
  the plan does not need it (the gate says so: `--main needs the tablet's serial`).
- `python -X utf8 -I tools/gate/gate.py --main --only "<check>,<check>"` runs those checks alone (builds and game data
  they need come with them).

`gate.py` takes `gate.lock` itself and waits for it (a held lock of a long run's part or of another landing: 6 to 32
minutes were waited on 2026-11-22; the seconds below are the run's own time). The run ends with `KIND ...` lines for
each result that is not a pass, then `VERDICT fail`. The line that shows the refusal is the first `KIND fail`, or for
the speed measure `KIND worse`, of the check named in the table. The same command on the clean tip must print
`VERDICT pass` and no `KIND` line of that check.

## The plants

| # | Check | Kind of fault | Where it was planted | Command | The line that shows the refusal | Seconds |
| --- | --- | --- | --- | --- | --- | --- |
| 1 | the replays and the tour pictures | a change of how the game plays, small enough that nobody sees it | the top walking speed of Mario, 1.0 to 1.02, in the table of walk targets (`native/petari/src/Game/Player/MarioWalk.cpp`, `sWalkTargetTable`), and one comment line in a file of the Vulkan renderer so that the plan holds all three renderers and the tablet (`runtime/src/render/vk/ambient_occlusion_vk.cpp`); commit d54e16819 | `--main --scope auto` with the tablet | `replays/d3d12 FAIL 0 of 12 the same; not: 010-CannonFleetGalaxy-1 (differs from step 190 ...`; `pictures/tour/<r> FAIL 13 of 24 the same` on each renderer; `validate/d3d12` and `validate/d3d11` 0 of 12 (`the step hashes differ from the recording`) | 1606 (39 checks, 7 failed) |
| 2 | the pictures of one renderer | a colour that is a little wrong on Direct3D 11 only | the red of every pass of the presentation chain times 0.9, a blend state set before the passes are drawn (`runtime/src/render/d3d11/post_process_d3d11.cpp`, in `PostProcessor::record`); commit 84543a53d | `--main --scope auto` with the tablet | `engine/d3d11 FAIL 17 of 18 pass; not: menu-key-icons fail`; `pictures/engine/d3d11 FAIL 0 of 30 the same`; `pictures/tour/d3d11 FAIL 0 of 24 the same` | 492 (32 checks, 3 failed) |
| 3 | the speed measure | a cost in the game thread's step, small and steady | a busy loop of 300 microseconds at the start of `svc_frame_wait` (`native/host/host_main.cpp`); commit 5bd889e3a. A loop of 100 microseconds (3b, commit a5f8172d9) is refused as well | `--main --scope auto` (the plan holds `measure/step/pc`) | `measure/step/pc WORSE ... d3d12 cpu_game_ms worse: base 0.317, candidate 0.619 (+87.2%, +88.1%, +90.0% in three rounds; the band is 5.5%)`; 3b: base 0.316, candidate 0.413 (+28.0%, +28.8%, +28.5%) | 675; 3b 659. 3: no cost under 0.1 ms was tried |
| 4 | the memory-fault tour | a read one element past the end of a block taken with `new` | in `NameObjListExecutor::executeMovement` (`native/petari/src/Game/NameObj/NameObjListExecutor.cpp`) every step: `int* b = new int[4]`, fill it, read `b[4]` through a volatile pointer into `b[0]`, `delete[]`; commit 0023a3376 | `--main --scope repo --also sanitizer/tour` (the gate has no `--only` for `tour-asan`) | `tour-asan/d3d12 FAIL 0 of 102 pass`; `KIND fail tour-asan/d3d12 001-BattleShipGalaxy-1 :: sanitizer report: AddressSanitizer: use-after-poison ... at NameObjListExecutor::executeMovement` | 553 (9 checks, 1 failed; the check itself 248.1). Before the mend of the sanitizer's heaps (2026-11-22, row P-341) the same plant passed 22 of 22 in 2616: the net could not see the game's memory |
| 4b | the known list of the sanitizer | a report that is listed, not refused | the plant of 4 plus one line in `tools/gate/sanitizer-known.tsv`: `NameObjListExecutor::executeMovement`, `pending`, `AddressSanitizer: use-after-poison`; commit 1c8bc3417 | as 4 | The tour passes: `tour-asan/d3d12 pass 102 of 102 pass; listed sanitizer reports: NameObjListExecutor::executeMovement x102 (pending)`. A list is the way to hide a fault: the strictness check refuses the landing of the list line (`tools/gate/strictness.tsv` names `tools/gate/sanitizer-known.tsv`) unless `docs/plan/strictness-log.tsv` has its line. Show 4b by running the strictness check on the branch (`python -X utf8 -I tools/gate/strictness.py --base origin/main`), not by the tour | tour 247.5 (9 checks, 298 in all) |
| 5 | the graphics debug layer | a call that the layer reports and no picture shows | a Direct3D 11 buffer of 0 bytes made at the start of the presentation chain (`runtime/src/render/d3d11/post_process_d3d11.cpp`, in `PostProcessor::initialize`: `ByteWidth = 0`, constant buffer, result ignored); commit e9e9fdd61 | `--main --only validate/d3d11` | `validate/d3d11 FAIL 0 of 12 pass`; `KIND fail validate/d3d11 010-CannonFleetGalaxy-1 :: Direct3D debug layer error 66 ... ID3D11Device::CreateBuffer: The Dimensions are invalid. ByteWidth cannot be 0.` | 151 (3 checks; the check 78.9) |
| 6 | the endurance run | a crash that comes after minutes of play | in `svc_frame_wait` (`native/host/host_main.cpp`) a static counter; at 18000 steps (5 minutes at 1/60 s) a write through a null pointer; commit 621bf41db | `--main --scope repo --also soak` | `soak FAIL` for each renderer, whose line begins with the crash (`crashed at 299.5 s: exception 0xC0000005 ... in stage FactoryGalaxy`; the soak names a crash or a hang first since 2026-11-22) and not with the laps of memory. | 378 (7 checks; the soak 300.9) |
| 7 | the unit tests | an expected value changed | one `expect` in `runtime/src/work_cpu_tests.cpp`: `w.total() == 60` to `61` ("100 to 160: 60"); commit edf1ed555 | `--main --only unit/windows,unit/game` | `unit/windows FAIL 99% tests passed, 1 tests failed out of 99` | 28 (3 checks; the check 5.1) |
| 8 | the endurance run's memory | a leak: memory allocated, written and kept, from the start of the run | in `svc_frame_wait` (`native/host/host_main.cpp`), at its start, a block of 87381 bytes taken with `malloc`, filled and kept, once every second of wall time (5 MB a minute), through a volatile pointer so that it is not dropped; commit 722379d9a on `plant/w-leak`. The rate is chosen for the short form: the third lap of a 30-minute run is half a lap, its median 9 minutes after the second's, so a leak is seen at about 3 MB a minute and not at 2 (the recorded clean run's samples plus a leak: +32 to +36 MB at 3, +25 to +30 at 2, bound 32) | `--main --scope repo --also soak` | `soak FAIL` on each renderer: `lap medians 667 732 797 MB; the most above lap 2: +65.0 MB (bound 32)` (d3d11), `+64.9` (d3d12), `+57.5` (vulkan); the clean side is the soak of the landing 20261122-125622-463d38612-main (+8.4, +12.9, +7.9) | 2523 (7 checks, the first build of its folder 681; the soak 1820). The two-hour judge (`judge_long`) has no plant run of its own |
| 9 | the rate check | a draw that changes the game, so that a run at a world rate of 2 (two steps for each frame drawn) is not the run at a rate of 1 | the fault is the game's own on main before S0.2.1: drawing took numbers from the game's random stream (`MR::getRandom` in Mario's draw code, `native/petari/src/Game/Player/MarioActorSpecialDraw.cpp`) and made step decisions in `draw()`; main c60999ee1 plus mend M-004 (commit c13ae4f01) is the plant, and `plant/M-004-1` (the game code of S0.2.1's commit 1f3f02fca, applied to the same tools, commit b5cf2ca4a) is the clean side. On a main that has S0.2.1, plant by taking 64 numbers from `MR::getRandom` in a draw function of Mario | `--main --scope repo --also replays/rates` | clean: `replays/rates pass 17 of 17 pass`, `VERDICT pass`; planted: `replays/rates FAIL 8 of 17 pass; not: 001-BattleShipGalaxy-1 fail, ...` and `KIND fail replays/rates 022-EggStarGalaxy-1 :: the step hashes at a rate of 2000 differ from those at a rate of 1000: differs from step 296 ...` | clean 450 (8 checks; the check 194.1, the build 234.7); planted 393 (the check 194.5) |

## Which plant for which mend

| The mend changes | Show |
| --- | --- |
| how a replay or a step hash is compared, how a picture is compared or approved, the tour or the engine tests' judge | 1 and 2 |
| the rate check (`tools/gate/replays_rates.py`), the runner's `rate` key, the game half's hash after each world step | 9 |
| the speed measure, its bands or its figures | 3 (and 3b when the mend touches the smallest cost the check sees) |
| the sanitizer's build, run or report reading | 4, and 4b on the strictness check |
| the validate lane, the debug layers' reading | 5 |
| the soak, the endurance run, a lane's time limit | 6; 8 when the memory judge changes |
| the unit-test runner or its result reading | 7 |
| the kind of a result (`tools/gate/kinds.py`), the three-failure rule, the landing queue | the plant of the check whose result passes through the part you changed; when you cannot tell, 1 and 7 |

## What a plant must not be

- Not a change of a test or of a tool: it is a fault in the game or in the renderer, the thing the check exists for.
- Not a patch kept in a file: it is made by hand from the recipe in the tree you have, so that it is a fault in today's
  code and not in the code of 2026-11-22.
- Not left behind: the plant branch is deleted when the report is written.
