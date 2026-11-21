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
| GitHub | Every `must` job of the push workflow (tools/gate/github_jobs.tsv: Mac, Linux, Linux unit tests, both sanitizer jobs, Rust on both systems) is green for the commit, when the landing's plan has the group `github` (section 8) |

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

- Each job of `.github/workflows/check.yml` has a class in `tools/gate/github_jobs.tsv`: **must** (Mac, Linux, Linux unit tests, both sanitizer jobs, Rust on both systems), **nightly** (the two Windows jobs and the two Android builds, about 50 minutes each: this machine builds and tests them itself, and a plan with the group `android` runs the Android build here) or **advisory** (the Android start check). A job the table does not name counts as must and the run says so.
- A landing or move whose plan has the group `github` (tools/gate/scope.tsv; a run with no plan has it) pushes the candidate branch, and **main does not move until every must job is green for the commit.** The gate waits for them after its own checks (up to 120 minutes, outside the lock). A job's state is read from every push run of that commit, under any branch name, and the newest run that has a result wins. A plan without `github` (documents, tools, test data, pictures) pushes nothing and waits for nothing.
- A red must job refuses a run before it starts, and stops it while it runs (the job's log, its last 200 lines, is saved in the run folder as `github-<job>.log`). A job that ended cancelled or startup_failure, or red at a set-up (`uses:`) step (checkout, a set-up action, a cache, an upload), is run again once; ending that way twice, or the time limit, is `env`: not judged, main does not move, the branch stays in the queue. A red advisory job is run again once and, red again, told in one line. When a `run:` step failed for a reason that is not the game (a download or an install), `--github-again` (on `gate.py land`, `--main --move` and `plan.py land`) runs every red must job again once before the start check, writes each to the tool-fault record as ENV, and judges by the new result. A main gate without `--move` waits for the same jobs up to the same limit.

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
  approved row), the pictures' files and the sheet to look at, `new-pictures/sheet/review.html`. The summary prints the count
  and the sheet's path.
