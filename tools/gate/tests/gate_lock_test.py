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


class TestClock:
    """The wait's clock: it moves only when the wait sleeps, so a loaded machine cannot run a wait out before its
    first poll (P-327: the gate of c01117add failed "a wait for a held lock waits" on the wall clock)."""

    def __init__(self):
        self.now = 1000.0

    def __call__(self):
        return self.now

    def sleep(self, also=None):
        def step(seconds):
            if also is not None:
                also(seconds)
            self.now += seconds
        return step


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
        clock = TestClock()
        held = gate.take_gate_lock(lock, "full gate of Other@beef pid 1", 0.001, str(board), "Agent 2",
                                   alive=lambda pid: True, sleep=clock.sleep(slept.append), poll=0.01, clock=clock)
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

        gate.free_gate_lock(lock, "gate by hand", str(board), "Agent 2", "x")
        import shutil
        shutil.rmtree(lock, ignore_errors=True)

        # Every level takes the gate lock (Planner 11-18 03:44): a quick gate never runs beside a held one.
        locks_for = getattr(gate, "locks_for", None)
        check(locks_for is not None and locks_for("quick", [], "g", "l") == ["g"] and locks_for("main", ["x"], "g", "l") == ["g"]
              and locks_for("final", [], "g", "l") == ["g", "l"] and locks_for("long", [], "g", "l") == ["l"],
              "the quick, main and final levels take the gate lock, --only runs too; the long run its own")

        # The queue: when a held lock frees, a waiting gate of Agent 3 or 4 goes first, and a waiter whose process ended
        # loses its place. Agent 1 waits on a hand lock while Agent 3's gate (process 4242) waits before it.
        queue = pathlib.Path(str(lock) + ".queue")
        lock.mkdir()
        (lock / "holder.txt").write_text("rows tour by hand (Agent 3) 2026-11-18 03:49:44\n", encoding="utf-8")
        queue.mkdir(exist_ok=True)
        agent3 = queue / "0-1-4242.txt"
        agent3.write_text("quick gate of A3@abc pid 4242\n", encoding="utf-8")
        alive = {4242: True, os.getpid(): True}

        def free_hand_lock(_):
            shutil.rmtree(lock, ignore_errors=True)

        clock = TestClock()
        held = gate.take_gate_lock(lock, me, 0.002, str(board), "Agent 1", alive=lambda pid: alive.get(pid, False),
                                   sleep=clock.sleep(free_hand_lock), poll=0.01, clock=clock)
        check(held is not None and not lock.exists(), "Agent 1 does not take the freed lock while Agent 3's gate waits before it")
        check(agent3.exists() and [f.name for f in queue.glob("*.txt")] == ["0-1-4242.txt"],
              "a waiter that gives up leaves the queue; the others stay")
        alive[4242] = False
        clock = TestClock()
        check(gate.take_gate_lock(lock, me, 0.002, str(board), "Agent 1", alive=lambda pid: alive.get(pid, False),
                                  sleep=clock.sleep(), poll=0.01, clock=clock) is None,
              "a waiter whose process ended loses its place")
        check(not agent3.exists() and not list(queue.glob("*.txt")), "the queue is empty once the lock is taken")
        gate.free_gate_lock(lock, me, str(board), "Agent 1", "x")
        check(gate.queue_rank("quick gate of Nebula-A1@abc pid 1") == 0 and gate.queue_rank("main gate of x pid 2") == 0
              and gate.queue_rank("long run part tour of Nebula-long pid 3") == 1
              and gate.queue_rank("measure.py: the play route measure of abc pid 4") == 1
              and gate.queue_rank("full gate of x pid 5") == 1,
              "a landing's gate ranks before the long run's parts, the measure and other work")

        # The pre-push checks (census, ratchet: a work tree and an inventory) are machine work: they run once the lock
        # is held, never before it (11-18: they ran beside a measurement while the gate waited).
        import inspect
        source = inspect.getsource(gate.run_main)
        check(0 < source.find("take_gate_lock(lock, holder") < source.find("push_checks(commit)"),
              "the pre-push checks run after the gate lock is taken")

        # The long run takes the lock part by part (Planner 11-18 03:44, condition 4): held during a part, free between
        # two, and a part that cannot have it within --wait is a failed check that says why.
        locked_part = getattr(gate, "locked_part", None)
        check(locked_part is not None, "the long run has parts under the lock")
        if locked_part is not None:
            lock_path, board_path = str(lock), str(board)

            class Args:
                mode, gate_lock, wait, board, who = "long", lock_path, 0, board_path, "Agent 1"
                queue_file, runner_lock = str(lock.parent / "landing-queue.tsv"), str(lock.parent / "runner.lock")

            class FakeGate:
                dry, args, rows = False, Args(), []

                def record(self, name, state, seconds, detail, log=None):
                    self.rows.append((name, state, detail))

            g = FakeGate()
            seen = []
            check(locked_part(g, "tours", lambda: seen.append(lock.exists()) or "ran") == "ran" and seen == [True],
                  "a part runs holding the gate lock")
            check(not lock.exists(), "the lock is free between two parts")
            lock.mkdir()
            (lock / "holder.txt").write_text("quick gate by hand (Agent 3) 2026-11-18 04:00:00\n", encoding="utf-8")
            check(locked_part(g, "tablet", lambda: seen.append("ran")) is None and seen == [True]
                  and g.rows and g.rows[-1][0] == "part/tablet" and g.rows[-1][1] == "env",
                  "a part that cannot have the lock does not run and is env (the machine was not fit) as part/<name>")
            shutil.rmtree(lock, ignore_errors=True)
            Args.mode = "main"
            check(locked_part(g, "x", lambda: lock.exists()) is False, "at the other levels a part takes no lock of its own")

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
