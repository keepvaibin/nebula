"""lock_holder_test.py: every form a lock folder's holder.txt is written in is a form every reader reads, and a holder that is gone loses
its lock (tools/gate/lockholder.py; the gate lock, the landing runner's and the queue's lock, the measurement lock).

  forms_are_read            `pid N` anywhere in the line: the gate's `pid 67700; covers ...`, the landing's, the measure's (its own `pid N`
                            twice), `since T pid N`; a line with no pid names no process
  gate_lock_dead_holder     take_gate_lock: a dead holder in each written form is taken over, one line printed naming its pid; a live one is not
  landing_lock_dead_holder  landing.FolderLock: the same
  measure_lock_dead_holder  measure.py take_lock: the same
  start_must_match          a live pid whose start is not the holder's `since` is gone; one whose start matches is not; no start readable: not
  hand_lock_holds           a line with no pid is held, never taken over
  written_lines_read_back   what the three writers write names this process and its real start
Exit 0 when every case holds. A test that breaks when the change is taken out: forms_are_read and gate_lock_dead_holder fail on the old
`lock_holder` (it read `pid 67700;` as no process, so the lock of a killed gate held for ever).
"""
import contextlib
import io
import os
import pathlib
import re
import sys
import tempfile
import time

HERE = pathlib.Path(__file__).resolve().parent
GATE = HERE.parent
TREE = GATE.parents[1]
sys.path.insert(0, str(GATE))
sys.path.insert(0, str(TREE / "tools" / "measure"))
import gate  # noqa: E402
import landing  # noqa: E402
import lockholder  # noqa: E402
import measure  # noqa: E402

failed = []


def check(ok, what):
    if not ok:
        failed.append(what)


def planted(root, name, text):
    lock = root / name
    lock.mkdir(parents=True)
    if text is not None:
        (lock / "holder.txt").write_text(text, encoding="utf-8")
    return lock


def printed(call):
    out = io.StringIO()
    with contextlib.redirect_stdout(out):
        result = call()
    return result, out.getvalue()


