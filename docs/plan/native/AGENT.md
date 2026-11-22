# Rules for the agent of the native plan

You do the native plan: 138 steps, one after another, alone, until every step is on main. Read this whole
file before your first command, and again after every context compaction. Where this file and a step's card
disagree about WHAT to build, the card wins. Where they disagree about HOW to work, this file wins.

You work alone. Start no helper, no subagent and no second session. The owner is away and is not a programmer
of this code: do not ask him anything and never wait for him. Decide from the card, the design and the code,
and write down what you decided and why in the ledger row's note.

## 1. What this is

Nebula is a native port of Super Mario Galaxy for Windows, Android, Linux and Mac. Parts of it still have the
console's shape. The native plan removes them in 138 steps, in a fixed order, in 24 packages. Everything about
the plan is in `docs/plan/native/` on main: the order (`steps.tsv`), the reasons (`02-NATIVE-OVERVIEW.md`), the
step cards (`03-NATIVE-STAGE-0.md`, `04-NATIVE-LANE-R.md`, `05-NATIVE-LANE-G.md`), the ledger rows of each
package (`rows/`).

A tool hands out each step, tests it and puts it on main. You never choose what is next, never choose which
tests run, and never move main yourself. The test tools are finished and stand still while you work: your time
goes to the game.

## 2. Your folder, your tool, your log

| | |
| --- | --- |
| Work folder | `D:/NebulaWork/MarioGalaxyRecomp/Nebula-native` |
| The step tool, `PLAN` below | `python -X utf8 -I D:/NebulaWork/MarioGalaxyRecomp/Nebula-native/tools/plan/plan.py` |
| Your log | `D:/NebulaWork/plan/native/agent-log.md` |

Never write the option `--agent`. The tool prints its commands filled in for your folder: copy them.

Change files only in the work folder. Never in the owner's own checkout of the repository (the folder
`MarioGalaxyRecomp/Nebula` under his Documents), never in the test tools' folders (`Nebula-landing`,
`Nebula-long`), never in any other folder under `D:/NebulaWork/MarioGalaxyRecomp`.

The log is your memory across compactions. One line for every event, appended with the Bash tool
(`echo "<line>" >> D:/NebulaWork/plan/native/agent-log.md`):

`<MM-DD HH:MM> | <step> | <started / landing / ahead / landed in N min / failed: <check> / blocked / tool / note> | <one sentence>`

Take the time from `date "+%m-%d %H:%M"`, never from memory.

## 3. First thing in a session

1. `date`.
2. Ask the app to keep the PC awake: load `mcp__ccd_host__request_keep_awake` with ToolSearch and call it.
   If the tool is not there or it is refused, go on.
3. Read the last 30 lines of your log if it exists. Run `PLAN status`.
4. Section 4.

## 4. The loop

    PLAN next -> PLAN start -> read, change, prove, audit, commit -> PLAN land (7 to 15 minutes, PC and tablet)
                                                                        |
                              while it plays: PLAN ahead, and write the next step on top of this one
                                                                        |
    passed: the step is on main -> PLAN start <the next step> takes up what you wrote ahead -> ...

Every step is built on the one before it, and nothing reaches main that its landing did not pass.

### 4.1 One step, from start to end

1. `PLAN next`. It prints what you have to do now: a REPAIR card (a fault found after a landing: it comes
   before any step), the item already in work (`IN WORK`), or the next step (`NEXT`). If it begins with
   `LANDING RUNNING`, a landing of yours is on the machine: go to point 8 with that item. With the item come
   the card, the package's ledger rows, what the ledger must say before the step can land, the checks the
   landing will run, and the exact commands. A line `PREPARED AHEAD` means you wrote part of this step during
   the last landing: `PLAN start` takes that up.
2. `PLAN start <ID>`, unless the item is already in work (`IN WORK`). `start` puts the work folder on the
   branch `native/<ID>`, on top of main, and prints your commands (section 9). Use those commands as printed;
   do not compose your own. Start before you read: until `start` the folder holds the step before.
   An item already in work, after a restart or a compaction: the folder is on its branch, with your earlier
   commits and perhaps changes you did not commit. Read `git log origin/main..HEAD` with its diff, then `git
   status` and `git diff`. Keep what is right, undo what is not, and go on from there.
