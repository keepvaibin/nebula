"""github_rule.py: what GitHub's push workflow decides about a commit (package T8a, "the GitHub rule"), without GitHub.

The jobs of .github/workflows/check.yml have three classes (tools/gate/github_jobs.tsv):

  must      the move waits until every one is green for the commit; a red one stops the run (fail);
  nightly   left out of every rule here (the Windows jobs: this machine builds and tests Windows itself);
  advisory  never blocks: a red one is run again once, and if it is red again it is told in one line.

A job's state for a commit is read from every push run of that exact commit, under any branch name. Where the same job is
in more than one run, the newest run that has a result for it wins: a job green on the work branch's own earlier push
counts for the candidate, a run again that went green wins over the earlier red. A job still running has no result yet.

A job that ended cancelled or startup_failure, or red at a set-up step (a `uses:` step of check.yml: checkout, a set-up
action, a cache, an upload; setup_classifier), is not a red: it is run again once (`gh run rerun`), and the same job
ending that way a second time makes the run env ("GitHub could not run <job>"). A failed `run:` step stays a red. A job the table does not know counts as
must, and the run says so in one line of kind tool.

A landing that moves main before GitHub's jobs ended (the plan has `github`, not `github-first`: tools/gate/scope.py github_form)
leaves one line in the live folder's github-pending.tsv; `settle_pending` reads those lines later, one question for each, and
either removes the line (green), opens a repair (a red `must` job) or lets it stay (not finished, up to 120 minutes).

`Rule` holds what is remembered between two readings (which jobs were run again); gate.py gives it the readers of GitHub
(`runs_of`, `rerun`, `logs_of`), the tests give it fakes. Nothing here starts a process of its own.
"""
import datetime
import pathlib
import re
import subprocess
import sys
import threading
import time

HERE = pathlib.Path(__file__).resolve().parent
TABLE = HERE / "github_jobs.tsv"
WORKFLOW_FILE = HERE.parent.parent / ".github" / "workflows" / "check.yml"

MUST, NIGHTLY, ADVISORY = "must", "nightly", "advisory"
CLASSES = (MUST, NIGHTLY, ADVISORY)

GREEN_CONCLUSIONS = ("success",)
RED_CONCLUSIONS = ("failure", "timed_out")
# A job that did not run (an `if` that was false): it has no result, as one not ended has none.
SKIPPED_CONCLUSIONS = ("skipped",)
# Anything else that ended (cancelled, startup_failure, stale, action_required...) says nothing about the commit.

# After a job was run again its old end may still be shown for a moment: that is not a second end.
RERUN_GRACE_SECONDS = 180.0
LOG_KEEP, LOG_SHOW = 200, 20
POLL_SECONDS = 60.0
# While the gate waits for GitHub it prints a line when the reading's text changes, and at least one this often.
SCREEN_EVERY_SECONDS = 300.0
# What a question to GitHub can fail with: asked again the next minute, it is not a red job.
ANSWER_ERRORS = (RuntimeError, OSError, ValueError, subprocess.SubprocessError)


class TableError(Exception):
    """The table cannot be read: each problem is one line."""

    def __init__(self, problems):
        Exception.__init__(self, "\n".join(problems))
        self.problems = problems


# The table -----------------------------------------------------------------------------------------------------------


def parse_table(text, name="github_jobs.tsv"):
    """{job name: class} from the table's text: comment lines ('#'), the header `job<TAB>class`, then one row per job."""
    problems, jobs, header = [], {}, False
    for n, line in enumerate(text.splitlines(), 1):
        if not line.strip() or line.lstrip().startswith("#"):
            continue
        cells = [c.strip() for c in line.split("\t")]
        if not header:
            if cells != ["job", "class"]:
                problems.append("%s:%d: the first line is the header: job<TAB>class" % (name, n))
            header = True
            continue
        if len(cells) != 2 or not all(cells):
            problems.append("%s:%d: a row is a job name and its class, with a tab between" % (name, n))
            continue
        if cells[1] not in CLASSES:
            problems.append("%s:%d: %r is not a class (%s)" % (name, n, cells[1], ", ".join(CLASSES)))
        if cells[0] in jobs:
            problems.append("%s:%d: %r is in the table twice" % (name, n, cells[0]))
        jobs[cells[0]] = cells[1]
    if not jobs and not problems:
        problems.append("%s: no row" % name)
    if problems:
        raise TableError(problems)
    return jobs