def main():
    here = os.getpid()
    mine = lockholder.process_started(here)

    # forms_are_read
    forms = {
        "gate, a `;` after the pid": ("full gate of Nebula-A1@0123456789 pid 67700; covers main abc", 67700, None),
        "gate, plain": ("main gate of Tree@c0ffee pid 12 2026-11-21 10:00:00", 12, None),
        "long part": ("long run part tour of Nebula-long pid 3045 2026-11-21 10:00:00", 3045, None),
        "landing": ("landing runner pid 4242 2026-11-21T11:00:00", 4242, None),
        "measure": ("measure.py abc D:/w 2026-11-21 08:00:00 pid 4243", 4243, None),
        "measure, its own pid first": ("measure.py: the pair of abc pid 5 2026-11-21 08:00:00 pid 5", 5, None),
        "with the start": ("full gate of T@abc since 1763750000 pid 99; covers x 2026-11-21 10:00:00", 99, 1763750000),
        "by hand": ("rows tour by hand (Agent 3) 2026-11-18 03:49:44", None, None),
        "empty": ("", None, None),
    }
    for name, (text, pid, since) in forms.items():
        check(lockholder.parse(text) == (pid, since), "form %r: read %r" % (name, lockholder.parse(text)))
    with tempfile.TemporaryDirectory() as tmp:
        t = pathlib.Path(tmp)
        for name, (text, pid, since) in forms.items():
            lock = planted(t, "read-%d" % len(list(t.iterdir())), text)
            check(gate.lock_holder(lock) == (text.strip() or "", pid) or (text == "" and gate.lock_holder(lock)[1] is None),
                  "gate.lock_holder reads the form %r" % name)
            check(landing.FolderLock(lock, lambda p: True).holder() == (text.strip(), pid), "FolderLock.holder reads the form %r" % name)

    with tempfile.TemporaryDirectory() as tmp:
        t = pathlib.Path(tmp)
        board = t / "BOARD.md"
        board.write_text("", encoding="utf-8")
        me = "full gate of Tree@c0ffee pid %d" % here

        # gate_lock_dead_holder: every written form, dead
        dead = 999999
        for name in ("gate, a `;` after the pid", "gate, plain", "long part", "with the start"):
            text = re.sub(r"pid \d+", "pid %d" % dead, forms[name][0])
            lock = planted(t, "g-dead-%d" % len(list(t.iterdir())), text)
            result, said = printed(lambda: gate.take_gate_lock(lock, me, 0, str(board), "Helper",
                                                               alive=lambda pid: pid != dead, started=lambda pid: None))
            check(result is None and "pid %d" % dead in said and "taken over" in said and gate.lock_holder(lock)[1] == here,
                  "the dead holder of the form %r is taken over, its pid on one printed line: %r %r" % (name, result, said))
            check(said.count("\n") == 1, "one printed line for the form %r: %r" % (name, said))
            gate.free_gate_lock(lock, me, str(board), "Helper", "x")
        # live: not taken
        lock = planted(t, "g-live", "full gate of Other@beef pid 67700; covers main abc")
        result, said = printed(lambda: gate.take_gate_lock(lock, me, 0, str(board), "Helper", alive=lambda pid: True))
        check(result is not None and "is held" in result and "67700" in result and "taken over" not in said and lock.exists(),
              "a live holder (the `;` form) is waited for, not taken over: %r %r" % (result, said))

        # landing_lock_dead_holder
        for name in ("landing", "gate, a `;` after the pid"):
            text = re.sub(r"pid \d+", "pid %d" % dead, forms[name][0])
            lock = planted(t, "l-dead-%d" % len(list(t.iterdir())), text)
            runner = landing.FolderLock(lock, lambda pid: pid == here, pid=here)
            result, said = printed(runner.try_take)
            check(result is True and "pid %d" % dead in said and "taken over" in said and runner.holder()[1] == here,
                  "FolderLock takes over the dead holder of the form %r, naming its pid: %r %r" % (name, result, said))
        lock = planted(t, "l-live", "landing runner pid 4242 2026-11-21T11:00:00")
        runner = landing.FolderLock(lock, lambda pid: True, pid=here)
        result, said = printed(runner.try_take)
        check(result is False and said == "" and runner.holder()[1] == 4242, "FolderLock: a live holder keeps the lock: %r %r" % (result, said))

        # measure_lock_dead_holder
        lock = planted(t, "m-dead", "measure.py abc D:/w 2026-11-21 08:00:00 pid %d" % dead)
        result, said = printed(lambda: measure.take_lock(lock, "measure.py new D:/w", alive=lambda pid: pid != dead))
        check("pid %d" % dead in said and "taken over" in said and lockholder.parse((lock / "holder.txt").read_text(encoding="utf-8"))[0] == here,
              "measure.take_lock takes over the dead holder, naming its pid: %r" % said)
        try:
            measure.take_lock(lock, "again", alive=lambda pid: True)
            check(False, "measure.take_lock: a live holder is refused")
        except SystemExit as error:
            check("is held" in str(error), "measure.take_lock: a live holder is refused: %s" % error)

        # start_must_match
        text = "full gate of T@abc since %d pid %d; covers x" % (mine or 1, here)
        check(lockholder.gone(text, lambda pid: True) is None, "this process with its own start is not gone")
        text = "full gate of T@abc since %d pid %d; covers x" % ((mine or 1) + 100, here)
        if mine is not None:
            check(lockholder.gone(text, lambda pid: True) == here, "a live pid that started at another time than `since` is gone")
        check(lockholder.gone("x since 1000 pid 7", lambda pid: True, lambda pid: 5000) == 7, "the number used again: gone")
        check(lockholder.gone("x since 1000 pid 7", lambda pid: True, lambda pid: 1001) is None, "a start within 2 s is the same process")
        check(lockholder.gone("x since 1000 pid 7", lambda pid: True, lambda pid: None) is None, "no start readable: it lives")
        check(lockholder.gone("x since 1000 pid 7", lambda pid: False, lambda pid: 1000) == 7, "not running: gone")
        lock = planted(t, "g-reused", "full gate of Old@abc since 1000 pid %d; covers x" % here)
        result, said = printed(lambda: gate.take_gate_lock(lock, me, 0, str(board), "Helper"))
        check((result is None) == (mine is not None) and ("taken over" in said) == (mine is not None),
              "gate.take_gate_lock: a live pid that is another start is taken over: %r %r" % (result, said))
        gate.free_gate_lock(lock, me, str(board), "Helper", "x")

        # hand_lock_holds
        lock = planted(t, "g-hand", "gate by hand (Helper) 2026-11-21 19:24:27")
        check(gate.take_gate_lock(lock, me, 0, str(board), "Helper", alive=lambda pid: False) is not None, "a hand lock holds")
        check(gate.long_lock_free(planted(t, "long-hand", "by hand"), lambda pid: False) is False, "a hand long lock is not free")
        check(gate.long_lock_free(planted(t, "long-dead", "long run part x of T pid 999999; y"), lambda pid: False) is True,
              "a long lock of a dead holder (the `;` form) is free")

        # written_lines_read_back
        lock = t / "w-gate"
        gate.take_gate_lock(lock, "full gate of Tree@abc %s; covers main x" % lockholder.named(), 0, str(board), "Helper")
        text = gate.lock_holder(lock)[0]
        check(lockholder.parse(text) == (here, mine) and gate.lock_holder(lock)[1] == here, "the gate's own line reads back: %r" % text)
        runner = landing.FolderLock(t / "w-landing", lambda pid: True)
        runner.try_take("landing runner")
        check(lockholder.parse(runner.holder()[0]) == (here, mine), "the landing's own line reads back: %r" % runner.holder()[0])
        measure.take_lock(t / "w-measure", "measure.py abc D:/w")
        text = (t / "w-measure" / "holder.txt").read_text(encoding="utf-8")
        check(lockholder.parse(text) == (here, mine) and text.rstrip().endswith("pid %d" % here), "the measure's own line reads back: %r" % text)
        if mine is not None:
            check(abs(mine - time.time()) < 7 * 86400, "the start of this process is a recent time: %d" % mine)

    print("\n".join(failed) if failed else "lock holders: every case holds")
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
