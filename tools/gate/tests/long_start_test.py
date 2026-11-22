"""long_start_test.py: a move starts the long run in the gate's own tree (N-1046, Planner 11-18 04:39).

python -X utf8 -I tools/gate/tests/long_start_test.py

In a scratch repository (a tree and a remote), with a stand-in for starting the process:
- a move with no long run in 20 hours makes the long run's tree (git worktree add, detached at the moved commit) and
  starts gate.py --long of that tree there with the mover's folders, locks and tablet, its output in
  <work>/long-<sha>.log, and writes long-started.txt;
- a second move 2 hours later starts none (the run's history line comes only at its end), nor does one while the long
  lock is held by a live process; one held by an ended process does not stop it;
- a move 19 hours later starts none, one 21 hours later brings the same tree to the new commit, detached, reset and cleaned (a file changed and a file
  added there by hand are gone), and starts the run again;
- a history line of a long run 3 hours ago stops a start.
Exit 0 when every case holds.
"""
import datetime
import os
import pathlib
import subprocess
import sys
import tempfile

HERE = pathlib.Path(__file__).resolve().parent
sys.path.insert(0, str(HERE.parent))
import gate  # noqa: E402

failures = []


def check(ok, what):
    if not ok:
        failures.append(what)


def sh(*a, cwd=None):
    return subprocess.run(list(a), cwd=cwd, capture_output=True, text=True)


start = getattr(gate, "start_long", None)
check(start is not None, "gate.start_long exists")
if start is not None:
    with tempfile.TemporaryDirectory() as tmp:
        t = pathlib.Path(tmp)
        git = gate.GIT
        source, long_tree, work = t / "tree", t / "Nebula-long", t / "runs"
        sh(git, "init", "-q", str(source))
        for k, v in (("user.name", "test"), ("user.email", "test@example.invalid"), ("commit.gpgsign", "false"),
                     ("core.hooksPath", ".git/hooks")):
            sh(git, "-C", str(source), "config", k, v)

        def commit(text):
            (source / "f.txt").write_text(text, encoding="utf-8")
            sh(git, "-C", str(source), "add", "f.txt")
            sh(git, "-C", str(source), "commit", "-q", "-m", text)
            return sh(git, "-C", str(source), "rev-parse", "HEAD").stdout.strip()

        c1 = commit("c1")
        args = gate.parser().parse_args(["--main", "--move", "--tablet", "R0", "--work", str(work), "--board", str(t / "B.md"),
                                         "--long-lock", str(t / "long.lock"), "--status", ""])
        started = []

        def popen(cmd, cwd=None, **kw):
            started.append((cmd, cwd, kw))

        now = datetime.datetime(2026, 11, 18, 6, 0, 0)
        cmd, what = start(c1, args, tree=long_tree, source=source, popen=popen, now=now)
        head = sh(git, "-C", str(long_tree), "rev-parse", "HEAD").stdout.strip()
        branch = sh(git, "-C", str(long_tree), "symbolic-ref", "-q", "HEAD")
        check(cmd is not None and len(started) == 1 and head == c1 and branch.returncode != 0,
              "a first move makes the tree, detached at the moved commit, and starts the run: %s" % what)
        if started:
            c, cwd, kw = started[0]
            check(pathlib.Path(cwd) == long_tree and c[4:6] == [str(long_tree / "tools" / "gate" / "gate.py"), "--long"]
                  and c[c.index("--tablet") + 1] == "R0" and c[c.index("--work") + 1] == str(work)
                  and c[c.index("--long-lock") + 1] == str(t / "long.lock"),
                  "the run is gate.py --long of the long tree with the mover's folders, lock and tablet: %s" % c)
            check(kw.get("stdout") is not None and (work / ("long-%s.log" % c1[:9])).exists()
                  and (kw.get("creationflags", 0) & 0x4000 if os.name == "nt" else kw.get("start_new_session")),
                  "detached, below normal priority, its output in the work folder's log")
        check((work / "long-started.txt").read_text(encoding="utf-8").startswith("2026-11-18T06:00:00 " + c1),
              "long-started.txt names when and what")

        c2 = commit("c2")
        cmd, what = start(c2, args, tree=long_tree, source=source, popen=popen, now=now + datetime.timedelta(hours=2))
        check(cmd is None and len(started) == 1 and "no long run is due: the last one started less than 20 hours ago" in what,
              "a second move 2 hours later starts none: %s" % what)
        cmd, what = start(c2, args, tree=long_tree, source=source, popen=popen, now=now + datetime.timedelta(hours=19))
        check(cmd is None and len(started) == 1 and "less than 20 hours ago" in what, "a move 19 hours later starts none: %s" % what)

        lock = t / "long.lock"
        lock.mkdir()
        (lock / "holder.txt").write_text("long gate of Nebula-long@x pid %d" % os.getpid(), encoding="utf-8")
        later = now + datetime.timedelta(hours=21)
        cmd, what = start(c2, args, tree=long_tree, source=source, popen=popen, now=later)
        check(cmd is None and "long lock is held" in what, "a held long lock stops a start: %s" % what)
        (lock / "holder.txt").write_text("long gate of Nebula-long@x pid 999999", encoding="utf-8")
        (long_tree / "f.txt").write_text("changed by hand", encoding="utf-8")
        (long_tree / "extra.txt").write_text("added by hand", encoding="utf-8")
        cmd, what = start(c2, args, tree=long_tree, source=source, popen=popen, now=later)
        head = sh(git, "-C", str(long_tree), "rev-parse", "HEAD").stdout.strip()
        check(cmd is not None and len(started) == 2 and head == c2
              and (long_tree / "f.txt").read_text(encoding="utf-8") == "c2" and not (long_tree / "extra.txt").exists(),
              "a move 21 hours later, the lock's process ended, brings the tree to the new commit, reset and cleaned: %s" % what)

        (work / "long-started.txt").unlink()
        history = work / "history.tsv"
        history.write_text("\t".join(gate.HISTORY_COLUMNS) + "\n" + "\t".join(
            [(later - datetime.timedelta(hours=3)).isoformat(), "run", "long", c2, "pass"]) + "\n", encoding="utf-8")
        cmd, what = start(c2, args, tree=long_tree, source=source, popen=popen, now=later)
        check(cmd is None and len(started) == 2, "a long run in history 3 hours ago stops a start: %s" % what)

        # A start by hand (gate.py long-start, T11-B): the 20-hour rule is not asked, the reason is written, the lock rule stays.
        why = "the long run of 11-21 died after its start was written"
        cmd, what = start(c2, args, tree=long_tree, source=source, popen=popen, now=later, why=why)
        check(cmd is not None and len(started) == 3, "a start by hand 3 hours after a long run starts one: %s" % what)
        check((work / "long-started.txt").read_text(encoding="utf-8").strip() ==
              "%s %s by hand: %s" % (later.isoformat(timespec="seconds"), c2, why), "long-started.txt names the commit and the reason")
        cmd, what = start(c2, args, tree=long_tree, source=source, popen=popen, now=later + datetime.timedelta(minutes=5), why="again")
        check(cmd is None and len(started) == 3 and "less than 10 minutes ago" in what,
              "a second start by hand 5 minutes later is refused (the run may not hold the lock yet): %s" % what)
        (lock / "holder.txt").write_text("long gate of Nebula-long@x pid %d" % os.getpid(), encoding="utf-8")
        cmd, what = start(c2, args, tree=long_tree, source=source, popen=popen, now=later + datetime.timedelta(minutes=11), why="again")
        check(cmd is None and len(started) == 3 and "long lock is held" in what,
              "a start by hand is refused while a process that runs holds the long lock: %s" % what)
        (lock / "holder.txt").write_text("long gate of Nebula-long@x pid 999999", encoding="utf-8")
        cmd, what = start(c2, args, tree=long_tree, source=source, popen=popen, now=later + datetime.timedelta(minutes=11), why="again")
        check(cmd is not None and len(started) == 4, "a start by hand 11 minutes later, the holder ended, starts: %s" % what)
        cmd, what = start(c2, args, tree=long_tree, source=source, popen=popen, now=later + datetime.timedelta(hours=1))
        check(cmd is None and len(started) == 4 and "less than 20 hours ago" in what,
              "a start by hand counts for the 20-hour rule of a move: %s" % what)
        sh(git, "-C", str(source), "worktree", "remove", "--force", str(long_tree))