def read_table(path=TABLE):
    path = pathlib.Path(path)
    try:
        text = path.read_text(encoding="utf-8")
    except OSError as error:
        raise TableError(["%s: cannot be read: %s" % (path, error)])
    return parse_table(text, path.name)


# A `uses:` step that is the check itself, not a set-up: a failure of it is the change's (red), not GitHub's.
REAL_WORK_ACTIONS = ("./.github/actions/start-check-windows",)
# Steps GitHub adds to every job: its set-up and its end, and the post steps of the actions that have one.
AUTOMATIC_SETUP = ("Set up job", "Complete job")


def workflow_job_names(text):
    """The names of the jobs of a workflow file, as GitHub shows them: each job's `name:` with its matrix legs written out
    (`Rust tests (${{ matrix.os }})` once for each os). Exact for what check.yml does: a `name:` at the job's own indent, a
    `matrix:` whose values are a flow list (`[a, b]`) or an expression holding JSON lists in quotes (the union of them)."""
    return [name for name, _ in workflow_jobs(text)]


def workflow_steps(text):
    """{job name: {step name: 'uses' | 'run'}} as GitHub names the steps: a step's `name:`, else `Run ` and its action or its
    first line. 'uses' is a set-up step (checkout, a set-up action, a cache, an upload); 'run' a command of the job, and the
    action that is the check itself (REAL_WORK_ACTIONS)."""
    return {name: steps for name, steps in workflow_jobs(text)}


def step_kind(steps, name):
    """'uses' (a set-up step) or 'run' for a step of a job whose steps workflow_steps gave; a name it does not know is 'run'
    (a failure there stays red)."""
    if name in AUTOMATIC_SETUP or name.startswith("Post "):
        return "uses"
    return steps.get(name, "run")


def parse_steps(block):
    """{step name: kind} of one job's lines: the items of its `steps:` list."""
    steps, item = {}, None
    items = []
    for line in block:
        if re.match(r"^      - ", line):
            item = [line]
            items.append(item)
        elif item is not None and re.match(r"^        \S", line):
            item.append(line)
        elif item is not None and re.match(r"^        ", line) is None and line.strip() and not re.match(r"^      ", line):
            item = None
    for item in items:
        fields, lines = {}, [item[0][8:]] + [l[8:] for l in item[1:]]
        for n, line in enumerate(lines):
            m = re.match(r"^([A-Za-z-]+):\s*(.*?)\s*$", line)
            if m and m.group(1) in ("name", "uses", "run") and m.group(1) not in fields:
                value = m.group(2)
                if value in ("|", ">", "|-", ">-") and n + 1 < len(lines):
                    value = lines[n + 1].strip()
                fields[m.group(1)] = value.strip("'\"")
        if "name" in fields:
            name = fields["name"]
        elif "uses" in fields:
            name = "Run " + fields["uses"]
        elif "run" in fields:
            name = "Run " + fields["run"]
        else:
            continue
        steps[name] = "uses" if "uses" in fields and fields["uses"] not in REAL_WORK_ACTIONS else "run"
    return steps