3. Read before you change anything:
   - the card, whole, and every row it names (`PLAN next` and `PLAN card <ID>` print the package's row table and
     name `docs/plan/native/rows/<package>.tsv`, which holds each row's full text; `PLAN rows <ID>` says which rows
     the card closes whole and which it only advances);
   - the design sections the card names ("FM" is `docs/engine/FRAME-MODEL.md`, "INV" is
     `docs/engine/CONSOLE-FREE.md`);
   - every source file the card's "Where" names, whole, and the callers of what you will change;
   - the card's "Read first" items: each one is a question the plan could not answer from its sources. Answer
     it from the code and write the answer into the design file in this same landing, before the code that
     depends on it.
   Line numbers in a card were taken at an older commit. Find the code by its names, not by the numbers.
   Some cards were written before the landing existed and say `quick gate`, `main gate` or `the gate`: each
   means the landing (`PLAN land`). You run nothing else for it.
4. Make the change the card describes: all of it, and nothing the card does not ask for. Something else you
   notice that is wrong gets a ledger row (section 10), not a fix in this step.
5. Prove it. Write the tests the card's "Proof" asks for, and run only the tests you wrote or changed, plus
   the build. Do not run the full test set yourself: the landing runs exactly what is needed, once.
6. Audit it before the machine does. Read your whole change once, line by line (`git diff origin/main`), as a
   reviewer who did not write it: each of the four renderers has its part; nothing of the old path is left
   that the card removes; nothing is changed that the card does not name; every new text is in 14 languages;
   every name in a document and in your commit message exists. A failed landing costs ten minutes and more;
   this reading costs two.
7. Bring the ledger up to date (`docs/plan/ledger.tsv`): each row the card closes whole is `fixed` with a proof
   text that names a test, a search or a measure; each row the card only advances gets a dated note. Commit
   (section 8). The work folder must hold no uncommitted change.
8. `PLAN land <ID>`, as a background command with a time limit of 600000 ms. For most steps a landing takes 7 to
   15 minutes. A step with an extra check (`PLAN next` lists them) is landed at the main level: every stop of
   the tour on every renderer and the other two discs, then each extra check one after another. `soak` adds 30
   minutes, `soak/long` two hours, each other one some minutes; one landing with the single extra check
   `speed/anchor` took 1345 seconds in all. A landing can also wait a few minutes for a piece of the long run.
   When the command is cut off before the landing has ended, or you no longer know whether a landing is running,
   run the same command again: it joins the landing that is running and never starts a second one.
9. While it plays: section 4.2.
10. When it has ended: act on its exit code (sections 4.3 and 5), write the log line, bring the progress page
    up to date (section 12), and go to point 1.

### 4.2 While the landing plays: the next step, written ahead

Do not wait for a landing with empty hands.

1. `PLAN ahead`. It makes the branch of the next step, `native/<M>`, in the work folder, on top of the commit
   that is being tested, and prints `AHEAD: <M>`, that step's card, rows and checks, and your commands.
   If it prints `nothing can be prepared ahead: ...` there is no step that can follow yet: join the landing
   (point 8 of 4.1) and wait for it. Any other refusal of `ahead` says why, and none deletes a branch or a
   commit. One that names what to do (uncommitted changes: commit them; the folder on another branch: the
   `git checkout` it names; `do not apply`: the paragraph under the table of 4.3): do that and give `ahead`
   again. Any other: nothing is written ahead this time. Never make a step's branch by hand.
2. Work on `<M>` exactly as in 4.1 points 3 to 7: read, change, build, prove, audit, ledger, commit. You are
   writing on top of a step that has not passed yet; that is intended.
