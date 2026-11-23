"""kinds.py: the kinds of result of a gate run (docs/plan/04-GATE-AND-MAIN.md section 9), without the game.

Every check and every test ends as one kind:
  pass   it passed;
  fail   the game or the change is wrong (the only kind that fails a run);
  tool   the test tool broke: the runner or a script raised, a result file is missing or cannot be read, or a check ran
         a different number of cases than it declares, or none; a picture or report the game writes at its exit that never
         came, the tablet's exit record that never came;
  env    the machine was not fit: the tablet away, the drive full, another graphics card than the approved pictures
         name, a lock not had in time, a test that reached its time limit with the game stepping at under 80% of full speed
         ("too-slow"; the gate runs it again alone, like a fail: alone it passes (flake), or it is a fail; a game that
         completed no step for 120 s is fail, "hung"; at 80% or more it is fail, "not-reached");
  flake  failed, then passed when run again alone (tests/flakes: <plan>/native/flakes.tsv);
  main   failed again alone, and fails on main's build in the same way: main is at fault, not the change;
  new    a picture with no approved copy, or one that the change declares (PICTURES-CHANGE.txt): it never fails a run, the
         gate approves it when the change lands (tools/gate/pictures.py);
  stress failed in the stress lane (twelve copies of the game at once, T12), then passed run alone: it never fails a run, is
         not written to the flakes file and does not count toward setting a test aside; the lane keeps the run folder and
         opens a repair (tools/gate/stress.py). It failing alone too is a plain fail; asked for by `--also stress` it is a fail.
  worse  the speed measure (tools/measure) found a figure past its band on the bad side twice in a row, in a landing's check
         measure/step/pc or measure/step/tablet: it fails a run like fail, is never run again alone (the measure measured
         twice itself), never written to the flakes file and never set aside. Its signature in the store of the same failure
         three times is the check and the worse figures' names in sorted order, never their values (worse_names).

Where each is decided:
  a test's pass, fail, tool or env   tools/lumacore/luma_test.py kind_of (written into result.json as "kind");
  tool for a result file the gate could not read, or a case count that is not the declared one   read_cases, here;
  a check's kind   state_of, here: the worst of its tests, in the order ORDER;
  flake and main   Settler.settle, here;
  the run's verdict   verdict_of, here: pass, fail (at least one counting fail), or not-judged (no fail, but a counting
  tool or env).

A check that runs tests hands the gate a Cases: each test's (kind, message), how many cases it declares, and a source
that can run some of its tests again (alone, or on main's build). Nothing here starts a game: a source does.
"""
import datetime
import json
import pathlib
import re
import subprocess

# The order a check's kind is the worst of its tests' (known, the list of tests expected to fail, and new, a picture to
# approve, sit just before pass: neither fails a run).
ORDER = ("fail", "worse", "tool", "env", "flake", "main", "stress", "known", "new", "pass")
RANK = {k: i for i, k in enumerate(ORDER)}
# What result.json's "kind" may say.
RESULT_KINDS = ("pass", "fail", "tool", "env")
FLAKE_DAYS = 7
FLAKES_TO_SET_ASIDE = 3
# A test that reached its time limit while the game ran at under 80% of full speed is env, verdict too-slow (luma_test.py
# evaluate), and its message begins so: the gate runs it again alone, as it does a fail.
SLOW_VERDICT = "too-slow"
SLOW_SPEED = re.compile(r"ran at (\d+)% of full speed")


def drive_full(error):
    """Whether an exception says the drive has no room (ENOSPC, or Windows' ERROR_DISK_FULL and ERROR_HANDLE_DISK_FULL):
    the machine was not fit, so env, not the tool's fault. (luma_test.py has its own copy: the runner stands alone.)"""
    return isinstance(error, OSError) and (error.errno == 28 or getattr(error, "winerror", None) in (39, 112))


def is_slow(r):
    """Whether a record is a test that was too slow when it ran with others (kind env, verdict too-slow), not yet run alone."""
    return (r["kind"] == "env" and r["counts"] and r["test"] != "-" and not r["final"]
            and r["message"].startswith(SLOW_VERDICT + ":"))