- *Approval at the landing.* When a run with `--move` has passed (and GitHub is green) and left new pictures, the gate calls
  `pictures.approve_landing` (`gate.py approve_pictures`) before it moves main: it puts the files in the picture store, adds or
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
renderer within seven days, the test goes to `D:/NebulaWork/plan/native/set-aside.tsv` and the gate says to open a ledger row for it (the files live outside the repository, so a test run never changes the tree).
A test listed there still runs and is reported (`set aside` on its line), and its `fail` does not fail a run; the gate
prints the set-aside tests at the top of every summary, and a line leaves the file when its row is closed. The tablet's tests
are not run again (the test app is removed when the tablet's part ends).

**Check against main.** A test that is still `fail` after its second run is run alone on main's build, but only when its
own definition (its test folder, or for a generated tour the files that write it) is the same on `origin/main` and in the tree
under test; otherwise the check is skipped, with a line saying so, and the test stays `fail`. Main's build is kept by the
gate in `D:/NebulaWork/build/base-bins/<commit>/bin`, a copy made whenever the gate moves main (the two newest are kept); with
no copy for `origin/main` the check is skipped too. A test that fails there with the same first message is `main`: it does
not fail the run, it is printed first in the summary, and the gate says to open a ledger row for it.

Two runs of fixed steps that differ are a determinism fault in the game or the harness, and are treated as one.

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

---

## 15. Checks by what changed, the landing queue, game data by key and locks (package T4)

This section is what the tools do now. Where it differs from an older section above, this section is right.

**Checks by what changed.** `tools/gate/scope.tsv` maps path patterns to groups of checks (`tools/gate/scope.py` names the
groups once: `repo`, `tools`, `build`, `unit`, `start`, `engine1`, `engine3`, `tour1`, `tour3`, `import`, `android`, `tablet12`).
The group `repo` (the cheap repository checks) is in every run, whatever the rows say. Every row a changed file matches adds its groups; a landing runs the union over all changed files (`git diff --name-only` from the
merge base with `origin/main`). A change of documents only runs `repo`, with no build, so it lands in minutes. A change to the
game runs the build, the unit tests, a start on each renderer and the 12-stop tour on Direct3D 12 with its pictures and replays.
A change to the renderers runs the engine tests and the tour on all three. A file that no row names makes the run `full`:
everything the main gate runs on the native game (every renderer's engine tests and 102-stop tour, the other discs, the Android
build and the tablet's short part). The full set on three renderers, three discs and the tablet is also the nightly run's first
part. `python -X utf8 -I tools/gate/scope.py --base origin/main` prints the groups and the checks of the branch you are in.

**The mod suites are off.** The owner set all mod work aside until the native rewrite is done. The line `mods = off` at the top of
`scope.tsv` keeps the mod suites and LumaLink's own check out of every landing, the main level and the nightly run. `short` (the
quick mod set) or `full` bring them back; `gate.py --mods short|full` does it for one run. The test driver is itself a mod
(`tools/test_driver/luma_test/luma_test.c`) and every tour and replay loads it, so a driver that does not load is a failure.

**Landing.** `gate.py land BRANCH [BRANCH ...]` puts the branches in the queue (`D:/NebulaWork/plan/landing-queue.tsv`, changed only
under a lock folder beside it) and, when no queue runner is alive, becomes the runner; otherwise it waits for its own branches.
The runner takes every waiting branch in queue order, replays each branch's own commits on a temporary branch at `origin/main` in
`D:/NebulaWork/MarioGalaxyRecomp/Nebula-landing` (a branch that does not apply is `conflict`: not judged, the others go on), runs
the union's checks once on the tip, and on a pass approves new pictures, reads GitHub (section 8) when its plan has the group `github`, and moves main. On a
fail it halves the batch to find the branches at fault, lands the rest and names the ones it removed with their `KIND` lines. A
tool or machine fault lands nothing: every branch waits again. `gate.py land --queue` runs what waits. Exit codes: 0 landed, 1 fail,
2 not judged, 3 the same failure three times. The failure's signature (the first failing check and its first error line) is kept
for the branch (`tools/plan/failures.py`); the third in a row stops the retries. A tool or machine fault never counts toward it,
and is written to `D:/NebulaWork/plan/native/tool-faults.tsv` at once. `plan.py land` makes this one call. `--quick` and `--main`
still work unchanged until package T10 removes them.

**A repair's check.** `gate.py land BRANCH --also NAMES` (`plan.py land R-003` sends it) runs the named checks in this landing besides
what the changed files need. NAMES are check names as the gate gives them, comma-separated; they are the main level's, so a check
that only the nightly run has (another disc's tour, the full tours) can be named. They run as the main level runs them, every stop of
a tour, so a landing with `--also` runs its groups' checks at that depth too (`--main --only NAME` is how one nightly check is run by
hand; `--also` does the same inside a landing). A name the gate does not know refuses the landing before anything is queued or run
(exit 2, `VERDICT not-judged`) and lists the known names; a check that only the long run has (the soak, the sanitizer tour, the
coverage report, the play route measure) is refused the same way and said so. The names travel with the branch in the queue (the
column `also`), so a caller that only waits for another's runner still gets its checks; a batch runs the union of its branches'.

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
The old rule that own work takes the gate lock, and its helper `lockrun.py`, are gone. The measure's build copies are kept while a
measure uses them.

**The machine (package T5).** A run that starts the game first asks the machine whether it can carry the run (`machine_ready` in
`gate.py`, the readings in `tools/gate/machine.py`); a run of documents, builds and tools only never asks. The points come in
this order, and the first one that cannot be met ends the run as ENV (not judged, never FAIL) with one plain line:

1. Leftovers. A game process under `D:/NebulaWork/` whose parent is gone and that is older than ten minutes is ended and named in the
   run's summary. A process a held lock (`gate.lock`, `long.lock`, `measure.lock`, `move.lock`) may own is never ended.
2. Disk. Drive D: needs 30 GB free for a landing (and the quick gate), 80 GB for the full run (`--main`), 150 GB for the nightly run.
   With less, the old run folders are thinned (the three newest passing and the three newest failing runs of each level stay whole;
   of every other run only the files at its top level stay; a run still going or waiting for GitHub is left alone) and the drive is
   looked at again.
3. The tablet, when the run has a tablet part: it answers adb, it is authorised, and the test data on it (the test app's `game.pak`)
   has this tree's store version (`runtime/include/galaxy/store_version.inc`); data of another version is pushed again
   (`luma_test_android.py push-data`).
4. The graphics card and the PC's memory. The card the game uses is the first one DXGI lists for high performance (what the game
   itself takes; `LUMA_TEST_GPU` names another); its total is DXGI's dedicated memory and what every program holds of it is Windows'
   counter `GPU Adapter Memory`, read by its English name through PDH (any maker's card, any language). One game copy takes what the
   gate has seen: the largest use of any game process on the card in each of the last 20 runs, per renderer, kept in
   `<plan>/native/machine.json` (3 GB until a run has shown it). The run plays as many copies at once as fit in 70% of the card's
   free memory and in 70% of the PC's free memory at 2 GB a copy, at most what its lanes ask for. When other programs hold more than
   half the card the summary says "card shared"; every result is judged as usual. When not even one copy fits the gate looks once a
   minute, says so every ten, and after four hours ends the run as ENV.

The gate puts itself in one Windows job object that ends everything in it when the gate ends or is killed, so every program a run
starts, and theirs, goes with it. A program that must outlive the run (the play route measure, the long run a move starts, the
tablet's adb server) is started with the leave-the-job flag (`machine.start_outliving`). Not on Windows nothing of this is done.
