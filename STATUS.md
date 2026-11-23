# Nebula native plan: status

Written 2026-11-23 02:43. If this time is more than three hours old, the work has stopped: open the session and send the word continue.

Steps landed: 5 of 138 (4%).

Now: S0.2.1 since 01:06, waits for mend M-004 (proof 2: a time rate of 2 against 1); ahead: nothing

In work now:
- S0.2.1 Landing A: drawing stops changing the game (agent 1)

## Packages

| Package | Landed | Of |
| --- | --- | --- |
| S0.1 | 5 | 5 |
| S0.2 | 0 | 6 |
| G6 | 0 | 8 |
| S0.3 | 0 | 2 |
| R1 | 0 | 4 |
| R2 | 0 | 5 |
| G1 | 0 | 5 |
| G2 | 0 | 9 |
| R3 | 0 | 13 |
| R4 | 0 | 10 |
| R4v | 0 | 2 |
| R5 | 0 | 8 |
| R6 | 0 | 7 |
| G3 | 0 | 7 |
| G4 | 0 | 5 |
| G5 | 0 | 5 |
| R7 | 0 | 8 |
| R8 | 0 | 6 |
| R9 | 0 | 2 |
| R10 | 0 | 4 |
| G7 | 0 | 5 |
| G8 | 0 | 4 |
| G9 | 0 | 5 |
| Z | 0 | 3 |

## Last 10 landings

| When | Step | Title | Code commit | Main commit |
| --- | --- | --- | --- | --- |
| 2026-11-22 18:13 | S0.1.5 | Render data path step 2, second landing: `f4/e3-2b` and `f4/e3-2c` | 02d236b5c | fb2dfa855 |
| 2026-11-22 17:13 | S0.1.4 | Render data path step 2, first landing: `f4/e3-2a` | d9dbc32ac | ce59efeb8 |
| 2026-11-22 16:37 | S0.1.3 | Render data path step 1: `f4/e3-1a`, `f4/e3-1b`, `f4/e3-1c` | 809c419ff | 8be38b7b8 |
| 2026-11-22 14:25 | S0.1.2 | Documents | 59bdb8776 | 2c5dbb271 |
| 2026-11-22 14:17 | S0.1.1 | Land `rows/p022c` and `rows/p022d` as one batch | 83b537af7 | 463d38612 |

## Repairs

0 open of kind fail, 3 in all.

| Id | State | Kind | Check | Renderer | Traced to | Seen |
| --- | --- | --- | --- | --- | --- | --- |
| R-001 | landed | fail | build/windows-asan | - | not traced | 1 |
| R-002 | landed | fail | coverage | - | not traced | 1 |
| R-003 | landed | fail | soak | - | not traced | 1 |

## For the owner to check by hand

- S0.1.2 Documents: P-096: on the tablet, set up the game once more over an installed one, and once with the setup cut off (close the app mid-copy); then start the launcher: the old game stays playable until the new one is whole, and nothing is left behind (no game.pak.new, no half copy). (agent 1, 2026-11-22 14:18)

## Waiting for the owner

Nothing waits for the owner.

## Blocked

Nothing is blocked.

## Tool mends

| Id | State | Step | Title | Attempts | Minutes | Commit |
| --- | --- | --- | --- | --- | --- | --- |
| M-001 | closed | R-002 | coverage: the history's key holds the suites that ran, a run that fell is not the next bar | 0 | 0 | 2d7287334 |
| M-002 | closed | R-003 | soak: a run of eight laps or more is judged from lap 6 (slope and lowest of the last laps), a spike is not growth | 0 | 0 | 2d7287334 |
| M-003 | closed | R-001 | build_windows.ps1 refuses a build folder configured with another NEBULA_SANITIZE instead of configuring it again | 0 | 117 | c60999ee1 |
| M-004 | open | S0.2.1 | the replay check cannot play a run at a time rate of 2 and compare its step hashes with a rate-1 run (S0.2.1 proof 2) | 0 | 68 | - |

## Decisions (0)

None.

## Checks changed during the run (14)

