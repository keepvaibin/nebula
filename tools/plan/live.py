"""live.py: the native plan's live state, shared by every agent and the progress page (package T13, items 5 to 7).

Everything two agents must both see lives in one folder (plan.py's LIVE constant, D:/NebulaWork/plan/native; tests give
it a temporary folder), never in an untracked file inside one work folder:

    state.tsv         step, state, agent, reason, time   the started and blocked steps (`landed` is in the repository)
    claims.tsv        package, agent, time, branch       who holds which package
    tool-faults.tsv   time, agent, step, kind, check, test, first_error, run_folder, could_not
    landings.tsv      step, commit, finished, seconds, agent
    progress.html, progress-data.json, progress-url.txt  (page.py and plan.py page)

Every change is made under one lock folder (`lock`, made with mkdir, which is atomic) and written as a whole file
replaced in one step, so a reader never sees half a file. A lock folder older than two minutes is taken as left by a
dead process and removed. Cells never hold a tab or a line break. Python standard library only.
"""
import contextlib
import datetime
import os
import pathlib
import re
import tempfile
import time

import failures

STATE_COLUMNS = ["step", "state", "agent", "reason", "time"]
CLAIM_COLUMNS = ["package", "agent", "time", "branch"]
FAULT_COLUMNS = ["time", "agent", "step", "kind", "check", "test", "first_error", "run_folder", "could_not"]
LANDING_COLUMNS = ["step", "commit", "finished", "seconds", "agent"]
STALE_HOURS = 48


class Busy(RuntimeError):
    pass


def stamp(now=None):
    return (now or datetime.datetime.now()).isoformat(timespec="seconds")


def parse_stamp(text):
    try:
        return datetime.datetime.fromisoformat(text)
    except (TypeError, ValueError):
        return None


def clean(value):
    return re.sub(r"[\t\r\n]+", " ", str(value)).strip()


@contextlib.contextmanager
def locked(live, timeout=30.0, stale=120.0):
    """Holds the lock folder of `live` (made if it is not there) for the block; waits up to `timeout` seconds."""
    live = pathlib.Path(live)
    live.mkdir(parents=True, exist_ok=True)
    lock = live / "lock"
    deadline = time.time() + timeout
    while True:
        try:
            os.mkdir(lock)
            break
        except FileExistsError:
            try:
                if time.time() - lock.stat().st_mtime > stale:
                    os.rmdir(lock)
                    continue
            except OSError:
                pass
            if time.time() > deadline:
                raise Busy("the lock folder %s is held by another process" % lock)
            time.sleep(0.05)
    try:
        yield
    finally:
        try:
            os.rmdir(lock)
        except OSError:
            pass


def atomic_write(path, text):
    path = pathlib.Path(path)
    path.parent.mkdir(parents=True, exist_ok=True)
    fd, tmp = tempfile.mkstemp(dir=str(path.parent), prefix=path.name + ".")
    try:
        with os.fdopen(fd, "w", encoding="utf-8", newline="\n") as f:
            f.write(text)
        os.replace(tmp, path)
    except BaseException:
        if os.path.exists(tmp):
            os.unlink(tmp)
        raise


def read_rows(path, columns):
    """[dict] from a tab-separated file with a header line; a missing file has no rows; a short row is padded."""
    path = pathlib.Path(path)
    if not path.exists():
        return []
    rows = []
    for line in path.read_text(encoding="utf-8").splitlines()[1:]:
        if not line.strip():
            continue
        cells = line.split("\t")
        cells += [""] * (len(columns) - len(cells))
        rows.append(dict(zip(columns, cells[:len(columns)])))
    return rows


def write_rows(path, columns, rows):
    text = "\t".join(columns) + "\n" + "".join("\t".join(clean(r.get(c, "")) for c in columns) + "\n" for r in rows)
    atomic_write(path, text)


# ---- started and blocked steps --------------------------------------------------------------------------------

def read_state(live):
    """{step: {state, agent, reason, time}} for the started and blocked steps."""
    return {r["step"]: r for r in read_rows(pathlib.Path(live) / "state.tsv", STATE_COLUMNS) if r["state"] in ("started", "blocked")}


def set_state_entry(live, step, state, agent=1, reason="", now=None):
    """A step becomes `started` or `blocked` (by `agent`, with the reason) or, for any other state, leaves the file."""
    path = pathlib.Path(live) / "state.tsv"
    with locked(live):
        rows = {r["step"]: r for r in read_rows(path, STATE_COLUMNS)}
        if state in ("started", "blocked"):
            rows[step] = {"step": step, "state": state, "agent": str(agent), "reason": reason, "time": stamp(now)}
        else:
            rows.pop(step, None)
        if rows or path.exists():
            write_rows(path, STATE_COLUMNS, list(rows.values()))


# ---- claims -------------------------------------------------------------------------------------------------------

def read_claims(live):
    return read_rows(pathlib.Path(live) / "claims.tsv", CLAIM_COLUMNS)