def needs_second_run(r):
    """Whether a record is run again alone: a fail, or a test that was too slow."""
    return (r["kind"] == "fail" and r["counts"] and r["test"] != "-" and not r["final"]) or is_slow(r)


def worst(kinds):
    """The worst of some kinds in ORDER ('pass' for none)."""
    return min(kinds, key=lambda k: RANK[k], default="pass")


def first_line(text, limit=240):
    """The first line with words in it, cut at `limit`."""
    for line in str(text).splitlines():
        if line.strip():
            return line.strip()[:limit]
    return ""


def say_number(value):
    return "-" if value is None else "%.6g" % value


def worse_message(worse):
    """The message of a `worse` result from [(figure, value, median, band)]: the figures' names first, in sorted order and joined
    by +, then each figure with its value, median and band: `worse: render_ms+step_ms: render_ms 8.2 (median 7.1, band 0.4); ...`."""
    ordered = sorted(worse)
    return "worse: %s: %s" % ("+".join(f[0] for f in ordered), "; ".join(
        "%s %s (median %s, band %s)" % (f[0], say_number(f[1]), say_number(f[2]), say_number(f[3])) for f in ordered))


WORSE_HEAD = re.compile(r"^worse: ([^\s:]+): ")


def worse_names(message):
    """The figures' names of a `worse` message (`render_ms+step_ms`): what the same-failure store keeps of it, never the values.
    The whole first line for a message that is not one."""
    found = WORSE_HEAD.match(message.strip())
    return found.group(1) if found else first_line(message)


def record(test, kind, message="", counts=True, note="", final=False):
    """One result a run reports: a test of a check ('-' for the check itself). `final`: not run again (a fault of a list,
    not of the game)."""
    return {"test": test, "kind": kind, "message": first_line(message), "counts": counts, "note": note, "final": final}


# --- Reading the runner's results -------------------------------------------------------------------------------------


def read_result(path, kind_of_verdict):
    """(kind, message) of one test's result.json. tool when the file is missing, cannot be parsed, is not an object or
    names no verdict. `kind_of_verdict` maps a verdict to a kind for a result that has no "kind" (an older runner)."""
    path = pathlib.Path(path)
    if not path.is_file():
        return "tool", "no result.json: the runner did not report this test"
    try:
        result = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, ValueError) as error:
        return "tool", "result.json cannot be read: %s" % first_line(error)
    verdict = result.get("verdict") if isinstance(result, dict) else None
    if not isinstance(verdict, str):
        return "tool", "result.json names no verdict"
    kind = result.get("kind")
    if kind not in RESULT_KINDS:
        kind = kind_of_verdict(verdict)
    reason = first_line(result.get("reason") or "")
    if kind == "pass":
        return "pass", ""
    return kind, reason if verdict == "fail" and reason else ("%s: %s" % (verdict, reason) if reason else verdict)


def result_path(work, suite_dir, test, renderer):
    """Where the runner writes a test's result (luma_test.py run_tests): runs/<suite>/<test>/<renderer>/result.json, or
    runs/<suite>/<test>/-/result.json for a test it could not read."""
    folder = pathlib.Path(work) / "runs" / suite_dir / test
    for place in (renderer, "-"):
        if (folder / place / "result.json").is_file():
            return folder / place / "result.json"
    return folder / renderer / "result.json"


def result_files(work, suite_dir, renderer):
    """{folder name: result.json} of every test the work folder holds a result for under this suite."""
    suite_folder = pathlib.Path(work) / "runs" / suite_dir
    out = {}
    for d in sorted(suite_folder.iterdir()) if suite_folder.is_dir() else []:
        for place in (renderer, "-"):
            if d.is_dir() and (d / place / "result.json").is_file():
                out[d.name] = d / place / "result.json"
                break
    return out


