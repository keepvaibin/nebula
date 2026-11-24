"""long_results.py: what follows from a long run's results (package B15, the planner's design d2 of 08-PROMPT-TESTING-AGENT.md).

    python -X utf8 -I tools/gate/gate.py long-results RUN_FOLDER [--dry]

The step runs at the end of a long run that ran all its checks, and alone on a finished run folder. For each check that is not a
pass it does what its kind says:

  fail     traced to the landing that brought it (below), then a repair of kind `fail` (plan.py repair add, source nightly) with the
           evidence folder <run>/evidence/<check>/ holding how-to-run.txt, the command that plays the failed test alone;
  stress   a repair of kind `stress` (the stress lane opens its own while the run is played: none is opened twice);
  worse    a repair of kind `worse`;
  tool, env, a part that did not run, a measure not trusted, the tablet owed
           one line in tool-faults.tsv, step `nightly`; no repair;
  planned  nothing.

Pictures of the run's own stops that have no approved copy are approved through `gate.py land` on a branch nightly/pictures-<date> when
no picture check failed and no branch waits in the landing queue. A repair that is `landed` becomes `confirmed` when this run passes
its check; a check that fails again opens a new repair whose note says "seen again after R-nnn".

Tracing. The commits looked at are the commits main MOVED TO (one for each landing: the record is gate-runs/history.tsv, the lines of
level `move`, and the landing queue's own rows for the landings before that column), after the newest earlier long run in which the
check passed, up to this run's commit. One commit: that is the one. More: the middle commit is built in the long run's own tree, the
failed tests of the check are played there alone, and so on by halving (three runs for eight landings). Never traced: NEVER_TRACED,
a check that is not one suite of the game's runner (only TRACEABLE are), a check whose one run takes over 15 minutes, and everything
once one night's tracing has used 90 minutes; such a repair says so, with the list of landings in between.

The step run twice on one folder changes nothing the second time: each action looks for its own record first (a repair whose evidence
is this run's, a fault line of this run, a repair already confirmed, a branch already landed).
"""
import datetime
import fnmatch
import json
import pathlib
import re
import subprocess
import sys

HERE = pathlib.Path(__file__).resolve().parent
TREE = HERE.parent.parent
sys.path.insert(0, str(HERE))
sys.path.insert(0, str(TREE / "tools" / "plan"))

PASSED = ("pass", "flake", "stress", "new")        # what an earlier run's state must be to count as having passed the check
TRACE_RUN_SECONDS = 15 * 60                          # one traced run
TRACE_NIGHT_SECONDS = 90 * 60                        # the tracing of one night
PART_NAMED_SECONDS = 45 * 60                         # a part over this is named in the report

# Checks that are never traced to a landing (B15 point 5): the sanitizer, coverage, soak, measure, stress, tablet and GitHub checks.
NEVER_TRACED = ("build/windows-asan", "unit/game-asan", "tour-asan/*", "mods-asan/*", "build/windows-coverage", "coverage", "soak", "soak/long",
                "measure/*", "speed/*", "stress/*", "build/android", "android/tablet", "tablet/*", "tour/tablet", "tour/tablet-pc",
                "replays/tablet", "pictures/tablet", "github*", "part/*", "lane/*", "gate/*")
# What a run alone can play: the suites of the game's runner (the engine's tests and the tours of every disc and renderer).
TRACEABLE = ("engine/*", "tour/d3d12", "tour/vulkan", "tour/d3d11", "tour-RMG*/*")
RENDERERS = ("d3d12", "vulkan", "d3d11")
# A check the long run no longer has (B15 point 7): its old rows are read and nothing is done.
GONE = ("measure/route",)
NEVER_TEXT = "%s is one of the checks never traced to a landing (the sanitizer, coverage, soak, measure, stress, tablet and GitHub checks)"
NOT_SUITE_TEXT = "%s is not one suite of the game's runner, so no one run of it can tell the landing"


def matches(name, patterns):
    return any(fnmatch.fnmatchcase(name, p) for p in patterns)


# ---- the record -----------------------------------------------------------------------------------------------------

# The columns history.tsv has (gate.HISTORY_COLUMNS: a test keeps the two equal); a file written before the last two is read as it is.
HISTORY_COLUMNS = ("started", "run", "level", "commit", "result", "seconds", "pass", "known", "planned", "owed", "failed", "only", "checks",
                   "covers", "moved")


def read_history(work):
    """The rows of <work>/history.tsv as dicts, a file written before the columns `covers` and `moved` read as it is."""
    path = pathlib.Path(work) / "history.tsv"
    if not path.exists():
        return []
    lines = path.read_text(encoding="utf-8").splitlines()
    rows = []
    for line in lines[1:]:
        row = dict.fromkeys(HISTORY_COLUMNS, "")
        # By position, not by the file's header: a file started before the columns `covers` and `moved` keeps its old header while its
        # newer lines have the two cells.
        row.update(zip(HISTORY_COLUMNS, line.split("\t")))
        rows.append(row)
    return rows


