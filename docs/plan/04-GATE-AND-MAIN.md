# 04. How work is proved and how it reaches main

Written by the planner on 2026-11-17, 16:05. The owner's words: "I want the best absolute plan how do modern engineers do
stuff like this. I am giving you full autonomy to decide. I want perfection." and "You can say or do whatever you want to
them."

This file replaces the board's gate RULE of 13:06, the line of 00-READ-FIRST 5.3 on how main moves, and the way the full
gate counts checks that are not built. Everything else in the plan stands: the owner's decisions, the ledger, what "done"
means (00-READ-FIRST section 4), the work-while-a-run-goes RULE of 13:51, the locks, the tablet rules, no sub-agents.

---

## 1. Why this changes

What the first day showed:

| Fact | What it costs |
|---|---|
| Main has not moved. The branches are 75 and 121 commits ahead of it | Each hour apart makes joining harder, and no fix is ever proven on the commit that ships |
| The full gate cannot pass. It counts 12 checks that are not built yet as failures, and one stage fault from a later package | Main cannot move until almost the whole plan is finished |
| The full gate runs every part one after another: 111 minutes, of which 56 are five tours and 40 are the tablet | A few attempts a day |
| The plan adds a two-hour soak per renderer, sanitizer builds and the mod tests on every renderer to that one gate | It would grow to many hours |
| GitHub's push workflow takes about two hours (its Android job alone 1 h 50 min) | Linux and Mac results arrive late |

## 2. The practice this follows

This is how teams that ship large software keep a main branch healthy (trunk-based development with a merge queue):

1. **Small steps reach main often.** Finished, proven work goes to main the same day. Long-lived branches are the main source of late surprises.
2. **A fast gate before main, long tests after.** What decides a move of main takes minutes. Soak, sanitizers and fuzzing run on main afterwards, all the time, and a failure there stops further moves until it is fixed or taken back.
3. **Main never gets worse.** The gate compares against a written list of the faults main already has. Anything new fails. The list can only shrink.
4. **Tests are independent, so they run side by side.** Each run has its own folder, its own saves and settings, and no wall-clock expectations.
5. **No flaky tests.** A test that fails and then passes with no change is a fault with a ledger row, never something to run again until it is green.
6. **Tools enforce the rules.** The gate takes the lock, moves main and writes the board line itself. A hook refuses a push to main that the gate did not allow.
7. **One page says where things stand,** written by a tool from the ledger and the gate's results.

"Done" does not get easier. At the end every check exists, both lists are empty, and every level is green on one commit.

---

## 3. The four levels

| Level | Name | Wall time to hold | When it runs | What it decides |
|---|---|---|---|---|
| 1 | Quick gate | 6 minutes | While working, and before asking for a main gate | Whether a commit may ask for a main gate |
| 2 | Main gate | 30 minutes | To move main | Whether main moves |
| 3 | Long run | About 3 hours, in the background | On main's newest commit, again and again | Whether main is green or red |
| 4 | Final check | As long as it takes | At the end of a package that changes speed or pacing, and at the very end | "Done" (00-READ-FIRST 4.2 to 4.5) |

### 3.1 Quick gate (as today, plus a quick set of mod tests)

The repository checks, the Windows build, the unit tests, the Rust tests, a start on each renderer, the self-tests, the
engine tests and a 12-stop tour on Direct3D 12 with pictures and replays, two stops of each other disc, and a **quick set
of mod tests** on Direct3D 12 (Agent 2 chooses it: about two minutes, one test or more from every suite).

### 3.2 Main gate

Everything that is deterministic and proves a change is right:

| Part | Runs |
|---|---|
| Build and unit | Windows build, unit tests, game unit tests, Rust tests, self-tests, collision proof |
| Start | 300 frames on Direct3D 12, Vulkan, Direct3D 11 |
| Engine tests | All, on three renderers, with pictures |
| Tour | 102 stops on three renderers with pictures and step-hash replays; the other two discs on Direct3D 12 |
| Mod tests | Every suite on three renderers, with pictures |
| Bad files, two mods | The stored corpus of bad files, and the two-mod tests, when Agent 2's packages bring them |
| Android | The APK build and the launcher's tests |
| Tablet, short | Install the test package, start, the 12 quick stops, their step hashes against the PC at the tablet's screen size, the quick set of mod tests |
| GitHub | The push workflow's Linux build, Linux unit tests, Mac build and Rust-on-Linux jobs on the same commit are green (section 8) |

The tablet's full tour and the mod tests in full on the tablet are in the long run.

### 3.3 Long run

On main's newest commit, in its own work tree and build folder, at below-normal priority:

| Part | Runs |
|---|---|
| Tablet, full | The 102-stop tour, every stop's step hashes against the PC, every mod suite |
| Sanitizers | Address and undefined-behaviour builds run the tour and the mod tests. The thread sanitizer runs on Linux in the push workflow |
| Soak | Two hours of random input on each renderer, the three side by side. No crash, no memory growth beyond the stated bound |
| Fuzzing | Each fuzz target for a fixed time, starting from the stored corpus. Every new failing input joins the corpus |
| All discs | Every disc on every renderer |
| Coverage | Which game and engine code no test runs. A list, kept with the results; the share of code run by tests may not fall. Planned (Agent 1 designs it) |
| Short measurement | A fixed short set of the measures (section 10), at the long run's end |

### 3.4 Final check

All three levels on one commit, plus the full measures of 00-READ-FIRST 4.3 on the PC and the tablet. Both lists of
section 4 are empty and no check is "planned".

---

## 4. Two lists that can only shrink

### 4.1 Known failures: `tools/gate/known-failures.tsv`

Columns: check, test, ledger row, commit where first seen, one line of what is wrong.

- A test on this list is expected to fail. The gate reports it as "known", and the gate still passes.
- **A listed test that passes fails the gate** ("passes but is listed: remove the line"). So the list cannot hide a fixed fault, and it cannot grow stale.
- A line may be added in two cases only:
  1. the same check fails the same way on main's newest commit (shown by running that check there), or
  2. it is a new test for a fault that is in the ledger and not fixed yet. A test may go in before its fix.
- A line is never added for something the candidate's own change broke. That change is fixed or it does not go in.
- Every line names an open ledger row. The commit that closes the row removes the line.
- First content: the stage that never starts (P-122, `030-FactoryGalaxy-4`) on each tour, and nothing else unless case 1 shows it.

### 4.2 Planned checks: `tools/gate/planned.tsv`

Columns: check, level, plan step or ledger row, owning agent.

- A check on this list is not built yet. The gate reports it as "planned" and does not fail for it.
- Every line has an open ledger row. Building the check removes the line.
- First content: pictures on the tablet, the bad-file corpus, the two-mod tests, the mod tests on the tablet, sanitizers and soak until their branch is in, fuzzing, coverage, the measurement job.

### 4.3 What stays a failure

- A check that exists and could not run (no build, a held lock that never frees, a missing disc): a failure, as today.
- **Except the tablet.** It is the owner's device and may be away, locked or in power saving. Then the gate records the tablet part as "owed", main may move, and a board line says so. The owed part runs on main's newest commit before anything else as soon as the tablet is back. If it is owed for more than 24 hours, write it on the board for the owner.

---

## 5. How main moves

One candidate at a time. `gate.lock` is the queue.