def read_cases(work, suite_dir, names, renderer, kind_of_verdict):
    """(results {test: (kind, message)}, declared, problem). `names`: the tests the suite lists (a test's result is in the
    folder of its name, or else in the one whose result.json names it), or None for one test folder, which declares one
    case, whatever its test is called. A test the suite lists that left no result is tool, and so is a count of results
    that is not the one declared, or none at all."""
    files = result_files(work, suite_dir, renderer)
    if names is None:
        results = {n: read_result(f, kind_of_verdict) for n, f in files.items()}
        declared = 1
    else:
        by_test = {}
        for n, f in files.items():
            try:
                by_test.setdefault(json.loads(f.read_text(encoding="utf-8")).get("test"), f)
            except (OSError, ValueError, AttributeError):
                pass
        results = {n: read_result(files.get(n) or by_test.get(n) or result_path(work, suite_dir, n, renderer), kind_of_verdict)
                   for n in names}
        declared = len(names)
    problem = None
    if declared == 0:
        problem = "the suite lists no case: nothing ran"
    elif not files:
        problem = "ran no case of the %d declared" % declared
    elif len(files) != declared:
        problem = "ran %d cases, %d declared" % (len(files), declared)
    return results, declared, problem


class Cases:
    """What a check that runs tests hands the gate. `results` {test: (kind, message)}; `declared` the number of cases it
    should run (None where a check has no such list); `problem` a check-level fault (a count that does not match: tool);
    `renderer` the renderer its tests ran on (the key of flakes and the set-aside list); `source` something that can run
    some of its tests again (see Settler); `final` the tests that are not run again (a fault of the lane's own rule, the same
    on every run: a validation layer's error); `extra` a sentence for the check's detail (what the lane counted);
    `stress` the check is a stress lane's (twelve copies at once): a test that fails there and passes alone is `stress`, not
    flake, and `stress_fails` (asked for by --also) makes it a fail."""

    def __init__(self, results, declared=None, problem=None, renderer="", source=None, final=(), extra="", stress=False,
                 stress_fails=False):
        self.results = dict(results)
        self.declared = declared
        self.problem = problem
        self.renderer = renderer
        self.source = source
        self.final = set(final)
        self.extra = extra
        self.stress = stress
        self.stress_fails = stress_fails


def cases_of_verdicts(verdicts, kind_of_verdict, renderer=""):
    """Cases from {test: verdict} (the rows a harness run printed): for the checks that do not read result files."""
    return Cases({t: (kind_of_verdict(v), "" if v == "pass" else v) for t, v in verdicts.items()}, renderer=renderer)


# --- A check's kind -----------------------------------------------------------------------------------------------------


def judge_cases(cases, known=None, aside=(), passed=True):
    """The records of a check's tests and its state: (records, state). A test in `known` {test: row} that is not a pass
    does not count (state known); one that passes fails the check (its line must go). A test whose (test, renderer) is in
    `aside` and failed does not count. A check-level problem is tool. `passed` False with no test to blame is a fail."""
    known = known or {}
    records = []
    for test, (kind, message) in cases.results.items():
        if kind == "pass":
            if test in known:
                records.append(record(test, "fail", "passes but is listed in known-failures.tsv, remove the line", final=True))
            continue
        if test in known:
            records.append(record(test, kind, message, counts=False, note="known (%s)" % known[test]["ledger row"]))
        elif kind == "fail" and (test, cases.renderer) in aside:
            records.append(record(test, kind, message, counts=False, note="set aside"))
        else:
            records.append(record(test, kind, message, final=test in cases.final))
    if cases.problem:
        records.append(record("-", "tool", cases.problem))
    if not passed and not records:
        records.append(record("-", "fail", "the check did not pass"))
    return records, state_of(records)