3. Three things you do not do for `<M>` yet: `PLAN start <M>`, `PLAN land <M>`, and a push.
4. What you run beside a landing. The build and a unit-test program: at any time (the build waits by itself for
   the minutes in which the landing measures the game's speed, at most 15 minutes, and says so in one line; a
   unit-test program does not wait). An engine test and a tour: not in a landing's first 15 minutes. Its own
   tests are playing then; yours beside them would slow the landing and could fail for the load alone. Read,
   write, build and audit in that time, and run them when the landing has ended, or after those 15 minutes when
   it still plays (an endurance run is left; it is made to play beside other work). A test of yours that failed
   while a landing was playing: run it once more when the landing has ended, before you look for a cause. That
   is the only case in which a second run is right.
5. The landing's command ends or is cut off while you work. When it is cut off (no verdict printed), give it
   again, as a background command: it joins from the branch you are on. Do not leave `<M>`'s branch for that.
6. If `<M>` is whole and committed and the landing still plays (a long extra check): read the card and the
   sources of the step that follows `<M>` in `docs/plan/native/steps.tsv` (`PLAN card <ID>`). Write nothing
   for it. One step ahead is the limit.

### 4.3 When the landing has ended

`<N>` is the step that was tested, `<M>` the step you were writing ahead.

| The landing of `<N>` | What you do |
| --- | --- |
| passed (code 0) | Log line, progress page. `PLAN next`. It gives `NEXT: <M>` with `PREPARED AHEAD`: `PLAN start <M>`. Your commits on `<M>` are taken up on top of main. (Uncommitted changes to tracked files stop `start`: it says to commit first, because main holds `<N>` under other commits and `<M>`'s have to be moved; commit as below and give the command again. Files git does not track stay as they are.) Go on where you were in 4.1. |
| passed, and `PLAN next` gives a REPAIR first | Commit what you have of `<M>` (below). `PLAN start R-00N`, do the repair (section 6). `<M>`'s branch is kept and comes back with `PLAN next` after it. |
| failed (code 1) | `<N>` comes first. Commit what you have of `<M>` (below). `git checkout native/<N>`. Find and fix the fault (section 5), commit, `PLAN land <N>` again. Then `PLAN ahead` again: it puts `<M>`'s commits on top of the mended `<N>`, and you go on with `<M>`. If your fix changed something `<M>` builds on, read `<M>`'s change again. |
| the same failure three times (code 3) | Commit what you have of `<M>` (below). `PLAN blocked <N> "..."` (section 5). Then `PLAN next`. If it gives `<M>`: `PLAN start <M>` puts `<M>`'s commits on main without `<N>`'s; what you wrote may lean on `<N>`: read your whole change against main (`git diff origin/main`) and mend it before you go on. If it gives another step, `<M>`'s branch stays as it is for later. |
| not judged (code 4) | Section 9, "When a tool fails". Landing `<N>` once more works from `<M>`'s branch as long as `<N>` needs no new commit; your work on `<M>` goes on meanwhile. Where `<N>` itself must change (its branch no longer applies to main): commit what you have of `<M>` (below), `git checkout native/<N>`, do it, `PLAN land <N>`, then `PLAN ahead` again. |

`PLAN ahead` or `PLAN start <M>` can say that the commits of `<M>` `do not apply on top of` `<N>` or of main:
your fix of `<N>`, or a repair that landed in between, changed lines that `<M>` changed too. The branch is
as it was. The line prints the one
command to give in the work folder (`git rebase --onto ...`; for a step that was kept on its branch while main
moved, `git rebase origin/main`): copy it. For each conflict read both sides: what was tested or landed
stands, and `<M>`'s change is made again on top of it. `git add` the files, `git rebase
--continue`, then give `ahead` or `start` again, as the line says.

Committing unfinished work of `<M>` so that the folder is clean: an ordinary commit on `native/<M>` whose
message says plainly what it holds and what is missing. It need not build. It lands later as part of `<M>`;
only the last commit of a step is tested.

A step that has taken more than four hours of your work without being handed to a landing: stop and decide,
in one log line. Either one thing holds it: name it and give it one more hour. Or the card cannot be done as
written: `PLAN blocked <ID> "<what is done, what is left, what stands in the way>"`.

## 5. What an exit code of `PLAN land` means, and the only thing to do

| Code | It means | Do this |
| --- | --- | --- |
| 0 | Landed. Main has your step. | Section 4.3, first row. |
| 1 | Refused, or a check failed. The lines starting `KIND` name each failed check and test; the line `result: <folder>\result.json` before them names the folder of the run. | A refusal names what is missing (a ledger row, an invented file reference, an uncommitted change, the wrong branch): supply it. For a failed test: open its run folder, read the log and the pictures, and find the cause in your change before you edit anything. Fix the game, never the test (unless the card changes what the test checks). Commit, land again. Never land the same commit twice hoping for another result (the one exception is under `worse` below). |
| 2 | The step tool did not do the command. Its line starts `plan.py:` and says why: the command or the id was wrong, a file of the plan cannot be read, or git failed. | A wrong command or id: correct it. A message `ledger line <N>: ...` names a line of `docs/plan/ledger.tsv` by its number: you wrote that line; give it exactly the 7 tab-separated columns, commit, and give the command again. Anything else: give the same command once more. The same line again: section 9, "When a tool fails", point 5. |
| 3 | The same failure three times in a row. | Stop working on it. `PLAN blocked <ID> "<what fails and what you tried, in two sentences>"`. Then `PLAN next`. |
| 4 | Not judged: a test tool or the machine failed, or your branch no longer applies to main. Nothing is wrong with your step as far as anyone knows. | Section 9, "When a tool fails". |

A kind you may see in the output:

- `fail`: your change or the game is wrong.
- `worse`: the speed check found a figure slower than the last judged build's, three rounds in a row. It is
  treated like `fail`: find what your change costs and remove the cost. A step may not make the game slower.
  The line names the renderer and the figure: `cpu_game_ms` is the game thread's work for one step,
  `cpu_render_ms` the render thread's, `cpu_work_ms` both together; `render_ms` and `gpu_ms` are the time to
  draw a picture, on the processor and on the graphics card; `gap_p99_ms` and `late_per_min` say how evenly
  the pictures arrive; `memory_mb` is the memory in use. The check can be wrong: two builds of the same code
  have measured up to 5% apart in one round. So when you have read your change against the figure and the
  renderer named, and no line of it runs in that work, land the same commit once more; this is the one case
  in which that is right. Worse again in the same figure: the cost is real, look until you find it. Passed:
  record it with `PLAN tool "speed check: <figure> worse on <renderer> for <commit>; the same commit passed a
  second landing"`.
- `flake` (failed beside other runs, passed alone), `main` (fails on main's build too), `new` (a picture with
  no approved copy), `owed` (the tablet was away, or a measure could not be trusted this time): none of these
  stops a landing and none needs anything from you.
- `tool`, `env`: the test tool or the machine. Code 4.

After main has moved, GitHub builds the Mac and Linux programs. A red job there comes back as a REPAIR card
through `PLAN next`; you do not wait for GitHub and you do not read it yourself.

## 6. Repairs

A REPAIR card from `PLAN next` is a fault found on main after a landing (by GitHub's jobs, by the long run
that plays every stop of the game about once a day, or by a speed check). It comes before any step.
`PLAN start R-00N`, read the evidence folder the card names and the command the card gives to run the failed
test alone, find the cause, fix the game, and `PLAN land R-00N`. If the cause is not in the game but in a
test tool: `PLAN tool "<what the tool did>"`, then `PLAN blocked R-00N "tool: ..."`.

The card's line `Traced to:` names the landing that brought the fault, where the long run could find it by
playing the builds in between. `Traced to: no landing (not traced)` means it could not (a memory-fault
build, the endurance run, the tablet, a fault under load): for those checks the card's note only says that
they are never traced, and a repair of kind `stress` or `worse` has no trace at all. Where a trace was begun and
did not end, the note says why and lists the landings it left open. Finding the cause is part of the repair:
start from the evidence.

A repair of kind `stress` (a fault that shows only under load) or `worse` (a speed figure past its band) is never
given by `PLAN next` and stops nothing. It waits in `PLAN repair list` until the end of the list (section 13).

A repair can come while a step is in work (GitHub's jobs end after main has moved). `PLAN next` then prints
the repair and a line `KEPT:` for the step. Commit the step's work as it is (section 4.3, last part), then
`PLAN start R-00N`: the step is kept on its branch and nothing is lost. When the repair has landed, `PLAN
next` gives the step again and `PLAN start <ID>` puts its branch on top of the new main. A step prepared
ahead is kept in the same way. While a landing plays, `start` is refused: go on with 4.2 until it has ended.

The landing of a repair of a red GitHub job waits for GitHub's Mac and Linux jobs before main moves
(`--github first`), so it takes longer than a step's by the time GitHub needs for its builds. A repair of
another source lands with its failed check added as an extra check, like a step that has one. Work ahead
meanwhile.

## 7. Do not invent

- Every file, function and line you write about must exist: open it first. The landing refuses a commit
  message or a document that names a file or a line that is not there.
- Never write "passes", "identical" or a figure that you did not read from a result of this step's own runs.
  Give the run folder with every such statement.
- Before calling something unused, search the whole tree for it, including the build files, the Android and
  Apple hosts and the importer.
- If the card and the code disagree, the code is what exists. Follow what the card is for, and write the
  difference into the ledger row's note. If the card cannot be done as written, do not improvise a different
  step: `PLAN blocked <ID> "<what the card assumes and what the code has>"`, then `PLAN next`.

## 8. Rules of the code and of git

- The four renderers move together: Direct3D 12, Direct3D 11, Vulkan, Metal. A fix or a change written for one
  is written for all four in the same step. Metal cannot run on this PC; it is compiled and its unit tests run
  by GitHub's Mac job after main has moved. Write the Metal side with the same care as the three you can run:
  nothing on this PC will catch a mistake in it before the landing.
- The game plays exactly as before unless the card says otherwise. The recorded replays (`tests/replays/`)
  prove it. Record a replay reference again only when the card says the replay changes, with the command
  `PLAN start` prints, and say "replay reference" in the commit message. In every other step a replay that
  differs means your change altered the game: find why.
- When a step is meant to change a picture, declare it: a file `PICTURES-CHANGE.txt` at the root of the
  branch, one line each, `<test path or pattern> <picture or *> <renderer or all> :: <reason>`. Look at each
  changed picture yourself first (the run's sheet) and be able to say why it changed. The landing approves
  declared and new pictures by itself. A picture that differs and is not declared fails.
- The old path goes in the step the card names for it. Leave no switch, no fallback and no dead copy behind
  unless the card names one.
- New code has the form the card and the design give, never the console's form made to compile. "The console
  did it this way" is not a reason to keep anything.
- Text a player can read is added in all 14 languages.
- Fixes for Android are general, decided by what a device can do, never by a list of devices.
- Mods and LumaLink are out of this work and their tests are off. If your change breaks a mod service, keep the
  service's shape, mark the place, give it a ledger row, and go on.
- When the cooked game files change form, raise the store version as the card says. The landing makes the
  test data again by itself; section 9 ("The game data") says how your own commands get it.
- Git is `"C:/Program Files/Git/cmd/git.exe"` and no other. Commit with `git commit -F <a new message file>`.
  Never skip or disable a hook. No attribution lines of any kind in a message (no "Co-Authored-By", no tool or
  session names). Never push to `main`; only a landing moves it. Never touch tags, stashes, `refs/keep/*`, or
  the branches `mods/*`, `link/*`, `app/*`, `ray-tracing`, `f-rt-*`. The only branches you ever change are
  your own `native/...` ones.
- Write files with your file-writing tool, not with shell here-documents (they lose backslashes here). Run
  Python as `python -X utf8 -I`, never from a scratch folder.

## 9. The test tools: what they do, what they cannot do, and what to do when one fails

You use the tools. You never repair them. Do not edit `tools/gate/`, `tools/plan/`, `tools/measure/`,
`tools/verify/`, the test runners (`tools/lumacore/luma_test.py`, `luma_test_android.py`), the self-tests of the
tools (`tools/lumacore/tests/`, `tools/test_driver/tests/`), `.github/`, `tests/pictures/approved.tsv`, or
`docs/plan/native/steps.tsv` and `step-checks.tsv`, unless the step's card names the file.

### Your own commands

`PLAN start` and `PLAN ahead` print them filled in for your folder, and `PLAN commands` prints them again:

1. build the game for Windows in your folder (after a change only what changed is built);
2. run one unit-test program by name;
3. run one engine test by name on Direct3D 12;
4. play the tour's 12 stops on Direct3D 12 (or the 20 stops whose replays are recorded);
5. record the replay references from that tour run (only where the card says they change).

Each one that plays the game (3 and 4) hides the window, makes no sound, takes no keyboard focus and ends every
game process it started. The build (1), the engine test (3) and the tour (4) wait by themselves while a speed
measure is running on the PC: one line says so (`waiting while a speed measure runs ...`), and after 15 minutes
they go on anyway. The unit-test program (2) and the recording (5) do not wait. Never start the game any other
way. Run one that can take more than two minutes as a background command with a time limit of 600000 ms.

One more, for a document you changed: `python -X utf8 -I <your folder>/tools/verify/doc_refs.py <the file>`. It
lists every file and line the document names that does not exist. `PLAN land` runs the same on every changed
`.md`, `.txt` and `.rst` file and on your commit messages before it hands the branch to the gate, so make it
pass first.

**The game data.** Commands 3 and 4 carry the game data they run on (`--pak`): the US disc's data for your
folder's store version and importer, found by the tools. A step that changes the importer
(`crates/nebula-import/`) or the store version (`runtime/include/galaxy/store_version.inc`) has no data yet.
Then the printed commands show, in place of the path, the command that makes it (`gamedata.py make RMGE01
--importer ...`): build first, run that command once as a background command with a time limit of 600000 ms
(it cooks the whole disc; if it is cut off, give it again), then `PLAN commands` prints the commands with the
path. Never name another data folder yourself, never cook with an older script, and never delete a data
folder: the tools keep and clear their own, and the older ones are the owner's. A landing finds or makes the
data of every disc it tests by itself; a landing that has to cook takes longer by the cooks.

### What a landing runs

`PLAN next` lists the checks of your step. They are chosen from the files you change. The PC and the tablet
work at the same time.

| Check | What it proves |
| --- | --- |
| repository checks | no forbidden content, every script parses, the code rules, the settings table matches the code (the file and line references of your documents and commit messages are read by `PLAN land` before the gate, not here) |
| Windows build, unit tests, a start on each renderer | it builds and starts |
| engine tests with their pictures (one renderer, or three) | each part of the engine draws and behaves as approved |
| the tour: 12 stops of the game (every stop when the step has an extra check), pictures and replays | the game plays exactly as recorded, on each renderer |
| the debug layers (renderer changes) | no misuse of the graphics API |
| Android build; on the tablet a start, a speed measure, and for Vulkan and Android changes the 12 stops | it runs on a real device as it does on the PC |
| the speed check on the PC | the game's and the renderer's work for one step is not slower than the last judged build's (both builds run side by side) |
| extra checks, only where the card's proof needs them: under load (`stress`), the memory-fault build (`sanitizer/tour`), the endurance run (`soak`, `soak/long`), the software graphics device (`replays/software`), the comparison with the build of the plan's start (`speed/anchor`) | listed by `PLAN next` when your step has one |

A failed test is run again alone, and on main's build, before the landing judges: that is where the kinds
`flake` and `main` come from. You never need to run a test "once more to see".

About once a day the long run plays what a landing does not: every stop of the game on the PC and on the
tablet, the other discs, the memory-fault build, two hours of endurance. It plays in short pieces between
landings. What it finds red comes to you as a REPAIR card that names the landing it was traced to, where
it could be traced (section 6).

The machine tests one thing at a time. When your landing or one of your commands says that a lock is held and
that it waits, that is normal: your own landing or a piece of the long run has the machine. Wait. Never end a
process you did not start, never remove a lock, never stop a landing or the long run.

### What the tools cannot do (do not rely on it)

- Metal is never run on this PC: only compiled and unit-tested by GitHub after the move.
- A landing plays 12 of the game's 102 stops, on one disc (a landing with an extra check plays every stop on
  every renderer and the other two discs on Direct3D 12). A fault in another stop is found by the long run,
  up to a day later, and comes back as a repair.
- The speed check does not see a cost smaller than its bands (the bands, and what was planted and refused,
  are in `D:/NebulaWork/plan/TESTING-DONE.md`). "Not slower" in a card still means not slower: do not add a
  cost because it is small.
- The Vulkan debug layer is not installed on this PC: that one check is left out with one line. Direct3D's
  layers run.
- There is one tablet. A device that is away makes its checks `owed`; nothing is yours to do.
- Nothing checks what needs a person or other hardware (section 11's last point).

### When a tool fails

A tool is at fault when a result has the kind `tool` or `env` (code 4), when a command does not do what its
own help says, or when a check cannot see what the card asks it to prove. Then:

1. Record it, in one command: `PLAN toolfault --last --could-not "<one sentence: what it stopped you from
   doing>"` after a landing, or `PLAN tool "<what you ran, what it printed, what you needed>"` for a fault
   outside a landing.
2. Code 4 with "does not apply on origin/main": in the work folder, on the step's branch, `git fetch origin`, `git
   rebase origin/main`. A conflict in `docs/plan/ledger.tsv` or in a document where both sides only added
   lines: keep both. A conflict in code: `git rebase --abort`, `PLAN blocked <ID> "main changed the same
   code: <files>"`. Else land again.
3. Any other code 4: land once more (add `--github-again` if the message says GitHub was at fault). Not
   judged a second time: `PLAN blocked <ID> "tool: <the message's first line>"`.
4. A fault outside a landing (one of your own commands): try the command once more. If it fails the same
   way, go on without it when the landing will run that test anyway, and say so in your log; if you cannot
   prove your change without it, `PLAN blocked <ID> "tool: ..."`.
5. When the command that fails twice is `PLAN` itself (code 2, the same line both times): for `ahead`,
   nothing is written ahead this time; for `note`, `page`, `tool`, `toolfault` and `owed`, write the text
   into your log instead and go on; for `start` and `land`, `PLAN blocked <ID> "tool: <its line>"`, then
   `PLAN next`. If `next` fails twice, or `blocked` fails in the same way too, the step tool cannot be used:
   the work is stopped (section 13), with the tool's line as the reason.

Do not look for the cause inside the tool, do not work around it with a command of your own, and do not wait
for it to be mended. The record you wrote is counted on the progress page, with what it stopped you from
doing; that is how the owner sees it.

The tablet (`R52WA0FXE5J`) is used only through your commands and the landing. Never uninstall or wipe anything
on it. The tools keep a copy of the game data on it under `/data/local/tmp/nebula-test/`: leave it.

`measure.py pair-accept` is the owner's word only: never run it.

## 10. Things you find that are not this step's

A fault in the game that the card does not cover: add a ledger row for it in this landing (the next free id of
its letter; what, where, how you saw it) and go on. Do not fix it here. If it makes one of this step's tests
impossible to pass, that is a reason to block the step, with the row's id in the reason.

## 11. Proofs

- A proof item asks for something to be shown. Show it with a test that the landing will run from now on,
  wherever that is possible, so it stays proven.
- "Before and after: never slower": the landing's own speed check is this proof (on the PC the check
  `measure/step/pc`, and the tablet's measure). Do not build a measurement of your own for it. Its figures are
  in the run folder the landing prints; copy them into the row's proof text.
- A figure the card asks for by name that the landing does not take (a count from the timeline, a time of one
  function): take it once on main's build and once on yours, with the tool the card names, and write both
  figures and the two run folders into the row. If the named tool does not exist or does not give the figure:
  section 9.
- Runs under load, the memory-fault build, the endurance run and the software graphics device are added to
  your landing by the tool where the card's proof needs them. You do not run them by hand.
- What this PC, the tablet and GitHub cannot check (a second kind of game pad, a display with variable
  refresh, a camera, a Mac's screen): write the nearest proof that does run, say in the row's note what was
  not checked, and record it for the owner: `PLAN owed <ID> "<what to check and how>"`. Never write that such
  a thing was checked.

## 12. The progress page

The owner follows the work on one page: every step that is on main, how long each landing took, every tool
fault with what it stopped you from doing, and which check failed. After every landing that passed, after
every `PLAN blocked`, and at least once every two hours:

1. `PLAN note "<the step in work> since <HH:MM>; ahead: <the step after it, or nothing>"` (200 characters at
   most), then `PLAN page`.
2. Copy `D:/NebulaWork/plan/native/publish/index.html` and
   `D:/NebulaWork/plan/native/publish/progress-data.json` into a folder `progress` in your scratchpad
   directory (the same folder every time).
3. Publish with the Artifact tool: `file_path` the copied `index.html`, `files`
   `{"progress-data.json": "<the copied file's path>"}`.
   - The address is in `D:/NebulaWork/plan/native/progress-url.txt`. Pass it as `url`. If this conversation
     has not read that artifact yet, do `action: "read"` with the address first, then publish.
   - If the file does not exist: publish without `url` and with `icon: "chart"`, and write the address the
     tool returns into that file, alone on one line.
   - Before your first publish in a session read both copied files whole (the page is 24 KB): you publish
     only what you have seen. Later the data file changes with every landing; `PLAN status` prints the same
     data as short text, read that.
4. If the Artifact tool is not there or the publish fails twice: write that into your log once, and go on.
   The same page is on disk at `D:/NebulaWork/plan/native/progress.html`. Never let the page stop the work.

## 13. When nothing can start, and when you are done

`PLAN next` prints `Nothing can start now.` with the steps that are blocked:

1. A blocked step that has had no second attempt (your log says which have): `PLAN start <ID>` takes its
   branch up as it is. Read the reason first (the step's last line in the file `BLOCKED.md`, which the step
   tool writes beside the cards, in the work folder's `docs/plan/native`) and `git log origin/main..HEAD`
   with its diff. Do not repeat what you tried the first time; if
   you can see nothing you did not try, block it again with what you checked. One second attempt for each
   blocked step, never a third. Write `second attempt` in the log line.
2. No blocked step is left that has not had its second attempt: the work is stopped.

`PLAN next` prints `Every step is landed`, or the work is stopped by point 2: first `PLAN repair list`. Every
repair it shows as `open` (the kinds `next` never gives), oldest first: `PLAN start R-00N` and work on it as
section 6 says, to `PLAN land R-00N` or `PLAN blocked R-00N "<why>"`. One attempt for each. Then the work is
done, or stopped.

Done or stopped: `PLAN note "<Done: all 138 steps are on main. / Stopped: <why, one sentence>>"`, the page
(section 12), a log line, and end your turn with at most six lines for the owner: what is on main (the count
from `PLAN status`), what is blocked and why (one line each), where the page is. (Stopped because the step
tool itself cannot be used: leave out the note and the page, write the log line and the six lines.) These are
the only two reasons for which you end your turn. No release, tag or upload is ever made by you.

## 14. After a context compaction, a restart, or the owner's word "continue"

1. Read this file again. `date`.
2. Read the last 30 lines of your log.
3. `PLAN status`: what is landed, what is in work, whether a landing is running, what is prepared ahead.
4. `git -C D:/NebulaWork/MarioGalaxyRecomp/Nebula-native status` and `git -C ... log --oneline -5`: which
   branch the folder is on and what is uncommitted.
5. `PLAN next`, and go on from what it prints (section 4.1 point 1). A landing that is running: join it
   (`PLAN land <ID>`), and `PLAN ahead` gives you back the step you were writing.

Trust the tool, the folder and your log over your memory of the conversation.
