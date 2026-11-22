# Nebula native plan: status

Written 2026-11-22 14:34.

Steps landed: 2 of 138 (1%).

Now: S0.1.3 since 14:26; ahead: nothing

In work now: Nothing started.

## Packages

| Package | Landed | Of |
| --- | --- | --- |
| S0.1 | 2 | 5 |
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

| When | Step | Title | Code commit | Main commit |
| --- | --- | --- | --- | --- |
| 2026-11-22 14:25 | S0.1.2 | Documents | 59bdb8776 | 2c5dbb271 |
| 2026-11-22 14:17 | S0.1.1 | Land `rows/p022c` and `rows/p022d` as one batch | 83b537af7 | 463d38612 |

## Repairs

No repair recorded.

## For the owner to check by hand

- S0.1.2 Documents: P-096: on the tablet, set up the game once more over an installed one, and once with the setup cut off (close the app mid-copy); then start the launcher: the old game stays playable until the new one is whole, and nothing is left behind (no game.pak.new, no half copy). (agent 1, 2026-11-22 14:18)

## Blocked

- S0.1.3 Render data path step 1: `f4/e3-1a`, `f4/e3-1b`, `f4/e3-1c`: The card's section names GXMT, GXTG, GXTM, GXWP (from 68a5ce1aa..d32c2c573) are refused at main's move by tools/verify/console_census.py, closed group fifo (31 hits in native/include/nebula_model.h, runtime/src/render/core/model_sections_tests.cpp, crates/nebula-import/src/cook/model_write.rs); renaming them changes the card's format and the store-39/40 data, so nothing was renamed. Build, model_sections test and doc refs passed; no run folder (refused before the runs).

## Tool faults

2 tool faults recorded.

The last 20:

| When | Step | Test | What happened | What the agent could not do |
| --- | --- | --- | --- | --- |
| 2026-11-22 14:33 | - | (by hand) | plan.py land S0.1.3 ended code 4 (not judged): gate.py's push check refused main's move because tools/verify/console_census.py's closed group fifo counts 31 (the card's section names GXMT, GXTG, GXTM, GXWP in native/include/nebula_model.h, runtime/src/render/core/model_sections_tests.cpp, crates/nebula-import/src/cook/model_write.rs); it is a certain refusal of the change, reported as not judged; plan.py toolfault --last then said: agent 1 has no tool fault recorded | - |
| 2026-11-22 12:50 | - | validate/vulkan | not run: the Vulkan validation layer is not installed on this PC | - |