def workflow_jobs(text):
    """[(job name, {step name: kind})] of a workflow file, a matrix's legs written out (each with the job's steps)."""
    lines = text.splitlines()
    names, i = [], 0
    while i < len(lines) and not re.match(r"^jobs:\s*$", lines[i]):
        i += 1
    i += 1
    blocks, current = [], None
    while i < len(lines):
        line = lines[i]
        if re.match(r"^\S", line) and line.strip() and not line.startswith("#"):
            break
        if re.match(r"^  [A-Za-z0-9_-]+:\s*$", line):
            current = []
            blocks.append(current)
        elif current is not None:
            current.append(line)
        i += 1
    for block in blocks:
        name, matrix = None, {}
        in_matrix, matrix_indent = False, 0
        for line in block:
            m = re.match(r"^    name:\s*(.*?)\s*$", line)
            if m and name is None:
                name = m.group(1).strip("'\"")
                continue
            m = re.match(r"^(\s+)matrix:\s*$", line)
            if m:
                in_matrix, matrix_indent = True, len(m.group(1))
                continue
            if in_matrix:
                if line.strip() and not line.lstrip().startswith("#"):
                    indent = len(line) - len(line.lstrip())
                    if indent <= matrix_indent:
                        in_matrix = False
                        continue
                    m = re.match(r"^\s+([A-Za-z0-9_-]+):\s*(.*?)\s*$", line)
                    if m:
                        matrix[m.group(1)] = matrix_values(m.group(2))
        if name is None:
            continue
        legs = [name]
        for key, values in matrix.items():
            token = "${{ matrix.%s }}" % key
            if any(token in n for n in legs):
                legs = [n.replace(token, v) for n in legs for v in values]
        steps = parse_steps(block)
        names += [(leg, steps) for leg in legs]
    return names


def matrix_values(text):
    """The values of one matrix key: `[a, 'b,c']` (a flow list) or an expression with JSON lists in quotes."""
    if text.startswith("["):
        return [v.strip().strip("'\"") for v in re.findall(r"""'[^']*'|"[^"]*"|[^,\[\]\s][^,\[\]]*""", text)]
    out = []
    for arr in re.findall(r"'(\[[^\]]*\])'", text):
        for v in re.findall(r'"([^"]*)"', arr):
            if v not in out:
                out.append(v)
    return out


def table_problems(classes, workflow_text):
    """What differs between the table and the workflow file's job names, one line each (none when they agree)."""
    names = workflow_job_names(workflow_text)
    problems = ["the workflow has the job %r and the table does not" % n for n in names if n not in classes]
    problems += ["the table has the job %r and the workflow does not" % n for n in classes if n not in names]
    return problems


def file_name_of(job):
    """The log's file name: github-<job name with spaces as dashes>.log (and nothing a Windows file name refuses)."""
    return "github-%s.log" % re.sub(r'[\s<>:"/\\|?*]+', "-", job).strip("-")


# What a job's results say --------------------------------------------------------------------------------------------


def setup_classifier(workflow_text):
    """`setup(job)` for judge: the name of the set-up step a red job failed at, else None. The job record has its steps
    (`gh run view --json jobs`: name, number, conclusion); the first one that failed is looked up in the workflow file's own
    steps of that job: a `uses:` step (checkout, a set-up action, a cache, an upload, GitHub's own set-up) is GitHub's fault,
    a `run:` step the job's. A job with no steps listed, or a step the file does not name, stays red."""
    steps_of = workflow_steps(workflow_text)

    def setup(job):
        failed = [s for s in job.get("steps") or [] if s.get("conclusion") in RED_CONCLUSIONS]
        if not failed:
            return None
        first = min(failed, key=lambda s: s.get("number") or 0)
        name = first.get("name") or ""
        return name if step_kind(steps_of.get(job.get("name") or "", {}), name) == "uses" else None
    return setup


def job_state(job, setup=None):
    """green, red, skipped, broken (ended cancelled, startup_failure..., or red at a set-up step: `setup`) or running (not
    ended) for one job of one run."""
    if job.get("status") != "completed":
        return "running"
    conclusion = job.get("conclusion")
    if conclusion in GREEN_CONCLUSIONS:
        return "green"
    if conclusion in RED_CONCLUSIONS:
        return "broken" if setup is not None and setup(job) else "red"
    if conclusion in SKIPPED_CONCLUSIONS:
        return "skipped"
    return "broken"