def describe(cases, records):
    """'N of M pass; not: ...' for a check that runs tests, after judging (and after settling): the tests that are not a
    pass with their kind (or how they were settled), then what listed or set aside tests and the check-level faults say."""
    total = len(cases.results)
    bad = [r for r in records if r["test"] != "-"]
    text = "%d of %d pass" % (total - len(bad), total)
    if bad:
        text += "; not: " + ", ".join("%s %s" % (r["test"], r["kind"]) for r in bad[:6]) + (" ..." if len(bad) > 6 else "")
    known = ["%s (%s)" % (r["test"], r["note"][7:-1]) for r in bad if r["note"].startswith("known (")]
    if known:
        text += "; known: " + ", ".join(known)
    listed = sorted(r["test"] for r in bad if r["message"].startswith("passes but is listed"))
    if listed:
        text += "; passes but is listed in known-failures.tsv, remove the line: " + ", ".join(listed)
    aside = sorted(r["test"] for r in bad if r["note"] == "set aside")
    if aside:
        text += "; set aside: " + ", ".join(aside)
    main = sorted(r["test"] for r in bad if r["kind"] == "main")
    if main:
        text += "; main is at fault: " + ", ".join(main)
    stress = sorted(r["test"] for r in bad if r["kind"] == "stress")
    if stress:
        text += "; failed under load, passed alone (stress): " + ", ".join(stress)
    if getattr(cases, "extra", ""):
        text += "; " + cases.extra
    problems = [r["message"] for r in records if r["test"] == "-"]
    if problems:
        text += "; " + "; ".join(problems)
    return text


def state_of(records):
    """A check's state from its records: the worst kind of those that count; known when only listed or set aside tests
    did not pass; else flake or main when a test was settled so, else pass."""
    kinds = [r["kind"] for r in records if r["counts"]]
    kinds += [r["kind"] for r in records if not r["counts"] and r["kind"] in ("flake", "main", "stress")]
    kinds += ["known" for r in records if not r["counts"] and r["kind"] not in ("flake", "main", "stress")]
    return worst(kinds)


def verdict_of(records_by_check):
    """pass, fail or not-judged from every record a run has: fail when one that counts is a fail; not-judged when none is
    but one that counts is tool or env; else pass. Records that do not count (known, set aside, flake, main, the owed
    tablet) never change it."""
    counting = [r["kind"] for records in records_by_check for r in records if r["counts"]]
    if "fail" in counting or "worse" in counting:
        return "fail"
    if "tool" in counting or "env" in counting:
        return "not-judged"
    return "pass"


EXIT_OF = {"pass": 0, "fail": 1, "not-judged": 2}


def verdict_line(verdict):
    """The last line of a run."""
    return "VERDICT " + verdict


def kind_lines(rows, say_aside=True):
    """The summary lines `KIND <kind> <check> <test> :: <first line of its message>`, one for each result that is not a
    pass: those settled as main first, then the rest in ORDER of kind. A result that does not count says why after its
    message ('known (P-1)', 'set aside', 'owed')."""
    items = []
    for row in rows:
        for r in row.get("records", ()):
            items.append((r["kind"] != "main", RANK[r["kind"]], len(items), row["check"], r))
    lines = []
    for _, _, _, check, r in sorted(items):
        message = r["message"] + (" [%s]" % r["note"] if r["note"] else "")
        lines.append("KIND %s %s %s :: %s" % (r["kind"], check, r["test"], message))
    return lines


def parse_kind_line(line):
    """(kind, check, test, message) of a KIND line, None for another line."""
    if not line.startswith("KIND "):
        return None
    head, _, message = line.partition(" :: ")
    words = head.split(" ", 3)
    return (words[1], words[2], words[3], message) if len(words) == 4 else None


# --- The lists outside the repository ---------------------------------------------------------------------------------


def parse_time(text):
    try:
        return datetime.datetime.fromisoformat(text)
    except ValueError:
        return None


def read_lines(path):
    path = pathlib.Path(path)
    if not path.is_file():
        return []
    return [l.split("\t") for l in path.read_text(encoding="utf-8").splitlines() if l.strip() and not l.startswith("#")]


def read_aside(path):
    """{(test, renderer): since} of the set-aside list: lines `time<TAB>test<TAB>renderer<TAB>why`."""
    return {(c[1], c[2]): c[0] for c in read_lines(path) if len(c) >= 3}


FLAKE_CHECK = "flake:"      # the repair's check of a test set aside: `flake:<test>`; the repair's renderer is the test's