def put_claim(live, package, agent, branch="", now=None):
    """The package is claimed by `agent` (a claim of the same agent is renewed: its time is now)."""
    path = pathlib.Path(live) / "claims.tsv"
    with locked(live):
        rows = [r for r in read_rows(path, CLAIM_COLUMNS) if r["package"] != package]
        old = next((r for r in read_rows(path, CLAIM_COLUMNS) if r["package"] == package), None)
        rows.append({"package": package, "agent": str(agent), "time": stamp(now), "branch": branch or (old or {}).get("branch", "")})
        write_rows(path, CLAIM_COLUMNS, rows)


def drop_claim(live, package):
    path = pathlib.Path(live) / "claims.tsv"
    with locked(live):
        rows = read_rows(path, CLAIM_COLUMNS)
        kept = [r for r in rows if r["package"] != package]
        if len(kept) != len(rows):
            write_rows(path, CLAIM_COLUMNS, kept)


def claim_of(claims, package):
    return next((c for c in claims if c["package"] == package), None)


def claim_age_hours(claim, branch_time, now=None):
    """Hours since the claim was last active: the later of the claim's time and its branch's last commit. `branch_time`
    is a function from a branch name to the commit time in seconds (or None when the branch is not there)."""
    now = now or datetime.datetime.now()
    last = parse_stamp(claim["time"])
    if claim.get("branch"):
        epoch = branch_time(claim["branch"])
        if epoch:
            committed = datetime.datetime.fromtimestamp(epoch)
            last = committed if last is None or committed > last else last
    return (now - last).total_seconds() / 3600.0 if last else float("inf")


def stale_packages(claims, branch_time, now=None, hours=STALE_HOURS):
    return {c["package"] for c in claims if claim_age_hours(c, branch_time, now) > hours}


# ---- tool faults --------------------------------------------------------------------------------------------------

def read_faults(live):
    return read_rows(pathlib.Path(live) / "tool-faults.tsv", FAULT_COLUMNS)


def append_fault(live, agent, step, kind, check, first_error, test="", run_folder="", could_not="", now=None):
    """One line per event, appended at once (never through a landing). Returns the row."""
    row = {"time": stamp(now), "agent": str(agent), "step": step, "kind": kind, "check": check, "test": test,
           "first_error": first_error, "run_folder": run_folder, "could_not": could_not}
    path = pathlib.Path(live) / "tool-faults.tsv"
    with locked(live):
        rows = read_rows(path, FAULT_COLUMNS)
        rows.append(row)
        write_rows(path, FAULT_COLUMNS, rows)
    return {k: clean(v) for k, v in row.items()}


def set_could_not_last(live, agent, sentence):
    """Sets "could not" on the last fault of `agent` (the last of all when agent is None). Returns that row or None."""
    path = pathlib.Path(live) / "tool-faults.tsv"
    with locked(live):
        rows = read_rows(path, FAULT_COLUMNS)
        for r in reversed(rows):
            if agent is None or r["agent"] == str(agent):
                r["could_not"] = sentence
                write_rows(path, FAULT_COLUMNS, rows)
                return {k: clean(v) for k, v in r.items()}
    return None


def fault_signature(row):
    """Faults with the same check, test and first error line are one signature (what changes from run to run, a hash or
    a time or a path, is left out of the line)."""
    return failures.signature("%s | %s" % (row["check"], row["test"]), row["first_error"])


def fault_groups(rows):
    """[{signature, count, first_seen, last_seen, step, agent, kind, check, test, first_error, could_not, run_folder}],
    the most frequent first (then the most recent). step, could_not and run_folder are the latest non-empty ones."""
    groups = {}
    for r in rows:
        g = groups.setdefault(fault_signature(r), {"signature": fault_signature(r), "count": 0, "first_seen": r["time"],
                                                   "last_seen": r["time"], "kind": r["kind"], "check": r["check"],
                                                   "test": r["test"], "first_error": r["first_error"], "step": "", "agent": "",
                                                   "could_not": "", "run_folder": ""})
        g["count"] += 1
        g["last_seen"] = max(g["last_seen"], r["time"])
        g["first_seen"] = min(g["first_seen"], r["time"])
        for key in ("step", "agent", "could_not", "run_folder"):
            if r[key]:
                g[key] = r[key]
    return sorted(groups.values(), key=lambda g: (-g["count"], _neg(g["last_seen"])))


def _neg(text):
    return [-ord(c) for c in text]


# ---- landings -----------------------------------------------------------------------------------------------------

def read_landings(live):
    return read_rows(pathlib.Path(live) / "landings.tsv", LANDING_COLUMNS)


def add_landing(live, step, commit, seconds, agent=1, now=None):
    path = pathlib.Path(live) / "landings.tsv"
    with locked(live):
        rows = read_rows(path, LANDING_COLUMNS)
        rows.append({"step": step, "commit": commit, "finished": stamp(now), "seconds": str(int(seconds)), "agent": str(agent)})
        write_rows(path, LANDING_COLUMNS, rows)