def minute_of(job):
    """' at HH:MM' (this computer's time) of a job's end, from GitHub's completedAt; '' where it has none."""
    when = job.get("completedAt") or ""
    try:
        moment = datetime.datetime.fromisoformat(when.replace("Z", "+00:00"))
    except ValueError:
        return ""
    if moment.year < 2000:  # GitHub's zero time: not ended
        return ""
    return " at " + moment.astimezone().strftime("%H:%M")


class State:
    """One job as the commit's runs say it: `state` (green, red, broken, running, absent), the run and job it comes from,
    that run's branch, and the job's own record."""

    def __init__(self, name, klass, state, run_id=None, job_id=None, branch="", job=None):
        self.name, self.klass, self.state = name, klass, state
        self.run_id, self.job_id, self.branch, self.job = run_id, job_id, branch, job or {}

    def said(self, minute=True):
        """`Mac failure on gate/x at 10:34`: the end of a red or broken job, for a line (without the minute, the line is the
        same from one run to the next: the landing's count of the same failure reads it)."""
        return "%s %s on %s%s" % (self.name, self.job.get("conclusion") or "?", self.branch,
                                  minute_of(self.job) if minute else "")


def effective(entries, setup=None):
    """The one result a job has for the commit, from its entries in the runs, newest run first: the newest run that has a
    result (green or red), else one still running, else one that ended broken, else none ('absent')."""
    kinds = [(job_state(job, setup), run, job) for run, job in entries]
    for wanted in (("green", "red"), ("running",), ("broken",)):
        for state, run, job in kinds:
            if state in wanted:
                return state, run, job
    return "absent", None, None


def judge(runs, classes, planned=(), setup=None):
    """{job name: State} of the commit from its runs ([{id, branch, jobs}], any order). Every job the table calls must or
    advisory is in it (`absent` when no run has it yet); a job no table row names is in it as must; the nightly ones and
    the planned ones (planned.tsv's github/<job> lines) are left out."""
    entries = {}
    for run in sorted(runs or [], key=lambda r: r.get("id") or 0, reverse=True):
        for job in run.get("jobs") or []:
            entries.setdefault(job.get("name") or "?", []).append((run, job))
    out = {}
    for name in list(classes) + [n for n in entries if n not in classes]:
        klass = classes.get(name)
        if klass == NIGHTLY or name in planned:
            continue
        # A matrix job that a branch push skips shows under its unexpanded name (`Android (${{ matrix.abi }})`); a job no
        # table row names that was skipped in every run that lists it never ran anywhere, so it is no job of the rule.
        if klass is None and all(job_state(job) == "skipped" for _, job in entries[name]):
            continue
        state, run, job = effective(entries.get(name, []), setup)
        out[name] = State(name, klass or MUST, state, (run or {}).get("id"), (job or {}).get("databaseId"),
                          (run or {}).get("branch") or "?", job)
    return out


# One reading ---------------------------------------------------------------------------------------------------------


class Finding:
    """A job the rule stops on: the job, what to say, and where its log is (run and job ids)."""

    def __init__(self, state, text):
        self.job, self.text = state.name, text
        self.run_id, self.job_id, self.state = state.run_id, state.job_id, state
        self.log = None   # the saved log file, when one was saved


class Reading:
    """What one reading of GitHub says: `green` and `pending` job names (must), and the `reds` and `envs` (Findings)."""

    def __init__(self):
        self.green, self.pending, self.reds, self.envs = [], [], [], []

    @classmethod
    def of(cls, reds=(), envs=(), pending=(), green=()):
        reading = cls()
        reading.reds, reading.envs, reading.pending, reading.green = list(reds), list(envs), list(pending), list(green)
        return reading

    @property
    def done(self):
        return not (self.pending or self.reds or self.envs)

    def detail(self):
        if self.reds:
            return "red: " + "; ".join(f.text for f in self.reds)
        if self.envs:
            return "; ".join(f.text for f in self.envs)
        if self.pending:
            return "%d green, not finished: %s" % (len(self.green), ", ".join(self.pending))
        return "%d must jobs green" % len(self.green)


