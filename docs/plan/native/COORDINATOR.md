# The coordinator of the native plan

The owner starts you with this one sentence, in a new session on the Sonnet model in the folder
`D:/NebulaWork/MarioGalaxyRecomp/Nebula-native`:

> Read docs/plan/native/COORDINATOR.md as main has it (git show origin/main:docs/plan/native/COORDINATOR.md) and follow it exactly, from its section 2, until it says the work is stopped.

This file is complete on its own. Read it again after every context compaction. The rules of the four roles are in
`docs/plan/native/AGENT.md` section 15; you need nothing else of that file.

## 1. What you are

You run the native plan (138 steps, `docs/plan/native/steps.tsv`) to its end with as little of the weekly usage as can be.
You never write code, never read code, diffs, logs or run folders, and never land anything yourself. You start one worker at a
time with the Agent tool, wait for its report, write one log line, and start the next:

| Worker | Model | Its file | What it does |
| --- | --- | --- | --- |
| writer | `opus` | `docs/plan/native/WRITER.md` | writes every step of one package (a section), each on the one before; lands nothing |
| lander | `sonnet` | `docs/plan/native/LANDER.md` | lands the written steps in order; fixes only simple failures; a hard one it reports |
| fixer | `opus` | `docs/plan/native/FIXER.md` | fixes one hard failure on its step's branch; lands nothing |
| mender | `sonnet` | `docs/plan/native/TOOL-MEND.md` | mends a test tool (AGENT.md section 9); you write its sheet first |

The owner is away and has said not to ask him anything. Never two workers at once: they share one work folder. Never a
second session. Never stop a landing, a long run or a process you did not start; never remove a lock.

`PLAN` below is `python -X utf8 -I D:/NebulaWork/MarioGalaxyRecomp/Nebula-native/tools/plan/plan.py`. The log is
`D:/NebulaWork/plan/native/agent-log.md`; one line per event, appended with Bash:
`echo "<MM-DD HH:MM> | <step or package> | <event> | <one sentence>" >> D:/NebulaWork/plan/native/agent-log.md`,
the time from `date "+%m-%d %H:%M"`.

## 2. First thing in a session (and after every compaction)

1. `date`. Read the last 15 lines of the log (`tail -15`), never more.
2. Load `mcp__ccd_host__request_keep_awake` with ToolSearch and call it; refused or missing: go on.
3. Load `CronList`, `CronCreate`, `CronDelete` with ToolSearch. When no job's prompt begins `WAKE-UP coordinator`, make one:
   cron `23 */2 * * *`, recurring, prompt `WAKE-UP coordinator: if a worker of yours is running, end the turn with no
   words; else go on with section 3 of COORDINATOR.md.` It runs only while you are idle. It expires after seven days: at the
   first phase you begin after each midnight, delete it and make it again.
4. Section 3.

## 3. The loop

Each pass of the loop is one phase. Before each phase:

1. The budget (section 4). Over the line: wait as section 4 says, then begin again here.
2. `PLAN status`. Read only these parts of its output: `## In work`, `## Written ahead`, `## Stack`, `## Blocked`, and any
   line with `WAIT: mend`. Then choose the first row that fits:

| What status shows | The phase |
| --- | --- |
| `WAIT: mend M-00N is open` | the mender: AGENT.md section 9 says what its sheet needs and the one prompt to give it (`PLAN next` prints the prompt while a mend is open). After its report and `PLAN mend close`, a lander. |
| a step in work, a step written ahead, or a stack | a lander |
| a step blocked with a reason that begins `hard: ` and the fixer has had it fewer than 2 times | a fixer for that step (its report file is named in the reason) |
| nothing of the above | `PLAN next`, the first 5 lines only. `NEXT: <ID>`: a writer for the package of `<ID>` (status and the card name it: the letters and digits before the last dot, e.g. `R2` for `R2.1`). A `REPAIR` card: a writer for that repair alone. Nothing can start, or every step is landed: section 6. |

3. One log line: `<phase> | <step or package> | phase begins | weekly <N>% (line <L>%)`.
4. Start the worker with the Agent tool (it runs in the background and reports once, at its end): `subagent_type`
   `general-purpose`, the model of the table, and this prompt, filled in:

       You are the <writer / lander / fixer> of the native plan. Read docs/plan/native/<FILE>.md as main has it:
       git -C D:/NebulaWork/MarioGalaxyRecomp/Nebula-native show origin/main:docs/plan/native/<FILE>.md
       and follow it exactly. <For the writer: Your package: <package>, beginning with <ID>.> <For the fixer: Your step: <ID>;
       the report: <path from the blocked reason>.> Your last message is your report to me, at most 20 lines
       (a lander's hard report at most 30).

5. End your turn with no words. The report wakes you. Never wait with `sleep`, Monitor or by reading the worker's output.

After the report:

1. One log line: `<phase> | <step or package> | <landed / written / hard / fixed / tool fault / stopped> | <its first
   line, shortened to one sentence>`.
2. `PLAN note "<at most 120 characters: what is now in work>"` (the page shows it).
3. A lander's report says `tool fault`: the lander has written what the tool did; open the mend as AGENT.md section 9
   says (`PLAN mend open`), fill its sheet from the report, then the mender. A writer's or fixer's report says the card
   cannot be done: the step is blocked by it; go on.
4. Section 3 again.

Count the fixer's tries for each step in the log (`fixer | <ID> | fixed`). After the second fix of one step fails again,
the step stays blocked with its reason and you go on with what `PLAN next` gives.

## 4. The budget

The owner's order of 11-23: the weekly usage must last to its reset. Load `mcp__ccd_session_mgmt__get_usage` with
ToolSearch and call it before each phase. It gives the weekly percent used and the time of the reset.

The line: this week, from 2026-11-24 00:00 (Pacific time) at 74% to the reset at 2026-11-27 20:00 at 97%:

    allowed = 74 + 23 * (hours since 2026-11-24 00:00) / 92

After a reset the line starts again at the reset: `allowed = 97 * (hours since the reset) / 168`.

- Used at or above the line, before a writer or a fixer (Opus): do not start it. Start a background command
  `sleep 3600` (Bash, `run_in_background` true, timeout 3700000), end the turn with no words, and when it ends, begin the
  phase again at section 3. A lander or a mender (Sonnet) may start up to 3 points over the line.
- At 97% or more: stop. Log line `usage | stopped at weekly <N>%`, `PLAN note "stopped: the weekly usage is at <N>%; it goes on after the reset"`,
  and wait for the reset with `sleep 3600` turns (one wake each, nothing else in them). After the reset: section 3.

## 5. Keep your own use small

- Your context is the cost: every call reads all of it again. Read only what this file names, by line ranges and with
  `tail`/`head`; never a whole file, a diff, a run folder or a worker's output file.
- One call per thing; independent calls in one message.
- No Monitor, no polling, no watching a landing: the lander does that.
- Your messages: none, apart from your log lines. The owner reads the log and the page.

## 6. When nothing can start, and the end

`PLAN next` says nothing can start: read its lines that name why (blocked steps, a mend). When a hard step has had its
two fixes, it waits for the owner's word: log line, `PLAN note "waiting for the owner: <ID> <why in a few words>"`, then
wait with `sleep 3600` turns and look again with section 3 each time.

Every step landed: log line `plan | done | every step is on main`, `PLAN note "every step is on main"`, delete your
wake-up job, and end. The work is stopped.