def end_set_aside(path, check, renderer, note=""):
    """The test of repair check `flake:<test>` counts again: its line leaves set-aside.tsv (`path`), and its lines leave flakes.tsv beside
    it, so that the window of seven days starts new. True when a line of the set-aside list went."""
    if not str(check).startswith(FLAKE_CHECK):
        return False
    test = str(check)[len(FLAKE_CHECK):]
    path = pathlib.Path(path)
    removed = False
    for name in (path, path.with_name("flakes.tsv")):
        if not name.is_file():
            continue
        keep, dropped = [], False
        for line in name.read_text(encoding="utf-8").splitlines():
            c = line.split("\t")
            if len(c) > 2 and line.strip() and not line.startswith("#") and c[1] == test and c[2] == renderer:
                dropped = True
                continue
            keep.append(line)
        if dropped:
            name.write_text("\n".join(keep) + ("\n" if keep else ""), encoding="utf-8", newline="\n")
            removed = removed or name == path
    return removed


def flakes_within(path, test, renderer, now, days=FLAKE_DAYS):
    """How many lines of flakes.tsv (`time<TAB>test<TAB>renderer<TAB>commit<TAB>message`) name this test and renderer
    within `days` before `now`."""
    since = now - datetime.timedelta(days=days)
    count = 0
    for c in read_lines(path):
        when = parse_time(c[0]) if len(c) >= 5 else None
        if when is not None and when >= since and c[1] == test and c[2] == renderer:
            count += 1
    return count


def append_line(path, cells):
    path = pathlib.Path(path)
    path.parent.mkdir(parents=True, exist_ok=True)
    with open(path, "a", encoding="utf-8", newline="\n") as f:
        f.write("\t".join(str(c).replace("\t", " ").replace("\n", " ") for c in cells) + "\n")


# --- Run again alone, and on main's build ---------------------------------------------------------------------------------