class Rule:
    """The rule for one commit. `runs_of(sha)` -> [{id, branch, jobs}] (every push run of the commit); `rerun(run_id, job_id)`
    -> (ok, text); `classes` {job: class}; `planned` the jobs planned.tsv takes out. `say` prints a line, `log` writes the
    notes of the run (github.log)."""

    def __init__(self, sha, classes, runs_of, rerun=None, planned=(), clock=time.time, say=print, note=None, setup=None):
        self.sha, self.classes, self.runs_of, self.rerun = sha, classes, runs_of, rerun
        self.setup = setup        # setup(job) -> the set-up step a red job failed at, else None (setup_classifier)
        self.last_reds = []       # the red must jobs (States) red_before found
        self.planned, self.clock, self.say = set(planned), clock, say
        self.note = note or (lambda text: None)
        self.reruns = {}          # job name -> (the job id that was run again, when)
        self.unknown = []         # job names the table does not know, in the order seen
        self.advised = {}         # advisory job name -> the line told once
        self.told_rows = set()    # the warnings and advisory lines already made rows of the run (gate.py)
        self.shown = (None, None)  # (the text, the clock) of the last reading's line printed on the screen
        self.lock = threading.Lock()

    def read(self):
        """One reading of GitHub: raises what a question to it raises (ANSWER_ERRORS)."""
        runs = self.runs_of(self.sha)
        with self.lock:
            return self.judge(runs)

    def judge(self, runs):
        reading = Reading()
        for name, st in sorted(judge(runs, self.classes, self.planned, self.setup).items()):
            self.see_unknown(name)
            if st.klass == ADVISORY:
                if st.state in ("red", "broken") and not self.just_rerun(st):
                    self.advise(st)
                continue
            if st.state == "green":
                reading.green.append(name)
            elif st.state == "red":
                if self.just_rerun(st):
                    reading.pending.append(name)   # --github-again: the end it had before the request, still shown
                else:
                    reading.reds.append(Finding(st, st.said()))
            elif st.state == "broken":
                if self.again(st):
                    step = self.setup(st.job) if self.setup is not None else None
                    reading.envs.append(Finding(st, "GitHub could not run %s%s" % (
                        name, (" (its set-up step %r failed)" % step) if step else "")))
                else:
                    reading.pending.append(name)
            else:
                reading.pending.append(name)
        return reading

    def just_rerun(self, st):
        """Whether this job was run again by the gate a moment ago and its old end is what is still shown."""
        record = self.reruns.get(st.name)
        return record is not None and st.job_id == record[0] and self.clock() - record[1] < RERUN_GRACE_SECONDS

    def rerun_red(self, st):
        """--github-again: a red job run again once, at the gate's request. (ok, what happened); the job is then waited for
        like one that was run again after it was cancelled, and a second red is a red."""
        if self.rerun is None:
            return False, "no way to run it again"
        ok, text = self.rerun(st.run_id, st.job_id)
        self.note("%s ended %s (run %s, job %s): run again once, at the request of --github-again: %s" % (
            st.name, st.job.get("conclusion"), st.run_id, st.job_id, text))
        if ok:
            self.reruns[st.name] = (st.job_id, self.clock())
        return ok, text

    def again(self, st):
        """Whether a broken (or red advisory) job already ended that way after it was run again once: True when it did.
        Otherwise the job is run again now (a refused request is made again at the next reading), and the answer is False."""
        record = self.reruns.get(st.name)
        if record is None:
            if self.rerun is None:
                return False
            ok, text = self.rerun(st.run_id, st.job_id)
            self.note("%s ended %s (run %s, job %s): run again once: %s" % (
                st.name, st.job.get("conclusion"), st.run_id, st.job_id, text))
            if ok:
                self.reruns[st.name] = (st.job_id, self.clock())
            return False
        job_id, when = record
        if st.job_id == job_id and self.clock() - when < RERUN_GRACE_SECONDS:
            return False    # the end it had before the request, still shown
        self.note("%s ended %s again (run %s, job %s)" % (st.name, st.job.get("conclusion"), st.run_id, st.job_id))
        return True

    def advise(self, st):
        """An advisory job that ended red or broken: run again once; at its second end one line, and nothing else."""
        if st.name in self.advised or not self.again(st):
            return
        self.advised[st.name] = "red twice on GitHub; advisory, blocks nothing"

    def see_unknown(self, name):
        """A job the table does not know, at the first reading that sees it: one line on the screen and in github.log that
        names the job and says what to do (the end of the run says it again as a row of kind tool)."""
        if name in self.classes or name in self.unknown:
            return
        self.unknown.append(name)
        text = unknown_text(name)
        self.note(text)
        self.say("gate.py: " + text)

    def warnings(self):
        """[(job, text)]: one line of kind tool for each job the table does not know."""
        return [(n, unknown_text(n)) for n in self.unknown]

    def show(self, text):
        """One line on the screen with the reading's words (the same text as github.log): when the text changed since the line
        last printed, and at least every SCREEN_EVERY_SECONDS."""
        said, when = self.shown
        now = self.clock()
        if said is None or text != said or now - when >= SCREEN_EVERY_SECONDS:
            self.shown = (text, now)
            self.say("gate.py: GitHub: " + text)

    def red_before(self):
        """Why a main gate would not start (rule 5): the red must jobs of the commit, `Mac failure on gate/x at 10:34`, or
        None when none is red or GitHub does not answer (the run then starts, and its own reading asks again)."""
        try:
            runs = self.runs_of(self.sha)
        except ANSWER_ERRORS:
            return None
        with self.lock:
            states = [s for s in judge(runs, self.classes, self.planned, self.setup).values() if s.klass == MUST]
            for s in states:
                self.see_unknown(s.name)
            self.last_reds = [s for s in states if s.state == "red" and not self.just_rerun(s)]
        red = [s.said() for s in self.last_reds]
        return ("red on GitHub: " + "; ".join(red[:4]) + (" ..." if len(red) > 4 else "")) if red else None

    def look(self):
        """(kind, detail, reading) from ONE reading, no waiting: `pass` (every must job green), `fail` (one is red), `env` (one
        ended broken twice), `pending` (not all ended, or GitHub did not answer: the reading is None then)."""
        try:
            reading = self.read()
        except ANSWER_ERRORS as e:
            return "pending", "GitHub did not answer: %s" % e, None
        detail = reading.detail()
        self.note(detail)
        if reading.reds:
            return "fail", detail, reading
        if reading.envs:
            return "env", detail, reading
        return ("pass" if reading.done else "pending"), detail, reading

    def wait(self, minutes, sleep=time.sleep, log=None):
        """(kind, detail, reading): asks GitHub once a minute until every must job is green (pass), one is red (fail) or ended
        broken twice (env), or `minutes` have passed (env, naming the jobs not finished). Not answering is asked again."""
        deadline = self.clock() + 60.0 * minutes
        while True:
            try:
                reading = self.read()
                detail = reading.detail()
            except ANSWER_ERRORS as e:
                reading, detail = None, "GitHub did not answer: %s" % e
            self.note(detail)
            self.show(detail)
            if reading is not None:
                if reading.reds:
                    return "fail", detail, reading
                if reading.envs:
                    return "env", detail, reading
                if reading.done:
                    return "pass", detail, reading
            if self.clock() >= deadline:
                late = ", ".join(reading.pending) if reading is not None else "all of them"
                return "env", "GitHub did not finish: %s (no result after %g minutes)" % (late, minutes), reading
            sleep(POLL_SECONDS)