def checks_of(row):
    """{check: (state, seconds)} of a history row's `checks` cell (name=state/seconds, space between)."""
    out = {}
    for token in (row.get("checks") or "").split(" "):
        name, _, rest = token.rpartition("=")
        state, _, seconds = rest.partition("/")
        if name and state:
            out[name] = (state, int(seconds) if seconds.isdigit() else 0)
    return out


def long_runs(rows):
    """The history rows of whole long runs (no --only), oldest first."""
    return sorted((r for r in rows if r.get("level") == "long" and r.get("only", "-") in ("-", "")), key=lambda r: r["started"])


def same_commit(a, b):
    a, b = (a or "").rstrip("+"), (b or "").rstrip("+")
    n = min(len(a), len(b), 40)
    return n >= 7 and a[:n] == b[:n]


def moves_of(rows, queue_file=None):
    """The landings that moved main, oldest first: [{"time", "commit", "branches"}]. From the lines of level `move` in history.tsv
    (the commit main moved to, the branches the landing covered), and for the landings before those lines existed from the rows of the
    landing queue marked `landed` (their note says `main moved to <sha>`; the rows of one run folder are one landing)."""
    out = [{"time": r["started"], "commit": r["moved"] or r["commit"], "branches": [b.strip() for b in r["covers"].split(",") if b.strip()]}
           for r in rows if r.get("level") == "move"]
    if queue_file and pathlib.Path(queue_file).exists():
        landed = {}
        for line in pathlib.Path(queue_file).read_text(encoding="utf-8").splitlines()[1:]:
            c = line.split("\t")
            if len(c) >= 7 and c[3] == "landed":
                found = re.search(r"main moved to ([0-9a-f]{7,40})", c[6])
                if found:
                    entry = landed.setdefault(found.group(1), {"time": c[0], "commit": found.group(1), "branches": []})
                    entry["branches"].append(c[1])
        out += [e for e in landed.values() if not any(same_commit(e["commit"], m["commit"]) for m in out)]
    return sorted(out, key=lambda m: m["time"])


def work_of(branches, branch_item):
    """What the branches of a landing are: a step `native/<ID>`, a repair `native/repair-00N` (R-00N), else a tools landing."""
    parts = []
    for b in branches:
        item = branch_item(b)
        parts.append(("step %s" % item[1] if item[0] == "step" else "repair %s" % item[1]) if item else "a tools landing: %s" % b)
    return "; ".join(parts) if parts else "a landing that named no branch"


# ---- tracing --------------------------------------------------------------------------------------------------------

class Trace:
    """What tracing a failed check came to. `landing` the move found (None when not traced); `between` the moves a trace that did not
    find one leaves open; `why` the reason it did not; `runs` the traced runs made, `seconds` their time."""

    def __init__(self, landing=None, between=(), why="", runs=0, seconds=0.0, steps=()):
        self.landing, self.between, self.why, self.runs, self.seconds, self.steps = landing, list(between), why, runs, seconds, list(steps)


def candidates(run_commit, run_started, check, rows, moves):
    """(the moves to look at, why none can be) for a check that failed in the run: main's own moves after the newest earlier long run
    that passed the check, up to the run's commit."""
    earlier = [r for r in long_runs(rows) if r["started"] < run_started and checks_of(r).get(check, ("", 0))[0] in PASSED]
    if not earlier:
        return [], "no earlier long run on record passed this check"
    prev = earlier[-1]
    end = next((i for i, m in enumerate(moves) if same_commit(m["commit"], run_commit)), None)
    if end is None:
        return [], "main's commit %s is not a landing's move in the record" % run_commit[:9]
    start = next((i for i, m in enumerate(moves) if same_commit(m["commit"], prev["commit"])), None)
    if start is None:
        start = max([i for i, m in enumerate(moves) if m["time"] <= prev["started"]], default=-1)
    found = moves[start + 1:end + 1]
    return (found, "") if found else ([], "no landing between the run that passed (%s) and this run" % prev["commit"][:9])


