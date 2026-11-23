# Nebula native plan: status

Written 2026-11-22 22:06. If this time is more than three hours old, the work has stopped: open the session and send the word continue.

Steps landed: 5 of 138 (4%).

In work now: Nothing started.

## Packages

| Package | Landed | Of |
| --- | --- | --- |
| S0.1 | 5 | 5 |
| S0.2 | 0 | 6 |
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
| G6 | 0 | 8 |
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

## Checks changed during the run (10)

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

## Tool faults

0 tool faults recorded.