# The log of a red job ------------------------------------------------------------------------------------------------


def unknown_text(name):
    """What is said of a job no row of tools/gate/github_jobs.tsv names: that it is waited for like a must job, and the way out."""
    return ("GitHub job %s is not in tools/gate/github_jobs.tsv: it is waited for like a must job. Add a line for it to that file "
            "(class must, advisory or nightly)" % name)


def advice(job):
    """The last line of what is said of a red must job: the way out when GitHub's side failed inside a `run:` step."""
    return ("If %s shows a download or an install failing, and not the game's own build or tests: land again with "
            "--github-again." % file_name_of(job))


def save_log(finding, folder, logs_of, say=print, advise=True):
    """Saves the last LOG_KEEP lines of the red job's log in `folder` as github-<job>.log and prints the first LOG_SHOW of
    them under a line that names the job, then the advice line (advice). A log that cannot be had is told in one line; the
    verdict stays."""
    where = "run %s, job %s" % (finding.run_id, finding.job_id)
    say("gate.py: GitHub job %s (%s)" % (finding.text, where))
    try:
        return _save_log(finding, folder, logs_of, say)
    finally:
        if advise:
            say(advice(finding.job))


def _save_log(finding, folder, logs_of, say):
    try:
        text = logs_of(finding.job_id)
        lines = (text or "").splitlines()[-LOG_KEEP:]
        if not lines:
            raise RuntimeError("it has no log lines")
        path = pathlib.Path(folder) / file_name_of(finding.job)
        path.write_text("\n".join(lines) + "\n", encoding="utf-8")
    except (*ANSWER_ERRORS, KeyError) as e:
        say("gate.py: the log of %s could not be fetched: %s" % (finding.job, e))
        return None
    finding.log = path
    say("gate.py: its log is in %s; the first %d of %d lines:" % (path, min(LOG_SHOW, len(lines)), len(lines)))
    for line in lines[:LOG_SHOW]:
        say("  " + line)
    return path