def halve(cands, play, budget_seconds=TRACE_NIGHT_SECONDS, spent=0.0, run_seconds=TRACE_RUN_SECONDS, say=print):
    """Finds the first of `cands` (oldest first; the last is known to fail) at which the check fails. `play(move)` gives
    (state, seconds): `fail`, `pass` or `unknown` (it could not be built or run, or took over `run_seconds`). Returns a Trace."""
    lo, hi = -1, len(cands) - 1        # cands[lo] passed (the run before the first), cands[hi] fails
    used, runs = 0.0, 0
    while hi - lo > 1:
        if spent + used >= budget_seconds:
            return Trace(None, cands[lo + 1:hi + 1], "the night's tracing used its %d minutes" % (budget_seconds // 60), runs, used)
        mid = (lo + hi) // 2
        state, seconds = play(cands[mid])
        used += seconds
        runs += 1
        say("trace: %s at %s: %s (%.0f s)" % ("the check", cands[mid]["commit"][:9], state, seconds))
        if state == "unknown" or seconds > run_seconds:
            return Trace(None, cands[lo + 1:hi + 1], "a traced run could not be made or took over %d minutes" % (run_seconds // 60),
                         runs, used)
        if state == "pass":
            lo = mid
        else:
            hi = mid
    return Trace(cands[hi], (), "", runs, used)


def trace(check, run_commit, run_started, rows, moves, play, spent=0.0, say=print):
    """Traces one failed check: a Trace."""
    if matches(check, NEVER_TRACED):
        return Trace(why=NEVER_TEXT % check)
    if not matches(check, TRACEABLE):
        return Trace(why=NOT_SUITE_TEXT % check)
    cands, why = candidates(run_commit, run_started, check, rows, moves)
    if not cands:
        return Trace(why=why)
    if len(cands) == 1:
        return Trace(cands[0], steps=["the only landing since the last long run that passed"])
    return halve(cands, play, spent=spent, say=say)


# ---- how to run a failed test alone ---------------------------------------------------------------------------------

def how_to_run(log_text, tests):
    """The text of how-to-run.txt, from the command the gate itself ran for the check (the first `$` line of its log that starts the
    runner): its --test lists are the failed tests, its program folder `<bin>`, its work folder and lock file new ones. None where the
    log holds no runner command (the check is not a suite)."""
    line = next((l[2:].strip() for l in log_text.splitlines() if l.startswith("$ ") and "luma_test.py" in l), None)
    if line is None:
        return None
    words, out, i = line.split(), [], 0
    while i < len(words):
        w = words[i]
        if w == "--test":
            i += 2
            continue
        if w in ("--bin", "--work", "--lock") and i + 1 < len(words):
            out += [w, {"--bin": "<bin>", "--work": "<new-empty-folder>", "--lock": "<lock-file-that-does-not-exist>"}[w]]
            i += 2
            continue
        out.append(w)
        i += 1
    out += [a for t in tests for a in ("--test", t)]
    return ("Plays the failed test alone, as the gate ran it. <bin> is the folder of the build's programs (build/windows makes them);\n"
            "<new-empty-folder> and <lock-file-that-does-not-exist> are any new ones.\n\n" + " ".join(out) + "\n")


# ---- reading a run --------------------------------------------------------------------------------------------------

class Run:
    """A long run's folder: result.json, and parts.json (the pieces and the time each held the machine) when the run wrote it."""

    def __init__(self, folder):
        self.folder = pathlib.Path(folder)
        self.result = json.loads((self.folder / "result.json").read_text(encoding="utf-8"))
        self.commit = self.result["commit"]
        self.started = self.result["started"]
        self.seconds = float(self.result["seconds"])
        self.rows = self.result["checks"]
        self.name = self.result.get("run") or self.folder.name
        try:
            self.parts = json.loads((self.folder / "parts.json").read_text(encoding="utf-8"))
        except (OSError, ValueError):
            self.parts = None

    @property
    def verdict(self):
        if self.result.get("verdict"):
            return self.result["verdict"]
        return "pass" if self.result.get("passed") else "fail"

    def failed_tests(self, row):
        """The tests of a row that did not pass: [(test, message)] from its records (a run before the kinds has none: the row itself)."""
        found = [(r["test"], r["message"]) for r in row.get("records") or [] if r.get("kind") in ("fail", "worse", "main") and r.get("test") not in (None, "-")]
        return found

    def log_text(self, row):
        try:
            return pathlib.Path(row["log"]).read_text(encoding="utf-8", errors="replace") if row.get("log") else ""
        except OSError:
            return ""


def renderer_of(check):
    last = check.rsplit("/", 1)[-1]
    return last if last in RENDERERS else "-"


def classify(row):
    """What a row asks for: pass (nothing), fail, stress, worse, fault (a line in tool-faults.tsv), pictures (a picture with no approved
    copy), consequence (the row says a check did not run because another failed: the other has the action), none."""
    state, check, detail = row["state"], row["check"], row.get("detail", "")
    if check in GONE:
        return "gone"
    if state == "stress":
        return "stress"
    if state in ("pass", "flake", "known", "planned"):
        return "none"
    if state == "new":
        return "pictures"
    if check.startswith("part/") and state != "pass":
        return "fault"
    if state in ("tool", "env", "owed"):
        return "fault"
    if state == "worse":
        return "worse"
    if state in ("fail", "main"):
        return "consequence" if detail.startswith("not run:") else "fail"
    return "none"


# ---- the step -------------------------------------------------------------------------------------------------------

class Actions:
    """What the step does to the world; a self-test puts fakes in its place. The defaults call plan.py and live.py."""

    def __init__(self, live, plan_tool=None, say=print):
        self.live, self.say = pathlib.Path(live), say
        self.plan_tool = pathlib.Path(plan_tool or TREE / "tools" / "plan" / "plan.py")

    def repairs(self):
        import live as lv
        return lv.read_repairs(self.live)

    def faults(self):
        import live as lv
        return lv.read_faults(self.live)

    def repair_add(self, kind, check, renderer, step, commit, evidence, note):
        """Opens (or counts) a repair of source nightly: its id."""
        cmd = [sys.executable, "-X", "utf8", "-I", str(self.plan_tool), "--live", str(self.live), "repair", "add", "--source", "nightly",
               "--kind", kind, "--check", check, "--renderer", renderer, "--step", step or "-", "--commit", commit or "-",
               "--evidence", str(evidence), "--note", note]
        done = subprocess.run(cmd, capture_output=True, text=True, cwd=str(TREE))
        said = (done.stdout or "").strip().splitlines()
        if done.returncode != 0 or not said:
            raise RuntimeError("plan.py repair add failed (exit %d): %s" % (done.returncode, ((done.stderr or "") + (done.stdout or "")).strip()[-300:]))
        return said[-1]

    def fault(self, kind, check, message, test, run_folder):
        import live as lv
        lv.append_fault(self.live, "gate", "nightly", kind, check, message, test=test, run_folder=str(run_folder))

    def confirm(self, repair_id):
        import live as lv
        lv.set_repair(self.live, repair_id, "confirmed")

    def branch_item(self, branch):
        import plan
        return plan.branch_item(branch)


class Context:
    """Everything the step reads and does: the run's work folder (history.tsv), the queue file, the actions, the tracer's play function
    (move -> (state, seconds)), the picture lander and the clock. `dry`: nothing is changed."""

    def __init__(self, work, actions, queue_file=None, play=None, land_pictures=None, now=None, dry=False, say=print, branch_item=None):
        self.work, self.actions, self.queue_file = pathlib.Path(work), actions, queue_file
        self.play, self.land_pictures = play, land_pictures
        self.now = now or datetime.datetime.now()
        self.dry, self.say = dry, say
        self.branch_item = branch_item or actions.branch_item


def evidence_folder(run, check):
    return run.folder / "evidence" / re.sub(r"[^A-Za-z0-9._-]+", "-", check)


def note_for(run, row, tests, traced, again):
    head = "long run %s of %s: %s failed" % (run.started[:10], run.commit[:9], row["check"])
    if tests:
        head += " (%s)" % "; ".join("%s: %s" % t for t in tests[:3]) + (" and %d more" % (len(tests) - 3) if len(tests) > 3 else "")
    else:
        head += ": " + row.get("detail", "")[:160]
    return " ".join(x for x in (head + ".", ("Seen again after %s." % again) if again else "", traced) if x)


def traced_text(t, work_of_move):
    if t.landing is not None:
        return "Traced to %s (main moved to %s at %s)." % (work_of_move(t.landing), t.landing["commit"][:9], t.landing["time"][:16].replace("T", " "))
    between = "; ".join("%s (%s)" % (m["commit"][:9], work_of_move(m)) for m in t.between)
    return "Not traced: %s.%s" % (t.why, (" Landings between: %s." % between) if between else "")


def results(run, ctx):
    """The step. Returns the report's data: {"opened", "counted", "faults", "confirmed", "pictures", "lines", "traces"}; with ctx.dry it
    changes nothing and `lines` says what it would do."""
    rows = read_history(ctx.work)
    moves = moves_of(rows, ctx.queue_file)
    act = ctx.actions
    out = {"opened": [], "faults": [], "confirmed": [], "pictures": "", "lines": [], "traces": []}
    repairs, faults = act.repairs(), act.faults()
    spent = 0.0

    def say(line):
        out["lines"].append(line)
        ctx.say(line)

    def move_work(m):
        return work_of(m["branches"], ctx.branch_item)

    if (run.folder / "long-results.json").exists() and not ctx.dry:
        say("long-results: %s was done already (long-results.json); nothing changed" % run.name)
        return json.loads((run.folder / "long-results.json").read_text(encoding="utf-8"))
    say("long-results: %s on %s, %s, %.0f minutes, verdict %s%s" % (run.name, run.commit[:9], run.started[:16].replace("T", " "),
                                                                    run.seconds / 60, run.verdict, " (dry: nothing is changed)" if ctx.dry else ""))
    failed_pictures = any(r["check"].startswith("pictures/") and r["state"] == "fail" for r in run.rows)
    for row in run.rows:
        kind, check = classify(row), row["check"]
        renderer = renderer_of(check)
        if kind in ("none", "consequence"):
            if kind == "consequence":
                say("  %-28s %-7s no action: %s" % (check, row["state"], row["detail"]))
            continue
        if kind == "gone":
            say("  %-28s %-7s no action: the long run has no speed part any more (B15 point 7); this is an old run's row" % (check, row["state"]))
            continue
        if kind == "pictures":
            continue
        if kind == "fault":
            have = next((f for f in faults if f["step"] == "nightly" and f["check"] == check and f["run_folder"] == str(run.folder)), None)
            fkind = "ENV" if row["state"] in ("env", "owed") or check.startswith("part/") else "TOOL"
            if have:
                say("  %-28s %-7s a line of tool-faults.tsv is there already" % (check, row["state"]))
            else:
                say("  %-28s %-7s %sone %s line in tool-faults.tsv (step nightly): %s" % (check, row["state"], "would write " if ctx.dry else "wrote ", fkind, row["detail"][:100]))
                if not ctx.dry:
                    act.fault(fkind, check, row["detail"], "", run.folder)
                out["faults"].append(check)
            continue
        # fail, stress, worse: a repair
        tests = run.failed_tests(row)
        evidence = evidence_folder(run, check)
        have = next((r for r in repairs if r["check"] == check and r["renderer"] == renderer and r["evidence"].replace("\\", "/").startswith(
            run.folder.as_posix())), None)
        if have:
            say("  %-28s %-7s repair %s has this run's evidence already" % (check, row["state"], have["id"]))
            continue
        again = next((r["id"] for r in reversed(repairs) if r["check"] == check and r["renderer"] == renderer and r["state"] in ("landed", "confirmed")), "")
        step = commit = "-"
        traced = ""
        if kind == "fail":
            t = trace(check, run.commit, run.started, rows, moves, lambda m, check=check, tests=tests: ctx.play(check, tests, m, run) if ctx.play else ("unknown", 0.0),
                      spent=spent, say=say) if not ctx.dry else dry_trace(check, run, rows, moves)
            spent += t.seconds
            out["traces"].append({"check": check, "landing": t.landing, "why": t.why, "runs": t.runs})
            traced = traced_text(t, move_work)
            if t.landing is not None:
                commit = t.landing["commit"]
                items = [ctx.branch_item(b) for b in t.landing["branches"]]
                step = next((i[1] for i in items if i and i[0] == "step"), "-")
        note = note_for(run, row, tests, traced, again)
        what = {"fail": "fail", "stress": "stress", "worse": "worse"}[kind]
        say("  %-28s %-7s %s repair of kind %s: %s" % (check, row["state"], "would open a" if ctx.dry else "opens a", what, note[:200]))
        if not ctx.dry:
            evidence.mkdir(parents=True, exist_ok=True)
            text = how_to_run(run.log_text(row), [t_[0] for t_ in tests]) if kind == "fail" else None
            if text:
                (evidence / "how-to-run.txt").write_text(text, encoding="utf-8")
            (evidence / "evidence.txt").write_text("run: %s\ncheck: %s (%s)\nlog: %s\n%s\n" % (
                run.folder, check, row["state"], row.get("log"), "\n".join("%s: %s" % t_ for t_ in tests) or row.get("detail", "")), encoding="utf-8")
            rid = act.repair_add(what, check, renderer, step, commit, evidence, note)
            out["opened"].append(rid)
            repairs.append({"id": rid, "check": check, "renderer": renderer, "evidence": str(evidence), "state": "open"})
            say("    repair %s" % rid)
    # A repair that landed is confirmed when this run passes its check.
    passed = {r["check"] for r in run.rows if r["state"] in ("pass",)}
    for r in repairs:
        if r["state"] == "landed" and r["check"] in passed:
            say("  repair %s (%s): landed, and this run passes %s: %s" % (r["id"], r["check"], r["check"], "would be confirmed" if ctx.dry else "confirmed"))
            if not ctx.dry:
                act.confirm(r["id"])
            out["confirmed"].append(r["id"])
    # Pictures of the run's own stops with no approved copy.
    new = [r["check"] for r in run.rows if r["state"] == "new"] or ([] if not (run.folder / "new-pictures.tsv").exists() else ["new-pictures.tsv"])
    if new:
        if failed_pictures:
            out["pictures"] = "not landed: a picture check failed"
        elif ctx.dry:
            out["pictures"] = "would land the approval of the new pictures on branch nightly/pictures-%s" % ctx.now.strftime("%Y%m%d")
        elif ctx.land_pictures is None:
            out["pictures"] = "not landed: no way to land them here"
        else:
            out["pictures"] = ctx.land_pictures(run)
        say("  pictures with no approved copy (%s): %s" % (", ".join(new[:4]), out["pictures"]))
    if not ctx.dry:
        write_report(run, out, ctx)
        (run.folder / "long-results.json").write_text(json.dumps({k: v for k, v in out.items() if k != "traces"} | {"done": True}, indent=2) + "\n", encoding="utf-8")
    return out


def dry_trace(check, run, rows, moves):
    """What tracing would face, with no run made: a Trace that says so."""
    if matches(check, NEVER_TRACED):
        return Trace(why=NEVER_TEXT % check)
    if not matches(check, TRACEABLE):
        return Trace(why=NOT_SUITE_TEXT % check)
    cands, why = candidates(run.commit, run.started, check, rows, moves)
    if not cands:
        return Trace(why=why)
    return Trace(cands[0] if len(cands) == 1 else None, cands if len(cands) > 1 else (), "%d landings to halve (about %d traced runs)" % (len(cands), max(1, (len(cands) - 1).bit_length())) if len(cands) > 1 else "")


# ---- the report -----------------------------------------------------------------------------------------------------

def part_minutes(run):
    """[(part, pieces, minutes held, longest piece in minutes, minutes waited)] from parts.json, else from the checks' seconds."""
    import long_parts
    if run.parts:
        names = []
        for p in run.parts:
            n = long_parts.part_of(p["part"])
            if n not in names:
                names.append(n)
        out = []
        for n in names:
            mine = [p for p in run.parts if long_parts.part_of(p["part"]) == n]
            out.append((n, len(mine), sum(p["held"] for p in mine) / 60.0, max(p["held"] for p in mine) / 60.0, sum(p.get("waited", 0) for p in mine) / 60.0))
        return out
    return [(r["check"], 1, r["seconds"] / 60.0, r["seconds"] / 60.0, 0.0) for r in run.rows if r["seconds"] >= 60]


def longest_wait(run):
    """(minutes, part): the longest a landing could wait for this run: its longest piece that held the gate lock."""
    held = [(p["held"], p["part"]) for p in (run.parts or []) if not p.get("beside")]
    return ((max(held)[0] / 60.0, max(held)[1]) if held else (0.0, ""))


def write_report(run, out, ctx):
    lines = ["# The long run of %s" % run.started[:16].replace("T", " "), "",
             "Commit %s. Verdict: %s. Minutes: %.0f." % (run.commit, run.verdict.upper(), run.seconds / 60), "", "## Parts", ""]
    for part, pieces, held, longest, waited in part_minutes(run):
        lines.append("- %s: %d piece%s, %.1f minutes holding the machine, the longest piece %.1f minutes%s%s" % (
            part, pieces, "" if pieces == 1 else "s", held, longest, ", waited %.1f minutes for landings" % waited if waited >= 1 else "",
            "  <- OVER 45 MINUTES: split it" if held * 60 > PART_NAMED_SECONDS else ""))
    wait, where = longest_wait(run)
    if run.parts:
        lines.append("- The longest a landing waited for this run: %.1f minutes (%s)." % (wait, where))
    lines += ["", "## Checks that are not a pass", ""]
    odd = [r for r in run.rows if r["state"] not in ("pass", "known")]
    lines += ["- %s: %s, %s" % (r["check"], r["state"], r["detail"][:200]) for r in odd] or ["- none"]
    lines += ["", "## What was done", ""]
    lines += ["- " + l.strip() for l in out["lines"][1:]] or ["- nothing"]
    lines += ["", "Repairs opened: %s. Confirmed: %s. Tool or machine faults written: %s." % (
        ", ".join(out["opened"]) or "none", ", ".join(out["confirmed"]) or "none", ", ".join(out["faults"]) or "none")]
    if run.result.get("machine"):
        lines += ["", "## From the machine", ""] + ["- " + m for m in run.result["machine"]]
    (run.folder / "nightly-report.md").write_text("\n".join(lines) + "\n", encoding="utf-8")


# ---- the newest long run, for plan.py status and the owner's page ---------------------------------------------------

def newest_long(work, now=None, alive=None, lock="D:/NebulaWork/plan/long.lock"):
    """One line about the newest whole long run: when, commit, verdict, the repairs it opened; `running since HH:MM` while one runs."""
    import lockholder
    alive = alive or (lambda pid: False)
    parts = []
    holder = pathlib.Path(lock) / "holder.txt"
    if holder.exists():
        text = holder.read_text(encoding="utf-8", errors="replace").strip()
        pid, _ = lockholder.parse(text)
        if pid is not None and alive(pid):
            found = re.search(r"(\d{4}-\d\d-\d\d) (\d\d:\d\d):\d\d\s*$", text)
            parts.append("running since %s" % (found.group(2) if found else "?"))
    runs = long_runs(read_history(work))
    if runs:
        r = runs[-1]
        opened = ""
        try:
            data = json.loads((pathlib.Path(work) / r["run"] / "long-results.json").read_text(encoding="utf-8"))
            opened = ", repairs opened: %s" % (", ".join(data.get("opened") or []) or "none")
        except (OSError, ValueError):
            opened = ", results not read yet"
        parts.append("last finished %s on %s, %s, %.0f minutes%s" % (r["started"][:16].replace("T", " "), r["commit"], r["result"].upper(),
                                                                    float(r["seconds"] or 0) / 60, opened))
    return "; ".join(parts) if parts else "none yet"


# ---- the real world: tracing runs, the pictures' landing, the command line ----------------------------------------

def runner_words(log_text, tests):
    """The recorded runner command as words, `<bin>` etc. in place of the run's own folders (how_to_run's text, as a list)."""
    text = how_to_run(log_text, tests)
    if text is None:
        return None
    return text.strip().splitlines()[-1].split()


class RealPlayer:
    """Plays one check's failed tests alone on the commit main moved to (ctx.play): the long run's own tree is brought to the commit and
    built (gate.py --long --only build/windows, which takes the gate lock part by part and gives way to landings), then the recorded runner
    command is run under the gate lock for itself, as a part is (gate.locked_part). The tree is put back to the run's commit at the end."""

    def __init__(self, gate, args, tree=None, clock=None):
        import time
        self.gate, self.args, self.clock = gate, args, clock or time.time
        self.tree = pathlib.Path(tree or gate.LONG_TREE)

    def __call__(self, check, tests, move, run):
        gate, args, t0 = self.gate, self.args, self.clock()
        names = [t[0] for t in tests]
        row = next((r for r in run.rows if r["check"] == check), None)
        words = runner_words(run.log_text(row), names) if row else None
        if not words or not names:
            return "unknown", 0.0
        try:
            if gate.long_tree_at(move["commit"], self.tree) is not None:
                return "unknown", self.clock() - t0
            work = pathlib.Path(args.work) / "trace"
            build = subprocess.run([sys.executable, "-X", "utf8", "-I", str(self.tree / "tools" / "gate" / "gate.py"), "--long", "--only",
                                    "build/windows", "--work", str(work), "--who", "nightly trace", "--tablet", args.tablet or "none",
                                    "--board", args.board, "--status", "", "--gate-lock", args.gate_lock, "--tablet-lock", args.tablet_lock],
                                   capture_output=True, text=True, timeout=TRACE_RUN_SECONDS)
            if build.returncode != 0:
                return "unknown", self.clock() - t0
            folder = work / ("played-%s-%s" % (move["commit"][:9], re.sub(r"[^A-Za-z0-9]+", "-", check)))
            bins = pathlib.Path(args.build_root) / "bins"
            swap = {"<bin>": str(bins), "<new-empty-folder>": str(folder), "<lock-file-that-does-not-exist>": str(folder) + ".lock"}
            cmd = [swap.get(w, w) for w in words]
            verdict = {"state": "unknown"}

            def play():
                done = subprocess.run(cmd, capture_output=True, text=True, timeout=TRACE_RUN_SECONDS)
                suite = next(w for w in cmd if w.endswith("suite.json"))
                renderer = cmd[cmd.index("--renderer") + 1]
                import kinds
                results, _, _ = kinds.read_cases(folder, pathlib.Path(suite).parent.name, names, renderer, gate.kind_of_verdict)
                found = {k for k, _ in results.values()}
                verdict["state"] = "pass" if found == {"pass"} and done.returncode == 0 else ("fail" if "fail" in found else "unknown")
            held = type("Held", (), {"dry": False, "part_log": [], "record": staticmethod(lambda *a, **k: None),
                                     "args": type("A", (), {"mode": "long", "wait": args.wait, "gate_lock": args.gate_lock, "board": args.board,
                                                            "who": "nightly trace", "queue_file": args.queue_file,
                                                            "runner_lock": args.runner_lock})()})()
            gate.locked_part(held, "trace %s at %s" % (check, move["commit"][:9]), play)
            return verdict["state"], self.clock() - t0
        except (OSError, subprocess.SubprocessError, StopIteration, ValueError):
            return "unknown", self.clock() - t0
        finally:
            gate.long_tree_at(run.commit, self.tree)


def picture_rechecker(run, new_tree):
    """pictures.approve_landing's `recheck`: the run's own picture commands (the first `$` line of each pictures-*.log) again with the
    manifest and declarations of `new_tree`; [(check, "pass" | "fail")]."""
    import pictures as picture_tool

    def recheck():
        states = []
        for log in sorted(run.folder.glob("pictures-*.log")):
            line = next((l[2:].split() for l in log.read_text(encoding="utf-8", errors="replace").splitlines()
                         if l.startswith("$ ") and "pictures.py" in l), None)
            if line is None:
                continue
            out, i = [], 0
            while i < len(line):
                w = line[i]
                if w == "--new-out":
                    i += 2
                    continue
                out.append(w)
                if w in ("--manifest", "--changes") and i + 1 < len(line):
                    out.append(str(pathlib.Path(new_tree) / (picture_tool.MANIFEST_NAME if w == "--manifest" else picture_tool.CHANGES_NAME)))
                    i += 1
                i += 1
            done = subprocess.run(out, capture_output=True, text=True, cwd=str(new_tree))
            states.append((log.stem, "pass" if done.returncode == 0 else "fail"))
        return states
    return recheck


def land_pictures_for(gate, args, ctx):
    """ctx.land_pictures: approves the run's new pictures on a branch nightly/pictures-<date> (one commit of the two picture files) and
    lands it through gate.py land; never while a branch waits or runs in the landing queue."""
    import pictures as picture_tool

    def land(run):
        busy = gate.landing_watch(args).busy()
        if busy:
            return "not landed: %s; `gate.py long-results %s` lands them when the queue is empty" % (busy, run.folder)
        tree = pathlib.Path(args.pictures_tree)
        branch = "nightly/pictures-%s" % ctx.now.strftime("%Y%m%d")

        def sh(*a, cwd=tree):
            return subprocess.run([gate.GIT, "-C", str(cwd), *a], capture_output=True, text=True)
        sh("fetch", "origin", "main", cwd=gate.TREE)
        if not (tree / ".git").exists():
            made = sh("worktree", "add", "--detach", str(tree), "origin/main", cwd=gate.TREE)
            if made.returncode != 0:
                return "not landed: the work folder %s could not be made: %s" % (tree, (made.stderr or made.stdout).strip()[-200:])
        for a in (("checkout", "-q", "--force", "-B", branch, "origin/main"), ("clean", "-q", "-d", "-f", "-f")):
            done = sh(*a)
            if done.returncode != 0:
                return "not landed: git %s: %s" % (a[0], (done.stderr or done.stdout).strip()[-200:])
        # carry: PICTURES-CHANGE.txt on main holds the lines of steps landed since the last section's check (mend M-006); the
        # nightly approval marks the lines its pictures used and leaves the file to that check.
        sha, why = picture_tool.approve_landing(tree, run.folder, picture_tool.STORE, picture_rechecker(run, tree), gate.GIT, carry=True)
        if why:
            return "not landed: " + why
        if sha is None:
            return "nothing to approve"
        done = subprocess.run([sys.executable, "-X", "utf8", "-I", str(tree / "tools" / "gate" / "gate.py"), "land", branch, "--who", "nightly",
                               "--tablet", args.tablet, "--tablet-wait", "30"], cwd=str(tree), capture_output=True, text=True)
        last = (done.stdout or "").strip().splitlines()[-1:] or ["no output"]
        return ("landed on %s (%s)" % (branch, sha[:9])) if done.returncode == 0 else "not landed: gate.py land exit %d: %s" % (done.returncode, last[0])
    return land


def build_context(gate, args, dry, say=print):
    actions = Actions(args.live, say=say)
    ctx = Context(args.work, actions, queue_file=args.queue_file, dry=dry, say=say)
    if not dry:
        ctx.play = RealPlayer(gate, args)
        ctx.land_pictures = land_pictures_for(gate, args, ctx)
    return ctx


def tracing_allowed(gate, args):
    """The long run's tree is the tracer's to use only while no other long run holds the long lock."""
    import os
    holder = gate.lock_holder(args.long_lock)[1] if pathlib.Path(args.long_lock).exists() else None
    return holder is None or holder == os.getpid() or not gate.process_alive(holder)


def make_args(**given):
    """The options the step reads, with the defaults of the gate's own places."""
    import argparse
    values = {"live": "D:/NebulaWork/plan/native", "work": None, "queue_file": "D:/NebulaWork/plan/landing-queue.tsv",
              "tablet": "R52WA0FXE5J", "build_root": "D:/NebulaWork/build/gate-Nebula-long", "board": "D:/NebulaWork/plan/BOARD.md",
              "gate_lock": "D:/NebulaWork/plan/gate.lock", "long_lock": "D:/NebulaWork/plan/long.lock",
              "tablet_lock": "D:/NebulaWork/plan/tablet.lock", "runner_lock": "D:/NebulaWork/plan/landing-runner.lock", "wait": 240.0,
              "pictures_tree": "D:/NebulaWork/MarioGalaxyRecomp/Nebula-nightly-pictures"}
    values.update({k: v for k, v in given.items() if v is not None})
    return argparse.Namespace(**values)


def build_parser():
    """The options of `gate.py long-results`."""
    import argparse
    p = argparse.ArgumentParser(prog="gate.py long-results", description=__doc__.split("\n")[0])
    p.add_argument("run_folder")
    p.add_argument("--dry", action="store_true", help="print what would be done and change nothing")
    for name, default in vars(make_args()).items():
        if name != "wait":
            p.add_argument("--" + name.replace("_", "-"), default=None, help="default %s" % default)
    p.add_argument("--wait", type=float, default=None)
    return p


def run_cli(gate, argv):
    """gate.py long-results RUN_FOLDER [--dry]."""
    ns = build_parser().parse_args(argv)
    args = make_args(**{k: v for k, v in vars(ns).items() if k not in ("run_folder", "dry")})
    args.work = args.work or str(pathlib.Path(ns.run_folder).resolve().parent)
    try:
        run = Run(ns.run_folder)
    except (OSError, ValueError, KeyError) as error:
        print("gate.py long-results: %s is not a run folder (%s: %s)" % (ns.run_folder, type(error).__name__, error), file=sys.stderr)
        return 2
    ctx = build_context(gate, args, ns.dry)
    if not ns.dry and not tracing_allowed(gate, args):
        ctx.play = None
        print("long-results: another long run holds the long lock: no traced run is made; a check that failed is not traced")
    results(run, ctx)
    return 0
