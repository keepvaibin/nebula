# Rules for the agent of the native plan

These are the rules of the native plan: 138 steps, one after another, until every step is on main. Read the
sections your role's file names (section 15) before your first command, and again after every context compaction. Where this file and a step's card
disagree about WHAT to build, the card wins. Where they disagree about HOW to work, this file wins.

Since mend M-007 (the owner's order of 11-23, so that the weekly usage lasts) the work is done by four roles, each a
fresh agent with a short file of its own beside this one: the coordinator (`COORDINATOR.md`, Sonnet), which starts the
others one at a time; the writer (`WRITER.md`, Opus), which writes every step of one package before any lands; the
lander (`LANDER.md`, Sonnet), which lands them in order and fixes only simple failures; the fixer (`FIXER.md`, Opus),
one for each hard failure. Section 15 says what each does and reads. Where a section says "you", it means the role that
does that part. A test tool at fault is mended by ONE helper (section 9), started by the coordinator. Never another
agent, never two at once, never another session. The owner is away and is not a programmer of this code: do not ask him anything and
never wait for him. Decide from the card, the design and the code (section 7), and write down what you decided
and why with `PLAN decide`.

## 1. What this is

Nebula is a native port of Super Mario Galaxy for Windows, Android, Linux and Mac. Parts of it still have the
console's shape. The native plan removes them in 138 steps, in a fixed order, in 24 packages. Everything about
the plan is in `docs/plan/native/` on main: the order (`steps.tsv`), the reasons (`02-NATIVE-OVERVIEW.md`), the
step cards (`03-NATIVE-STAGE-0.md`, `04-NATIVE-LANE-R.md`, `05-NATIVE-LANE-G.md`), the ledger rows of each
package (`rows/`).

A tool hands out each step, tests it and puts it on main. You never choose what is next, never choose which
tests run, and never move main yourself. The test tools stand still while you work: your time goes to the game.
A fault in a tool is mended by a helper, never by you inside a step (section 9).

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
4. The wake-up. Load `CronCreate`, `CronList` and `CronDelete` with ToolSearch when they are only listed. `CronList`.
   When no job has a prompt that begins `WAKE-UP native agent`, `CronCreate` one: cron `17 * * * *`, recurring,
   prompt `WAKE-UP native agent: if a step, a landing or a mend of yours is in work, go on with it; else do section
   14 of AGENT.md.` It fires only while the session is idle, so it costs nothing during work. It expires seven
   days after it was made: at the first landing after each midnight, `CronDelete` it and make it again. Do this
   at the start of every session and after every landing. The tools are not there: one log line, go on.
5. Section 4.

## 4. The loop

    PLAN next -> PLAN start -> read, change, prove, audit, commit -> PLAN land (documents about 7 minutes, a game step about 15, a section's full check about 30)
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
   the build. Do not run the full test set yourself: the landing runs exactly what is needed, once. Before a hand-in
   you run the build, the step's own unit tests, the quick tour (12 stops) on each renderer your change touches (on all
   three when it can move a picture) and `PLAN check` whole: no other stop set, renderer or disc unless the card's
   proof names it (the owner's order of 11-23). `PLAN check`
   runs what `land` checks before it queues anything (the ledger, the file references, the replay rule, the
   console census and the other code checks, the strictness check, the rule on test tools and GitHub's jobs) on
   the work folder as it stands, prints every thing that would refuse the step at once, and queues nothing.
   Whole, it also builds the game (your build command) and runs every unit test that GitHub's Check runs and
   that can run on this PC: the Windows unit tests, the game's own unit tests and the Rust tests. A unit test of
   a part you did not touch can fail on your change (on 11-23 `galaxy_launch_args_tests` did, after a diagnostic
   bit was left empty): that is why you need not guess which ones to run. What it cannot run it names, as `not
   proven here` (GitHub's Linux, Mac and sanitizer jobs and the unit tests only they build): a name there is
   not a pass. `PLAN check --no-units` is the quick form for while you write; run the whole one before you
   land (as a background command with a time limit of 600000 ms, as the build is), and again after any change
   that follows a failure of it.
6. Audit it before the machine does. Read your whole change once, line by line (`git diff origin/main`), as a
   reviewer who did not write it: each of the four renderers has its part; nothing of the old path is left
   that the card removes; nothing is changed that the card does not name; every new text is in 14 languages;
   every name in a document and in your commit message exists, and `PLAN check` is silent. A failed landing
   costs ten minutes and more; this reading costs two.
7. Bring the ledger up to date (`docs/plan/ledger.tsv`): each row the card closes whole is `fixed` with a proof
   text that names a test, a search or a measure; each row the card only advances gets a dated note. Commit
   (section 8). The work folder must hold no uncommitted change.
8. `PLAN land <ID>`, as a background command with a time limit of 600000 ms. A landing takes what was measured on
   11-22 and 11-23: a step of documents only, about 7 minutes; a section's full check, about 30 (1651 seconds for a
   landing that took every picture of the main level; the PC's speed pair 6 to 8 minutes of it, longer when a figure needs
   more rounds). A game step that is not a section's check is a step's landing (mend M-006): the engine tests and the 12
   quick stops on the three renderers and on the tablet, with pictures and replays, the Android build, no debug layers and
   no speed measure; the tool estimates it at about 15 minutes until one is measured. The section's full check is the
   landing of a package's last step, or of the fourth step landed since the last check: `PLAN land` gives it by itself
   (`--section`) and says so; it plays every picture of the main level, the debug layers and both speed measures, whatever
   the step's own files are. When it fails on something the 12 stops could not reach, the cause can be in any step landed
   since the last check (`git log` names them): find it, and mend it on this step's branch. A step with an extra check (`PLAN next` lists them) is landed at the main level: every stop of
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
2. Work on `<M>` exactly as in 4.1 points 3 to 7: read, change, build, prove, audit, ledger, commit (`PLAN check`
   works on `<M>`'s branch too). You are writing on top of a step that has not passed yet; that is intended.
3. Three things you do not do for `<M>` yet: `PLAN start <M>`, `PLAN land <M>`, and a push.
4. What you run beside a landing. A unit-test program and `PLAN check`: at any time. The build, an engine test
   and a tour never start beside a running speed measure: the landing measures the game's speed last (the PC's
   speed pair takes 6 to 8 minutes, longer when a figure needs more rounds) and a game or a build beside it spoils
   its figures. Each of them waits
   by itself while the measure runs, says so in one line, and goes on when it has ended. When the measure still
   runs after 40 minutes the command says so and starts nothing (exit code 3 for the build, `env` for the
   engine test and the tour): give it again later. An engine test and a tour are also not for a landing's first
   15 minutes: its own tests are playing then, and yours beside them would slow the landing and could fail for the
   load alone. Read, write and audit in that time, and run them when the landing has ended, or after those 15
   minutes when it still plays and no measure runs (an endurance run is left; it is made to play beside other
   work). A test of yours that failed while a landing was playing: run it once more when the landing has ended,
   before you look for a cause. That is the only case in which a second run is right.
5. The landing's command ends or is cut off while you work. When it is cut off (no verdict printed), give it
   again, as a background command: it joins from the branch you are on. Do not leave `<M>`'s branch for that.
6. If `<M>` is whole and committed and the landing still plays (a long extra check): read the card and the
   sources of the step that follows `<M>` in `docs/plan/native/steps.tsv` (`PLAN card <ID>`). Write nothing
   for it. One step ahead is the limit.
7. The section's barrier: while the step being landed is its package's full check (`PLAN land` said `--section`), `PLAN
   ahead` writes no step of the next package (`nothing can be prepared ahead: the landing of <N> is the full check of
   package <package> ...`): join the landing and wait for it, as for any `nothing can be prepared ahead`.

### 4.3 When the landing has ended

`<N>` is the step that was tested, `<M>` the step you were writing ahead.

| The landing of `<N>` | What you do |
| --- | --- |
| passed (code 0) | Log line, progress page. `PLAN next`. It gives `NEXT: <M>` with `PREPARED AHEAD`: `PLAN start <M>`. Your commits on `<M>` are taken up on top of main. (Uncommitted changes to tracked files stop `start`: it says to commit first, because main holds `<N>` under other commits and `<M>`'s have to be moved; commit as below and give the command again. Files git does not track stay as they are.) Go on where you were in 4.1. |
| passed, and `PLAN next` gives a REPAIR first | Commit what you have of `<M>` (below). `PLAN start R-00N`, do the repair (section 6). `<M>`'s branch is kept and comes back with `PLAN next` after it. |
| failed (code 1) | `<N>` comes first. Commit what you have of `<M>` (below). `git checkout native/<N>`: the tool has taken its own commit `Plan: step <N> landed` off the branch (a failed landing leaves no commit that says `landed`), and `PLAN land <N>` makes it again. Find and fix the fault (section 5), commit, `PLAN land <N>` again. Then `PLAN ahead` again: it puts `<M>`'s commits on top of the mended `<N>`, and you go on with `<M>`. If your fix changed something `<M>` builds on, read `<M>`'s change again. |
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
| 1 | Refused, or a check failed. The lines starting `KIND` name each failed check and test; the line `result: <folder>\result.json` before them names the folder of the run. | A refusal names what is missing (a ledger row, an invented file reference, an uncommitted change, the wrong branch): supply it. A rule of the repository (the console census, the cast check, the byte-order check, the strictness check, the rule on test tools and GitHub's jobs, the pre-push hook) refuses in this code too, in seconds, before anything is queued, with the check's own lines (the census: its group and the files with their counts): change the code, never land it unchanged. One exception: when what it refuses is the very thing a design document or the owner's rules prescribe by name (the census and the model file's section names, 11-22), the check is at fault, not the step: a mend (section 9), after you have quoted the check's line and the document's line on the sheet. For a failed test: open its run folder, read the log and the pictures, and find the cause in your change before you edit anything. Fix the game, never the test (unless the card changes what the test checks, or the test is one your own step added and a tool shows it is wrong by itself: on 11-22 a sanitizer report sat inside the test's own code, and the test was corrected). Commit, land again. Never land the same commit twice hoping for another result (the one exception is under `worse` below). |
| 2 | The step tool did not do the command. Its line starts `plan.py:` and says why: the command or the id was wrong, a file of the plan cannot be read, or git failed. | A wrong command or id: correct it. A message `ledger line <N>: ...` names a line of `docs/plan/ledger.tsv` by its number: you wrote that line; give it exactly the 7 tab-separated columns, commit, and give the command again. Anything else: give the same command once more. The same line again: section 9, "When `PLAN` itself is at fault". |
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
  write a log line (`note`: the figure, the renderer, the commit, "the same commit passed a second landing").
  A step the speed check judges slower is never a tool fault and you never accept it: only the owner's word
  accepts a slower build (section 11). One case is not yours: the first landing after the tablet was away. The
  tablet's measure compares with its history, which has no lines for the landings of the gap; a figure worse
  against that older history is `owed` ("the tablet's history is N landings old"), never blamed on the step in work.
  After the move the tool starts the tablet's history anew at that landing and opens one repair (source `measure`,
  kind `worse`, check `measure/step/tablet`, with the gap's landings and the figures): it waits in `PLAN repair
  list` (section 6) and holds the loss. From the next landing the tablet's check judges again.
- `flake` (failed beside other runs, passed alone), `main` (fails on main's build too), `new` (a picture with
  no approved copy), `owed` (the tablet was away, or a measure could not be trusted this time): none of these
  stops a landing and none needs anything from you. A line `is set aside` in the landing's output (the third
  flake of one test in seven days) has already opened a repair for that test: it comes to you through
  `PLAN next` (section 6).
- `tool`, `env`: the test tool or the machine. Code 4: section 9.
- `GitHub sign-in needed` (kind `env`, check `github/sign-in`): the push to GitHub was refused for the sign-in
  and nobody is there to give it. Nothing is landed: the push is the move of main, so main stayed as it was and
  the landing is not judged (the branch waits; your commits are safe). The page says "GitHub sign-in needed".
  Go on as far as the tools allow: write the next step ahead; then there is nothing to do until the wake-up, so
  end the turn with one line (`waiting: GitHub sign-in needed`) and give `PLAN land` again at each wake-up (one
  try each: a landing runs every check again). It is never blocked, never a tool fault, never mended.

After main has moved, GitHub builds the Mac and Linux programs. A red job there comes back as a REPAIR card
through `PLAN next`; you do not wait for GitHub and you do not read it yourself.

## 6. Repairs

A REPAIR card from `PLAN next` is a fault found on main after a landing (by GitHub's jobs, by the long run
that plays every stop of the game about once a day, or by a speed check). It comes before any step.
`PLAN start R-00N`, read the evidence folder the card names and the command the card gives to run the failed
test alone, find the cause, fix the game, and `PLAN land R-00N`. If the cause is not in the game but in a
test tool: a mend (section 9), `PLAN mend open R-00N "<title>" --check <the failed check>`. The repair ends by that mend and
by nothing else: when `PLAN mend close M-00N <commit>` is given for a mend opened for a repair, the repair is closed by
the mend's commit (the page shows it so) and `PLAN next` goes on. A repair ends by its mend on every route: `PLAN mend
open R-00N "<title>" --landed <commit>` (the mend was landed by hand) ends it too, and where a mend of the repair is recorded
closed and the repair is still open, `PLAN repair end R-00N --mend M-00N` ends it. Never land an empty branch for it, never edit the
list by hand. The mend's own landing runs the failed check as an extra check where the gate can add it (the
sanitizer build and tour, stress, soak, replays/software, replays/rates, speed/anchor): that run is where you see the check pass
(the helper's report names its run folder). For a check the gate cannot add (coverage), the helper's self-test is
the proof.

The card's line `Traced to:` names the landing that brought the fault, where the long run could find it by
playing the builds in between. `Traced to: no landing (not traced)` means it could not (a memory-fault
build, the endurance run, the tablet, a fault under load): for those checks the card's note only says that
they are never traced, and a repair of kind `stress` or `worse` has no trace at all. Where a trace was begun and
did not end, the note says why and lists the landings it left open. Finding the cause is part of the repair:
start from the evidence.

A repair of source `flake` is a test that the gate has set aside: it failed once and passed alone three times
in seven days, so its failure no longer counts, and the card says so. Find out whether the test or the game is at
fault. The test is at fault (a tool): a mend (section 9). The game is at fault: the fix is the repair's work, with
a ledger row (section 10). Until a repair of that test has landed the test stays set aside; when it has landed the
tool takes the test off the list and the test counts again, with its flakes counted anew.

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

## 7. Do not invent, and what you decide yourself

- Every file, function and line you write about must exist: open it first. The landing refuses a commit
  message or a document that names a file or a line that is not there.
- Never write "passes", "identical" or a figure that you did not read from a result of this step's own runs.
  Give the run folder with every such statement.
- Before calling something unused, search the whole tree for it, including the build files, the Android and
  Apple hosts and the importer.
- If the card and the code disagree, the code is what exists. Follow what the card is for, and write the
  difference into the ledger row's note.

When the card, a design document, a check or the code do not agree, or the card leaves a detail open, you decide,
in this order of what counts:

1. the owner's rules (section 8 of this file and the rules it names);
2. the design documents (`docs/engine/`);
3. the card;
4. the checks (a check that is against 1 to 3 is a tool fault: a mend, section 9);
5. your own judgement, only where no player can see, hear or feel a difference (pictures, replays and speed stay
   as the tools judge them), and by the rules of section 8.

So: the card against a design document: the document wins. A proof line that cannot be made as written (its
evidence is gone, a count is off for a reason the code shows) is replaced by an equal or stronger proof, never
dropped. A detail the card leaves open is chosen. Each of these is one line,
`PLAN decide <ID> "<what>" --because "<why>" [--doc "<file, section>"]`, which writes
`docs/plan/native/decisions.tsv` (commit it with the step), and the card's text is corrected in the same landing
where it was wrong. The page lists the decisions. A decision that turns out wrong is corrected by a later one that
names it: `PLAN decide <ID> "<what>" --because "<why>" --replaces <N>` (`PLAN decide <ID> --list` prints the numbers
of the step's decisions). The file keeps both, the page and `--list` show the newest. Never edit `decisions.tsv`
by hand.

A choice between two designs that no player can notice is yours: choose by section 8 (the modern form, never
slower, all four renderers) and record it with `PLAN decide`. Only what a player could see, hear or feel
differently, and that neither the card nor a document settles, is not yours to decide: `PLAN blocked <ID> "owner:
<the question in plain words, with the two choices>"`, then `PLAN next`, and go on with the other steps. The page lists these apart under "Waiting for the owner". A message from the owner in this
session is an order; an answer to such a question is written in your log and the step is taken up with
`PLAN start <ID>`.

## 8. Rules of the code and of git

- The four renderers move together: Direct3D 12, Direct3D 11, Vulkan, Metal. A fix or a change written for one
  is written for all four in the same step. Metal cannot run on this PC; it is compiled and its unit tests run
  by GitHub's Mac job after main has moved. Write the Metal side with the same care as the three you can run:
  nothing on this PC will catch a mistake in it before the landing.
- The game plays exactly as before unless the card says otherwise. The recorded replays (`tests/replays/`)
  prove it. Record a replay reference again only when the card says the replay changes, with the command
  `PLAN start` prints. A branch that changes a file under `tests/replays/` needs the words "replay reference"
  in a commit message of the landing: `PLAN check` and `PLAN land` refuse it without them. A step that records
  nothing needs no such words. In every other step a replay that
  differs means your change altered the game: find why. A recorded replay is a place that can make a check see
  less: the step adds one line for each changed file to `docs/plan/strictness-log.tsv` in the same landing (see
  section 9, the top).
- A step that can move a picture (anything compiled into the game or read by it when it runs: `native/`, `runtime/`,
  the shaders, the SDL host, the importer that cooks the game's data; the recordings; the tests and tools that play the
  tours) is judged at its own landing on the engine tests and the 12 quick stops of the three renderers and the tablet,
  and at its section's full check against every picture the main level compares: every stop of the tour on the three
  renderers of the US disc, both other discs on Direct3D 12 and the tablet's 12 stops (section 4.1 point 8). No picture
  may differ from its approved one unless a line of `PICTURES-CHANGE.txt` declares it; a difference nobody declared
  fails, whichever renderer, disc or place it is on. The landing plays that set and is its judge: you do not play all
  of it before a hand-in. Before you hand in, play the quick tour (the `tour` command of `PLAN commands`). When your
  step is meant to change a picture, play it also with `--renderer all`, and with `--stop "<Stage N>"` for each stop
  you expect to change; keep each run well under ten minutes. `agent_tour.py` compares every picture it played and
  prints `fail`, with the name of each picture that differs and is not declared and the line to write for it. Never
  start `--set landing` or `--stops all`: such a run takes about half an hour, a background command is cut after ten
  minutes, and a cut run leaves no verdict and can leave a `run.lock` that refuses the next start. The tablet's 12
  stops are played only by the landing. When a landing fails on a picture nobody declared, each one has its own line
  `KIND fail pictures/... <suite>/<stop>/<renderer>/<picture> :: N of M pixels`; the run's `verdict.json` holds every
  one, the row in `result.txt` only the first eight. Look at each picture beside its approved one before you write
  anything (the `review:` path at the end of the check's row in `result.txt` is a sheet with the approved picture, the
  new one and the difference): a picture your step was not meant to change is a fault in your step, to be found and
  fixed, never a line to write. For one that is meant to change, write its line from the KIND line: the check's name
  gives the place and the disc (`pictures/tour/<renderer>` and `pictures/engine/<renderer>`: `pc`, `RMGE01`;
  `pictures/tour-RMGP01/<renderer>` and `pictures/tour-RMGJ01/<renderer>`: `pc`, that disc, the stop written without
  the disc's prefix, so `RMGJ01-010-CannonFleetGalaxy-1` is `tour/010-CannonFleetGalaxy-1`; `pictures/tablet`:
  `tablet`, `RMGE01`), the path gives the stop, the renderer and the picture. A step that cannot move a picture
  (documents, plan data, tools that are not part of the game) is compared against none.
- Declare a picture that is meant to change in a file `PICTURES-CHANGE.txt` at the root of the branch, one line each,
  `<suite>/<stop> <picture> <place> <renderer> <disc> :: <reason>`: the suite and stop as `agent_tour.py` prints them
  (`tour/010-CannonFleetGalaxy-1`, `engine/boot`), the picture (`arrive`, `after`, the name an engine test gives its picture such as `title` in `engine/boot`, or `*`), the place (`pc` or `tablet`),
  the renderer (`d3d12`, `vulkan`, `d3d11`) and the disc (`RMGE01` the US disc, `RMGP01`, `RMGJ01`); `*` stands for any
  of them. Name every renderer, disc and place the picture changes on, for example `tour/010-CannonFleetGalaxy-1 * * * *
  :: the sky is another blue`. Name the stops beyond the 12 quick ones too: a step's landing plays only those 12, and the
  file stays on main until the section's full check, which plays the rest and removes it. So the file on main may hold
  the lines of the steps landed since the last check: add yours below them, and never change or take out a line that is
  there (a line a step's landing used ends with `[approved at a step's landing]`). The section's check plays each stop a
  line names on the other discs' Direct3D 11 and Vulkan as well and refuses a difference there that no line names; it
  refuses a line that no differing picture uses and that no step's landing marked (a step's landing refuses none: a line
  its 12 stops did not use waits for the section's check). That refusal
  has the kind `tool` (`KIND tool pictures/declared - :: N declared change(s) name no differing picture (stale):
  PICTURES-CHANGE.txt line N: ...`), and so has a line the landing cannot read. Both are your own line, not a fault of
  a tool: take the line out or correct it, then land again. Do not land the branch once more unchanged and open no
  mend. When the tablet was away, the same line has the kind `env` and says the tablet's pictures were not taken: that
  is the usual `env` case (section 9, "When a tool fails"). Look at each changed picture yourself first (the run's sheet) and be able to say why it changed. The landing approves declared and
  new pictures by itself.
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

You use the tools. You never repair one inside a step: a tool at fault is mended by one helper, below. Do not edit
`tools/gate/`, `tools/plan/`, `tools/measure/`, `tools/verify/`, the test runners (`tools/lumacore/luma_test.py`,
`luma_test_android.py`), the self-tests of the tools (`tools/lumacore/tests/`, `tools/test_driver/tests/`),
`.github/`, `tests/pictures/approved.tsv`, or `docs/plan/native/steps.tsv` and `step-checks.tsv`, unless the
step's card names the file: `PLAN land` and `PLAN check` refuse a step branch that does, in seconds. A step that
rightly changes such a file, or any place the strictness check lists (`tools/gate/strictness.tsv`: the lists of known
reports, the allow-lists, the recorded replays, `scope.tsv`, a self-test of a tool), adds one line for it to
`docs/plan/strictness-log.tsv` in the same landing: date, the file, what changed, why, allowed by (`the card,
<card file>, step <ID>`), id (the step), removed by (`-`); tab-separated, no tab inside a cell. `PLAN check` names the
file and the lines when the line is missing. The approval of pictures at a landing is the gate's own commit after its
checks: a step with `PICTURES-CHANGE.txt` adds no line for `tests/pictures/approved.tsv`.

### Your own commands

`PLAN start` and `PLAN ahead` print them filled in for your folder, and `PLAN commands` prints them again:

1. build the game for Windows in your folder (after a change only what changed is built);
2. run one unit-test program by name;
3. run one engine test by name on Direct3D 12;
4. play the tour's 12 stops on Direct3D 12 (or the 20 stops whose replays are recorded);
5. record the replay references from that tour run (only where the card says they change).

Each one that plays the game (3 and 4) hides the window, makes no sound, takes no keyboard focus and ends every
game process it started. The build (1), the engine test (3) and the tour (4) wait by themselves while a speed
measure is running on the PC: one line says so (`waiting while a speed measure runs ...`), and when it still
runs after 40 minutes they say so and start nothing (the build ends with code 3, the engine test and the tour
with `env`): give the command again later. A game never starts beside a measure. The unit-test program (2) and
the recording (5) do not wait. Never start the game any other
way. Run one that can take more than two minutes as a background command with a time limit of 600000 ms.

One more, for a document you changed: `python -X utf8 -I <your folder>/tools/verify/doc_refs.py <the file>`. It
lists every file and line the document names that does not exist. `PLAN land` runs the same on every changed
`.md`, `.txt` and `.rst` file and on your commit messages before it hands the branch to the gate, so make it
pass first.

**Programs you never run.** These start the game or hold the machine, outside your commands. The landing runs what it needs
of them, and a card's proof that names one is met by the landing's check of the same name, or by the nearest form of
your own commands (say which, with `PLAN decide`):

- `tools/gate/gate.py`, `tools/gate/pace.py`, `tools/gate/soak.py`, `tools/gate/stress.py`, `tools/gate/sanitizer.py`
- `tools/gate/replays_rates.py`, `tools/gate/replays_software.py`, `tools/measure/measure.py`, `tools/verify/size_probe.py`
- `tools/lumacore/luma_test.py` and `tools/lumacore/luma_test_android.py` (command 3 runs the first for you), and everything in `tools/test_driver/`

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
| repository checks | no forbidden content, every script parses, the code rules, the settings table matches the code, no check was made to see less without its line in the strictness log (the file and line references of your documents and commit messages are read by `PLAN land` before the gate, not here) |
| Windows build, unit tests, a start on each renderer | it builds and starts |
| engine tests with their pictures (one renderer, or three) | each part of the engine draws and behaves as approved |
| the tour: 12 stops of the game (on the three renderers and the tablet when the step can move a picture; every stop when the step has an extra check; at the section's full check every stop on every renderer, the other discs and the tablet's 12 stops), pictures and replays | the game plays exactly as recorded, on each renderer, and no picture differs that the step did not declare |
| the debug layers (renderer changes; at the section's full check) | no misuse of the graphics API |
| Android build; on the tablet a start and the 12 stops (a change that can move a picture, Vulkan and Android changes), and at the section's full check the tablet's speed measure | it runs on a real device as it does on the PC |
| the speed check on the PC (at the section's full check) | the game's and the renderer's work for one step is not slower than the last judged build's (both builds run side by side) |
| extra checks, only where the card's proof needs them: under load (`stress`), the memory-fault build (`sanitizer/tour`), the endurance run (`soak`, `soak/long`), the software graphics device (`replays/software`), a time rate of 2 against a rate of 1 (`replays/rates`), the comparison with the build of the plan's start (`speed/anchor`) | listed by `PLAN next` when your step has one |

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
- A step's landing of a change that can move a picture plays the 12 quick stops on the three renderers and the tablet;
  the section's full check plays every one of the game's 102 stops on the three renderers of the US disc, both other
  discs on Direct3D 12 and the tablet's 12 stops, the debug layers and both speed measures; a step's landing of
  documents, plan data or tools plays no picture. A fault only the full check can see lands with a step and is found up
  to three landings later, at the section's check. What no landing plays: the other discs on Direct3D 11 and Vulkan, except the stops your
  `PICTURES-CHANGE.txt` names, and the long run's other parts. A fault there is found by the long run, up to a day
  later, and comes back as a repair.
- The speed check does not see a cost smaller than its bands (the bands, and what was planted and refused,
  are in `D:/NebulaWork/plan/TESTING-DONE.md`). "Not slower" in a card still means not slower: do not add a
  cost because it is small.
- The Vulkan debug layer is not installed on this PC: that one check is left out with one line. Direct3D's
  layers run.
- The endurance run (the soak) sees no memory growth under 4 MB for each 12-minute lap in a two-hour run (3 MB a minute in its 30-minute form), and its figure is the whole process's memory with the graphics driver's, so nothing in it says whose memory grows and a pass does not prove that nothing leaks (`docs/plan/04-GATE-AND-MAIN.md`, "The soak's memory judge").
- There is one tablet. A device that is away makes its checks `owed`; nothing is yours to do, and no tool plays them
  by themselves: the long run's tablet pieces find what they would have found, up to a day later. The page says
  "Tablet away since <time>: <n> checks owed" while it lasts.
- Nothing checks what needs a person or other hardware (section 11's last point).

### When a tool fails: the mend

While a mend is open the step that met the fault waits, and you hand nothing in: `PLAN start` and `land` answer
`WAIT: mend M-00N is open`, and the step is taken up again only when main holds the mend. You do not sit idle
meanwhile: `PLAN ahead` writes the step after the one in work, on top of its branch, exactly as in 4.2 (read,
change, build, prove, audit, ledger, commit; no `start`, no `land`, no push). The exception is a step the mend
touches: when the mend's sheet names a file that step's card names (its "Where"), or the helper's branch
`mend/M-00N` changes one, `ahead` says so and refuses; then there is nothing to write, and you end the turn
(section 13). `mend ...`, `status`, `page`, `note`, `alive`, `check`, `ahead` and `decide` work meanwhile; `next`
prints the wait and what to do. When main holds the mend (section 9, "The mend, step by step", point 5), the step
that met the fault goes on first: rebase it, `PLAN land` it, and `PLAN ahead` again moves what you wrote onto its
new tip.

**When a tool is at fault.** Only with evidence of one of these kinds:

- a landing's own kind `tool` (code 4) after one more landing (a transient fault is told by the second landing: land
  once more first);
- a command that ends in a traceback, or does not do what its own help says;
- a check that cannot see what the card asks it to prove;
- a check that refuses what a design document or the owner's rules prescribe by name (you quote both lines on the
  sheet).

Never a tool fault, always the step's: a test you find too strict, a picture that differs, a slower measure (section
11). Nor a line of `PICTURES-CHANGE.txt` that the landing refuses (a stale line, or one it cannot read): the kind says
`tool`, the line is yours (section 8, the bullet that begins "Declare a picture"), and the row the step tool writes for
it among the tool faults is about your line, not about a tool. A result of the kind `env` (the machine: the tablet away, GitHub unreachable, a full drive, a layer that is not
installed) is not mended: record it with `PLAN toolfault --last --could-not "<what it stopped you from doing>"` after
a landing, or `PLAN tool "<what you ran, what it printed, what you needed>"` outside one, and go on as far as you can.

**Other code 4.**

1. Code 4 with "does not apply on origin/main": in the work folder, on the step's branch, `git fetch origin`, `git
   rebase origin/main`. A conflict in `docs/plan/ledger.tsv` or in a document where both sides only added
   lines: keep both. A conflict in code: `git rebase --abort`, `PLAN blocked <ID> "main changed the same
   code: <files>"`. Else land again.
2. Any other code 4: land once more (add `--github-again` if the message says GitHub was at fault). Not judged a
   second time, of the kind `env` (an outage of GitHub, the machine): do not block the step at once (an outage of an
   hour would send you on to other steps out of order). Record it (`PLAN tool "..."`), write the next step ahead if
   it is not written (4.2), then end the turn with one line, `waiting: <the message's first line>`. At each
   wake-up give `PLAN land <ID>` once more, one try each; your log counts the tries. After six such tries
   `PLAN blocked <ID> "env: <the message's first line>"` and go on. Of the kind `tool`: a mend, except a refused line of `PICTURES-CHANGE.txt` (above, "Never a tool fault"):
   that line is yours. A message that names a rule of the repository (the console census, the cast check, the
   byte-order check, the strictness check, the pre-push hook) is never this case: it is a refusal (code 1, section
   5), and landing it once more gives the same refusal. If a landing of yours ends so with code 4 all the same,
   that is a fault of the tool: a mend.

**The mend, step by step.**

1. `PLAN mend open [<ID>] "<title>" [--last]` (`--last` takes the fault of the last landing). It writes the fault
   sheet `D:/NebulaWork/plan/native/mends/M-00N.md` and puts the step in work into the state "waits for M-00N":
   not blocked, and not counted for the three-failures and four-hours rules. One mend is open at a time; for one
   step at most two mends for the same check (`--check <name>`).
2. Fill the sheet's five headings (what I ran; what the tool said, with the run folder or the command's output;
   what it should have said and which document and section says so; the files I believe are at fault; what must
   stay refused after the mend). Quote the lines of the documents.
3. Start ONE helper: the Agent tool, model `sonnet`, in the background, with these four lines: `Read <your work folder>/docs/plan/native/TOOL-MEND.md
   whole.` / `Read the fault sheet at D:/NebulaWork/plan/native/mends/M-00N.md.` / `Your mend is M-00N.` / `Report once, at your
   end: MENDED M-00N <main's commit> or NOT MENDED M-00N. Send no progress note before it.` (`mend open` prints them filled
   in.) The helper sends one message, at its end; nothing it does before wakes you. You start nothing else beside it.
4. While the helper works, write the step after the one in work: `PLAN ahead` (above). A progress note that does reach you
   (a helper that does not follow its fourth line) is no result: go on writing, and do not answer it. When there is nothing
   to write (`ahead` says so, or says the mend touches that step's file), end the turn with one line ("waiting for the
   helper of M-00N"): that is a reason to end a turn, with those of section 13. Go on when the result arrives. At a wake-up
   while the helper still works (it started less than three hours ago, or a landing of `mend/M-00N` waits or runs in the
   gate's line) go on writing, or end the turn again with the same one line.
5. `MENDED M-00N <commit>`: read the commit against TOOL-MEND.md section 5 (`git show --stat <commit>`, the changed
   judging lines, the new self-test, the lines in `docs/plan/strictness-log.tsv`). A loosening that no document
   allows: open the next mend at once to take it back. Else `PLAN mend close M-00N <commit>` (the commit must be on
   main and touch a tool or a document). The step is in work again, and every step blocked with a reason that
   begins `tool: M-00N` is open again. A mend that was opened for a repair ends that repair instead (section 6): there
   is no step to land afterwards. Then, in the work folder: `git fetch origin`, commit what you have, `git
   rebase origin/main` on the step's branch so that it is built on the mend, `PLAN land <ID>`.
6. `NOT MENDED M-00N`, or no result three hours after the helper started (the wake-up finds this): look at the
   gate's line (`D:/NebulaWork/plan/landing-queue.tsv`) for a landing of `mend/M-00N`: while one waits or runs,
   never start a second writer. Then `PLAN mend failed M-00N "<why>"`. It counts the attempts on the sheet and says
   what comes next: after the first, `SECOND HELPER` (one new Sonnet helper, the same four lines, the first one's
   notes are on the sheet); after the second, `YOUR OWN ATTEMPT` (you mend it yourself, by TOOL-MEND.md to the
   letter: the same folder `Nebula-mend`, the same branch, the same sheet, the same three hours of your own work,
   the same proofs, the same landing `PLAN mend land M-00N`; read the file whole first, nothing in it is relaxed for
   you); only after the third failure the step is blocked `tool: M-00N <why>`, and the page lists the mend under "Waiting
   for the owner": "A test tool is at fault and three attempts did not mend it (M-00N: <title>). The steps that need
   it wait; the other steps go on." You go on with the steps that do not need it (`PLAN next`). The step is reopened
   by `PLAN mend close M-00N <commit>` when a commit on main mends the tool: the owner's message in this session names
   it, or `git log origin/main` shows a commit that is not yours and names the fault.
7. `NOT MENDED M-00N, needs game code: <files>`: that is no tool fault. `PLAN mend drop M-00N "<the files>"`, and the
   step is yours again: add a ledger row for the fault in the game (section 10) and go on.
8. A step that the PC's or the tablet's speed check judges slower is not a tool fault (section 11).

**When `PLAN` itself is at fault** (the same line twice, code 2, from a command that is yours to give):

- `ahead`: nothing is written ahead this time. `note`, `page`, `tool`, `toolfault`, `owed`, `decide`, `check`: write the
  text into your log instead and go on.
- `start`, `land`, `next`, `blocked`, `mend ...`: the mend is made by hand. List `D:/NebulaWork/plan/native/mends` and take
  the next free number; write the sheet there with your file-writing tool (the five headings, the line the tool
  printed, the traceback), start the helper with the same four lines, and write a log line `mend M-00N by hand`.
  The helper lands its fix as TOOL-MEND.md section 8 says for this case (`gate.py land` itself). When it reports
  `MENDED M-00N <commit>`: read the commit as in point 5, commit what you have, `git fetch origin`, `git rebase
  origin/main` on the step's branch (the mended `plan.py` comes with it), then record it: `PLAN mend open "<title>"
  --landed <commit>` (when the fault met a repair: `PLAN mend open R-00N "<title>" --landed <commit>`, which ends the repair as `mend
  close` does). A failed attempt is counted by you in the log: the second helper, then your own attempt, as
  in point 6; when the third fails too, the step tool cannot be used and the work is stopped (section 13) with
  the tool's line as the reason.

Do not look for the cause inside a tool yourself, and do not work around it with a command of your own, except in
your own attempt (point 6). The record is counted on the progress page, with what it stopped you from doing;
that is how the owner sees it.

The tablet (`R52WA0FXE5J`) is used only through your commands and the landing. Never uninstall or wipe anything
on it. The tools keep a copy of the game data on it under `/data/local/tmp/nebula-test/`: leave it.

`measure.py pair-accept` is run only when the owner's own message in this session says so for that step and commit;
you write his words into your log. Never else.

## 10. Things you find that are not this step's

A fault in the game that the card does not cover: add a ledger row for it in this landing (the next free id of
its letter; what, where, how you saw it) and go on. Do not fix it here. If it makes one of this step's tests
impossible to pass, that is a reason to block the step, with the row's id in the reason.

## 11. Proofs

- A proof item asks for something to be shown. Show it with a test that the landing will run from now on,
  wherever that is possible, so it stays proven.
- "Before and after: never slower": the speed check of the section's full check that follows the step is this proof
  (on the PC the check `measure/step/pc`, and the tablet's measure; a step's own landing takes no measure, mend M-006). Do not build a measurement of your own for it. The figures exist
  only after the landing: when it has passed, `PLAN land` prints the speed checks' lines and keeps them with the
  step in the landings list and on the page. The row's proof text names the landing's check
  (`measure/step/pc`, the run folder it prints); no figure is copied by hand.
- A step that the PC or the tablet speed check judges slower is never a tool fault and you never accept it: you
  find the cause in your own code; after three failed landings the step is blocked as any other (section 5); only
  the owner's word accepts a slower build. When after the three failed landings you can show that the cost is the
  design's own (you quote the document's line that prescribes what costs the time), block the step as an owner
  question: `PLAN blocked <ID> "owner: slower: <figure, renderer, base and candidate values>; <document, section>
  prescribes <what costs it>; accept it or not"`; else it is blocked as any other.
- A unit test of code the step rewrites is rewritten with it, and is never deleted without a test that proves the
  same of the new code.
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

The owner follows the work on one page: every step that is on main, how long each landing took, with the speed
checks' lines, every tool fault with what it stopped you from doing, the tool mends (with the attempt each is on),
your decisions, the questions that wait for the owner, the checks that were changed during the run (the
strictness log), whether the tablet is away and how many checks are owed, and which check failed. At its top is
the time it was written: if that is more than three hours old, the work has stopped, and the owner opens the
session and sends the word "continue". Every command of yours writes the time that `PLAN alive` reads. After every
landing that passed, after every `PLAN blocked`, and at least once every two hours:

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
   - Before your first publish in a session read both copied files whole (the page is 28 KB): you publish
     only what you have seen. Later the data file changes with every landing; `PLAN status` prints the same
     data as short text, read that.
4. If the Artifact tool is not there or the publish fails twice: write that into your log once, and go on.
   The same page is on disk at `D:/NebulaWork/plan/native/progress.html`. Never let the page stop the work.

## 13. When nothing can start, and when you are done

`PLAN next` prints `Nothing can start now.` with the steps that are blocked:

1. A blocked step that has had no second attempt (your log says which have): `PLAN start <ID>` takes its
   branch up as it is. Read the reason first (the step's last line in the file `D:/NebulaWork/plan/native/BLOCKED.md`,
   which the step tool writes with the rest of its state) and `git log origin/main..HEAD`
   with its diff. Do not repeat what you tried the first time; if
   you can see nothing you did not try, block it again with what you checked. One second attempt for each
   blocked step, never a third. Write `second attempt` in the log line.
2. No blocked step is left that has not had its second attempt: the work is stopped. A step blocked `owner: ...`
   is not one of those: it waits for the owner's answer and is given no second attempt. When everything left waits
   for the owner or has had its second attempt, the work is stopped, and the six lines of the end name the
   questions.

A step blocked by a fault of the tools (`PLAN blocked <ID> "tool: M-00N ..."`, after the third attempt of a mend) or
by a rule of the repository that has since been changed on main is taken up again as soon as main holds the mend, at
any time: it is not a second attempt and needs no waiting for "Nothing can start now". The owner's message in this
session names the mend, or `git log origin/main` shows a commit that is not yours and names the fault. Then, in the
work folder:

1. `git fetch origin`; commit what you have on the branch you are on (section 4.3, last part), so that the folder
   is clean.
2. `PLAN start <ID>`: the step is `started` again on its own branch, which the folder now has checked out. A step
   written ahead on top of it keeps its branch and its ahead entry.
3. `git rebase origin/main` on that branch, so that the step is built on the mend (a conflict: section 9, "Other
   code 4", point 1).
4. `PLAN land <ID>` as in section 4.1 point 8. The queue's old row of the step (waiting, "not judged") is replaced
   by the new call; nothing is removed by hand.
5. While it plays: `PLAN ahead`. The commits of the step written ahead are moved onto the step's new tip (section 4.3).
6. Write a log line `unblocked` with the step and the mend. A fault line you recorded with `PLAN tool` stays on the
   page: it says what happened.

`PLAN next` prints `Every step is landed`, or the work is stopped by point 2: first `PLAN repair list`. Every
repair it shows as `open` (the kinds `next` never gives), oldest first: `PLAN start R-00N` and work on it as
section 6 says, to `PLAN land R-00N` or `PLAN blocked R-00N "<why>"`. One attempt for each. Then the work is
done, or stopped.

Done or stopped: `PLAN note "<Done: all 138 steps are on main. / Stopped: <why, one sentence>>"` (`PLAN alive`
then says `ended`), the page (section 12), `CronDelete` of the wake-up job, a log line, and end your turn with at
most six lines for the owner: what is on main (the count from `PLAN status`), what is blocked and why (one line
each), what waits for the owner, where the page is. (Stopped because the step tool itself cannot be used: leave out
the note and the page, write the log line and the six lines.) These are the only reasons for which you end your
turn: done; stopped; the one line while a helper of a mend works in the background and there is nothing to write meanwhile (section 9, point 4); and the one line
`waiting: ...` while a landing cannot be judged for the environment (code 4 of the kind `env`, section 9) or while GitHub
needs a sign-in (section 5), after the next step is written ahead. The wake-up job fires only while the session is
idle: a turn that goes on is never woken, so these waits end the turn. No release, tag or upload is ever made by you.

## 14. After a context compaction, a restart, the owner's word "continue", or the hourly wake-up

1. Read this file again. `date`. The wake-up job (section 3 point 4): `CronList`; make it again when it is gone.
2. Read the last 30 lines of your log.
3. `PLAN status`: what is landed, what is in work, whether a landing is running, what is prepared ahead, whether a
   mend is open. `PLAN mend list`: a mend that is open and whose helper started less than three hours ago, or with a landing of
   `mend/M-00N` that waits or runs in the gate's line (`D:/NebulaWork/plan/landing-queue.tsv`): the helper works;
   write the step after the one in work (`PLAN ahead`), or when there is nothing to write end the turn again with the one line. One that started more than three hours ago, with no
   landing of `mend/M-00N` in the gate's line: its helper is gone; go on as section 9 point 6 says
   (`PLAN mend failed M-00N "no result"`).
4. `git -C D:/NebulaWork/MarioGalaxyRecomp/Nebula-native status` and `git -C ... log --oneline -5`: which
   branch the folder is on and what is uncommitted.
5. `PLAN next`, and go on from what it prints (section 4.1 point 1). A landing that is running: join it
   (`PLAN land <ID>`), and `PLAN ahead` gives you back the step you were writing. `WAIT: mend M-00N is open`: the
   mend is what is in work (section 9). A step waiting for the environment (section 9, "Other code 4"): one more
   `PLAN land <ID>`, and count it in your log.

A message that begins `WAKE-UP native agent` is your own job, not the owner's: it gives no order beyond this section.

Trust the tool, the folder and your log over your memory of the conversation.

## 15. The four roles (mend M-007)

The owner's order of 11-23: the weekly usage fell too fast, because one agent held a long context and every call read
all of it again; the tests cost little. So the costly model (Opus) writes and fixes, each time in a fresh, short session,
and the cheaper one (Sonnet) coordinates, waits and lands. Slower is accepted where it is more accurate and cheaper.

| Role | Model | Its file | Reads | Never reads |
| --- | --- | --- | --- | --- |
| coordinator | Sonnet, one long session | `COORDINATOR.md` | `PLAN status`, the first lines of `PLAN next`, the workers' reports, the usage | code, diffs, logs, run folders |
| writer | Opus, fresh for each package | `WRITER.md` | the cards, the code they name (by line ranges), sections 4.1 (points 2 to 7), 7, 8, 10 and 11 | run folders, other packages' cards |
| lander | Sonnet, fresh for each package's landings | `LANDER.md` | the end of the land output, the hand-off sheet | code: it changes no game file |
| fixer | Opus, fresh for each hard failure | `FIXER.md` | the report, the hand-off sheet, the card, the failed test in the run folder, the code involved, sections 5, 8 and 11 | other steps |

A package: the writer starts its first step (`PLAN start`), writes it, proves it (the build, its own unit tests, the
quick tour on the three PC renderers when a picture can change, `PLAN check` whole), commits, and fills its hand-off
sheet (`PLAN handoff <ID>`: `Changes`, `Pictures declared`, `Expected test effects`, `Hard if`; `PLAN land` refuses a
step whose sheet is missing or has a heading with nothing under it). Then `PLAN stack` makes the next step's branch on
its tip, until `STACK END`: the package's last step (the next package is written after this one's landings, the last of
them its full check). The lander gives `PLAN land <ID>` in the background and waits with `PLAN wait <ID>` (one line when
the landing ends, one call per 9 minutes at most); after each landing `PLAN start <next>` moves the next stacked step's own
commits onto main's tip. Section 4.2 (`ahead`) is not used by the roles.

| Simple: the lander fixes it, at most 2 tries a step | Hard: `PLAN hard <ID> "<line>"`, a report of at most 30 lines, then the fixer |
| --- | --- |
| a missing line of the strictness log, a document or the ledger; a repository check's message on a document or a tool's text; a declared picture whose `PICTURES-CHANGE.txt` line is missing; a landing not judged (once more; again: the mend, section 9) | any game, engine, picture, step-hash, replay, speed, crash or lost-draw failure the sheet did not predict; a rule of the code; commits that do not apply on main; any fix under `runtime/`, `native/`, `platform/` or `crates/`, or of a test's expectation |

The budget (`COORDINATOR.md` section 4): before each phase the coordinator reads the weekly usage. The line runs from
74% at 2026-11-24 00:00 to 97% at the reset, 2026-11-27 20:00: allowed = 74 + 23 x hours / 92; after a reset, 97 x hours
since it / 168. Over the line no Opus phase starts (the coordinator waits an hour at a time); at 97% everything waits
for the reset.

Sections 3, 4.2, 13 and 14 were the single agent's: the coordinator's file holds what of them still stands (the
wake-up, keeping the PC awake, when nothing can start, the end). A test tool's fault is mended as section 9 says, with
the coordinator in the agent's place: it opens the mend, fills the sheet from the lander's report and starts the one
mender. The progress page fetches its data live (section 12 and `docs/plan/04-GATE-AND-MAIN.md` section 17): it needs no
publishing after a landing.