# The commits whose jobs are read after the move ---------------------------------------------------------------------

PENDING_MINUTES = 120


def first_log_line(text):
    """The first line of what save_log keeps of a log (its last LOG_KEEP lines) that has a word: what the gate calls the first
    error line of a red job (--github-again takes it the same way)."""
    return next((l.strip() for l in (text or "").splitlines()[-LOG_KEEP:] if l.strip()), "")


def _live():
    """tools/plan/live.py, the one writer of the repairs, the faults and the pending file."""
    folder = str(HERE.parent / "plan")
    if folder not in sys.path:
        sys.path.insert(0, folder)
    import live
    return live


def encode_reruns(reruns):
    """{job: (job id, when)} as the pending file keeps it: `id@epoch@name|id@epoch@name`."""
    return "|".join("%s@%d@%s" % (job_id, when, name) for name, (job_id, when) in sorted(reruns.items())) or "-"


def decode_reruns(text):
    out = {}
    for item in (text or "").split("|"):
        parts = item.split("@", 2)
        if len(parts) == 3 and parts[1].isdigit():
            out[parts[2]] = (int(parts[0]) if parts[0].isdigit() else parts[0], float(parts[1]))
    return out


def repair_text(state, sha, branches, first):
    """The text of the repair of a red job: what is red, where it landed from, and the first error line of its log."""
    return "GitHub's job %s is red on %s, landed from %s: %s" % (state.name, sha[:9], branches or "?", first or "(no log line)")


def repair_note(state, sha, branches, first):
    """The repair's note: the text, the job's page and the command that prints the failed part of its log."""
    page = state.job.get("url") or "(GitHub gave no address for the job)"
    return "%s | page: %s | log: gh run view %s --log-failed --job %s" % (
        repair_text(state, sha, branches, first), page, state.run_id, state.job_id)