class Settler:
    """After every lane has ended: each test that is fail is run once more alone; a pass is flake; one that still fails
    is run alone on main's build, and failing there in the same way is main. A test that was too slow (env, verdict
    too-slow) is run alone too: a pass is flake (the machine was busy); too slow again, or a fail, is a fail ("too slow even
    alone: the game ran at N% of full speed": the change made the game slow); it is not run on main's build. A source
    (Cases.source) does the runs:

      alone(names) -> {name: (kind, message)}        the same build, nothing else running, a fresh folder;
      adopt(name)                                    a flake's second run takes the place of its first, for the checks
                                                     that read the runs' files afterwards (pictures, replays);
      on_main(names) -> ({name: (kind, message)}, {name: why it was not checked})
                                                     the same tests on main's build, those whose definition differs from
                                                     main's or for which no build of main is kept skipped, with the reason.

    `say(text)` prints a line; `open_rows` collects what the person at the gate must write in the ledger."""

    def __init__(self, flakes, aside, commit, now=None, say=print):
        self.flakes, self.aside, self.commit = pathlib.Path(flakes), pathlib.Path(aside), commit
        self.now = now or datetime.datetime.now
        self.say = say
        self.open_rows = []
        self.newly_aside = []
        # The third flake of a test in seven days: [{check, test, renderer, message, count}]. The gate opens one repair for each (source
        # `flake`), and the repair, once landed, ends the time the test is set aside (end_set_aside).
        self.flake_results = []
        # The stress lane's (Cases.stress): what failed with the other copies running and passed alone, [{check, test, renderer,
        # message, folder}], and the tests that were only too slow under the load (not a fault, only counted).
        self.stress_results = []
        self.slow_under_load = []

    def settle(self, check, cases, records):
        """Records after the second run and the check against main."""
        failing = [r for r in records if r["kind"] == "fail" and r["counts"] and r["test"] != "-" and not r["final"]]
        slow = [r for r in records if is_slow(r)]
        if not (failing or slow) or cases.source is None:
            return records
        names = [r["test"] for r in failing + slow]
        self.say("%s: %d test%s failed or were too slow, run again alone: %s" % (
            check, len(names), "" if len(names) == 1 else "s", ", ".join(names[:6]) + (" ..." if len(names) > 6 else "")))
        try:
            again = cases.source.alone(names)
        except Exception as error:  # noqa: BLE001 - a second run that breaks leaves the first result standing
            again = {}
            self.say("%s: the second run broke (%s: %s); the first result stands" % (check, type(error).__name__, error))
        for r in slow:
            second = again.get(r["test"], ("tool", "the second run reported nothing"))
            if cases.stress and second[0] == "pass":
                # Slow with twelve copies is no fault: counted, the test is a pass, its second run takes the first's place.
                records.remove(r)
                cases.source.adopt(r["test"])
                self.slow_under_load.append(r["test"])
                cases.extra = "; ".join(x for x in (cases.extra, "%d run%s too slow under the load" % (
                    len(self.slow_under_load), "" if len(self.slow_under_load) == 1 else "s")) if x)
                self.say("%s: %s was too slow under the load and passed alone: only counted" % (check, r["test"]))
                continue
            self.settle_slow(check, cases, r, second)
        still = []
        for r in failing:
            kind, message = again.get(r["test"], ("tool", "the second run reported nothing"))
            if kind == "pass":
                first = r["message"]
                if cases.stress:
                    self.stress(check, cases, r, first)
                    continue
                r.update(kind="flake", counts=False, note="failed once, passed alone")
                cases.source.adopt(r["test"])
                self.flake(check, cases, r["test"], first)
            else:
                r["again"] = (kind, message)
                still.append(r)
        if still and cases.stress:
            for r in still:
                r["note"] = "failed alone too: not the load"
                self.say("%s: %s failed alone too: fail" % (check, r["test"]))
        elif still:
            self.against_main(check, cases, still)
        return records

    def stress(self, check, cases, r, first):
        """A test of the stress lane that failed with the other copies running and passed alone: kind stress (never fails a run,
        no line in flakes.tsv, not counted toward setting a test aside), its first failure kept for the repair; with
        Cases.stress_fails (the landing asked for the lane by --also: its proof that a fault under load is gone) a fail."""
        test = r["test"]
        cases.source.adopt(test)
        if cases.stress_fails:
            r.update(kind="fail", final=True, message="failed with the other copies running, passed alone: %s" % first)
            self.say("%s: %s failed with the other copies running and passed alone: fail (asked for by --also)" % (check, test))
            return
        r.update(kind="stress", counts=False, note="failed with the other copies running, passed alone")
        kept = getattr(cases.source, "kept_first", None)
        self.stress_results.append({"check": check, "test": test, "renderer": cases.renderer, "message": first,
                                    "folder": kept(test) if kept else ""})
        self.say("%s: %s STRESS (failed with the other copies running, passed alone): %s" % (check, test, first))

    def settle_slow(self, check, cases, r, second):
        """A test too slow in the crowd, after its run alone: a pass is flake; too slow again is fail, with its speed; a fail is
        fail; a second run that is tool or env (the machine not fit, nothing reported) leaves it env, as it was."""
        kind, message = second
        if kind == "pass":
            first = r["message"]
            r.update(kind="flake", counts=False, note="too slow with others, passed alone")
            cases.source.adopt(r["test"])
            self.flake(check, cases, r["test"], first)
            return
        speed = SLOW_SPEED.search(message)
        if kind == "env" and message.startswith(SLOW_VERDICT + ":") and speed:
            r.update(kind="fail", message="too slow even alone: the game ran at %s%% of full speed" % speed.group(1), final=True)
            self.say("%s: %s is too slow even alone (%s%% of full speed): fail" % (check, r["test"], speed.group(1)))
        elif kind == "fail":
            r.update(message="too slow with others, then failed alone: %s" % first_line(message), kind="fail", final=True)
            self.say("%s: %s was too slow with others and failed alone: fail" % (check, r["test"]))
        else:
            r["again"] = second
            r["note"] = "second run: %s" % first_line(message)

    def flake(self, check, cases, test, first_message):
        append_line(self.flakes, (self.now().isoformat(timespec="seconds"), test, cases.renderer, self.commit, first_message))
        n = flakes_within(self.flakes, test, cases.renderer, self.now())
        self.say("%s: %s FLAKE (failed once, passed alone; %d in %d days): %s" % (check, test, n, FLAKE_DAYS, first_message))
        if n >= FLAKES_TO_SET_ASIDE and (test, cases.renderer) not in read_aside(self.aside):
            append_line(self.aside, (self.now().isoformat(timespec="seconds"), test, cases.renderer,
                                     "%d flakes in %d days" % (n, FLAKE_DAYS)))
            self.newly_aside.append((test, cases.renderer))
            self.flake_results.append({"check": check, "test": test, "renderer": cases.renderer, "message": first_message, "count": n})
            self.say("%s: %s is set aside (%d flakes in %d days): a repair is opened for it" % (check, test, n, FLAKE_DAYS))

    def against_main(self, check, cases, still):
        names = [r["test"] for r in still]
        try:
            results, skipped = cases.source.on_main(names)
        except Exception as error:  # noqa: BLE001
            results, skipped = {}, {n: "the run on main's build broke: %s: %s" % (type(error).__name__, error) for n in names}
        for r in still:
            test = r["test"]
            if test in skipped:
                r["note"] = "not run on main: %s" % skipped[test]
                self.say("%s: %s: the check against main is skipped: %s" % (check, test, skipped[test]))
                continue
            kind, message = results.get(test, ("tool", "the run on main's build reported nothing"))
            same = {first_line(r["message"]), first_line(r["again"][1])}
            if kind == "fail" and first_line(message) in same:
                r.update(kind="main", counts=False, note="main is at fault: it fails the same way on main's build")
                self.open_rows.append("main is at fault: %s on %s fails on main's build too (%s)" % (
                    test, cases.renderer, first_line(message)))
                self.say("%s: %s: main is at fault, it fails the same way on main's build" % (check, test))
            else:
                r["note"] = "on main's build: %s" % (("passes" if kind == "pass" else "%s: %s" % (kind, first_line(message))))


