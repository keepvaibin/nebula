# Nebula v2: the plan. Read this file first

Written 2026-11-17 by the coordinator of the audit, for two agents who will each work alone and for a long time. The owner will not be at hand for most of it. This file is the same for both agents. Each agent then has its own plan (file 01 or 02). File 03 is the ledger of every finding.

| File | For |
|---|---|
| `00-READ-FIRST.md` | Both: the goal, the owner's decisions, what done means, the rules, who owns which files |
| `01-AGENT-1-ENGINE.md` | Agent 1: the gate, the engine, the renderers, pacing, performance, platforms |
| `02-AGENT-2-MODS.md` | Agent 2: the mod system, LumaKit, LumaScript, LumaLink, the importer's safety, the docs |
| `03-LEDGER.md`, `ledger.tsv` | Both: all 376 findings, each with its agent and work package |
| `PROMPT-AGENT-1.txt`, `PROMPT-AGENT-2.txt` | The owner: what starts each agent |

---

## 1. The goal

**Nebula v2 as a completely modernised, professionally built game on every platform it ships on, mods included.**

- **Stable.** It does not crash. It cannot lose a player's data. A mod cannot bring it down.
- **Smooth.** Even pictures at every display rate, with as little delay as the method allows.
- **Fast.** From a phone to a high-end PC, with no work done that the picture does not need.
- **Proven.** Every one of these is shown by tests, measurements and arithmetic that run without a person. Nothing is assumed.
- **Best practice everywhere.** Where a part is built the old console way, or patched together, it is rebuilt the way a modern engine does it. Rewrites are allowed wherever they make the result stronger.

The release date does not matter. Ray tracing is outside this plan.

**The finish line, in the owner's words:** at the end, both agents can say "Nebula v2 is perfect now; nothing more can be built to improve it", and can show why. Section 4 says what has to be true before either agent may write that sentence.

---

## 2. What you are starting from

### 2.1 The audit (read it before you change anything)