def settle_pending(live, rule_for, delete_candidate, logs_of, say=print, now=None, limit_minutes=PENDING_MINUTES):
    """Reads the commits that moved main before GitHub's jobs ended (the lines of <live>/github-pending.tsv): one reading for each,
    no waiting. Returns what happened [(commit, what, text)], `what` one of green, red, kept, none, away.

      every `must` job green        the line is removed, the candidate branch deleted; `GitHub: <sha> green, N minutes after the move`
                                    is printed and added to github.log of the landing's run folder
      a `must` job red              the line is removed, the candidate deleted, and a repair opened for each red job (kind fail, check
                                    github/<job>, source github): live.add_repair, the one `plan.py repair add` writes with; a job whose
                                    repair is open adds a line to that repair (the commit and the branches) and opens nothing
      not finished, < 120 minutes   the line stays
      not finished, >= 120 minutes  the line is removed, the candidate deleted, and `GitHub gave no result for <sha> in 120 minutes`
                                    is a line of tool-faults.tsv (kind ENV)
      GitHub cannot be reached      nothing changes, one printed line, the rest wait for the next call

    `rule_for(sha)` is a Rule of the commit (its runs_of and rerun read GitHub); `delete_candidate(branch)`; `logs_of(job id)`
    the failed steps' log of a job. It never raises."""
    out = []
    try:
        lv = _live()
        rows = lv.read_pending(live)
        when = now or datetime.datetime.now()
        for row in rows:
            try:
                out.append(_settle_one(lv, live, row, rule_for, delete_candidate, logs_of, say, when, limit_minutes))
            except ANSWER_ERRORS as e:
                said = str(e).splitlines()[0] if str(e) else type(e).__name__
                say("gate.py: GitHub cannot be reached (%s); %s stays pending" % (said, row["commit"][:9]))
                out.append((row["commit"], "away", str(e)))
                break      # the next line would meet the same answer
    except Exception as e:  # noqa: BLE001 - what comes after a move is a report: it never fails the caller
        say("gate.py: the pending GitHub results could not be read (%s: %s)" % (type(e).__name__, e))
    return out


def _settle_one(lv, live, row, rule_for, delete_candidate, logs_of, say, now, limit_minutes):
    sha, branches = row["commit"], row["branches"]
    moved = datetime.datetime.fromisoformat(row["time"]) if row["time"] else now
    minutes = max(0, int((now - moved).total_seconds() // 60))
    rule = rule_for(sha)
    rule.reruns = decode_reruns(row.get("rerun"))
    before = encode_reruns(rule.reruns)
    reading = rule.read()
    after = encode_reruns(rule.reruns)
    if after != before:
        lv.set_pending_rerun(live, sha, None if after == "-" else after)
    folder = pathlib.Path(row["run_folder"]) if row.get("run_folder") not in (None, "", "-") else None

    def note(text):
        if folder is not None and folder.is_dir():
            try:
                with open(folder / "github.log", "a", encoding="utf-8") as f:
                    f.write("%s %s\n" % (datetime.datetime.now().strftime("%H:%M:%S"), text))
            except OSError:
                pass

    def done():
        lv.drop_pending(live, sha)
        if row.get("candidate") not in (None, "", "-"):
            delete_candidate(row["candidate"])

    if reading.reds:
        told = []
        for finding in reading.reds:
            first = ""
            try:
                first = first_log_line(logs_of(finding.job_id))
            except (*ANSWER_ERRORS, KeyError):
                pass
            step = next((b.split("/", 1)[1] for b in branches.replace(",", " ").split() if b.startswith("native/")), "-")
            repair, new = lv.add_repair(
                live, "github", "fail", "github/" + finding.job, "-", step, sha, str(folder or "-"), "-",
                repair_note(finding.state, sha, branches, first),
                seen_note="red again on %s, landed from %s" % (sha[:9], branches or "?"))
            told.append("%s (%s %s)" % (finding.job, "repair" if new else "added to repair", repair["id"]))
        done()
        text = "GitHub: %s red, %d minutes after the move: %s" % (sha[:9], minutes, "; ".join(told))
        say("gate.py: " + text)
        note(text)
        return sha, "red", text
    if reading.done:
        done()
        text = "GitHub: %s green, %d minutes after the move" % (sha[:9], minutes)
        say(text)
        note(text)
        return sha, "green", text
    if minutes >= limit_minutes:
        text = "GitHub gave no result for %s in %d minutes" % (sha[:9], limit_minutes)
        lv.append_fault(live, "1", "", "ENV", "github/after", text, run_folder=str(folder or ""))
        done()
        say("gate.py: " + text)
        note(text)
        return sha, "none", text
    return sha, "kept", "not finished, %d minutes after the move: %s" % (minutes, ", ".join(reading.pending) or "GitHub's jobs")