# --- Whether main's build can say anything about a test ------------------------------------------------------------------


def git_ok(git, tree, *args):
    return subprocess.run([git, "-C", str(tree), *args], capture_output=True, text=True).returncode == 0


def definition_same(git, tree, ref, paths):
    """Whether every path (relative to the tree) is the same on `ref` and in the tree under test: present on ref, no
    difference from it in the files, no file the tree has and ref has not. A test whose definition differs says nothing
    about main's build."""
    for path in paths:
        listed = subprocess.run([git, "-C", str(tree), "ls-tree", "-r", "--name-only", ref, "--", path],
                                capture_output=True, text=True)
        if listed.returncode != 0 or not listed.stdout.strip():
            return False
        if not git_ok(git, tree, "diff", "--quiet", ref, "--", path):
            return False
        extra = subprocess.run([git, "-C", str(tree), "ls-files", "--others", "--exclude-standard", "--", path],
                               capture_output=True, text=True)
        if extra.returncode != 0 or extra.stdout.strip():
            return False
    return True


def base_of(base_bins, commit):
    """The kept copy of main's build for a commit (<base_bins>/<commit>/bin), or None."""
    folder = pathlib.Path(base_bins) / commit / "bin"
    return folder if commit and folder.is_dir() else None


def keep_base(sha, bin_dir, base_bins, keep=2):
    """A copy of the build's bin folder as <base_bins>/<sha>/bin, made when the gate moves main; the `keep` newest are
    kept. None when copied, else why not."""
    import shutil
    base = pathlib.Path(base_bins)
    try:
        base.mkdir(parents=True, exist_ok=True)
        copy = base / sha
        shutil.rmtree(copy, ignore_errors=True)
        shutil.copytree(bin_dir, copy / "bin")
        for old in sorted((d for d in base.iterdir() if d.is_dir()), key=lambda d: d.stat().st_mtime)[:-keep]:
            shutil.rmtree(old, ignore_errors=True)
    except OSError as error:
        return "main's build could not be kept: %s" % error
    return None
