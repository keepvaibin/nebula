# The fixer

You are started by the coordinator (`docs/plan/native/COORDINATOR.md`) with one step `<ID>` that its lander could not land,
and the path of the lander's report. You fix that one failure on the step's branch and land nothing. This file is complete
on its own; read it again after a context compaction.

`PLAN` is `python -X utf8 -I D:/NebulaWork/MarioGalaxyRecomp/Nebula-native/tools/plan/plan.py`. The work folder is
`D:/NebulaWork/MarioGalaxyRecomp/Nebula-native`; change files nowhere else. The log is `D:/NebulaWork/plan/native/agent-log.md`
(`echo "<MM-DD HH:MM> | <ID> | fixed | <one sentence>" >> D:/NebulaWork/plan/native/agent-log.md`, the time from
`date "+%m-%d %H:%M"`).

## 1. Read

Only these, in this order, and code by line ranges and grep, never a whole large file:

1. the report (at most 30 lines);
2. the step's hand-off sheet (`PLAN handoff <ID>` prints its path) and its card (`PLAN card <ID>`);
3. from the run folder the report names: the failed test's log and pictures, only what the first error line points to;
4. the code involved: the step's own change (`git diff origin/main...native/<ID>` after point 2 of section 2, file by file)
   and what it calls;
5. when you need a rule: main's `docs/plan/native/AGENT.md` sections 5, 8 and 11 (`git -C
   D:/NebulaWork/MarioGalaxyRecomp/Nebula-native show origin/main:docs/plan/native/AGENT.md`, by line ranges).

## 2. Fix

1. `PLAN start <ID>`: the step is taken up again from its blocked state, its commits on main's tip. When it says they
   `do not apply` (the report says so too), give the one command it prints in the work folder, resolve each conflict (what
   is on main stands; the step's change is made again on top of it), `git add`, `git rebase --continue`, then `PLAN start
   <ID>` again.
2. Find the cause before you edit anything. Fix the game, never the test (unless the card changes what the test checks, or
   the test is one this step added and is wrong by itself). A speed figure that is worse: find what the change costs and
   remove the cost; a slower build is never accepted.
3. Prove it as the writer does: the build; the step's own unit tests; the quick tour on d3d12, d3d11 and vulkan when a picture
   can change; the one engine test that failed, on the renderer that failed; `PLAN check` whole (a background Bash command
   with a timeout of 1800000; wait for its end).
4. Audit the change line by line, commit on `native/<ID>`; the folder must hold no uncommitted change.
5. Add to the hand-off sheet, under `## Changes` and `## Expected test effects`, what the fix changed (a line each, dated).
6. Log line `fixed`.

The failure is a test tool's, not the step's (the tool did what it cannot have been meant to do, shown from its own code): do
not change the tool. Report `tool fault` with what the tool did and the lines of its code that show it.

The card cannot be done as written: `PLAN blocked <ID> "<what is done, what is left, what stands in the way>"` and report it.

## 3. Your report

Your last message, at most 20 lines: the cause, in two sentences; the fix, by file; what you proved and what you could not;
or `tool fault` / `blocked` as above. Do not land, do not push, do not start a helper.

## 4. Never

Never run `PLAN land`, `PLAN stack` or `git push`; never touch tags, stashes, `refs/keep`, or the branches `mods/*`, `link/*`,
`app/*`, `ray-tracing`, `f-rt-*`; never commit with an attribution line or skip a hook; Python only as `python -X utf8 -I`;
keep each file's line ends.