`C:\Users\<user>\Documents\MarioGalaxyRecomp\NEBULA-AUDIT-2026-11-17\`

| File | What it holds | Who must read it whole |
|---|---|---|
| `00-MASTER-REPORT.md` | Verdict, run results, the six root causes, what to rewrite, each area's standing | Both |
| `A-findings-mods.md` | M-001 to M-177, each with file and line, the trigger and the fix; the readers' design notes (the "needs" marks, the event safety table) | Agent 2 whole; Agent 1 the findings the ledger gives it |
| `B-findings-port.md` | P-001 to P-183 in the same form; the pass tables per renderer; the render data path map (R13.1); the ranked game-step list (R8.3, R9.9); what to remove for ray tracing | Agent 1 whole |
| `C-verification.md` | The coordinator's check of each finding against the code | Both, for their findings |
| `D-run-results.md` | Every run of the audit and its result | Both |
| `E-pacing-interpolation-as-built.md` | How pacing and the in-between pictures are built today (I-001 to I-016) | Agent 1 whole |
| `F-interpolation-and-frame-unlock.md` | The nine gaps (G1 to G9), the target design, the PC and tablet measurements (M1 to M4, T1 to T7) | Agent 1 whole; Agent 2 sections G5 and G7 |
| `G-pacing-runs-digest.txt` | Raw figures of the real-time runs | Agent 1 |
| `scripts\` | Every script the audit used: the 102-stop tour generator, the real-time pacing kit for PC and tablet, the digests, the ledger builder | Both |

A finding is a lead with evidence, not an order. About one in forty of the audit's findings was wrong. Before you fix one, read the lines it cites in the tree as it is now and confirm it. If it is wrong, close it in the ledger as `not-a-fault` with the proof.

### 2.2 The code

| Thing | Where |
|---|---|
| The game and engine repository | Main checkout `C:\Users\<user>\Documents\MarioGalaxyRecomp\Nebula`. The tree with the newest work: `D:\NebulaWork\MarioGalaxyRecomp\Nebula-Clean`, branch `clean/work`, at the same commit as main, with **79 files uncommitted**, among them the first-person camera and mouse work of 11-16 |
| LumaLink, LumaScript, LumaKit | `D:\NebulaWork\lumalink-work\lumalink-sweep`, branch `sweep`, `lumakit/source/camera.lum` modified and uncommitted |
| The mod conformance tests (514 test folders, 58 suites) | **Outside both repositories:** `D:\NebulaWork\agents\luma\{f,fb,g,h,j,k}` and the Android runner in `...\luma\d\platforms` |
| The build the audit tested | `D:\NebulaWork\build\luma-bins\fp1`; game data `D:\NebulaWork\build\all1-pak-34\game.pak` |
| The unit-test build | `D:\NebulaWork\build\rel2\avx2` |
| Ray tracing | Branch `ray-tracing` (pushed), tree `D:\NebulaWork\MarioGalaxyRecomp\Nebula-RT`. Do not touch |
| Other work trees | 78 old trees under `D:\NebulaWork\MarioGalaxyRecomp` hold uncommitted files from earlier lanes. Leave them alone |
| The project's notes | The memory index of this project folder loads with your session. It holds the owner's standing rules and facts that cost time to learn (for example: PowerShell's `git` on this machine is a different git; Bash heredocs lose backslashes; a build script can print "exit 0" after a failed build). Read the index on your first turn |

### 2.3 Where the build stands (measured on 11-17, nothing fixed since)

| Area | Standing |
|---|---|
| Unit tests | 79 of 79, on Windows only |
| Mod engine tests | 418 of 428 on Direct3D 12, 417 on Vulkan, 409 on Direct3D 11 |
| Galaxy tour, ten seconds of random play per stop | 101 of 102 on each PC renderer and on the tablet; `FactoryGalaxy` scenario 4 never gives control |
| PC, 240 Hz, in-between pictures on | Every picture exactly one refresh after the last. Input to display 38 to 42 ms with VSync on |
| PC, in-between pictures off, VSync on | A frame's present call starts 12.9 ms after its step and blocks 16.4 ms |
| Tablet, 60 Hz | Every picture at its asked time, 50 ms after its step starts |
| Tablet, 120 Hz | Not held: 105 pictures a second in a light galaxy, an uneven 72 in the heaviest. No governor reacts. In-between pictures switch off on the GPU's clock speed and can stay off |
| A gate | None. The old test tools are dead on this build |

---

## 3. The owner's decisions (binding)

Given on 2026-11-17. Where the owner did not answer, the coordinator's recommendation stands and is marked so.

### Gameplay

| Matter | Decision |
|---|---|
| Mario's collision keeps every surface he touches (the console stopped at 8) | Keep the port's behaviour |
| The ground-pound rule in zero gravity that the port dropped | Restore the console's rule |
| Collision arithmetic rounds a little differently from the console | A small difference is fine. A faster form may be used if it is measured faster, even slightly; replays must still match on every platform |
| A sensor's list of touching sensors drops entries when full | Let it grow. **This is the rule for every console-era limit: everything grows or shrinks as needed. No fixed console size may remain anywhere** |
| Rail gravity has no point at the rail's end | Leave it |
| The movie decoder's kept console bug | Leave it |
| Vendor frame generation (DLSS, FSR) | Stays. It is intentional |
| A mode that guesses motion ahead to cut delay | Do not build it |

### Product

| Matter | Decision |
|---|---|
| The notice after a crash with mods on | On the Mods list. It is a safety net; the aim is that the crashes do not happen |
| Direct3D 11 | **Stays**, for old Windows systems. It must reach the same standard as the others: same tests, same pictures, and no option shown that it cannot do |
| Ray tracing code on main | Remove it now. It lives on its branch |
| The blog and the docs | Any wording may be corrected. **But a mod must be able to do anything; that is the point of LumaKit.** Fix the words about permissions; never narrow what a mod can do |
| A smaller bloom on phones | Allowed, as a setting |
| The Mac build | Must compile and be complete in code. The owner tests it on Apple silicon later |
| Linux | The same: complete in code and compiling. The owner tests on a desktop later |
| iOS, and Windows on ARM | Stop building mods for them until the game itself builds there. Their game builds are out of scope |
| The Toy Time comet mission that never gives control | The owner will look later. Do not wait: find the cause by running it, and fix it |
| Low-end phones | None are available. Remove every measured cause of slowness first; only then tune further |

### Frame rate

| Matter | Decision |
|---|---|
| A 59.94 Hz display | The game may lock to it (0.1% slower, as the console was). Tolerance 0.2%, no wider |
| When a device cannot hold 120 | **Do not switch to 60 by itself.** That feels wrong to a player and is not how good games behave. The answer is performance: the engine has to be fast enough that devices hold their target. Section 6 of Agent 1's plan says what replaces the governors |
| The default frame rate | Decided 11-22 (the owner: "should be 60fps."): 60 on every system, a player chooses a higher rate. The earlier recommendation, a 120 Hz display starting at 120, is withdrawn |
| The proof targets | Accepted (section 4.3) |

### How you work

| Matter | Decision |
|---|---|
| Who builds the gate | Agent 1, first |
| Git | The 79 pending files are committed first as the baseline. Each agent has its own branch and work tree. Main is never rewritten again. Branches may be pushed freely. An agent moves main only when the gate passes |
| Sub-agents | **None.** Each of you works alone |
| The tablet | Allowed, for both. Never uninstall, clear or change the owner's installed Nebula (`dev.nebula.galaxy`); use the test package (`dev.nebula.galaxy.test`), and remove it when a run of work is over |
| Visible game windows on the PC | Allowed for measurements. No sound |
| Who says a thing is done | **Tests, gates and arithmetic.** Never "the owner has played it": the owner may not be there |
| Ray tracing | On hold. Outside this plan except for its removal from main |

---

## 4. What "done" means

### 4.1 The ledger is closed

Every row of `ledger.tsv` ends in one of four states: `fixed` (with the commit and the test that fails without the fix), `not-a-fault` (with the proof), `decided` (the owner's words), `out-of-scope` (named in section 7). No row is `open`.

New faults you find go into the ledger as new rows (`N-001` onward, with your agent number) the moment you find them, in the same form as the audit's findings: what, where, how it shows, how sure. They are closed the same way.

### 4.2 The gate is green

The gate (Agent 1 builds it in package E0) runs on every push, and it passes on main:

| Check | Passing means |
|---|---|
| Build | Windows (three renderers), Android, Linux and Mac all compile from a clean checkout, with warnings as errors where the tree already holds that line |
| Unit tests | All pass, on Windows and on Linux |
| Mod engine tests | All pass, on Direct3D 12, Vulkan, Direct3D 11 and on Android |
| Galaxy tour | 102 of 102 on every renderer and on the tablet, for every game region whose data is on the machine |
| Pictures | Every tour stop's pictures match the approved ones on every renderer, within the tolerance the gate states |
| Replays | The step-hash replay is identical before and after every change to game source, and identical between platforms |
| Hostile files | The corpus of bad mod files is refused cleanly: no crash, no hang, bounded memory and time |
| Two mods | Every shared value has a test with two mods, one stopping |
| Sanitizers | The tour and the mod tests run clean under the address and undefined-behaviour sanitizers; the thread sanitizer runs clean on Linux |
| Soak | Two hours of random input per renderer: no crash, no memory growth beyond the stated bound |

### 4.3 The measures hold

These are the numbers. They are recorded by the game itself and checked by the gate's measurement job, on the PC and on the tablet.

| Measure | Target |
|---|---|
| Game speed | 60.000 steps a second within 0.01%, except the 59.94 lock |
| Even pictures, PC, any offered rate, VSync on | Every present exactly the intended number of refreshes after the one before, over ten minutes in three galaxies. No missed refresh |
| Animation error (the game time a picture shows against the time it reaches the display) | 99 of 100 pictures within 1 ms |
| Delay with in-between pictures on | One step plus one refresh, plus the step's own work, or less |
| Delay with in-between pictures off | The step's own work plus one refresh, or less |
| A long step outside loading | None over 18 ms on the PC |
| Hitch at a scene's start, second run onward | None over one refresh |
| Tablet at 60 Hz | Every picture at its asked time; two refreshes from step to display, or better |
| Tablet at 120 Hz | **Held in every one of the 102 tour stops:** 99 of 100 pictures exactly one refresh after the last. Dynamic resolution may act to hold it |
| Render-thread work for an in-between picture | Does not grow with the number of objects drawn |
| The full-size scene | Opened once per picture when nothing reads it back; copied only when something visible reads it |

**If a target cannot be met,** you do not lower it and you do not go quiet. You write the measurements and the arithmetic that show where the floor is and why, add it to the ledger as a row for the owner, and go on with the rest.

### 4.4 Nothing unread

The audit did not read everything (master report, section 10). Each agent's last package is to read what was not read on its side, with the same care, and to close what that finds. "Perfect" cannot be claimed over code nobody has read.

### 4.5 The last sentence

An agent may write "Nebula v2 is perfect now; nothing more can be built to improve it" for its side only when 4.1 to 4.4 are all true for its side, and it lists the evidence: the ledger's counts, the gate run, the measures table with the measured values.

---

## 5. The rules of the work

### 5.1 Proof before anything else

- **A fix without a test is not a fix.** The test goes into the gate, and it must fail on the code before the fix.
- **A change to game source is proved by the step-hash replay:** the same recorded input, the same position, velocity, gravity and state of every object after every step, before and after.
- **A change to a renderer or to what the game hands it is proved by pictures:** every tour stop, every renderer, before and after.
- **A performance claim is a measurement,** made with the machine otherwise idle, with the figures and the method in the commit's message. Never an estimate.
- "It compiled and its own tests pass" proves nothing about the game. Run the gate.

### 5.2 One system, not patches

- Find the cause and fix the cause. Do not add a special case beside an old path; replace the path.
- When the same fault exists in several places (four renderers, three platforms, twenty-two LumaKit modules), fix every place in the same piece of work, and write the Vulkan, Direct3D 11 and Metal side with every Direct3D 12 change.
- No fix that names a device, a galaxy or a mod. Fixes are general and rest on what the device or the data can do.
- No rewrite without a failing test that shows why a fix is not enough. The rewrites this plan names (the render data path, the frame, the gate) already have that evidence.
- Small gains count. "It was only 2%" is not a reason to leave something slow or wrong.

### 5.3 Git

- Agent 1's first action is the baseline (its plan, E0.1): commit the 79 pending files in small commits and push. Agent 2 starts from that commit.
- Each agent: one work tree, one long-lived branch (`engine/work`, `mods/work`), small commits, pushed often. For LumaLink's repository only Agent 2 commits.
- Main moves forward only by fast-forward, only from a branch that has been rebased on main and on which the gate passed after the rebase. Never rewrite, squash or force-push main.
- Commit messages say what and why in plain words, written to a message file. **No attribution lines of any kind** (no "Co-Authored-By", no tool or session names). The only identity in any commit is the owner's GitHub name and its no-reply address, as the repository is already configured. Never put the owner's e-mail address or real name anywhere.
- Never open a pull request or an issue against the upstream decompilation project.

### 5.4 Two agents, one machine

- **File ownership** is in section 6. Stay in your files. When you need a change in the other agent's file, either ask on the board, or make the smallest possible change yourself and write it on the board the same hour.
- **The board:** `D:\NebulaWork\plan\BOARD.md` (create the folder if it is not there). Append only. Each entry: time, agent, kind (`NOTICE`, `REQUEST`, `ANSWER`, `MAIN-MOVED`, `LOCK`), text. Read it at the start of every piece of work and before every rebase.
- **Timed runs need the machine alone.** Before any measurement of time, take `D:\NebulaWork\plan\measure.lock` (make the folder with `mkdir`, which fails if it exists; write your name and the time inside; remove it after). While the other agent holds it, run no game and no build. A lock older than 45 minutes with no matching process is stale; say so on the board and take it.
- **The tablet is one device.** `D:\NebulaWork\plan\tablet.lock`, the same way. Its serial is `R52WA0FXE5J`. It may be locked or in power saving; check before a run and, if it is, write it on the board for the owner and go on with other work.
- **Disk.** Keep at least 150 GB free on `D:`. Test runs copy the programs; delete your own copies when a run ends and keep the logs and results.
- Functional test runs use a hidden window and no sound. Only measurements of pacing use a visible window.

### 5.5 Wording, everywhere

Everything a person reads (settings, messages, docs, comments, commit messages) is plain, short and exact. No filler, no reassurance, no manner. Every new text exists in all 14 languages, with glyphs from the language packs' fonts.

### 5.6 What you never do

- Never start a sub-agent.
- Never change, clear or uninstall the owner's installed game on the tablet, or change the tablet's or the PC's system settings.
- Never enter the owner's accounts, sign releases, or publish a release. Branches may be pushed; releases are the owner's.
- Never touch ray tracing beyond removing its code from main as Agent 1's plan says.
- Never delete work trees or folders you did not create, except where your plan names them.
- Never stop because something is hard or long. Stop only when the ledger is closed, or when a decision is truly the owner's; then write the question on the board, with your recommendation, and go on with everything that does not depend on it.

### 5.7 Records

- `docs/plan/` in the repository holds this plan and the ledger (Agent 1 adds them in E0.1; the audit folder goes in beside them as `docs/audit-2026-11-17/`).
- Each agent keeps `docs/plan/PROGRESS-AGENT-N.md`: one dated entry per finished package with what was done, the gate run, the measurements. Short and factual.
- The ledger is updated in the same commit as the fix it records.

### 5.8 A fault that shows rarely

Never hunt a fault that shows rarely by repeating runs. Give it a watch: a cheap check inside the game or the renderer that prints one
line naming what was wrong when it happens (Direct3D 11's `[constants] watch` is the model), turn it on in the gate for the renderer
(`WATCHES` in `tools/gate/gate.py`), and go on. A test that fails once under the load of twelve copies of the game (the nightly run's
`stress` lane) and passes alone is a `stress` result with a repair; its note carries the watch's line. If the line does not name the
cause, the next change adds a watch that will.

---

## 6. Who owns which files

| Agent 1 (engine) | Agent 2 (mods) |
|---|---|
| `runtime/src/render/**`, `runtime/include/galaxy/render/**` | `native/host/luma*.cpp`, `native/include/luma*.h` |
| `native/host/host_main.cpp` and the host's non-mod files | `native/petari/**/Luma*`, `NebulaMods*`, `NebulaModCalls*` and the mod glue in the game |
| `platform/**` (SDL, Android, Apple), `installer/**` | `runtime/src/mods.cpp`, `mod_*.cpp`, the Mods screens |
| `native/petari/src/Game/**` (the game), except the mod glue | `crates/luma-validate/**` |
| `runtime/src/` audio, input, saves, settings, benchmark, update | The LumaLink repository whole: LumaScript, LumaKit, LumaLink |
| `tools/verify/**`, `tools/test_driver/**`, the workflows, the gate | `tools/lumacore/**`, the generators (`instrument.py`, `gen_types.py`, `gen_classes.py`) |
| `crates/nebula-import/**`: the formats it writes (models, materials, textures, programs) | `crates/nebula-import/**`: how it reads files from strangers (limits, checks) |
| `docs/` except modding | `docs/modding/**` |

**Shared edges, and who does which side**

| Edge | Agent 1 | Agent 2 |
|---|---|---|
| The test harness | Owns it; adds the real-time mode, the tour, the replay | Uses it; adds mod tests and asks on the board for what the harness lacks |
| After a crash on Android, Linux and Mac | The platform's crash handler writes the safe-start record (E12) | Says what the record must hold and what the next start does with it (M5) |
| The importer | The cooked formats (model format 12 and the rest of E3) | Reads Agent 1's format notes on the board and keeps the checks in step; does M6 early so the two do not meet in the same lines |
| The frame list and the mod draw services | Changes the frame's format (E3, E4) and keeps every mod draw service working; the mod tests in the gate are the proof | Reports on the board any service the change breaks |
| Fresh pointer and free-look camera (file F, G5) | The host and renderer side: a per-picture value | The LumaKit side: a mod marks its camera as free-look |
| Stated camera cuts and teleports (G7) | The game's own calls and what the renderer does with them | `camera.cut()` and the teleport call for mods |
| `host_main.cpp` | Owner | May change the mod-related functions; writes it on the board |

---

## 7. Outside this plan

- Ray tracing, except removing its code from main.
- A game build for iOS, and for Windows on ARM.
- Running the Mac build on Apple silicon and the Linux build on a desktop (the owner does it later). The code and the build are in scope.
- Publishing a release, signing, the website's deployment.
- A low-end phone as a test device. Its place is taken by the rule: remove every measured cause first.

---

## 8. Facts about this machine that save time

- PC: Ryzen 9 9950X3D, RTX 5090, Windows 11. Display: 240 Hz, 3840x2160.
- Tablet: SM-X810 (Adreno 740, Android 16), 2800x1752, 60 and 120 Hz modes. It shows 120 Hz only with power saving off and its display set to the adaptive rate.
- Real-time measurement in a stage: `NEBULA-AUDIT-2026-11-17\scripts\audit_pace.sh` (PC) and `audit_pace_tablet.sh`, with `audit_pace_digest.py` and `audit_pace_chain.py`. The memory note "pacing-measure-kit" lists what they need.
- The 102-stop tour as harness tests: `scripts\audit_make_tour.py`, `audit_tourh.sh`.
- The mod tests as the audit ran them: `scripts\audit_conf.sh`.
- The test harness: `tools/lumacore/luma_test.py` (needs `--lock`). It runs fixed steps and cannot measure pacing until Agent 1 gives it a real-time mode.
- The Android test build has its own package id and installs beside the owner's game: `scripts\audit_android_build.sh`.
- On Android the test runner reads the exit record too early and reports a clean exit as a failure about twice in a hundred runs. Agent 1 fixes it in E0.
- The work drive was nearly full during the audit. One old folder was removed on 11-17 with the owner's approval. About 3,000 old build folders remain under `D:\NebulaWork\build`; the owner chose to keep them. Do not delete them; do not add to them carelessly.
