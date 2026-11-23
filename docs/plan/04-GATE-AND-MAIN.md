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
| Speed | A landing whose plan holds an engine or a tour group runs the play route once on Direct3D 12 (`measure/step/pc`, last, with the machine alone) and, with the Android build in the plan, once on the tablet (`measure/step/tablet`): section 10 |
| GitHub | Every `must` job of the push workflow (tools/gate/github_jobs.tsv: Mac, Linux, Linux unit tests, both sanitizer jobs, Rust on both systems) is green for the commit: before the move when the landing's plan has the group `github-first`, after it when the plan has `github` alone (section 8) |

The tablet's full tour and the mod tests in full on the tablet are in the long run.

### 3.3 Long run

On main's newest commit, in its own work tree and build folder, at below-normal priority:

| Part | Runs |
|---|---|
| Tablet, full | The 102-stop tour, every stop's step hashes against the PC, every mod suite |
| Sanitizers | Address and undefined-behaviour builds run the game's unit tests, the tour and the mod tests. The thread sanitizer runs on Linux in the push workflow |
| Soak | Two hours of random input on each renderer, the three side by side. No crash, no memory growth beyond the stated bound |
| Fuzzing | Each fuzz target for a fixed time, starting from the stored corpus. Every new failing input joins the corpus |
| All discs | Every disc on every renderer |
| Coverage | Which game and engine code no test runs. A list, kept with the results; the share of code run by tests may not fall, compared with the last run of the same binaries and the same suites that passed (see "The coverage history's key" below) |
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
- **Except the tablet.** It is the owner's device and may be away, locked or in power saving. Then the gate records the tablet part as "owed", main may move, and a board line says so. No tool plays the owed part by itself: the board line names the command (`gate.py --main --only "<rows>"`), and the daily long run plays the tablet's pieces on main's newest commit, so a fault the owed part would have found comes back as a repair up to a day later. Nothing counts the owed rows and no number of them stops a landing. A landing that moves main writes the checks it owed to `tablet-owed.tsv` in the live folder, and one that plays a tablet check clears the file: the progress page shows "Tablet away since <time>: <n> checks owed" while the file has lines. (The old note that a part owed for more than 24 hours goes on the board had no code behind it and is taken out.)

---

## 5. How main moves

One candidate at a time. `gate.lock` is the queue.

