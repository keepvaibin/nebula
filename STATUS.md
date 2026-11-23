# Nebula native plan: status

Written 2026-11-23 03:27. If this time is more than three hours old, the work has stopped: open the session and send the word continue.

Steps landed: 5 of 138 (4%).

In work now: Nothing started.

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

None yet.

## Repairs

1 open of kind fail, 1 in all.

| Id | State | Kind | Check | Renderer | Traced to | Seen |
| --- | --- | --- | --- | --- | --- | --- |
| R-001 | open | fail | tour/d3d12 | d3d12 | step S1.1 commit abc | 1 |

## For the owner to check by hand

Nothing is owed.

## Waiting for the owner

Nothing waits for the owner.

## Blocked

Nothing is blocked.

## Tool mends

No tool was mended.

## Decisions (0)

None.

## Checks changed during the run (18)

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
- 2026-11-23 L-linux-window .github/workflows/check.yml: the Linux start check runs on a virtual screen of 1920 x 1080 with a window manager (openbox) and fails unless the log says the window is borderless full screen at that size and the system's pointer is hidden (allowed by the planner's message of 2026-11-23 (the owner's Linux faults))
- 2026-11-23 L-linux-window .github/workflows/release.yml: the Linux package start check runs as the check.yml one, and first checks the unpacked folder's top (install.sh, README.txt, files, support) and the first line of README.txt (allowed by the planner's message of 2026-11-23 (the owner's Linux faults))
- 2026-11-23 L-linux-window tools/gate/tests/linux_launch_test.py: the test is rewritten to run the start script on every route of its words (terminal, dialog tools, file) and for no install and an error ending; the cases it had stay (allowed by the planner's message of 2026-11-23 (the owner's Linux faults))
- 2026-11-23 L-linux-window tools/gate/tests/mod_support_package_test.py: the Linux fixture holds the menu entry and the type file under support/ (the package's new layout), as the check finds them by name (allowed by the planner's message of 2026-11-23 (the owner's Linux faults))
- 2026-11-23 M-004 tools/gate/tests/gate_line_test.py: the table of the --also parts that the test names has replays/rates beside the others (the check name in CHECKS and the sorted list of ALSO_PARTS) (allowed by docs/plan/04-GATE-AND-MAIN.md section 15, the table of a step's or repair's extra checks)
- 2026-11-23 M-004 tools/gate/tests/plan_rerun_test.py: the set of the step tool's extra checks (ALSO_CHECKS) has replays/rates (allowed by docs/plan/04-GATE-AND-MAIN.md section 15, the table of a step's or repair's extra checks)

## Tool faults

0 tool faults recorded.