# The form of the command: no reason, or no such commit, starts nothing and gives 2.
check(getattr(gate, "long_start_main", None) is not None, "gate.long_start_main exists")
if getattr(gate, "long_start_main", None) is not None:
    with tempfile.TemporaryDirectory() as tmp:
        t = pathlib.Path(tmp)
        own = ["--work", str(t / "runs"), "--board", "", "--long-lock", str(t / "long.lock"), "--status", ""]
        check(gate.long_start_main(own) == 2 and gate.long_start_main(["--why", "  "] + own) == 2, "no reason: refused, exit 2")
        check(gate.long_start_main(["--why", "x", "--sha", "0123456789abcdef0123456789abcdef01234567"] + own) == 2
              and not (t / "runs" / "long-started.txt").exists(), "no such commit: refused, exit 2, nothing written")

# The play route measure's command (the long run's measure part, N-1043 and P-321): the PC's renderers, the tablet with the
# test app and the pak when the gate has built the app. No measure is started by a move (package T13c): there is no start_measure.
with tempfile.TemporaryDirectory() as m:
    m = pathlib.Path(m)
    margs = type("A", (), {"work": str(m / "work"), "paks": "P", "who": "Agent 1", "tablet": "SERIAL",
                           "tablet_lock": "T.lock", "pak": "US.pak"})()
    cmd = gate.measure_command("0123456789abcdef", m / "bin", m / "android", m / "Nebula.apk", m / "w", margs)
    check(str(gate.MEASURE) in cmd and cmd[cmd.index("--bin") + 1] == str(m / "bin")
          and cmd[cmd.index("--runs") + 1].endswith(",tablet") and cmd[cmd.index("--device") + 1] == "SERIAL",
          "measure.py on the build, with the tablet: %s" % cmd)
    check(cmd[cmd.index("--apk") + 1] == str(m / "Nebula.apk") and cmd[cmd.index("--pak") + 1] == "US.pak"
          and cmd[cmd.index("--device-lock") + 1] == "T.lock", "the test app and the pak handed to the tablet's run: %s" % cmd)
    cmd = gate.measure_command("fedcba9876543210", m / "bin", m / "android", None, m / "w", margs)
    check(not cmd[cmd.index("--runs") + 1].endswith(",tablet") and "--device" not in cmd,
          "no test app built: the tablet is left out: %s" % cmd)
check(not hasattr(gate, "start_measure"), "a move starts no speed measure: gate.start_measure is gone")

if failures:
    print("long_start_test.py: FAILED:\n  " + "\n  ".join(failures))
    sys.exit(1)
print("long_start_test.py: pass")