1. **Rebase** the candidate on main's newest commit.
2. **Quick gate** passes on that commit, whole.
3. **Push** the candidate's branch, so GitHub starts on the same commit.
4. **Main gate** on that commit. It takes `gate.lock`, or waits for it.
5. **The gate moves main itself** when it passes: it checks that main is an ancestor of the candidate, fast-forwards main, pushes it, writes a `MAIN-MOVED` board line (commit, what it holds, the run's name) and frees the lock. No hand does this.
   When the push does not reach GitHub (git's text is the network's, not a refusal), the gate looks at origin's main and tries again after 15, 30, 60, 120 and 240 seconds, six tries in all; when all fail nothing was judged: the run ends `not-judged` and the landing waits. When origin's main already holds the commit after a failed push, the push arrived and only its answer was lost, and that is a move (nobody has found why the push was reset on 2026-11-21). A run whose checks passed ends `fail` only when a job of GitHub is red; every other reason main did not move is `not-judged`.
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

**The time is watched.** Every result holds each check's seconds, and `D:/NebulaWork/build/gate-runs/history.tsv` gets one line per run. A main
gate over 30 minutes is a reason to run more parts side by side, never fewer checks; the gate prints nothing about it (the ledger row for Agent 1 was a duty of the four-agent process, which is gone).

---

## 8. GitHub's push workflow

It is the only place Linux and Mac code is built, so it is part of the main gate.

- Each job of `.github/workflows/check.yml` has a class in `tools/gate/github_jobs.tsv`: **must** (Mac, Linux, Linux unit tests, both sanitizer jobs, Rust on both systems), **nightly** (the two Windows jobs and the two Android builds, about 50 minutes each: this machine builds and tests them itself, and a plan with the group `android` runs the Android build here) or **advisory** (the Android start check). A job the table does not name counts as must and the run says so.
- A landing has one of three forms (the groups of `tools/gate/scope.tsv`). With **neither `github` nor `github-first`** (documents, tools, test data, pictures) it pushes no candidate and waits for nothing. With **`github`** (game code, the SDL host, the renderers this machine builds, the importer, Android, the unit tests) it pushes the candidate branch at its start, and **main moves when this machine's own checks pass**: GitHub's Mac and Linux builds took 15 to 27 minutes and almost never fail when the Windows and Android builds here pass, so they are read after the move. With **`github-first`** (a full run, and files this machine cannot build: the Metal renderer, the Apple and Linux parts of the host and the platform layer, a unit test that only Linux builds, every CMake file, the workflow itself) **main does not move until every must job is green for the commit**: the gate waits for them after its own checks (up to 120 minutes, outside the lock). A landing that has both groups waits. `gate.py land --github first` makes one landing wait (the step tool uses it for a repair of a red GitHub job). `result.json` has `"github": "first"` or `"after"`, and the history line starts its checks with `github-form=first` or `github-form=after`. A job's state is read from every push run of that commit, under any branch name, and the newest run that has a result wins.
- **The form `github`, step by step.** When the checks have passed, the gate reads GitHub once. A must job that has its end is judged at once (all green: main moves; one red: main does not move, as in the waiting form). When some are still running, the gate prints `GitHub's jobs for <sha> are read after the move`, moves main, and adds one line to `github-pending.tsv` in the live folder (time of the move, commit, branches, candidate branch, run folder); the candidate branch stays on GitHub. Every `gate.py land` and every `plan.py next`, `status` and `land` then reads each pending line once (no waiting): all must jobs green removes the line, deletes the candidate and prints `GitHub: <sha> green, N minutes after the move`; a red must job removes the line and opens a repair (kind fail, check `github/<job>`, source github) whose text is `GitHub's job <job> is red on <sha>, landed from <branch>: <first line of the log>` with the job's page and the command that prints the failed part of its log (`gh run view <run id> --log-failed --job <job id>`); a second red of the same job while its repair is open adds a line to that repair and opens nothing. Not finished within 120 minutes of the move removes the line and writes `GitHub gave no result for <sha> in 120 minutes` to `tool-faults.tsv`. When GitHub cannot be reached nothing changes. `plan.py next` gives such a repair before any step, even while a step is in work: the step stays on its branch (`plan.py start R-nnn` keeps it there and starts the repair), the repair lands in the waiting form, and `plan.py start <step>` then goes on with the step on top of the new main.
- **Main moves in one way only.** `gate.py --main --move` given by hand is refused (`main moves only through gate.py land <branch>`, exit 2, no lock, no run folder). The landing runner's own call carries `--landing-runner <its process id>`, which must be the holder of the runner's lock (`landing-runner.lock`).
- `check.yml` runs on a push to `main` and to `gate/**` (the candidates), on a pull request and by hand; a push to a work branch starts nothing. On a candidate only the jobs a landing reads run: the nightly jobs (the two Windows jobs, the two Android builds) and the Android start check run on main and by hand.
- A red must job refuses a run before it starts, and stops it while it runs (the job's log, its last 200 lines, is saved in the run folder as `github-<job>.log`). A job that ended cancelled or startup_failure, or red at a set-up (`uses:`) step (checkout, a set-up action, a cache, an upload), is run again once; ending that way twice, or the time limit, is `env`: not judged, main does not move, the branch stays in the queue. A red advisory job is run again once and, red again, told in one line. When a `run:` step failed for a reason that is not the game (a download or an install), `--github-again` (on `gate.py land`, `--main --move` and `plan.py land`) runs every red must job again once before the start check, writes each to the tool-fault record as ENV, and judges by the new result. A main gate without `--move` waits for the same jobs up to the same limit. While it waits it prints one line each time the reading's text changes, and at least one every five minutes, with the same words as `github.log`. A job the table does not name is told at the first reading that sees it, on the screen and in `github.log`, with the file to add its line to. The last summary line a run prints carries the same checks and seconds as `result.txt` (GitHub's row is added after the first one, and the line is printed again).

---

## 9. Pictures and flaky tests

**Pictures** (`tools/gate/pictures.py`). A picture that differs from its approved copy fails the gate, unless the change
declares it. A picture with no approved copy is `new`: it never fails a run.

- *One list.* `tests/pictures/approved.tsv` holds every approved picture for every level and suite. A row is found by the
  test's own place (its folder under `tests/`, which the runner writes into `result.json` as `test_path`; for a tour, which
  is written into the run's folder, the tour: `tour`, `tour-RMGP01`, `tour-RMGJ01`), the picture's name, the renderer and the
  machine, never by the name a run gave its suite. A picture approved from any passing run counts everywhere.
- *The machine is part of the key.* A row belongs to a place (the PC or the tablet, taken from the run's platform) and the
  adapter and driver that place's `machine` row names for the renderer. The PC's and the tablet's Vulkan pictures are two sets
  of rows. A run on an adapter or driver no row names is `env`, and its pictures are not judged.
- *Declaring a change.* The branch has a file `PICTURES-CHANGE.txt` at its root, one line for each picture or pattern:
  `<test path or pattern> <picture or *> <renderer or all> :: <reason>` (shell-style `*`; the test path is `suite/test` as the
  run's lines print it, for example `tour/010-CannonFleetGalaxy-1`). A differing picture a line names is `new`; one no line
  names is `fail`. A line that names no differing picture is stale: the check `pictures/declared` of a main gate is `tool`
  (`env` when the tablet's pictures were not taken, since they cannot be told from it).
- *What a run leaves.* In its folder: `new-pictures.tsv` (one line for each new or declared picture, complete enough to be its
  approved row), the pictures' files and the sheet to look at, `D:/NebulaWork/build/gate-runs/<run folder>/new-pictures/sheet/review.html`. The summary prints the count
  and the sheet's path.
- *Approval at the landing.* When a run with `--move` has passed (and GitHub is green) and left new pictures, the gate calls
  `pictures.approve_landing` (called by `approve_pictures` in `gate.py`) before it moves main: it puts the files in the picture store, adds or
  replaces the rows, removes `PICTURES-CHANGE.txt`, and makes ONE commit on top of the tested commit that touches only those
  files (message `pictures: N approved for <the tests>`, the repository's own identity). Then it runs the picture checks again
  on the same run folders (no game runs); they must all be `pass`, else nothing moves and the result is `tool`. Only then
  main moves, to that commit. The runs' working files are pruned after this.
- *Nightly pictures.* The full tours and the other discs' pictures are taken only by the nightly run. Its new ones are `new`
  there, and `pictures.py approve --new --runs <the nightly's folder>` writes their rows (the rows for a normal landing).
- *A driver change.* `pictures.py driver-update --runs <folder>` for runs on an adapter or driver no row names: each picture is
  compared with the approved copy of the machine the rows name (same renderer, same card maker); equal, or no channel of any
  pixel off by more than 1 of 255, is approved for the new machine; the rest are listed as `FAIL`. It moves nothing by itself.
- A picture whose change has no known cause is never declared; it is a fault to trace.

**Kinds of result.** Every check and every test ends as one kind (`tools/gate/kinds.py`; the runner writes `"kind"` into each
test's `result.json`):

| Kind | Meaning | Fails a run? |
|---|---|---|
| `pass` | It passed. | no |
| `fail` | The game or the change is wrong: once the game has started, a crash dump, frames that stopped, an exit code other than 0, a loss report that is not all 0, a wrong step hash, a picture that differs from its approved copy. | yes |
| `tool` | The test tool broke: the runner or a script raised, a file the runner must provide is missing, the game could not be started at all, a result file is missing or cannot be read, a check ran another number of cases than its suite lists, or none. | no: the run is not judged; the tool is fixed and the check run again |
| `env` | The machine was not fit: the tablet does not answer, the drive is full, another graphics card or driver than the approved pictures name, a lock not had in time. The tablet's `owed` state is `env`. | no: not judged (an owed tablet part does not change the verdict) |
| `flake` | It failed, then passed when run again alone. | no |
| `main` | It failed again alone and fails the same way on main's build: main is at fault. | no |
| `new` | A picture with no approved copy, or one the change declares in `PICTURES-CHANGE.txt`. | no: approved at the landing |

A check's kind is the worst of its tests, in the order fail, tool, env, flake, main, known, new, pass. The run ends with one line
`KIND <kind> <check> <test> :: <first line of its message>` for each result that is not a pass (those of kind `main`
first), then the last line `VERDICT pass` (exit 0), `VERDICT fail` (a fail is left: exit 1) or `VERDICT not-judged` (no
fail, but a tool or env: exit 2). The same facts are in `verdict.json` in the run's folder. Main moves only on
`VERDICT pass`.

**Run again alone.** After every lane has ended, each test that is `fail` is run once more by itself: the same build, no
other game process, a fresh folder `rerun/` in the run's folder. If it passes, its kind is `flake` and one line goes to
`D:/NebulaWork/plan/native/flakes.tsv` (time, test, renderer, commit, the first failure's message); its second run takes the
first's place in the run's folder, the first is kept in `rerun/first/`. When that file holds three lines for one test and
renderer within seven days, the test goes to `D:/NebulaWork/plan/native/set-aside.tsv` and the gate opens one repair for it (source `flake`, kind `fail`, check `flake:<test>`, the run folder kept whole as its evidence; `plan.py next` gives it before a step). The files live outside the repository, so a test run never changes the tree.
A test listed there still runs and is reported (`set aside` on its line), and its `fail` does not fail a run; the gate
prints the set-aside tests at the top of every summary, and a line leaves the file, with the test's lines of flakes.tsv, when its repair has landed (`plan.py land R-00N` does it): until then the test stays set aside, which is for ever if nobody lands the repair. The tablet's tests
are not run again (the test app is removed when the tablet's part ends).

**Check against main.** A test that is still `fail` after its second run is run alone on main's build, but only when its
own definition (its test folder, or for a generated tour the files that write it) is the same on `origin/main` and in the tree
under test; otherwise the check is skipped, with a line saying so, and the test stays `fail`. Main's build is kept by the
gate in `D:/NebulaWork/build/base-bins/<commit>/bin`, a copy made whenever the gate moves main (the two newest are kept); with
no copy for `origin/main` the check is skipped too. A test that fails there with the same first message is `main`: it does
not fail the run, and it is printed first in the summary.

Two runs of fixed steps that differ are a determinism fault in the game or the harness, and are treated as one.

---

## 10. Measurements

They need the machine alone, so they are not in levels 1 and 2.

- **The speed measure** (`tools/measure/measure.py`): the play route (`tests/measure/route`, one heavy stage, three minutes in real time, a visible window, no sound) on Direct3D 12, Vulkan, Direct3D 11, Direct3D 12 held to four processors (`d3d12-4cores`) and the tablet. It starts in no long run and after no move of main: the single measure is `measure/step/tablet` of a landing; the PC's speed is the pair (10.1). A player never sees it; it tells whether a change made the game slower.
  - *Commands.* `measure.py run --bin BIN --work WORK --commit SHA [--runs ...] [--history FILE] [--pending FILE]` measures. `measure.py keep FILE [--history H]` adds the lines of a `--pending` file to the history (a line already there is not added twice). `measure.py anchor --commit SHA` marks a commit's trusted lines as the start of the native work. `measure.py accept --commit SHA` restarts the baseline after a slowdown that was looked at and accepted. `measure.py digest RUN_FOLDER` prints one run's figures.
  - *History.* `D:/NebulaWork/build/gate-runs/measure-history.tsv`, one line per run, holding main's commits only. The nightly run and a move of main write their lines at once. A measure inside a landing is to run with `--pending FILE`: it compares with the history as usual but writes its trusted lines to FILE, and the gate calls `keep` when the landing has moved main (the gate's `measure/step` checks, below).
  - *Figures.* Judged: the 99th-percentile gap between pictures (`gap_p99_ms`), late pictures a minute (`late_per_min`), the render thread's time per picture (`render_ms`), the graphics card's time per picture (`gpu_ms`, only where the renderer gives it; else "not measured"), the most memory held (`memory_mb`) and the cost of the game's work: the processor time the frame threads spend working, per game step, all together (`cpu_work_ms`), the game thread's (`cpu_game_ms`) and the render thread's (`cpu_render_ms`). Each frame thread reads its own processor clock at the marks of work and of waiting the engine already has and adds up the work; a timer's spin and a present that blocks are not in it. Shown and never judged: the longest gap (`gap_max_ms`), the longest step (`step_max_ms`), the steps over 18 ms (`steps_over_18`), the median game step (`step_ms`) and its 99th percentile (`step_p99_ms`) (they hold waiting) and `cpu_ms`, the processor time of the whole process per step, which holds waiting too (section 10.1): one slow frame in three minutes is not a change in the game.
  - *Worse.* A figure is compared with the median of the last ten trusted runs with the same key (computer, graphics card, run kind pc, pc-4cores or tablet, renderer, scene, picture size, rate). It is worse only beyond the largest of three times the spread (median absolute deviation times 1.4826), 5% of the median and the figure's floor: 0.25 ms for the gap figures, 0.02 ms for the step, render and card times, 2 for late pictures a minute, 32 MB for memory (so a median of 0 never gives a band of 0). With fewer than five trusted runs it says "no baseline yet". A run with a worse figure is measured again once, by itself; worse twice is WORSE (exit 1): a ledger row.
  - *Not trusted* (exit 3, no history line, compared with nothing), each with its own plain reason: a compiler, build tool or another copy of the game was running (the measure waits for a quiet machine up to 30 minutes first); the graphics card was used by other programs for more than 10% (the desktop's compositor, `dwm.exe`, is not counted: it draws the game's window too); other programs used more than 10% of all the processors' time over the run; the game's window was minimized, covered (less than half of it visible, the windows above it subtracted as rectangles), the active window, or the pointer moving over it (the runner's `window-watch.txt`; a pointer that only rests under the window, as the owner's does where the window comes up, is not counted: a window that takes no focus reads no pointer that does not move). The tablet's run does not read the PC's load. A busy PC gives "not trusted", never a false "worse".
  - *The anchor.* Every run also compares each judged figure with the median of the anchor's lines of its key, with the same band. A figure beyond it says "slower than at the start of the native work by N%"; with fewer than five anchor lines it says "anchor: not enough lines". This never changes the exit code: the nightly run reads it, and it catches a slow creep that a rolling median follows.
  - *Result for a tool.* Every run writes `result.json` in its work folder: for each run kind its state (`ok`, `better`, `no-baseline`, `worse`, `not-trusted`, `tool`; `left-out` for a kind not asked for), the reason, and each figure's value, median, band, whether it is judged, its verdict and its anchor comparison. Exit codes: 0 ran, 1 WORSE, 2 the measure could not run or read its results, 3 not trusted.
  - *Locks.* The PC's runs take the gate lock (through `--gate-lock`, unless the caller holds it) and `measure.lock`, whose `holder.txt` names the measure's process (`pid N`); a lock whose process has ended is taken over. A run of the tablet alone takes no `measure.lock` and waits for no quiet PC. A build (`build_windows.ps1`) or a test run (`luma_test.py run`) started by hand waits while `measure.lock` is held, one line saying so, at most 15 minutes, then goes on (`tools/gate/measure_wait.py`); the gate, a landing and the measure itself set `NEBULA_NO_MEASURE_WAIT` and never wait.
  - *In a landing* (`gate.py land`, package B8). A plan that holds an engine or a tour group (`engine1`, `engine3`, `tour1`, `tour3`) gets the group `measure_pc`, and one that also holds the Android build (`android`) gets `measure_tablet`; the plan the landing prints names them. No row of `tools/gate/scope.tsv` names them, the other levels do not have them, and a plain `--main` run has both. The two run names are the constants `MEASURE_PC_RUN` (`d3d12`) and `MEASURE_TABLET_RUN` (`tablet`) at the top of the measure's code in `tools/gate/gate.py`.
    - `measure/step/pc` (B14 part 3) starts when the PC's own lanes are done (the builds, the engine suites, the tours, validation and every other check that runs the game on the PC; not the tools' self-tests, `harness/self` and `description/self`, which go on beside it) and the Android build is over, and runs after `tour/tablet-pc`, one at a time, while the tablet's lane goes on beside it: never beside a build or another game copy of the gate on the PC. The landing ends when both lines and the tablet's are done; `replays/tablet` and the pictures run when their two sides exist. With `--also` the pair stays last (after the parts asked for); a check of the PC's lanes that did not pass, or a failed Android build, keeps it away (it is left out, with no row, when a check before it failed; with a failed test pending its second run it waits for the second runs, as before). Proof, 2026-11-22, `main` against itself, Direct3D 12, bands of the calibration of record: five pairs beside the tablet's lane (its measure, then its 12 stops) and five beside the tools' self-tests (8 at a time): every figure of every pair inside its band, except that the first pair beside the tablet's lane, which began with the tablet's install, needed a second round (round 1: `cpu_render_ms` +6.5 % against 3.9 %, `cpu_work_ms` +5.6 % against 4.1 %, `render_ms` +5.3 % against 5.0 %; round 2 inside); three more pairs, begun in the fourth minute of the tablet's lane (its measure's route, then its tour), one round each, every figure inside. Whether the pair waits for the self-tests is `PAIR_WAITS_FOR_SELF` in `tools/gate/gate.py` (False); Rust's tests, which build, stay in a lane that is waited for. It is the pair of section 10.1, not the single measure (section 10.2 says what it is judged against): `measure.py pair --base <the base's bins> --cand <the landing's bins> --runs d3d12[,d3d11,vulkan] --work <run folder>/measure-pc --pending <run folder>/measure-pc.tsv --base-pak <the base's game.pak> --cand-pak <the landing's> --base-commit ... --cand-commit ... --name measure/step/pc` (the landing holds the gate lock, so no `--gate-lock`; no quiet minutes are waited for). `d3d12` is always asked; `d3d11` and `vulkan` where the landing's plan has an engine or a tour check of that renderer (a full run has all three).
    - `measure/step/tablet` runs in the Android lane right after a `build/android` that passed and before the tablet's part, so it starts on a rested tablet: `measure.py run --runs tablet --device <serial> --device-lock <the tablet's lock> --device-wait <--tablet-wait> --android-bin <the Android build> --apk <the test app> --pak <the game data> --pending <run folder>/measure-tablet.tsv`. The measure takes the lock, installs the test app, plays and removes the app by itself, and the tablet's part prepares it again as before. It runs beside the PC's lanes (a tablet-only measure takes no PC lock and its figures do not depend on the PC). No `--tablet`, or a tablet that does not answer adb, makes the row `owed`.
    - *What the result means.* The gate reads `result.json`, the run's entry; the exit code only as a cross-check.

      | The run's state | The row | Main |
      |---|---|---|
      | `ok`, `better`, `no-baseline` | `pass`; the message names the judged figures with value and median, and says "no baseline yet" where a figure has none (the tablet's check; the PC's pair is in section 10.2) | moves |
      | `worse` | kind `worse`; the message lists each worse figure with value, median and band | does not move; exit 1 |
      | `not-trusted` | `owed: the measure was not trusted: <reason>`, one line in `tool-faults.tsv` | moves |
      | `tool`, no readable `result.json`, an exit code and a state that disagree, or the time limit | `owed: the measure could not run: <reason>`, one line in `tool-faults.tsv` | moves |
      | `left-out` (the tablet away) | `owed`, as the tablet's other parts | moves |

    - *The kind `worse`* fails the run like `fail`. It is never run again alone (the measure measured twice itself), never written to the flakes file and never set aside. In the store of the same failure three times its signature is the check and the worse figures' names in sorted order, never the values (`measure/step/pc | render_ms+step_ms`): the same figures worse three times stop the branch (exit 3), other figures do not.
    - *Time limit.* The tablet's check: a route of three minutes measured at most twice, with two minutes for each run's start and end, the wait for its lock (`--tablet-wait`) and five minutes for the install and removal. The pair's: 120 seconds and, at 150 seconds a round (one took about 121 s), three rounds of every renderer asked for and one repeat (`pair_limit`: 12 minutes for `d3d12`, 27 for all three). At the limit the row is `owed: the measure could not run: its time limit`, with its line in `tool-faults.tsv`. At most one line for each check in a landing; the test named in it is the check.
    - *The history.* The tablet's pending file is kept (`measure.py keep`, with the gate's `measure-history.tsv`) only after main has moved; the pair keeps no file in that history (it writes `pairs.tsv` itself and its `--pending` table is the gate's to read). A `keep` that fails changes nothing about the landing: one printed line and one line in `tool-faults.tsv`. When main did not move, the files stay in the run folder and are never kept.
  - *The game's window.* Every game the runner starts gets `--no-focus 1`: its window is shown without being activated and none of the focus calls is made, and the game runs as it does with the focus (docs/using-nebula/command-line-arguments.md). A player's start is unchanged.
- **Full set** (the table of 00-READ-FIRST 4.3): at the end of each package that changes speed or pacing, and at the final check.
- While `measure.lock` is held: no gate, no long run part, no build, no game. Reading and writing code goes on.

### 10.1 The pair (package B13)

The speed measure above judges one run against lines kept from earlier days and trusts a run only when the PC was quiet. On a PC in use that never holds (the owner works on it, his own jobs use the processors and the graphics card), and a line from a quiet hour compared with a run from a busy one would say a step got slower when a job had only started. The pair is the form that holds there: **two builds run side by side, in the same minutes, on the same processors, and only the ratio of a figure is judged.** Whatever the PC does reaches both alike, and nothing is compared with another day. The tablet's measure stays as it is (one run, kept lines, a device nobody else uses).

- *What a round is.* For one renderer both builds are started at the same moment, each through the runner the single measure uses (`luma_test.py`), each with its own build's programs and game data (a side's store version may differ from the other's), its own work folder, saves and app data, the same stage (the route of `tests/measure/route`, cut at game step 6200 after the stage's first frame), size and rate, no sound. The windows are hidden: nothing appears on the owner's screen (`--form visible` gives the single measure's window, shown without the focus). The tool waits for both. The measured span is the stage's first 5400 game steps after its frame 0 (90 seconds), cut by the game frame numbers of the timeline, so both sides are measured over the same moments of the game; the `[pace]` windows inside it are the ones whose steps add up to 5400.
- *The same processors.* Both sides are held to one set of logical processors by the process affinity of the runner the tool starts (the game inherits it): from the system's own description (`GetLogicalProcessorInformationEx`), the processors of the highest efficiency class and, of those, the ones that share the largest level-3 cache (several of that size: the first); when that is every processor nothing is set. The set is printed. On a PC with two halves of different cache a build that happened to land on the smaller half would look slower.
- *The figures.* The cost of a step is the processor time of the frame threads' WORK. Each frame thread (the game thread, the render thread) reads its own processor clock at every change of its mark between work and waiting (`runtime/include/galaxy/perf_hint.h`: `perf_hint_busy`, `perf_hint_frame_done`; Windows on x64 `QueryThreadCycleTime` as time by the time stamp counter's rate over the same span, elsewhere `CLOCK_THREAD_CPUTIME_ID`; where the machine gives none the fields are left out, never 0) and adds up the span from a mark of work to the next mark of waiting. The `[pace]` line has `cpu_work_us_per_step` (all frame threads together) and `cpu_<name>_us_per_step` for each (`game`, `render`; a thread added later shows without a change), beside `cpu_us_per_step`, the whole process's processor time. Judged: `cpu_work_ms`, `cpu_game_ms`, `cpu_render_ms` (each the median over the windows of the span; floor 0.02), `gap_p99_ms`, `late_per_min`, `render_ms`, `gpu_ms`, `memory_mb`. Shown, never judged: `cpu_ms` (the whole process: it holds waiting, the spin of `PreciseWait`, which grows with how late the timer wakes on a busy PC, and the time a present of a hidden window holds the threads; one build read 1.3 ms and 3.4 ms a step by it, and a build with 0.3 ms of added work in each step was not told apart), every other `cpu_<name>_ms`, `step_ms` and `step_p99_ms` (they hold waits as well as work: one build gave 2.6 to 16.6 ms). `gpu_ms` is the median of the frames' GPU times (a frame's spans added), not an average that takes in every moment another program held the card. The kept-lines file has the columns `cpu_ms`, `cpu_work_ms`, `cpu_game_ms` and `cpu_render_ms` at its end; a line from before reads them as empty, never 0. The tablet's measure judges the same list. A mark costs 168 ns (it reads the thread's clock): about 0.7 microseconds a step of 16.7 ms in all.
- *The ratio and the band.* `r = (candidate + f) / (base + f)`, `f` the figure's floor (`band.FLOORS`), so 0 against 0 is 1. The band of a figure is learned from pairs of one build against itself (`pair-calibrate`: `band = max(least, 2 x the largest |r - 1|)` of five rounds; least: `cpu_work_ms`, `cpu_game_ms` and `cpu_render_ms` 0.03, `render_ms` 0.05, `gpu_ms` 0.05, `memory_mb` 0.02, `gap_p99_ms` 0.25, `late_per_min` 0.50) and kept for the key (computer, graphics card, renderer, scene, size, rate). `r > 1 + band` says worse, `r < 1 - band` better, else ok; no line for the key says `no-baseline` ("not calibrated on this PC"; it passes).
- *Three rounds before "worse".* A figure worse in round 1 gets round 2 with the order of starting swapped; worse again, round 3 (the order of round 1). Worse in all three is `worse`; anything else `ok`. The common case is one round; `better` is said from round 1 alone. A round that did not run (a side ended early, the stage was not reached, a figure file is missing) is made once more; twice is `tool`, with the runner's own last lines.
- *Commands.* `measure.py pair --base BIN --cand BIN --runs d3d12[,d3d11,vulkan] --work WORK --pending FILE [--calibration FILE] [--base-pak P --cand-pak P] [--base-commit SHA --cand-commit SHA] [--name measure/step/pc] [--gate-lock ...]` judges. `measure.py pair-calibrate --bin BIN --runs ... --pairs 5 --work WORK [--commit SHA]` learns the bands. `measure.py pair-accept --commit SHA --renderer R --figure F --reason TEXT` writes an accepted line (only the owner's word puts one there). `measure.py pair-probe --bin BIN --renderer R --work WORK [--form ...]` runs one build alone and lists its figures (it chose the form: a hidden run that gives every judged figure makes hidden the form). The pair takes the measurement lock like the single measure; it asks for no quiet minutes and reads no window watch or processor watch: the trust rules of the single measure do not apply to a pair, because a ratio of two builds that ran side by side does not depend on what else the PC was doing. A build or a game of someone else's beside it is allowed.
- *Files.* Beside the history (`gate-runs/`): `pair-bands.tsv` (computer, gpu, renderer, scene, size, rate, figure, band, the five ratios, commit, time), `pairs.tsv` (every figure of every round: time, base commit, candidate commit, renderer, round, figure, base value, candidate value, ratio, the round's word) and `pair-accepted.tsv` (candidate's commit, renderer, figure, reason, date). In the work folder: `result.json` in the single measure's shape (for each renderer its state, reason and figures; a figure's `value` is the candidate's and its `median` the base's; `rounds`, `processors` and `form` are added) and, for `--pending FILE`, a table with one row for each renderer and figure: `check`, `renderer`, `figure`, `state`, `line`.
- *Words.* States: `ok`, `better`, `no-baseline`, `worse`, `tool`, and `accepted` (new: a figure that would be `worse` and has a line for the candidate's commit in `pair-accepted.tsv` passes, and its line says so; a renderer with no worse figure and an accepted one is `accepted`). One line for each judged figure, for example `measure/step/pc d3d12 cpu_game_ms worse: base 0.312, candidate 0.613 (+88.4%, +90.7%, +89.6% in three rounds; the band is 3.6%)` or `measure/step/pc d3d12 gpu_ms ok: base 0.205, candidate 0.206 (+0.5%; the band is 5.0%)`. Exit: 0 ran and nothing worse, 1 worse, 2 tool.
- *A pair's pictures.* Both sides present with VSync off (`GALAXY_VSYNC_INTERVAL=0`: how long a present of a hidden window holds the threads is Windows' choice, and it differed between the two sides) and keep the route's frame cap (`GALAXY_MAX_FPS=120`: two pictures a step); `result.json` says both and the pictures a step. The single measure and the tablet's run keep their settings.
- *Measured 11-21 on the owner's PC (hidden windows, Direct3D 12, while it was in use).* Main against itself, five pairs: `cpu_game_ms` ratios within 2 % and a band of 3.6 %. A build with 0.30 ms of added work in the game thread's work in each step read +0.298, +0.301 and +0.296 ms of `cpu_game_ms` in three rounds (+0.298 three times with eight processors kept busy), worse each time, `cpu_render_ms` ok; one with 0.30 ms for each picture in the render thread (two a step) read +0.564, +0.591 and +0.582 ms of `cpu_render_ms`, `cpu_game_ms` ok. The same build read 1.3 ms or 3.4 ms a step by `cpu_ms`. The render thread's time per picture is not steady: 533 to 622 microseconds a step in the same build in different rounds (the game thread's stays within 2 %), and its band came out 17.2 %, 12.5 % and 5.7 % in three calibrations; it is above the 6 % that the design allows (ledger P-337 says what was found of the cause).

### 10.2 How a landing is judged for speed: the base and the anchor (package B13, part B)

`measure/step/pc` (group `measure_pc`, name, plan and board unchanged) is the pair of section 10.1. The candidate is the landing's build (its `bins` and the game data it was built for); the other side is **the base**: the build of the last commit on main whose speed was judged, kept in `D:/NebulaWork/build/speed-base/` with its own game data. The gate reads `result.json` and the `--pending` table as data and names no figure; the figures, bands and lines are the pair's (`tools/gate/speedbase.py` reads them).

| What the pair said | The row | Main | The base after the move |
|---|---|---|---|
| every figure `ok`, `better` or `accepted` | `pass`: each renderer and its state | moves | the candidate |
| a figure `worse` in all three rounds | kind `worse`: `worse: d3d12.cpu_game_ms+...: <the pair's plain lines>`; the same figures three times stop the branch | does not move; exit 1 | stays |
| a figure with no band (`not calibrated on this PC`) | `owed`, with the command that makes the bands (`measure.py pair-calibrate ...`); no line in `tool-faults.tsv` | moves | stays |
| no base yet | `owed`, nothing is run; the line names `gate.py speed-base make` | moves | stays |
| the pair could not run on the candidate's side, or on both, no readable `result.json`, an exit that is not the states', the time limit | `owed: the measure could not run: <why>`, one line in `tool-faults.tsv` (kind TOOL) | moves | stays |
| the base's side could not run the route and the candidate's can by itself (`measure.py pair-probe` of the candidate) | `owed: the base <sha9> cannot run this route any more (<renderer>: the base side ended with exit N: <the runner's last lines>): the base moves to <sha9> unjudged` | moves | the candidate, with the row `rebased` in `base-history.tsv` |
| the base's side ended because its game data is of another store version than its own build | `owed: the measure could not run: ...`, one line in `tool-faults.tsv` (kind TOOL); never the row above | moves | stays |

Without the last two rows one such landing would leave every later one unjudged: the base is a build of an older route that no longer starts. The row `cannot run this route` is for a kept build that does not start (its program ended with a code, a crash, a stage not reached); the line says which renderer's side ended and how. **A difference of the store version between the base and the candidate is never a reason for it.** Each side of the pair is run by the runner of the tool's tree (`luma_test.py`) with its own build's programs and its own `game.pak`; the runner wants a pak of the store version its build's own importer writes (`nebula_import.dll`, the function `nebula_import_store_version`, asked in a program of its own; where that cannot be asked, the tree's `store_version.inc`), so a base of store 37 plays a candidate of store 40 side by side. Until 11-22 the runner wanted its own tree's version for both sides: every step that changed the store version (S0.1.3, S0.1.4, S0.1.5's anchor) moved the base or the anchor unjudged. The route (`tests/measure/route`) names a stage by its name and gives inputs by game step: nothing in it depends on the data's version. A landing whose plan has no `measure_pc` group does not touch the base. `worse` accepted by the owner's word (`measure.py pair-accept`, section 10.1) is `accepted`: a pass. **When the word is `worse` the agent mends the step** (a change that made a frame thread's work cost more is read and changed, not measured again); only the owner's word accepts a worse figure.

*The base.* `D:/NebulaWork/build/speed-base/<base>-<commit>-<time>/` holds `bins/` (the build's bin folder with its `pak.path` pointing at the folder's own `game.pak`), `game.pak` (a hard link to the pak the build was made for, a copy where a link cannot be made, so that no clean-up of the pak folders takes it away) and `kept.json` (the commit, the time, the data's key); `current.txt` names the current folder, and `base-history.tsv` has a row (made, moved, kept, rebased) for each change. After `MOVED`, where the tablet's `keep_measures` runs, `keep_speed` follows the table above: the new base is made whole before `current.txt` changes, then the old folder is removed. `gate.py speed-base make --bin <bins> --commit <sha> [--pak P]` makes the first one by hand; `gate.py speed-base show` prints the base and the anchor with commit, time, the data's key and the sizes (measured 2026-11-21: a base is 527 MB of `bins` and `speed-base make` takes 8 s; the game data, 4.02 GB, costs nothing more where it is a link; one round of the pair takes about 121 s).

*The anchor.* A second kept build, made once on the day the native work starts (`gate.py speed-base anchor --bin <bins> --commit <sha>`; a second make needs `--replace`) and never replaced by a landing that judged it. `--also speed/anchor` (the extra check of a step: `docs/plan/native/step-checks.tsv` has it for the last step of every package, so a package's last landing asks for it) runs the candidate against the anchor on all three renderers, before the landing's own pair, with the table above (`worse` fails with kind `worse`; no anchor yet is `owed`). A judged `speed/anchor` keeps its ratios (`anchor-last.json`: the ratios of the landing's judged figures against this anchor). Where the anchor does not start (never for a store version: see the table) the check is `owed` with the line `the anchor <sha9> cannot run this route any more (<what ended and how>): the anchor moves to <sha9> unjudged`, and after the move the candidate becomes the anchor; `anchor-history.tsv` then gets a row with the new anchor's commit, the anchor before it and the last ratios judged against that one, so that the drift since the first anchor is the product of the rows' ratios and the newest judged ratio.

*Where things are not yet.* With no `pair-bands.tsv` for this PC (the calibration of section 10.1 has not been run), every pair says `not calibrated on this PC` and the check is owed; that is the state until `measure.py pair-calibrate` has been run on a quiet PC for each renderer.


---

## 11. The tools hold the rules

Agent 1 builds these into `tools/gate`; each has a test that fails without it.

| Tool | What it does |
|---|---|
| `gate.py --quick`, `--main`, `--long`, `--final` | The four levels |
| `--main --move` | Only the landing runner's own call: takes the lock, runs, reads GitHub (before the move for `github-first`, after it for `github`), moves main, writes the board, frees the lock |
| `--only CHECK` | One check alone, for tracing a red main |
| The two lists | Read by every level, with the rules of section 4 |
| A pre-push hook | Refuses a push to main unless the gate recorded that commit as allowed |
| `D:/NebulaWork/build/gate-runs/history.tsv` | One line per run: commit, level, result, seconds per check |
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

Whoever moves main starts it, if none is running and the last one started twenty hours ago or more (a run with `--only` is not a long run
for this rule). It starts by itself after a move: the landing's last lines say that a long run started, where its log is
(`gate-runs/long-<sha>.log`), and that it gives way to landings. A move starts no speed measure, and the long run has no speed part either:
speed is judged in every landing (the pair on the PC, the tablet's own measure) and at the end of each package (`speed/anchor`). One long run
at a time, under `D:\NebulaWork\plan\long.lock` (a lock whose process has ended is taken over).
A start by hand, for a day on which the twenty hours stand in the way (a run that died after its start was written): `gate.py long-start --why TEXT [--sha COMMIT]`. It asks no twenty hours, writes the reason after the commit in the marker that stops a second start for twenty hours, and is stopped by a long lock held by a process that runs and by a start of the last ten minutes.

**The long run gives way to landings (package B15).** No part holds the machine longer than 7 minutes (`tools/gate/long_parts.py`:
`PIECE_SECONDS`, the seconds per stop taken from the long runs on record). A part whose list of stops would take longer is played as
pieces, each of which takes `gate.lock` for itself; a build of the run's own (the first build, the sanitizer build, the coverage build, the
Android build) keeps the lock to its end. A piece does not take the lock while a branch waits or runs in the landing queue
(`landing-queue.tsv`; a `running` row counts while the queue runner lives): it looks every 30 seconds, and when it has the lock it looks
once more, so a branch that came between the look and the lock goes first. A landing that arrives during a piece waits for that piece
only. The board keeps one line for each check: the pieces play into one run folder and the check is read and judged once, after the last
(all pieces pass is a pass; a failed test is named as before; a piece that could not start is `tool`). The tablet's pieces take
`tablet.lock`, not `gate.lock`, and play beside the PC's parts; they do not start while a branch waits or runs in the landing queue, nor while
a speed measure holds `measure.lock` (until a proof shows that the pair's figures stay inside their bands beside the tablet's lane:
`long_parts.TABLET_BESIDE_MEASURE`). The PC's own run of the same stops (`tour/tablet-pc`) is a PC part under `gate.lock`, a piece after each
tablet piece. The soak plays beside everything at below-normal priority and takes no lock.

**What follows from its results (package B15, `tools/gate/long_results.py`).** At the end of a long run that ran all its checks, and alone
on a finished run folder (`gate.py long-results RUN_FOLDER [--dry]`), each check that is not a pass leads to work: `fail` is traced to the
landing that brought it and opens a repair (`plan.py repair add`, source nightly) whose evidence folder holds `how-to-run.txt`, the command
that plays the failed test alone (the gate's own command, `<bin>` for the build's programs); `stress` and `worse` open a repair of that kind;
`tool`, `env`, a part that did not run and a tablet that was away are one line each in `tool-faults.tsv` (step `nightly`); `planned` is
nothing. Tracing looks at the commits main moved to (the lines of level `move` in `history.tsv`: the commit and the branches the landing
covered; before them the landing queue's rows) after the newest earlier long run that passed the check, and halves them by building the
middle one in the long run's own tree and playing the failed tests there under the gate lock. It never traces the sanitizer, coverage, soak,
measure, stress, tablet and GitHub checks, a check that is not a suite of the game's runner, a check whose one run takes over 15 minutes, and
nothing once the night's tracing used 90 minutes; such a repair names the landings in between. A repair that landed becomes `confirmed` when
a later long run passes its check; a check that fails again opens a repair that says "seen again after R-nnn". New pictures of the run's own
stops are approved on a branch `nightly/pictures-<date>` through `gate.py land` when no picture check failed and no branch waits. The run
folder gets `nightly-report.md` (the parts with their minutes, a part over 45 minutes named) and `long-results.json`; the step run twice on
one folder changes nothing the second time.

---

## 15. Checks by what changed, the landing queue, game data by key and locks (package T4)

This section is what the tools do now. Where it differs from an older section above, this section is right.

**Checks by what changed.** `tools/gate/scope.tsv` maps path patterns to groups of checks (`tools/gate/scope.py` names the
groups once: `repo`, `tools`, `build`, `unit`, `start`, `engine1`, `engine3`, `tour1`, `tour3`, `validate`, `import`, `android`, `tablet12`).
The group `repo` (the cheap repository checks) is in every run, whatever the rows say. Every row a changed file matches adds its groups; a landing runs the union over all changed files (`git diff --name-only` from the
merge base with `origin/main`). A change of documents only runs `repo`, with no build, so it lands in minutes. A change to the
game runs the build, the unit tests, a start on each renderer and the 12-stop tour on Direct3D 12 with its pictures and replays.
A change to the renderers runs the engine tests and the tour on all three, and `validate` (below). A file that no row names makes the run `full`:
everything the main gate runs on the native game (every renderer's engine tests and 102-stop tour, the other discs, the Android
build and the tablet's short part). The full set on three renderers, three discs and the tablet is also the nightly run's first
part. `python -X utf8 -I tools/gate/scope.py --base origin/main` prints the groups and the checks of the branch you are in.

**Faults under heavy load, and faults in how the card is driven (package T12).** Two lanes, both on the machine alone for their tests'
time, neither read by a person until it fails.
`validate` (`tools/gate/validate.py`) plays the 12-stop tour one copy at a time on each renderer with the graphics API's debug layer
on (`luma_test.py run --gpu-validation 1`, time limits four times the usual; Direct3D 11's and 12's debug layer, Vulkan's validation
layer where the game finds one). It is in every landing that changes `runtime/src/render/**` or `runtime/include/galaxy/render/**`
(group `validate`), and in the nightly run on the whole tour. An error message of a layer is a fail, the first line the message; an
error whose id is listed in `tools/gate/validate-known.tsv` (renderer, id, ledger row, first words) is counted and reported; a
warning is counted only. Each renderer says at its start whether its layer is on (`[gpu] d3d11 debug-layer=1`, `[gpu] d3d12
debug-layer=1`, `[vulkan] ... validation=1`); a layer that is not installed leaves its check out with one line ("not run: the
Direct3D debug layer is not installed on this PC") and one line a day in `tool-faults.tsv`: never fail, tool or env. A run that
reached its time limit while the game still moved is `tool` ("the validate lane's time limit is too short for this test").
`gate.py --main --scope validate --serial` runs the lane alone, one copy at a time.
`stress` (`tools/gate/stress.py`; the nightly run only, or by name: `gate.py --long --only "stress/*"`) plays twelve copies of the game
at once, whatever the graphics card's rule says (the PC's memory rule still holds: fewer copies when it is short, and the run says how
many): for each renderer one lane of the engine suite and the 12-stop tour (`stress/engine/<r>`, `stress/tour/<r>/1`) and three more
tours (`stress/tour/<r>/2` to `/4`), each with its own run folder and saves and every engine watch on. A test that fails there and
passes run again alone has the kind `stress`: it never fails a run, is not in flakes.tsv and does not count toward setting a test
aside; the run folder is kept whole (`keep.txt`, which the thinning and the pruning leave alone) and one repair is opened (source
`stress`, kind `stress`; the watch's line or the first failure line is its note). One that fails alone too is a plain `fail`. A run
that reached its time limit while the game still moved and passed alone is only counted ("N runs were too slow under the load").
A fault that shows rarely is never hunted by repeating runs: it gets a watch (a cheap check in the game that prints one line naming
what was wrong), and the work goes on.

**The mod suites are off.** The owner set all mod work aside until the native rewrite is done. The line `mods = off` at the top of
`scope.tsv` keeps the mod suites and LumaLink's own check out of every landing, the main level and the nightly run. `short` (the
quick mod set) or `full` bring them back; `gate.py --mods short|full` does it for one run. The test driver is itself a mod
(`tools/test_driver/luma_test/luma_test.c`) and every tour and replay loads it, so a driver that does not load is a failure.

**Landing.** `gate.py land BRANCH [BRANCH ...]` puts the branches in the queue (`D:/NebulaWork/plan/landing-queue.tsv`, changed only
under a lock folder beside it) and, when no queue runner is alive, becomes the runner; otherwise it waits for its own branches.
The runner takes every waiting branch in queue order, replays each branch's own commits on a temporary branch at `origin/main` in
`D:/NebulaWork/MarioGalaxyRecomp/Nebula-landing` (a branch that does not apply is `conflict`: not judged, the others go on), runs
the union's checks once on the tip, and on a pass approves new pictures, moves main (waiting for GitHub first when its plan has the group `github-first`; with `github` alone GitHub is read after the move, section 8). On a
fail it halves the batch to find the branches at fault, lands the rest and names the ones it removed with their `KIND` lines. A
tool or machine fault lands nothing: every branch waits again. `gate.py land --queue` runs what waits. Exit codes: 0 landed, 1 fail,
2 not judged, 3 the same failure three times. The failure's signature (the first failing check and its first error line) is kept
for the branch (`tools/plan/failures.py`); the third in a row stops the retries. A tool or machine fault never counts toward it,
and is written to `D:/NebulaWork/plan/native/tool-faults.tsv` at once. `plan.py land` makes this one call, and can be run again at any time: it never resets the agent's work folder. The tool's own commit (`Plan: step X landed`, only `steps.tsv`, the step's row `landed` with the code's commit) is made once; a tip that already is that commit is handed over as it is, and fix commits on top of it get a second one that corrects the code's commit. A step is `landed` only when `origin/main` says so; the folder's own `steps.tsv` saying `landed` for a step main does not have means the landing has not passed, and the step is `started`. Just before the gate is called the tool writes a landing record (`landing-<agent>.tsv` in the step tool's live folder: step, branch, the tip handed over, time, process id) and removes it when the gate has answered. While it stands, `land` joins the landing (`joined the landing of X, in the queue since HH:MM`: the queue does not take a branch twice, so the gate's call attaches to the row and waits for its end), `status` and `next` begin with `LANDING RUNNING: X since HH:MM. Run plan.py land X to wait for its end.`, and `start` refuses. A record whose queue row has ended and whose process is gone is dropped. When the landing that was joined judged an older tip than the folder has now and failed, the present tip is landed in the same call; when it passed, the later commits are named as not on main. When `origin/main` already holds the step (the gate finished without the tool), `land` does what follows a pass now (the landings list, the claim, the page: once for each step id) and ends with `LANDED (found on main)`. `plan.py card ID` prints any step's card, its package's rows and the checks it needs, and changes nothing. A branch that changes a file under `tests/replays/` is refused unless the step's card says the replay changes (`re-record`, `recorded again`, `replay changes` or `new reference`), and then a commit message of the landing must contain `replay reference`. The extra checks of `docs/plan/native/step-checks.tsv` go to the gate as `--also`. `plan.py owed ID "text"` writes what only a person or other hardware can check to `owed.tsv`; `status`, `STATUS.md` and the page list it, and it blocks nothing. Each agent runs `plan.py` from its own work folder (`Nebula-native`, `Nebula-native-2`); `next`, `start`, `status` and `land` read what has landed from `origin/main` as well as from that folder, so a landing of one agent is seen by the other at once. `--quick` and `--main`
still work unchanged until package T10 removes them.

**A step written ahead.** While the landing of the item in work (a step or a repair) is in the gate's hands, `plan.py ahead` makes the branch of the step that follows it on top of the tip being tested, in the agent's work folder, and prints that step's card, its checks and its commands; nothing of it is landed, claimed or pushed, and one step is written ahead at a time. `plan.py start` of that step takes the branch up once the item is on main (its own commits are moved onto main when main has the item under other ids or does not have it), `plan.py land` of the item works from the ahead branch without writing to its working tree, and `next`, `status` and `blocked` name the ahead step.

**A step's or repair's extra checks.** `gate.py land BRANCH --also NAMES` (`plan.py land` sends it) runs the named checks in this
landing besides what the changed files need. NAMES are comma-separated: the nightly's parts, which the table `ALSO_PARTS` in
`tools/gate/gate.py` names (name, the function that runs the part in the landing tree), or any check of the main level.

| Name | What runs |
|---|---|
| `stress` | the stress lane (twelve copies of the game at once). Asked for by name, a result that fails beside the others and passes alone counts as `fail` in this landing: it is the step's proof that a fault under load is gone |
| `sanitizer/tour` | the game's unit tests (`unit/game-asan`: those of `unit/game`, among them the arena tests that ask the sanitizer whether the game's own memory is poisoned where it must be) and the 102-stop tour on the sanitizer build (address and undefined behaviour), in the build's own kept folder. A stop ends at the first report the sanitizers print (no recovery): the report is a finding about the game, the check is `fail` with the report as its message, unless the first frame of the game's code in it is listed in `tools/gate/sanitizer-known.tsv` (frame, ledger row, first words): that is counted and shown. Run by name: `gate.py land BRANCH --also sanitizer/tour`. Times on the build PC (11-21): the sanitizer build from nothing 12 minutes, the 12-stop tour 85 seconds, the 102 stops 12 minutes |
| `soak` | the soak (`tools/gate/soak.py`: one plan of seeded random play through 12 tour stages, a lap, in real time) on Direct3D 12, Vulkan and Direct3D 11, three processes side by side, for 30 minutes. It passes when every renderer ends with no crash, no stop in frames and no stage that fails to load, and its memory does not grow (a median per lap at most 32 MB above the second lap's; the first lap is left out; the 30 minutes are two laps and a half). What a step's "both soaks clean" means: this check, passed. What its memory judge sees: "The soak's memory judge" below |
| `soak/long` | the same for 120 minutes (`--soak-minutes`), the nightly's soak: ten laps, judged from lap 6 on (the next section) |
| `replays/software` | the recorded plays (`tests/replays`) on Microsoft's software graphics device, Direct3D 12 and 11 (`tools/gate/replays_software.py`, with the game's software-device launch option), as many copies at once as the PC's memory and processors hold and with no graphics-card slot; every step hash is compared with the recording; the recorded stops hold at 1280x720 only (ledger row P-340), and a run at another size is named "recorded at WxH, run at WxH" (`tool`) and not compared; a play that crashed or hung is played once more alone after the others (passed alone: `flake`, failed again: `fail`), a step hash that differs never; a play during which the game's graphics device was removed (its log says so, and the game makes a new device) is a `fail` that names the step, whatever else it did (P-339) |
| `replays/rates` | every recorded stop (`tests/replays`) played twice on Direct3D 12 on the graphics card (`tools/gate/replays_rates.py`), at a world rate of 1 and of 2, and the step hash of every world step compared between the two plays: a hash that differs, or a step one play has and the other has not, is a `fail` that names the stop and the first step. The plays are the tour's stop with its input in world steps, moved to the steps where a frame begins at a rate of 2 (the controls are read once a frame), no pictures, ending at world step 601; the runner's test key `rate` (`tools/lumacore/luma_test.py`: 1000 to 16000, a whole number of steps a frame, the plan's `at` and `frames` then count world steps and a step that is not where a frame begins is refused) sets the rate through the time service from the run's first step, and the game half (`tools/test_driver/luma_test/luma_test.c`) writes the step hash after every world step of such a run (before `GameSystem::calcAnim`, which every world step calls), numbered from the first (`tools/verify/step_hash.py` reads them by that number). This is the proof that drawing does not change the game (S0.2.1, proof 2): a build that still lets a draw change the game's state fails it. The step's row in `docs/plan/native/step-checks.tsv` makes the step's landing run it |

They run after the checks the groups need have passed (a landing that failed runs none), before `measure/step/pc`, one after another,
each under its usual rules of kind, and the landing's printed plan names them ("and, asked for by name: ..."). A name of the table whose
part is not built yet, and a name that is neither in the table nor a check of the main level, refuse the landing before anything is queued
or run (exit 2, `VERDICT not-judged`); the unknown name's message lists the known names. `soak` and `soak/long` are not asked together. A
landing that names any check runs its groups' checks as the nightly run runs them, every stop of a tour (`--main --only NAME` is how one
nightly check is run by hand). The names travel with the branch in the queue (the column `also`), so a caller that only waits for
another's runner still gets its checks; a batch runs the union of its branches'.

**The soak's memory judge (`tools/gate/soak.py`, `judge` and `judge_long`; W long findings, 11-22).** The figure is one number for the whole
process: its private bytes, the graphics driver's and the test driver's included, a sample every 30 seconds, the median of each lap's
samples (a lap is 12 stages of a minute: 12 minutes). Nothing in it separates the game's own arenas from the rest. A leak is judged as steady
growth a lap; the sight is what growth a lap a run of so many laps refuses:

| Laps in the run | Rule | Steady growth refused (MB a lap, a minute) |
|---|---|---|
| 3 (the 30-minute check: two laps and the first half of a third) | the third lap's median at most 32 MB above the second's | the third lap's samples lie 9 minutes after the second's median: a leak from the start of 3 MB a minute (+32 to +36 MB on the recorded clean run) is refused, 2 MB a minute (+25 to +30) is not (added to the recorded clean run's samples, 11-22 12:56) |
| 4 to 7 | the most above lap 2 at most 32 MB | 32 / (laps - 2): 16, 10.7, 8, 6.4 |
| 8 or more whole laps (the nightly's 10) | from lap 6 on: the growth a lap, the median of the slopes between every two laps' medians, at most 32 / (laps - 2); and the lowest median of the last 3 laps at most 32 MB above lap 6's | 32 / (laps - 2): 8 laps 5.3, 10 laps 4.0 (0.33 MB a minute, 20 MB an hour), 12 laps 3.2, 20 laps 1.8 (0.15 MB a minute) |

The rule of fewer than 8 laps is the rule the soak had from 11-17 (a lap counts when it is not the last, or when its samples reach to within
a minute of its end). The long rule keeps the old rule's sight for ten laps (the old rule refused a steady ramp above 4 MB a lap there) and
moves its reference from lap 2 to lap 6: the first judge of a real two-hour run (11-22) read 23, 24 and 31 MB of warming up between lap 2 and
lap 6 on the three renderers, which left 1 to 9 MB of the bound for growth, and refused all three for it. The lowest sample of each lap,
laps 2 to 10 of that run, in MB: d3d11 646 660 659 663 670 673 670 676 676, d3d12 803 809 810 815 828 838 830 833 843, vulkan 1179 1200 1214
1218 1221 1221 1220 1221 1223; vulkan's stopped rising at lap 6, d3d11's and d3d12's had not at lap 10 (ledger row P-343, open). One lap
that rises and falls back does not move a median of slopes (it is in 4 of the 10 pairs of the five laps from lap 6) nor the lowest of the last
three laps: a spike is not growth. Memory that rises and stays for the last 2 laps moves the slope; for 4 laps or more, the lowest of the last
three. That run's laps through the long rule read: d3d11 +3.11 MB a lap (at most 4.00), d3d12 +1.94, vulkan +0.71: all pass. What it does not
see: growth under the sight above; a rise of more than 32 MB between lap 2 and lap 6 (the old rule saw it; it is what warming up read at
23 to 31); a rise that stays for 3 laps only and then falls back. Shown with a leak by the real gate: a leak of 5 MB a minute in the game process (recipe 8 of `docs/plan/native/TOOL-MEND-PLANTS.md`) is refused by the 30-minute form on all three renderers (d3d11 +65.0 MB, d3d12 +64.9, vulkan +57.5 above lap 2; run 20261122-203137-722379d9a-main); the long rule has no plant run of its own (a run of ten laps is two hours): its proof is its self-tests, the recorded laps of 11-22 and the ramps fed to it.

**The coverage history's key (`tools/gate/coverage.py`; W long findings, 11-22).** The file `coverage-history.tsv` in the gate's run folder (`D:/NebulaWork/build/gate-runs`) has one line a long run: the
share of lines run, the key and the verdict. The key is the binaries' names and the suites whose counts were merged (`engine`, `tour`,
`mods`): a long run with the mod suites off (since 11-21, `tools/gate/scope.tsv`) merges 18 engine tests and 102 tour stops, and its share
(16.23 % on 11-22) is not that of a run with 548 mod tests too (22.07 %): each key has its own line. A run whose share is lower than the last
line of its key that passed fails and is written with the verdict `fell`; the next run is compared with the last pass, so a fall does not
lower the bar. The lines up to 11-21 were re-keyed once (engine,mods,tour) and the 11-22 line (engine,tour); a line with no verdict counts as
a pass.

**Game data by key.** `D:/NebulaWork/build/game-data/` holds one folder per disc, store version and importer
(`<disc>-<store>-<first 12 characters of the git tree hash of crates/nebula-import>`). The gate asks for a disc's data by that
key (the check `data/RMGE01`, and `data/RMGP01` and `data/RMGJ01` for the other discs): found, it is used; not found, it is made
there with the tree's own importer from the disc's image (the US data from `D:/NebulaWork/testimg/SMG-rebuilt.iso`, never from a
Downloads folder). The build refuses nothing for a wrong data version: it names no data, and the gate writes `pak.path`.
`python -X utf8 -I tools/gate/gate.py data adopt` registers the older folders (`us-pak-37` and the others) under their keys by
reading their headers; nothing is moved or deleted. Folders the gate made that no run used for 14 days are removed when D: has
under 300 GB free.

**Locks.** One gate run at a time: `gate.lock` is the queue runner's (and a run's by hand). An agent's own build and unit tests in
its own work folder take no lock: no verdict depends on the clock, and the speed measure runs at night under `measure.lock`.
The old rule that own work takes the gate lock, and its helper `lockrun.py`, are gone.

**A lock whose holder is gone.** Every lock folder (`gate.lock`, `long.lock`, `move.lock`, the landing runner's lock, the queue file's lock, `measure.lock`) names its holder in `holder.txt`, and one reader (`tools/gate/lockholder.py`) reads them all: `pid N` anywhere in the line (`full gate of Tree@abc pid 67700; covers ...` is one), the last `pid N` being the holder's. A line written by gate.py, the landing runner or measure.py also holds `since T pid N`, T being the process's start in seconds since 1970. A holder is gone when its process does not run, or runs from another start than `since` (the number was used again). The next run takes such a lock over and prints one line naming the old pid (`the gate lock <path> was held by pid N, which has ended: taken over`); a lock whose holder lives is waited for; a line with no `pid` is a lock taken by hand and is never taken over. Seen 2026-11-21 16:50: a killed gate's lock held for ever because the reader took `pid 67700;` for no process. Shown the other way on 2026-11-21 20:20: a landing's gate was ended with its first checks running, and the next gate took its lock over with the line above and landed.

**The compiler cache's own failures.** On 2026-11-21 one job of about 4,700, in a build from nothing with the compiler cache's server running throughout, ended with a line of the cache's own client (`sccache: error: Only one usage of each socket address ... (os error 10048)`) and no compiler message; nothing more is known of the cause. `tools/gate/build_steps.ps1`, used by `build_windows.ps1` for every Windows build, looks at each FAILED block of a failed step's log (from `FAILED:` to the next `[n/m]` or `ninja:` line). When every block has a line starting `sccache: error` and no compiler or linker diagnostic (` error C`, `error LNK`, `: error:`, `fatal error`), the step is run once more by itself (it builds only what is missing) and one line says `<step>: N job(s) failed in the compiler cache, not in the compiler; built again`; the second result is the step's. A second failure of that kind ends the script with exit 2 (`build not judged: ...`), and the gate gives `build/windows` the kind `tool`, never `fail`: it counts toward no failure signature. Any real diagnostic is never run again and stays `fail`. Each build run again is one line (kind TOOL, check `build/windows`) in `D:/NebulaWork/plan/native/tool-faults.tsv`. The first log of a step run again stays as `<log>.cache-error`. A build that fails for a real reason is named in the gate's line: the count is the script's own (the sum of the steps' `N FAILED`) and the detail is `<step>: <first compiler or linker message of the first failed step>` (else its first `FAILED:` line), cut to 200 characters, so three different compile errors are three different failures.

**The game folder's settings.** A build folder is kept between runs, so a folder built before with other settings meets a run that asks for others (the long run's sanitizer folder had `address`, the run asked for `address,undefined`). `build_windows.ps1` reads `NEBULA_SANITIZE` and `NEBULA_COVERAGE` from the game folder's `CMakeCache.txt` (`Get-Game-Config` and `Game-Config-Differs` in `tools/gate/build_steps.ps1`) and, when either differs from `-Sanitize` and `-Coverage`, configures the folder again with the ones asked for, as it does for a folder configured with another compiler launcher. A folder that after configuring still differs is refused (`game: ... was configured with ..., asked for ...`, exit 1), and a configure that fails is a failed `game configure` step: a build never runs with other sanitizers or another coverage setting than asked. Self-test: `tools/gate/tests/build_steps_test.py` (`GameConfig`).

A landing's output is line-buffered (`gate.unbuffer_output`) and its children run unbuffered, so each check's line is on the screen, or in the file the output goes to, when the check ends.

**A self-test never reaches the live state.** The live state is `D:/NebulaWork/plan` (board, locks, the landing queue and its runner lock, the failure store, the step tool's folder with its claims, landings and repairs) and the files directly in `D:/NebulaWork/build/gate-runs` (the history; a run folder and what is below it are free). A self-test that reads or writes them behaves differently inside a real landing (the runner's lock is held) and, later, when an agent has filled them. `tools/gate/selftest_guard.py` makes it impossible: a `sys.addaudithook` hook refuses every event that opens, lists, makes, renames, copies or removes a path there, appends `<event> <path>` to the file named by `NEBULA_SELF_TEST_LOG` and raises `RuntimeError`. The `harness/self` and `description/self` checks start every script through `tools/gate/tests/guarded.py` with `NEBULA_SELF_TEST=1` and a log of its own; a script whose log is not empty fails whatever its exit code, and the check's detail names the script and the first path. Every tool a self-test starts as a child process calls `install_if_asked()` first in its `main()`, so the guard follows it. There is no exception list: a test that trips gets temporary paths (`gate.py land --runner-lock`, `--queue-file`, `--failures`, `--live`, `--landing-tree`, `--board`, `--status`, `--gate-lock`).

**The machine (package T5).** A run that starts the game first asks the machine whether it can carry the run (`machine_ready` in
`gate.py`, the readings in `tools/gate/machine.py`); a run of documents, builds and tools only never asks. The points come in
this order, and the first of points 1, 2 and 4 that cannot be met, or a failed push in 3, ends the run as ENV (not judged, never FAIL) with one plain line:

1. Leftovers. A game process under `D:/NebulaWork/` whose parent is gone and that is older than ten minutes is ended and named in the
   run's summary. A process a held lock (`gate.lock`, `long.lock`, `measure.lock`, `move.lock`) may own is never ended.
2. Disk. Drive D: needs 30 GB free for a landing (and the quick gate), 80 GB for the full run (`--main`), 150 GB for the nightly run.
   With less, the old run folders are thinned (the three newest passing and the three newest failing runs of each level stay whole;
   of every other run only the files at its top level stay; a run still going or waiting for GitHub is left alone) and the drive is
   looked at again.
3. The tablet, when the run has a tablet part and it answers adb: the test data on it (the test app's `game.pak`) has this tree's
   store version (`runtime/include/galaxy/store_version.inc`); data of another version is pushed again (`luma_test_android.py
   push-data`). A tablet that does not answer or is not authorised stops nothing: its part is owed (section 4.3) and the rest of the
   run is judged.
   **The game data stays on the tablet between runs (B14 part 2).** The test app is removed after every use (the tablet's measure stands
   alone), but the 4.0 GB of game data are kept at `/data/local/tmp/nebula-test/game-<key>.pak` (the key the gate gives the data, as in
   `RMGE01-37-3aa6533b1f08`) with `game-<key>.pak.info` beside it (the size and a hash). `luma_test_android.py prepare` installs the app and
   fills its own game data file by a copy on the tablet (`run-as ... cp`; where the tablet refuses that, a shell line that pipes the kept
   file into the app), then checks the size. A key with no kept copy, a kept file of another size than the recorded one, or one with no info
   file is pushed (`adb push`, once for each version of the data), its hash made on the tablet (`sha256sum`, else `md5sum`, else the size only,
   and a `machine:` line says so) and compared with the PC's; unequal: one more push, then exit 3 (kind `tool`, both values in the line). A
   copy of another key is removed first: one kept copy at a time. With less free room (`df /data`) than the kept copy, the app's copy and 1 GB
   the run goes the old way (a temporary file pushed, copied in, removed; nothing kept) and a `machine:` line says so; a run never fails for
   room. The `machine:` lines of `prepare` join the landing's machine notes, and the landing's start says `game data kept on the tablet:
   <key>, 4.0 GB` or that none is kept. The tablet's measure, which installs the app itself, gets the same copy. `luma_test_android.py remove
   --data` removes the kept copy and nothing else; it is run when a run of work is over. Only `/data/local/tmp/nebula-test/` and the package
   `dev.nebula.galaxy.test` are touched.
4. The graphics card and the PC's memory. The card the game uses is the first one DXGI lists for high performance (what the game
   itself takes; `LUMA_TEST_GPU` names another); its total is DXGI's dedicated memory and what every program holds of it is Windows'
   counter `GPU Adapter Memory`, read by its English name through PDH (any maker's card, any language). One game copy takes what the
   gate has seen: the largest use of any game process on the card in each of the last 20 runs, per renderer, kept in
   `<plan>/native/machine.json` (3 GB until a run has shown it). The run plays as many copies at once as fit in 70% of the card's
   free memory and in 70% of the PC's free memory at 2 GB a copy, at most what its lanes ask for. When other programs hold more than
   half the card the summary says "card shared"; every result is judged as usual. When not even one copy fits the gate looks once a
   minute, says so every ten, and after four hours ends the run as ENV.

The gate puts itself in one Windows job object that ends everything in it when the gate ends or is killed, so every program a run
starts, and theirs, goes with it. A program that must outlive the run (the long run a move starts, the
tablet's adb server) is started with the leave-the-job flag (`machine.start_outliving`). So are the servers that other builds
share: the gate starts the adb server before its first adb call and the compiler cache's server (sccache, port 4227, the build
script's settings) before its Windows build, when none runs, so neither is ever inside a run's job. The Android build calls Gradle
with `--no-daemon` (`platform/android/build_apk.ps1`), so no Gradle daemon is left to be cut off. Not on Windows nothing of this is
done.

## 16. Time limits in the test runner (package T6, runner part)

No verdict depends on how long something took. A test that is slow on a busy machine is never `fail` because of it; it is `fail`
only when the game itself stopped. This section lists every time limit in `tools/lumacore/luma_test.py`,
`tools/lumacore/luma_test_android.py` and `tools/gate/gate_tour.py`, what it guards, and what happens when it passes. Lines are
those of the T6 branch.

**How the runner sees that the game is moving.** The game's host writes one line a second into the run's log while the game takes
steps: `[  12.345] host: frame 721, file reads ...` (`native/host/host_main.cpp`, the main loop; the number is the game's own step
count, the time is the host's clock). The line is written on every platform; on a tablet it is in the device's log under the tag
`Nebula`. A new line is "a step was completed". The runner reads it from `run.log` as the file grows (`luma_test.Progress`), and on a
tablet from the log of the game's process every 2 seconds. The host writes no line before the game's first step and none while no step
is taken, and the host's line `boot: main at` starts every process, so the start-up of a game, and of the game that takes its place
after a restart, is nobody's pause.

**What the runner does with it.** The count in the host's line is the game's own step count (`frame`), not the pictures drawn (that is `renderer frames`, a field of its own).

| What happened | Result | Kind |
|---|---|---|
| The game's process was alive and no step was completed for 120 seconds | verdict `hung`; the dump step is called first (it names the stall dump the host wrote, `nebula-hang-*.dmp`, which only a Windows test run writes; the game writes none on a tablet or elsewhere); the process is ended | `fail` |
| A pause of more than 10 and less than 120 seconds | a line `stall <seconds> s at frame <n>` in the test's `result.json` (`"stalls"`) and on the console under the test's line; the test is judged as if it had not happened | none: counted |
| The run's time limit passed while the game was in a pause (no step for 10 seconds) | nothing is decided yet: the runner waits on. If steps come again, the row below applies then; if the pause reaches 120 seconds, verdict `hung` as above, so a short limit never turns a hang into `env` | `fail` if it hangs |
| The run's time limit passed with the game stepping, and the game's speed over the run (its steps divided by its seconds; the game steps 60 a second at full speed) was 80% of that or more (48 a second) | verdict `not-reached`: "its end was not reached in N s although the game ran at full speed"; the machine was not the reason | `fail` |
| The same, with the speed under 80% | verdict `too-slow`: "too slow on this machine now: still moving when its time limit of N s passed (the game ran at P% of full speed)". The gate runs the test again alone at the end of the run, as it does a fail (`kinds.py`, `Settler`): if it passes alone it is a `flake` (the machine was busy); if it is too slow again, or fails, it is `fail` with "too slow even alone: the game ran at P% of full speed" (it is not run on main's build). Slowness never ends as `env` twice | `env` for now |
| The run's time limit passed and the game never took a step (it never started stepping) | verdict `timeout` | `fail` |
| A picture a step asked for, or the loss report, is not there and complete 60 seconds after the game ended by itself at its end step | verdict `output-missing`; nothing else was wrong | `tool` |
| On a tablet, Android's record of how the game's process ended did not come within 60 seconds | verdict `no-exit-record`: "the tablet gave no exit record" | `tool` |

The runner ends with a line `summary: N results (pass A, fail B, tool C, env D); stalls: S`. A pause is measured between two step
lines on the host's own clock, so how late the runner looked does not change it.

**The limits.** "Same" means the length is kept. "Now" says what it means since T6.

| # | Where | Length | What it guards | Now |
|---|---|---|---|---|
| 1 | `luma_test.py` 489 | `limit_seconds` of the test, 180 when it names none (the tests name 120 to 480) | when the runner looks at how the run stands | same length; judged by pause and speed as in the table above: `hung`, `not-reached` (fail), `too-slow` (env, then run again alone) or `timeout` (never stepped) |
| 2 | `luma_test.py` 1777 | the limit plus 150 seconds, as the game's own `--seconds` (was the limit) | a safety net: the game ends by itself if the runner is gone | longer by 150 s, so that the runner and not the game decides what the limit means; the game waits out a pause of 120 s past the limit |
| 3 | `luma_test.py` 1805, 1988, 2002 | the limit (was the limit and 60 seconds), for each process of a run (a restart gives the new process its own) | the runner ends the game at the limit | the runner ends it at the limit, or waits on if the game is in a pause; judged as in 1 |
| 4 | `luma_test.py` 145 | 120 seconds with no step, process alive | a game that has stopped | new: `hung` (fail); the host's 10-second stall dump no longer fails a run |
| 5 | `luma_test.py` 146 | 10 seconds with no step | a pause worth a line | new: a `stall` line (was a failure: the check "the game never stopped making frames") |
| 6 | `luma_test.py` 147, 2685 | 60 seconds, from the game's end | the pictures and the loss report the game writes at its exit | new: waited for; absence is `tool` (was `fail`, with no wait) |
| 7 | `luma_test.py` 699 | 600 frames, a step's `timeout` when the test gives none | a wait for a fact that does not come: a frame count, not time | same; it is the test's own stop, `fail` |
| 8 | `luma_test.py` 2798, 2994 | the limit times `--limit-scale` (at least 1) | a renderer slower than the limits were set for | same |
| 9 | `luma_test.py` 1760 | the limit, as the length of the sound recording | how long the game's sound is kept | same; no verdict |
| 10 | `luma_test.py` 2070 | 60 seconds | waiting for a game that was ended at its limit to be gone, so no process holds a file | same; no verdict |
| 11 | `luma_test.py` 2248 | 2 seconds | the image of a process that has just ended closes a moment later (removing the run's copy of the game) | same; no verdict |
| 12 | `luma_test.py` 2257, 2848 | the limit | waiting for the game's crash watcher (a second copy of the game) before removing the run's copy | same; no verdict (a line is printed) |
| 13 | `luma_test_android.py` 62 | 60 seconds (was 5) | Android's exit record of the game's process | now waits for the record; absence is `no-exit-record` (tool) |
| 14 | `luma_test_android.py` 341, 350, 388 | the limit (was the limit and 60 seconds) | the tablet's game that did not end | as in 3 |
| 15 | `luma_test_android.py` 377 | 15 seconds | the new process after a restart (`restartGame`) to appear | same; without one the run ends there |
| 16 | `luma_test_android.py` 78, 82, 86 | 600 seconds per adb call | a call that does not answer | same; a call that times out ends the test as `tool` (the runner raised), or `env` when the device no longer answers |
| 17 | `luma_test_android.py` 453, 459 | 3600 and 1800 seconds | pushing the game data to the tablet, and copying it in | same; not a verdict on a test |
| 18 | `luma_test_android.py` 67, 542 | 60 seconds for the game process to appear, `--limit` 300 seconds for a start check | the push workflow's start check | same; the start check is not a test |
| 19 | `luma_test_android.py` 64 | 2 seconds | how often the game's step lines are read from the tablet's log | new; no verdict |
| 20 | `gate_tour.py` 81 | 240 seconds per stop | the longest a stop of the tour may take | same; judged as in 1 |

Not time limits: the runner's polling pauses (0.1 s for a process, 0.2 s for a file at the end, 0.25 and 0.5 s on a tablet).

**Limits in the game that the tests meet.** These are in the game, not in the runner, and T6's runner part does not change them.
They decide whether the game itself writes something, and so what the runner then finds; the unit-test and game part of T6 owns them.

| Where | Length | What it guards |
|---|---|---|
| `native/host/host_main.cpp` 4725 | 5 seconds | the game's wait at its exit for the pictures a test asked for, taken from the card; after it the game ends without them (the log line "the pictures asked were not all written at exit") |
| `runtime/include/galaxy/render/render_core.h` 579 (`kProbeWaitNs`) | 2 seconds | the game's step waiting for the render thread's screen probes; the Vulkan device restart tests meet it at exit (N-1078) |
| `native/host/host_main.cpp` 4581 | 10 seconds | a process that takes the place of another waiting for the files the old one hands over |
| `native/host/host_main.cpp` 4293 | 2 seconds | a fixed-step run waiting for the window to take a new size |
| `platform/sdl/host_window_sdl.cpp` 1677 | 2 seconds | the log line "the renderer kept the surface past 2 s" |
| `native/host/host_main.cpp` 6570, 6573 | 10 and 60 seconds | the host's stall dump after 10 seconds with no frame, left out for up to 60 seconds while a new graphics device is made |

Since the runner no longer judges by the host's 10-second dump, a run in which those fixed waits run out is `fail` only when the
game then did something wrong that a check sees. A picture that was never written is `tool`, because the runner waited for it.

---

## 17. The unattended run: mends, the strictness check, what no tool may wait for (brief U)

The agent of the native plan works for days with nobody there. These are the tools that make it possible; the rules the agent
follows are `docs/plan/native/AGENT.md` (sections 3, 7, 9, 13, 14) and, for the helper that mends a tool,
`docs/plan/native/TOOL-MEND.md` with the recipes of `docs/plan/native/TOOL-MEND-PLANTS.md`.

**The mend** (`tools/plan/plan.py`): `mend open [<step>] "<title>" [--last] [--check C]` writes the fault sheet
`D:/NebulaWork/plan/native/mends/M-00N.md` and puts the step in work into the state "waits for M-00N" (still `started`, not
blocked, outside the three-failures and four-hours rules); one mend is open at a time, and `next`, `start`, `ahead` and `land`
answer `WAIT: mend M-00N is open`. `mend land M-00N` (the helper's, from its own folder on the branch `mend/M-00N`) runs the
references check, the code checks and the strictness check, then hands the branch to the gate with `--who "mend M-00N"`; its
landing record is `landing-mend.tsv`, so it never meets the agent's own. `mend close M-00N <commit>` needs a commit on `origin/main`
that touches `tools/`, `docs/` or `.github/`; the waiting step is in work again and every step blocked `tool: M-00N` is open
again. `mend failed M-00N "<why>"` counts the attempts on the sheet (the helper, a second helper, the agent itself): only the third
failure blocks the step. `mend drop` ends a mend whose fault needs game code (not a tool fault). A mend opened for a repair (`mend open R-00N ...`) ends that repair
when it is closed (`mend close`): the repair is `landed`, closed by the mend's commit, with the note `ended by mend M-00N`; the same end is made by `mend open R-00N ... --landed <commit>` and by `repair end R-00N --mend M-00N` (a closed mend of that repair, the repair still `open` or `started`; refused for a mend that is not closed, a mend of another step, a repair that is landed already and a repair that waits for another, open mend); its landing
(`mend land`) runs the failed check as an extra check where the gate can add it (`also_for_check` in `plan.py`: the sanitizer, stress, soak,
replays/software, replays/rates, speed/anchor). `mend open ... --landed <commit>`
records a mend that was landed by hand because `plan.py` itself was at fault. `mend list` lists them (mends.tsv in the live folder);
the page shows each with its attempt. `decide <ID> "<what>" --because "<why>" [--doc "<file, section>"]` appends a row to
`decisions.tsv` in the plan's folder (docs/plan/native). `check` runs what `land` checks before it queues (the ledger, the file references, the replay
rule, the code checks, the strictness check, the rule on test tools and GitHub's jobs) on the work folder and queues nothing; `land`
refuses in seconds a step branch that changes a test tool, a GitHub job, `approved.tsv`, `steps.tsv` or `step-checks.tsv` when the
card does not name the file. A new `land` of a step takes the tool's own earlier `Plan: step X landed` commit off the branch before it
makes the new one (a failed landing left it, and the code commit it names was no longer the step's); main's history stays as it is.
`alive` prints `working` (a command in the last three hours, or a landing or mend landing in the gate's line), `ended` (a note
beginning `Done` or `Stopped`, or every step landed) or `stale`; every command but `alive` writes `heartbeat.txt` in the live folder.
When a landing has passed, `land` prints the speed checks' lines and keeps them in `landings.tsv` (column `figures`) and on the page.

**The strictness check** (`tools/gate/strictness.py`, the gate's check `repo/strictness`, and `plan.py land`, `check` and `mend land`
in seconds): `tools/gate/strictness.tsv` lists the places whose change can make a check see less (pattern, mode `added`, `removed`
or `any`, `*` or a named list inside a file, what it could hide): the known-failure, planned-check, sanitizer and debug-layer
lists, the allow-lists, `scope.tsv`, `github_jobs.tsv`, the approved pictures, the recorded replays, `step-checks.tsv`, the engine
suite and the tours' stops, GitHub's workflows, the census, cast and byte-order checks, the speed band rule, the push guard, the
guard itself, the never-traced list, the flake limits, the repository audit's lists, and every changed or deleted line of an
existing self-test of the tools. A landing that changes one needs, in the same landing, one line in `docs/plan/strictness-log.tsv`
(date, file, what changed, why, allowed by, id, removed by); the log only grows. The gate judges each branch it covers on its own
against main; a `mend/*` branch may not change source files of the game, the engine, the importer or the platform code (their build
files are free). The check runs in every landing, because `scope.tsv` puts the group `repo` in every row that can name a listed file
(a self-test pins it). Outside the repository nothing is judged: the speed bands of the pair and the accepted slowdowns live in the
gate-runs folder, and `pair-accept` is the owner's word only.

**The tablet's measure after a gap.** `measure/step/tablet` compares with the median of the last ten trusted lines of the same key in
`measure-history.tsv` (the band rule of `tools/measure/band.py`). While the tablet was away its checks were owed, so the history has no line for
those landings (`tablet-owed.tsv` lists them). A figure `worse` against that older history, in the first landing after the gap, is `owed`
(`the tablet's history is N landings old`), not `worse`. After the move the gate runs `measure.py accept --commit <this landing> --renderer
tablet`, the tool's own restart of a baseline (the tablet's lines before it stop counting, its `worse` marks become trusted), and opens one
repair (source `measure`, kind `worse`, check `measure/step/tablet`) that holds the loss with the gap's landings and the figures. The next
landing is judged against the figures from this landing on.

**Planted faults** (`TOOL-MEND-PLANTS.md`): a branch named `plant/...` is judged by a gate command without `--move`, is never pushed
(the push guard refuses the ref) and is never landed (`gate.py land plant/...` is refused before anything is queued).

**Nothing waits for a person.** `GIT_TERMINAL_PROMPT=0` and `GCM_INTERACTIVE=never` and the Windows error mode (no crash box;
`tools/gate/noprompt.py`) are set once at the start of `plan.py` and `gate.py` and inherited by every child; the game host sets its
own (`SetErrorMode`, no C-runtime assert box). The gate builds the release runtime (no assert box); a debug-runtime program is not
started by any tool. A push to GitHub that is refused for the sign-in is the environment (kind `env`, check `github/sign-in`):
nothing is landed (the push is the move of main), the landing is not judged, and the page says "GitHub sign-in needed". The agent's
own commands never start a game beside a running speed measure: the build, the engine test and the tour wait up to 40 minutes and
then start nothing (`measure_wait.py --stop`, exit 3).

**Locks.** `gate.lock`: a holder whose process has ended is taken over with one line; a line that names no process (taken by hand)
is waited for up to the run's `--wait` (240 minutes), then the run is not judged. `tablet.lock`: its holder line now carries the
holder's `since T pid N`, and a holder that has ended loses it with one line (before, a run that died with it kept the tablet away
for ever); a lock taken by hand holds. `measure.lock`: a dead holder is taken over; a hand lock stops the measure (owed) and makes the
agent's builds wait their 40 minutes. The page names any lock of `gate.lock`, `tablet.lock`, `measure.lock` or `long.lock` that has
been held for more than 30 minutes by a line that names no process, or by a process that has ended. No tool takes a hand lock.

**A queue runner that dies** during a landing leaves its row `running`; the next `land` takes the dead runner's lock over, puts
the row back to waiting and runs the whole batch again (the children ended with the runner's job object).