- 2026-11-22 S0.1.3 tools/verify/console_census.py: the open group gx_sections counts the four section names GXMT, GXTG, GXTM and GXWP with a ceiling of 31, and the closed group fifo excludes only those four whole words (allowed by docs/engine/FRAME-MODEL.md section 2.9)
- 2026-11-22 U-unattended tools/gate/main_push_guard.py: the guard also refuses a push of a branch named plant/... (allowed by docs/plan/native/TOOL-MEND-PLANTS.md, how a plant is kept from ever landing)
- 2026-11-22 U-unattended tools/gate/strictness.py: new: the strictness check itself (allowed by docs/plan/native/TOOL-MEND.md section 6)
- 2026-11-22 U-unattended tools/gate/tests/plan_live_test.py: the landings list has a seventh column, figures (allowed by docs/plan/native/AGENT.md section 11)
- 2026-11-22 U-unattended tools/gate/tests/plan_page_test.py: the page data has the new parts and the old-page line; heartbeat.txt is in the live folder (allowed by docs/plan/native/AGENT.md section 12)
- 2026-11-22 U-unattended tools/gate/tests/plan_rerun_test.py: a new landing takes the tool's earlier state commit off the branch; a card and a log line permit a test tool in a step; the heartbeat is not a change (allowed by docs/plan/native/AGENT.md section 9)
- 2026-11-22 U-unattended tools/gate/tests/plan_test.py: the repair rules name the mend; the allow-list case names the file in the card and writes its log line (allowed by docs/plan/native/AGENT.md section 9)
- 2026-11-22 U-unattended tools/gate/tests/push_again_test.py: the 4xx case that is not tried again uses 404 (allowed by docs/plan/native/AGENT.md section 5)
- 2026-11-22 U-unattended tools/gate/tests/result_kinds_test.py: the test gate has its own live folder; the third flake gives a repair, not a ledger row to open (allowed by docs/plan/04-GATE-AND-MAIN.md section 9)
- 2026-11-22 U-unattended tools/gate/tests/scope_test.py: the repository checks of a run now include repo/strictness (allowed by docs/plan/04-GATE-AND-MAIN.md section 17)
- 2026-11-22 Y-beta-build .github/workflows/check.yml: the Mac job builds both processors (build_macos.sh -a "x86_64 arm64", libc++ for both under one cache key, the x86_64 Rust target); the Linux start check runs with the Khronos validation layer (--gpu-validation 1) and fails on a [VK-LIVE-ERR] line (allowed by briefs/Y-beta-build.md pieces 1 and 5, and the planner's message of 2026-11-22 21:58 (item 6))
- 2026-11-22 Y-beta-build .github/workflows/release.yml: the version step also gives mod support and the version's label to every package job; the lumakit job runs only with mod support (the jobs that need it go on when it is skipped); a dry run can be told its version; the Mac job builds both processors with the compiler check.yml pins, and its package is kept for the Intel Mac job; the Linux start check runs with the validation layer; the pgo check of a pre-release tells and goes on; every package is checked by tools/check_mod_support_package.py (allowed by briefs/Y-beta-build.md pieces 1 to 3 and the planner's message of 2026-11-22 21:58 (items 4 to 7))
- 2026-11-23 M-004 tools/gate/tests/gate_line_test.py: the table of the --also parts that the test names has replays/rates beside the others (the check name in CHECKS and the sorted list of ALSO_PARTS) (allowed by docs/plan/04-GATE-AND-MAIN.md section 15, the table of a step's or repair's extra checks)
- 2026-11-23 M-004 tools/gate/tests/plan_rerun_test.py: the set of the step tool's extra checks (ALSO_CHECKS) has replays/rates (allowed by docs/plan/04-GATE-AND-MAIN.md section 15, the table of a step's or repair's extra checks)

## Tool faults

1 tool fault recorded.

The last 20:

| When | Step | Test | What happened | What the agent could not do |
| --- | --- | --- | --- | --- |
| 2026-11-22 14:33 | - | (by hand) | plan.py land S0.1.3 ended code 4 (not judged): gate.py's push check refused main's move because tools/verify/console_census.py's closed group fifo counts 31 (the card's section names GXMT, GXTG, GXTM, GXWP in native/include/nebula_model.h, runtime/src/render/core/model_sections_tests.cpp, crates/nebula-import/src/cook/model_write.rs); it is a certain refusal of the change, reported as not judged; plan.py toolfault --last then said: agent 1 has no tool fault recorded | - |