1. **Rebase** the candidate on main's newest commit.
2. **Quick gate** passes on that commit, whole.
3. **Push** the candidate's branch, so GitHub starts on the same commit.
4. **Main gate** on that commit. It takes `gate.lock`, or waits for it.
5. **The gate moves main itself** when it passes: it checks that main is an ancestor of the candidate, fast-forwards main, pushes it, writes a `MAIN-MOVED` board line (commit, what it holds, the run's name) and frees the lock. No hand does this.
6. **If it fails,** the lock is freed and the result is handled before any other work (the 13:51 rule).

Rules:

- **Fast-forward only. Main is never rewritten, squashed or force-pushed.** Unchanged.
- **How often:** at the end of every package step, and whenever finished work off main reaches about 20 commits or half a day. Work that is not finished stays on its branch.
- **Both agents in one gate** is allowed when both have a candidate waiting: the second rebases onto the first and one gate proves both. If it fails, the agent whose check failed drops out and the other runs alone.
- **After another agent's move,** rebase on main before your next quick gate. Read the board first, as always.
- A side branch is deleted once main holds it.

---

## 6. When main is red

Main is red when a long run fails on it, when an owed tablet part fails, or when a GitHub job on main's commit ends red.

1. Write `MAIN-RED` on the board: the check, the run, the first lines of the failure.
2. **Only fixes for the red check may move main** until it is green again. Other work goes on in its branches.
3. Find the commit: run the failing check alone over the moves since the last green long run (`git bisect run` with the gate's single-check mode).
4. The owner of the faulty commit fixes it. **If no fix has passed the main gate within two hours, take the commit back** with a revert commit (a new commit; main is not rewritten) and reopen its ledger row.
5. The fault gets a ledger row and a test in the quick or main gate that fails on it, so the main gate catches that kind next time.
6. Write `MAIN-GREEN` when the failing check passes on main's newest commit.

A fault the long run finds that main has always had (not brought in by a recent move) is not red: it gets a ledger row and a
known-failures line by case 1, and its owner takes it next.

---

## 7. Tests run side by side

The main gate holds its 30 minutes by running its parts at the same time: the tours of the three renderers and the two
other discs, the mod suites per renderer, and the Android build and the tablet beside them.

What every test must be for that:

| Rule | Meaning |
|---|---|
| Its own folder | Each run has its own copy of the program, its own saves, settings, mods folder and log. Nothing is shared and nothing is in a fixed place |
| No wall-clock expectations | Limits are counted in game steps. A wall-clock limit is only a guard against a hang, at least five times the unloaded time. A gate under load never fails for being slow |
| No timed tests in levels 1 and 2 | Anything that measures time (pacing, the benchmark's figures) belongs to the measurement job, under `measure.lock` |
| The same result alone or together | Proof, once and after any change to how the gate runs things: the same commit run one after another and side by side gives the same pictures and the same step hashes at every stop |

While `gate.lock` is held, other builds on the machine use at most 8 jobs.

**The time is watched.** Every result holds each check's seconds, and `gate-runs/history.tsv` gets one line per run. A main
gate over 30 minutes is a ledger row for Agent 1; the answer is more parts side by side, never fewer checks.

---

## 8. GitHub's push workflow

It is the only place Linux and Mac code is built, so it is part of the main gate.

- At its end the main gate reads GitHub's jobs for the candidate's commit. **Linux, Linux unit tests, Mac and Rust on Linux must have ended green** (they take 5 to 35 minutes; the branch was pushed before the gate started; the gate waits up to 15 minutes more). A red one fails the gate.
- The Windows and Android jobs there repeat what the local gate proves and take up to two hours today. They may still be running when main moves. If one ends red, main is red.
- **Agent 1 brings the whole workflow under 20 minutes** (compiler caches kept between runs, the Android job's dependencies cached, no job waiting on another without need). From then on every job must be green before main moves.

---

## 9. Pictures and flaky tests

**Pictures.** A changed picture fails the gate. It is approved only in a commit that names the cause: the ledger row or the
intended change, and which pictures. A picture whose change has no known cause is never approved; it is a fault to trace.

**Flaky tests.** The gate retries nothing. If a check fails and the same commit passes it on a second run:

- that is a fault, in the test or in the game. It gets a ledger row at once, with both logs kept;
- the second run does not count as the pass. The cause is found, or the test goes on the known-failures list with its row while its owner fixes it;
- two runs of fixed steps that differ are a determinism fault in the game or the harness, and are treated as one.

---

## 10. Measurements

They need the machine alone, so they are not in levels 1 and 2.

- **Short set** (15 minutes or less, Agent 1 chooses it in E0.17): one renderer per run in turn, 120 Hz, one heavy stop, the benchmark. It runs at the end of each long run under `measure.lock`. Its figures go into `gate-runs/measure-history.tsv`. A figure worse than the last ten runs' spread is a ledger row, and main is red if a recent move caused it.
- **Full set** (the table of 00-READ-FIRST 4.3): at the end of each package that changes speed or pacing, and at the final check.
- While `measure.lock` is held: no gate, no long run part, no build, no game. Reading and writing code goes on.

---

## 11. The tools hold the rules

Agent 1 builds these into `tools/gate`; each has a test that fails without it.

| Tool | What it does |
|---|---|
| `gate.py --quick`, `--main`, `--long`, `--final` | The four levels. `--full` goes away |
| `--main --move` | Takes the lock, runs, reads GitHub, moves main, writes the board, frees the lock |
| `--only CHECK` | One check alone, for tracing a red main |
| The two lists | Read by every level, with the rules of section 4 |
| A pre-push hook | Refuses a push to main unless the gate recorded that commit as allowed |
| `gate-runs/history.tsv` | One line per run: commit, level, result, seconds per check |
| `D:\NebulaWork\plan\STATUS.md` | Written after every gate and every move (section 12) |

---

## 12. One page for the owner

`D:\NebulaWork\plan\STATUS.md`, written by a tool, never by hand:

- main's commit and when it last moved; green or red;
- the last main gate and the last long run, with their results;
- ledger rows closed and open, per agent and per package;
- how many known failures and how many planned checks remain;
- how many commits each agent has off main.

---

## 13. The order of this change

It comes before all other package work, because everything else lands through it.

| Step | Who | What |
|---|---|---|
| 1 | Agent 1 | The gate run of 14:58 finishes; its result is handled. It is the last full gate of the old kind |
| 2 | Agent 1 | The levels, the two lists, the short tablet part, the GitHub read, `--move`, the hook, the history and status files |
| 3 | Agent 2 | The quick set of mod tests; the mod suites able to run side by side (section 7) |
| 4 | Both | **The first move of main:** Agent 1's joined branch with its side branches, Agent 2's branch on top, one main gate for both. It does not wait for steps 5 and 6. Aim: today |
| 5 | Agent 1 | The gate's parts side by side, with the proof of section 7. Then the main gate holds 30 minutes |
| 6 | Agent 1 | The push workflow under 20 minutes (section 8) |
| 7 | Both | The first long run on main; from then on as section 3.3 says |

Each step that is new work gets a ledger row from its owner (N-1xxx, N-2xxx). This file goes into `docs/plan/` with the
first move.

## 14. Who starts the long run

Whoever moves main starts it, if none is running and the last one started six hours ago or more. One at a time, under
`D:\NebulaWork\plan\long.lock`. It takes `tablet.lock` for its tablet part and `measure.lock` for its last 15 minutes.
A main gate may run beside it; they share the tablet through its lock.
