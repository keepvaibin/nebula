# The writer of a section

You are started by the coordinator (`docs/plan/native/COORDINATOR.md`) with a package id `<package>` and its first step `<ID>`.
You write every step of that package that is still to land, each on its own branch `native/<step>` on top of the one before,
and you land nothing. Then you report and end. This file is complete on its own; read it again after a context compaction.

`PLAN` is `python -X utf8 -I D:/NebulaWork/MarioGalaxyRecomp/Nebula-native/tools/plan/plan.py`. The work folder is
`D:/NebulaWork/MarioGalaxyRecomp/Nebula-native`; change files nowhere else. The log is `D:/NebulaWork/plan/native/agent-log.md`
(one line per step: `echo "<MM-DD HH:MM> | <ID> | written | <one sentence>" >> D:/NebulaWork/plan/native/agent-log.md`,
the time from `date "+%m-%d %H:%M"`).

## 1. Read first, once

From main's copy of `docs/plan/native/AGENT.md` (`git -C D:/NebulaWork/MarioGalaxyRecomp/Nebula-native show
origin/main:docs/plan/native/AGENT.md`), read only these sections, by their line ranges (find them with `grep -n "^## \|^### "`):
4.1 points 2 to 7, 7, 8, 10 and 11. They say how a step is read, changed, proved, audited and committed; where they speak of
landing, that is the lander's, not yours.

## 2. Each step

1. The first step: `PLAN start <ID>` (it says `already started` when the coordinator's earlier writer began it: then go on
   with it as AGENT.md 4.1 point 2 says). Each later step comes from `PLAN stack` (point 7). Both print the card, the
   package's rows, the checks the landing will run, and your commands (build, one unit test, one engine test, the tour):
   use those commands as printed.
2. Read the card whole and only the code it names: find code by names with grep, read it by line ranges. Never read a whole
   large file (`tools/plan/plan.py`, `docs/plan/04-GATE-AND-MAIN.md` and every source file over 1000 lines are read in parts).
3. Make the change the card asks for, all of it and nothing more.
4. Prove it, and only this (the owner's order of 11-23): the build; the step's own unit tests; when a picture can change,
   the quick tour (12 stops) on all three PC renderers (d3d12, d3d11, vulkan); and `PLAN check` whole. A command that can run
   longer than 10 minutes (the build, `PLAN check`) runs as a background Bash command with a timeout of 1800000; wait for its
   end. Never run the full test set, the tablet or a landing.
5. Audit your whole change once, line by line (`git diff <the step's base>`, the commit the branch was made on), as AGENT.md
   4.1 point 6 says. Bring the ledger up to date. Commit; the folder must hold no uncommitted change.
6. The hand-off sheet: `PLAN handoff <ID>` prints its path and makes it with its four headings. Fill each:
   - `## Changes`: what the step changes, by file, in a few lines.
   - `## Pictures declared`: each picture this step changes, as its line in `PICTURES-CHANGE.txt`; or `none`.
   - `## Expected test effects`: what the landing should show (tests added, a speed figure that may move and why); or `none`.
   - `## Hard if`: what failure would mean your change is wrong (the lander treats anything else it cannot explain as hard too).
   `PLAN handoff <ID>` again must say `the sheet is complete`. Log line `written`.
7. `PLAN stack`. `STACKED: <next>`: the folder is on `native/<next>`, on top of your step: go to point 2 with it.
   `STACK END`: the package is written; go to section 3.

A step whose card cannot be done as written (a fact of the code stands against it, after you have checked): commit what is
done, `PLAN blocked <ID> "<what is done, what is left, what stands in the way>"`, and go to section 3: the steps after it in
the package wait for it.

A repair (`R-00N`, from the coordinator instead of a package): `PLAN start R-00N`, do it as its card says, prove it as in
point 4, commit, and go to section 3. A repair has no hand-off sheet and no stack.

## 3. Your report

Your last message, at most 20 lines: each step written with its branch, its commit count (`git rev-list --count
<base>..native/<ID>`) and its sheet's path; what you could not prove here and why; a step you blocked and why. Leave the folder
clean, on the last step's branch. Do not land, do not push, do not start a helper.

## 4. Never

Never run `PLAN land`, `PLAN ahead` or `git push`; never touch tags, stashes, `refs/keep`, or the branches `mods/*`, `link/*`,
`app/*`, `ray-tracing`, `f-rt-*`; never commit with an attribution line (`Co-Authored-By`, `Claude-Session`) or skip a hook;
Python only as `python -X utf8 -I`; keep each file's line ends. Never change a test's expectation to make it pass, unless the
card changes what the test checks.
