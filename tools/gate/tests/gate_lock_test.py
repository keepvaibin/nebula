"""gate_lock_test.py: tools/gate/gate.py's gate lock (one full gate at a time) and its quick-gate-first rule on
folders whose answer is known: a free lock taken with its board line, a held one refused or waited for, a dead
holder's lock taken over, a lock freed only by its own holder, and quick gates that do and do not let a full one run."""
import json
import os
import pathlib
import sys
import tempfile

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[1]))
import gate  # noqa: E402


def main():
    failures = []

    def check(ok, what):
        if not ok:
            failures.append(what)

    with tempfile.TemporaryDirectory() as tmp:
        t = pathlib.Path(tmp)
        lock, board = t / "gate.lock", t / "BOARD.md"
        board.write_text("", encoding="utf-8")
        me = "full gate of Tree@c0ffee pid %d" % os.getpid()

        check(gate.take_gate_lock(lock, me, 0, str(board), "Agent 1") is None, "a free lock is taken")
        check(gate.lock_holder(lock) == (gate.lock_holder(lock)[0], os.getpid()), "the holder names its process")
        check("Agent 1 LOCK: gate.lock taken by gate.py: " + me in board.read_text(encoding="utf-8"), "taking says so on the board")

        held = gate.take_gate_lock(lock, "full gate of Other@beef pid 1", 0, str(board), "Agent 2", alive=lambda pid: True)
        check(held is not None and "is held" in held and "Tree@c0ffee" in held, "a held lock is refused, naming its holder")

        slept = []
        held = gate.take_gate_lock(lock, "full gate of Other@beef pid 1", 0.001, str(board), "Agent 2",
                                   alive=lambda pid: True, sleep=slept.append, poll=0.01)
        check(held is not None and slept, "a wait for a held lock waits, then refuses at its end")

        gate.free_gate_lock(lock, "full gate of Other@beef pid 1", str(board), "Agent 2", "x")
        check(lock.exists(), "another run cannot free the lock")
        gate.free_gate_lock(lock, me, str(board), "Agent 1", "all checks passed")
        check(not lock.exists() and "Agent 1 FREE: gate.lock freed by gate.py: " + me in board.read_text(encoding="utf-8"),
              "its own run frees the lock, with its summary on the board")

        lock.mkdir()
        (lock / "holder.txt").write_text("full gate of Gone@dead pid 999999 2026-11-17 10:00:00\n", encoding="utf-8")
        check(gate.take_gate_lock(lock, me, 0, str(board), "Agent 1", alive=lambda pid: False) is None,
              "a lock whose holder's process ended is taken over")
        check("taken over" in board.read_text(encoding="utf-8") and gate.lock_holder(lock)[1] == os.getpid(),
              "the take-over is on the board and the lock is this run's")
        gate.free_gate_lock(lock, me, str(board), "Agent 1", "x")

        lock.mkdir()
        (lock / "holder.txt").write_text("gate by hand (Agent 2) 2026-11-17 13:00:00\n", encoding="utf-8")
        check(gate.take_gate_lock(lock, me, 0, str(board), "Agent 1") is not None, "a lock taken by hand (no process) holds")
        check(gate.process_alive(os.getpid()) and not gate.process_alive(999999), "a process is alive, a made-up one is not")

        work = t / "runs"
        def quick(name, passed):
            f = work / name
            f.mkdir(parents=True)
            rows = [{"check": c, "passed": passed} for c in gate.QUICK_FIRST]
            (f / "result.json").write_text(json.dumps({"checks": rows}), encoding="utf-8")
        quick("20261117-100000-c0ffee-quick", False)
        check(gate.quick_passed(work, "c0ffee") is None, "a quick gate whose build or engine checks failed does not count")
        quick("20261117-110000-beef-quick", True)
        check(gate.quick_passed(work, "c0ffee") is None, "another commit's quick gate does not count")
        quick("20261117-120000-c0ffee-quick", True)
        check(gate.quick_passed(work, "c0ffee").name == "20261117-120000-c0ffee-quick", "this commit's passed quick gate counts")

    if failures:
        print("gate_lock_test.py: FAILED:\n  " + "\n  ".join(failures))
        return 1
    print("gate_lock_test.py: pass")
    return 0


if __name__ == "__main__":
    sys.exit(main())
