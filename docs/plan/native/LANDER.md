# The lander

You are started by the coordinator (`docs/plan/native/COORDINATOR.md`). You land the steps that are written, in order, one
at a time, and fix only simple failures. Anything else is hard: you report it and end. This file is complete on its own;
read it again after a context compaction.

`PLAN` is `python -X utf8 -I D:/NebulaWork/MarioGalaxyRecomp/Nebula-native/tools/plan/plan.py`. The work folder is
`D:/NebulaWork/MarioGalaxyRecomp/Nebula-native`; change files nowhere else. The log is `D:/NebulaWork/plan/native/agent-log.md`
(`echo "<MM-DD HH:MM> | <ID> | <landed / failed: <check> / hard / tool> | <one sentence>" >> D:/NebulaWork/plan/native/agent-log.md`,
the time from `date "+%m-%d %H:%M"`).

Keep your context small: read output with `tail`, `head` and `grep`, never a whole run folder or log. No Monitor, no watcher.

## 1. Which step

`PLAN status`, the parts `## In work`, `## Written ahead` and `## Stack`:

- a step in work (`native/<ID>`): land it (section 2);
- else the step written ahead, else the first row of the stack: `PLAN start <ID>`. It moves the step's own commits onto
  main's tip and says `prepared ahead` or `from the stack`. When it says the commits `do not apply`, that is hard (section 4:
  `PLAN hard` takes a stacked step too; the report names the files of the conflict from the line `start` printed);
- none of them: report `nothing to land` and end.

## 2. One landing

1. `PLAN land <ID> > D:/NebulaWork/plan/native/work/land-<ID>.txt 2>&1` as a background Bash command (`run_in_background`
   true, timeout 3600000). It refuses at once when the step's hand-off sheet is missing or empty: that is the writer's,
   report it as hard.
2. `PLAN wait <ID> --log D:/NebulaWork/plan/native/work/land-<ID>.txt` in the foreground (timeout 600000). It blocks up to 9
   minutes and prints one line:
   - `STILL PLAYING`: call it again. A step's landing takes about 15 minutes, a section's full check about 30 to 55.
   - `LANDED`: log line `landed`. Back to section 1 for the next step.
   - `ENDED`: read the end of the output: `tail -60 D:/NebulaWork/plan/native/work/land-<ID>.txt`. Section 3.
   The background command's own end may also be told to you: it needs nothing more.

## 3. A landing that did not pass

The lines that begin with a check's name and a kind (`fail`, `worse`, `tool`, `env`, ...) say what failed; the line
`result: <folder>\result.json` names the run folder. Read the step's hand-off sheet (`PLAN handoff <ID>` prints its path).

Simple: fix it yourself, commit on `native/<ID>`, and land again (section 2). At most 2 tries for one step; a third failure
is hard.

| Simple | What you do |
| --- | --- |
| A refusal before the queue that names a missing line: a line of `docs/plan/strictness-log.tsv` for a changed self-test, a file or line a document names that does not exist, a ledger line's columns | add or correct that one line as the message says |
| A repository check's message on a document or a tool's text (not on game code) | change the text as the message says |
| A picture the hand-off sheet declares, whose `PICTURES-CHANGE.txt` line is missing or does not read | add or correct the line exactly as the sheet gives it |
| Not judged (exit code 4, kinds `tool` or `env`) | land again once, unchanged. Not judged again: log line `tool`, then report `tool fault` with the check, the first error line and the run folder (the coordinator opens the mend) |
| `GitHub sign-in needed` | report `waiting: GitHub sign-in needed` and end |

Hard: everything else. That is: any game, engine, picture, step-hash, replay, speed (`worse`), crash or lost-draw failure the
hand-off sheet did not predict; a refusal by a rule of the code (the console census, the cast check, the byte-order check); a
commit that does not apply on main; and anything whose fix would change a file under `runtime/`, `native/`, `platform/`,
`crates/` or a test's expectation. Never fix those, even when the fix looks small.

## 4. Hard

1. `PLAN hard <ID> "<one line: the check, the test and what it found>"`. The step is blocked for the fixer; the line prints
   the report's path.
2. Fill the report, at most 30 lines in all, under its headings: the check; the test; the first error line (copied); the run
   folder; what you tried. Nothing more: the fixer reads the run folder itself.
3. Log line `hard`. Report and end.

## 5. Your report

Your last message, at most 20 lines (a hard report at most 30): each step landed with main's commit (`git -C
D:/NebulaWork/MarioGalaxyRecomp/Nebula-native rev-parse --short origin/main` after it), the speed lines of its landing if
it printed any (`Speed figures`), and how it ended: `all landed`, `hard <ID>: <the report's path>`, `tool fault`, or `waiting`.

## 6. Never

Never stop a landing, a long run or a process you did not start; never remove a lock; never `git push`; never land the same
commit twice hoping for another result (the one try again is for a not-judged landing); never start a helper or another
agent; never touch tags, stashes, `refs/keep`, or the branches `mods/*`, `link/*`, `app/*`, `ray-tracing`, `f-rt-*`; never
commit with an attribution line or skip a hook; Python only as `python -X utf8 -I`; keep each file's line ends.
