"""gate_levels_test.py: the gate's levels and rules of docs/plan/04-GATE-AND-MAIN.md, without the game (N-1039).

1. The two lists: the tree's own read cleanly; a line whose ledger row is closed or missing, a level that is none of the
   four, a test listed twice, a check in both lists, a wrong header, are each refused.
2. A check's state: a failure only in listed tests is known; a listed test that passes fails; a failure outside the
   list fails; a check whose runs were not all reported cannot be known; a planned check that runs fails; an owed
   check does not fail the run; planned checks of the level are reported; history.tsv gets one line per run.
3. The levels (a dry run of each): quick has no tablet, GitHub or soak; main has the tablet's short part and GitHub and
   no soak; long has the soak, the sanitizer tour and mod tests, every disc on every renderer, and no GitHub; final has both.
   --only keeps a check and what it needs.
4. The tablet's part is owed, not failed, when the tablet does not answer.
5. GitHub (section 8, N-1047, package T8a): every `must` job of the commit's runs is required; a red job ends the wait at
   once and fails, a job planned.tsv names (github/<job>) does not count, jobs not ended or not in a run yet are waited for
   up to 120 minutes (env at the limit), and a failed question to GitHub is asked again, not taken for a red job.
6. Main moves only through the gate: in a scratch repository with the guard as its pre-push hook, a plain push to main
   and a deletion of main are refused, another branch's push goes, move_main moves main and writes MAIN-MOVED, a
   main that moved during the gate is not overwritten, a commit main already holds is a move done, and a candidate
   still waiting for GitHub must be held by the next one.
7. The rules main() enforces before it starts: the main level needs the tablet named, --move goes with --main alone.
8. The status page names main, its colour, the runs, the ledger counts and the lists.
9. Lanes (section 7): checks of lanes run side by side and their rows join in the lanes' order whichever ends first;
   a lane that breaks is a row of kind tool (the gate's own fault, not the game's); --serial runs them in turn.
"""
import os
import pathlib
import subprocess
import sys
import tempfile

HERE = pathlib.Path(__file__).resolve().parent
sys.path.insert(0, str(HERE.parent))
import gate  # noqa: E402
import main_push_guard  # noqa: E402
import status  # noqa: E402


def args_for(*argv):
    return gate.parser().parse_args(list(argv))


def main():
    failures = []

    def check(ok, what):
        if not ok:
            failures.append(what)

    with tempfile.TemporaryDirectory() as tmp:
        t = pathlib.Path(tmp)

        # 1. The lists.
        known, planned, problems = gate.read_lists()
        check(not problems, "the tree's lists read cleanly: %s" % problems)
        check("pictures/tablet" not in planned, "the tablet's pictures are a check, not planned (N-1040)")
        ledger = t / "ledger.tsv"
        ledger.write_text("id\tagent\tpackage\tstate\tproof\ttitle\tnote\nP-1\t1\tE0\topen\t\tx\t\nP-2\t1\tE0\tfixed\tc\tx\t\n",
                          encoding="utf-8")
        head_k = "\t".join(gate.KNOWN_COLUMNS)
        head_p = "\t".join(gate.PLANNED_COLUMNS)

        def lists(known_rows, planned_rows, known_head=head_k):
            (t / "k.tsv").write_text("# c\n" + "\n".join([known_head] + known_rows) + "\n", encoding="utf-8")
            (t / "p.tsv").write_text("\n".join([head_p] + planned_rows) + "\n", encoding="utf-8")
            return gate.read_lists(t / "k.tsv", t / "p.tsv", ledger)

        check(not lists(["tour/d3d12\tA\tP-1\tabc\tw"], ["coverage\tlong\tP-1\tAgent 1", "fuzz\tlong\tM6\tAgent 2"])[2],
              "open rows and a plan step are accepted")
        for bad, why in (((["tour/d3d12\tA\tP-2\tabc\tw"], []), "a closed row"),
                         ((["tour/d3d12\tA\tP-9\tabc\tw"], []), "a row not in the ledger"),
                         ((["tour/d3d12\tA\tM6\tabc\tw"], []), "a known line naming no row"),
                         (([], ["coverage\tsoon\tP-1\tAgent 1"]), "a level that is none of the four"),
                         ((["tour/d3d12\tA\tP-1\tabc\tw", "tour/d3d12\tA\tP-1\tabc\tw"], []), "a test listed twice"),
                         ((["coverage\tA\tP-1\tabc\tw"], ["coverage\tlong\tP-1\tAgent 1"]), "a check in both lists"),
                         ((["tour/d3d12\tA\tP-1\tabc"], []), "a line with a cell missing")):
            check(lists(*bad)[2], "%s is refused" % why)
        check(lists([], [], known_head="check\ttest")[2], "a wrong header is refused")

        # 2. States.
        row = {"A": {"ledger row": "P-1"}}
        check(gate.judge(False, "1 of 2", {"A": "setup-failed", "B": "pass"}, row)[0] == "known", "a listed failure is known")
        state, detail = gate.judge(True, "2 of 2", {"A": "pass", "B": "pass"}, row)
        check(state == "fail" and "remove the line" in detail, "a listed test that passes fails: %s" % detail)
        check(gate.judge(False, "0 of 2", {"A": "crashed", "B": "crashed"}, row)[0] == "fail", "a failure outside the list fails")
        check(gate.judge(False, "x", None, row)[0] == "fail", "runs not all reported cannot be known")
        check(gate.judge(True, "x", {"B": "pass"}, row)[0] == "pass", "a listed test not in the run is not judged")
        log = t / "h.log"
        log.write_text("A d3d12 pass ok\nB d3d12 device-lost gone\nC vulkan pass ok\n", encoding="utf-8")
        check(gate.verdicts(log, "d3d12") == {"A": "pass", "B": "device-lost"}, "verdicts per renderer")

        work = t / "runs"
        a = args_for("--main", "--tablet", "T", "--work", str(work), "--board", "", "--status", "")
        g = gate.Gate(a, known={"tour/d3d12": row}, planned={
            "coverage": {"level": "long", "plan step or ledger row": "N-1", "owner": "Agent 1"},
            "pictures/tablet": {"level": "main", "plan step or ledger row": "N-2", "owner": "Agent 1"},
            "built": {"level": "main", "plan step or ledger row": "N-3", "owner": "Agent 1"}})
        g.check("tour/d3d12", lambda log: (False, "1 of 2 pass", {"A": "setup-failed", "B": "pass"}))
        g.check("built", lambda log: (True, "it ran"))
        g.owe("tour/tablet", "the tablet does not answer")
        g.plan_rows()
        states = {r["check"]: r["state"] for r in g.results}
        check(states == {"tour/d3d12": "known", "built": "fail", "tour/tablet": "owed", "pictures/tablet": "planned"},
              "states: %s" % states)
        check(g.write() is False, "a planned check that runs fails the run")
        g2 = gate.Gate(a, known={"tour/d3d12": row})
        g2.check("tour/d3d12", lambda log: (False, "1 of 2 pass", {"A": "setup-failed", "B": "pass"}))
        g2.owe("tour/tablet", "away")
        g2.check("rust", lambda log: (True, "468 passed"))
        g2.check("start/d3d12", lambda log: (False, "exit 1"))
        g2.owe_last("the tablet left adb")
        check(g2.results[-1]["state"] == "owed", "a failure because the tablet left is owed")
        check(g2.write() is True, "known and owed checks do not fail the run")
        history = (work / "history.tsv").read_text(encoding="utf-8").splitlines()
        check(len(history) == 3 and history[0].split("\t") == list(gate.HISTORY_COLUMNS)
              and history[2].split("\t")[4] == "pass" and "tour/d3d12=known/" in history[2], "history: %s" % history[-1:])

        # 9. Lanes.
        import threading
        import time
        g4 = gate.Gate(a)
        both = threading.Barrier(2, timeout=10)

        def slow():
            both.wait()
            time.sleep(0.3)
            g4.check("first", lambda log: (True, "a"))

        def fast():
            both.wait()
            g4.check("second", lambda log: (False, "b"))

        def broken():
            raise RuntimeError("broke")
        g4.lanes([slow, fast, broken])
        check([(r["check"], r["state"]) for r in g4.results] == [("first", "pass"), ("second", "fail"), ("lane/2", "tool")],
              "lanes side by side, rows in lane order: %s" % [(r["check"], r["state"]) for r in g4.results])
        g5 = gate.Gate(args_for("--main", "--tablet", "T", "--work", str(work), "--serial"))
        order = []
        g5.lanes([lambda: order.append(1), lambda: order.append(2)])
        check(order == [1, 2], "--serial runs the lanes in turn")

        # 3. The levels.
        names = {}
        for level in gate.LEVELS:
            dry = gate.run_checks(args_for("--" + level, "--tablet", "T", "--work", str(work)), known, planned, dry=True)
            names[level] = set(dry.needs)
            if level == "main":
                main_needs = dry.needs
            if level == "long":
                long_needs = dry.needs
        q, m, lg, f = names["quick"], names["main"], names["long"], names["final"]
        check(tuple(main_needs.get("pictures/tablet", ())) == ("tour/tablet",),
              "main: the tablet's pictures checked after its tour: %s" % (main_needs.get("pictures/tablet"),))
        check("tour/d3d12" in q and not any(n.startswith(("tablet/", "tour/tablet", "github", "soak")) for n in q),
              "quick: no tablet, GitHub or soak: %s" % sorted(q))
        check({"tour/vulkan", "tour/d3d11", "replays/tablet", "tour/tablet-pc", "github", "build/android",
               "tour-RMGP01/d3d12", "pictures/tablet"} <= m and "soak" not in m and "tour-RMGP01/vulkan" not in m,
              "main: %s" % sorted(m))
        check({"soak", "tour-asan/d3d12", "tour-RMGJ01/vulkan", "tour-RMGP01/d3d11", "tour/tablet", "coverage"} <= lg
              and not any(n.startswith(("measure/", "speed/")) for n in lg) and "github" not in lg and "unit/windows" not in lg and "pictures/tablet" not in lg, "long: %s" % sorted(lg))
        # The mod suites and LumaLink's own check run in no level while the table says mods = off (package T4).
        check(not any(n.startswith("mods") or n == "lumalink" for n in q | m | lg | f if n not in planned),
              "mods = off: no mod check at any level: %s" % sorted(n for n in q | m | lg | f if n.startswith("mods") or n == "lumalink"))
        # Behind the switch the sanitizer build runs the mod tests too (N-1041): a check of its own after that build.
        long_mods = gate.run_checks(args_for("--long", "--tablet", "T", "--work", str(work), "--mods", "full"), known, planned, dry=True)
        check(tuple(long_mods.needs.get("mods-asan/d3d12", ())) == ("build/windows-asan",) and "lumalink" not in long_mods.needs
              and "mods/d3d12" not in long_mods.needs,
              "mods = full: the mod tests on the sanitizer build: %s" % (long_mods.needs.get("mods-asan/d3d12"),))
        # The sanitizer build runs the game's unit tests as a check of its own (unit/game-asan): in the long run only.
        check(tuple(long_needs.get("unit/game-asan", ())) == ("build/windows-asan",) and "unit/game-asan" in lg
              and not any("unit/game-asan" in n for n in q | m),
              "long: the sanitizer build's unit tests after that build: %s" % (long_needs.get("unit/game-asan"),))
        main_mods = gate.run_checks(args_for("--main", "--tablet", "T", "--work", str(work), "--mods", "short"), known, planned, dry=True)
        check({"lumalink", "mods/d3d12", "mods/vulkan", "mods/d3d11"} <= set(main_mods.needs),
              "mods = short: LumaLink's check and the mod tests of every renderer are back in the main level")
        # The play route measure runs at the long run's end (N-1043): a check of its own, not a planned one.
        check(set(long_needs.get("coverage", ())) == {"build/windows-coverage", "data/RMGE01"} and "build/windows-coverage" in lg,
              "long: the coverage build's report runs: %s" % (long_needs.get("coverage"),))
        check("measure/route" not in long_needs and "measure/route" not in f,
              "long and final: the long run has no speed part any more (B15 point 7): %s" % sorted(n for n in lg if n.startswith("measure")))
        # The landing's speed measure (package B8) is a check of the main level alone: the long run has no speed part.
        step = {"measure/step/pc", "measure/step/tablet"}
        check(step <= m and not step & (q | lg | f), "the speed measure's two checks are the main level's alone: %s" % sorted(step & (q | lg | f)))
        check((m | lg) - step <= f | {"pictures/tablet"} and "measure/full" in f, "final has main's and long's: %s" % sorted((m | lg) - f))
        chosen = gate.select(main_needs, ["replays/vulkan"])
        check(chosen == {"replays/vulkan", "tour/vulkan", "tour/d3d12", "build/windows", "data/RMGE01"}, "--only and its needs: %s" % sorted(chosen))
        chosen = gate.select(main_needs, ["replays/tablet"])
        check({"tour/tablet", "tablet/prepare", "tablet/remove", "tour/tablet-pc", "build/android"} <= chosen
              and "tour/d3d12" not in chosen, "the tablet's compare and its needs: %s" % sorted(chosen))

        # 4. The tablet away.
        class Away:
            def answers(self):
                return False
        g3 = gate.Gate(a)
        g3.android_built = True
        gate.tablet_part(g3, gate.QUICK_STOPS, device=Away())
        # At the main level its part includes the pictures' check.
        whole = list(gate.TABLET_CHECKS) + ["pictures/tablet"]
        check([r["state"] for r in g3.results] == ["owed"] * len(whole)
              and [r["check"] for r in g3.results] == whole, "a tablet away owes its whole part")

        # 5. GitHub: every `must` job of the commit's runs (section 8; the rule's own cases are github_rule_test.py).
        def job(name, state="completed", conclusion="success"):
            return {"name": name, "status": state, "conclusion": conclusion, "databaseId": 1}

        musts = [n for n, c in gate.github_rule.read_table().items() if c == "must"]
        green = [job(n) for n in musts]
        classes = gate.github_rule.read_table()
        now = [0.0]

        def tick(seconds):
            now[0] += seconds

        def rule_of(runs_of, planned=()):
            return gate.github_rule.Rule("abc", classes, runs_of, None, planned, clock=lambda: now[0], say=lambda s: None)

        def one(jobs, planned=()):
            """(kind, detail) of the rule's wait on one run with these jobs, a limit of 3 minutes."""
            now[0] = 0.0
            kind, detail, _ = rule_of(lambda sha: [{"id": 1, "branch": "gate/x", "jobs": jobs}], planned).wait(3, sleep=tick)
            return kind, detail

        check(one(green)[0] == "pass", "every must job green")
        check(one(green[:-1] + [job(musts[-1], "queued", None)])[0] == "env", "a job still queued is waited for to the limit")
        check(one(green[:3])[0] == "env", "jobs not in the run yet are waited for")
        kind, detail = one(green[:-1] + [job(musts[-1], conclusion="failure")])
        check(kind == "fail" and musts[-1] + " failure" in detail and now[0] == 0.0,
              "a red job ends the wait at once: %s" % detail)
        check(one(green[:-1] + [job(musts[-1], conclusion="failure")], planned={musts[-1]})[0] == "pass",
              "a job planned.tsv names does not count")
        check(gate.planned_jobs({"github/Mac": {}, "fuzz": {}}) == {"Mac"}, "planned.tsv's github/<job> lines")
        answers = iter([[], [{"id": 1, "branch": "gate/x", "jobs": green[:2]}], [{"id": 1, "branch": "gate/x", "jobs": green}]])
        now[0] = 0.0
        kind, detail, _ = rule_of(lambda sha: next(answers)).wait(120, sleep=tick)
        check(kind == "pass" and now[0] == 120.0, "waits while the run goes, then green: %s after %s s" % (detail, now[0]))
        now[0] = 0.0
        kind, detail, _ = rule_of(lambda sha: [{"id": 1, "branch": "gate/x", "jobs": green[:2]}]).wait(120, sleep=tick)
        check(kind == "env" and "120 minutes" in detail and now[0] >= 7200, "the wait has a limit: %s" % detail)
        # N-1070: the CPU-only checks run in a lane beside the game lanes, their programs at below-normal priority;
        # the unit tests and start checks stay before every lane.
        import inspect
        cpu_lane = getattr(gate, "cpu_lane", None)
        check(cpu_lane is not None, "the CPU-only checks have a lane of their own")
        if cpu_lane is not None:
            source = inspect.getsource(gate.run_checks)
            check(source.find("cpu_lane(") < source.find("g.lanes(lanes)") and "rust" not in inspect.getsource(gate.unit_part),
                  "the lane is among the game lanes and the unit part runs no CPU-only check")
            seen = []

            class CpuArgs:
                discs, build_root, jobs = str(t / "no-discs"), str(t), "1"

            class CpuGate:
                args = CpuArgs()

                def check(self, name, fn, needs=()):
                    seen.append((name, getattr(gate.LANE, "below_normal", False)))

                def missing(self, name, why, needs=()):
                    seen.append((name, getattr(gate.LANE, "below_normal", False)))

            cpu_lane(CpuGate(), ("RMGE01",))()
            check([n for n, _ in seen] == ["rust", "import/collision/RMGE01", "harness/self", "description/self"]
                  and all(low for _, low in seen) and not getattr(gate.LANE, "below_normal", False),
                  "the lane's checks run in order at below-normal priority, and the lane gives the flag back (no LumaLink: mods = off)")
            if os.name == "nt":
                out = t / "priority.log"
                gate.LANE.below_normal = True
                try:
                    gate.run([sys.executable, "-c", "import ctypes; k = ctypes.windll.kernel32; "
                              "k.GetCurrentProcess.restype = ctypes.c_void_p; k.GetPriorityClass.argtypes = [ctypes.c_void_p]; "
                              "print('class', k.GetPriorityClass(k.GetCurrentProcess()))"], str(out))
                finally:
                    gate.LANE.below_normal = False
                check("class 16384" in out.read_text(encoding="utf-8", errors="replace"),
                      "a program the lane starts runs at below-normal priority (16384)")

        # --github-wait (Planner 11-18 03:49): 120 unless a candidate asks more, and the move's wait takes it.
        args = gate.parser().parse_args(["--main", "--move"])
        longer = gate.parser().parse_args(["--main", "--move", "--github-wait", "180"])             if "--github-wait" in gate.parser().format_help() else None
        check(longer is not None and args.github_wait == 120 and longer.github_wait == 180
              and "rule.wait(args.github_wait" in __import__("inspect").getsource(gate.run_main),
              "--github-wait sets the move's wait for GitHub, 120 minutes unless given")
        asked = [0]

        def flaky(sha):
            asked[0] += 1
            if asked[0] == 1:
                raise RuntimeError("gh run list: timeout")
            return [{"id": 1, "branch": "gate/x", "jobs": green}]

        now[0] = 0.0
        kind, detail, _ = rule_of(flaky).wait(120, sleep=tick)
        check(kind == "pass" and asked[0] == 2, "a failed question is asked again: %s" % detail)

        # 6. Main moves only through the gate.
        git = gate.GIT
        origin, clone = t / "origin.git", t / "clone"

        def sh(*cmd, env=None, cwd=clone):
            return subprocess.run(list(cmd), cwd=cwd, capture_output=True, text=True, env=env)

        sh(git, "init", "-q", "--bare", "-b", "main", str(origin), cwd=t)
        sh(git, "clone", "-q", str(origin), str(clone), cwd=t)
        for k, v in (("user.name", "keepvaibin"), ("user.email", "122556909+keepvaibin@users.noreply.github.com"),
                     ("core.hooksPath", ".git/hooks"), ("commit.gpgsign", "false")):
            sh(git, "config", k, v)

        def commit(text):
            (clone / "f.txt").write_text(text, encoding="utf-8")
            sh(git, "add", "f.txt")
            sh(git, "commit", "-q", "-m", text)
            return sh(git, "rev-parse", "HEAD").stdout.strip()

        c0 = commit("c0")
        sh(git, "push", "-q", "origin", "HEAD:refs/heads/main")
        hook = clone / ".git" / "hooks" / "pre-push"
        hook.write_text('#!/bin/sh\nexec "%s" -I "%s"\n' % (pathlib.Path(sys.executable).as_posix(),
                                                         (HERE.parent / "main_push_guard.py").as_posix()), encoding="utf-8")
        c1 = commit("c1")
        env = {k: v for k, v in os.environ.items() if k != gate.ALLOWED_ENV}
        r = sh(git, "push", "origin", "%s:refs/heads/main" % c1, env=env)
        check(r.returncode != 0 and main_push_guard.REFUSAL in r.stderr and "main moves only through gate.py land <branch>" in r.stderr
              and "--move" not in r.stderr, "a plain push to main is refused with the one way that exists: %s" % r.stderr[-200:])
        r = sh(git, "push", "origin", ":refs/heads/main", env=env)
        check(r.returncode != 0 and "never deleted" in r.stderr, "deleting main is refused")
        r = sh(git, "push", "origin", "%s:refs/heads/side" % c1, env=env)
        check(r.returncode == 0, "another branch's push goes: %s" % r.stderr[-200:])
        board = t / "BOARD.md"
        why = gate.move_main(c1, "run-1", t, str(board), "Agent 1", "c1 for main", tree=clone)
        check(why is None and sh(git, "rev-parse", "origin/main").stdout.strip() == c1, "the gate moves main: %s" % why)
        check("Agent 1 MAIN-MOVED: main %s -> %s (1 commits): c1 for main; gate run-1." % (c0[:9], c1[:9])
              in board.read_text(encoding="utf-8"), "MAIN-MOVED on the board")
        check(not (t / "main-allowed.txt").exists(), "the allowance is used once")
        sh(git, "checkout", "-q", c0)
        c3 = commit("c3")
        why = gate.move_main(c3, "run-2", t, str(board), "Agent 1", "", tree=clone)
        check(why is not None and "main moved during the gate" in why and
              sh(git, "rev-parse", "origin/main").stdout.strip() == c1, "a main that moved is not overwritten: %s" % why)
        why = gate.move_main(c1, "run-3", t, str(board), "Agent 1", "", tree=clone)
        check(why is None and "main already holds %s" % c1[:9] in board.read_text(encoding="utf-8"),
              "a commit main already holds is a move done: %s" % why)
        waiting = t / "waiting"
        waiting.mkdir()
        gate.add_waiting(waiting, c1, "gate/run-1", os.getpid())
        gate.add_waiting(waiting, c0, "gate/run-0", 999999)
        check(gate.unheld_candidates(waiting, c3, tree=clone) == ["gate/run-1"],
              "a waiting candidate the next one does not hold is named; one whose gate has ended is not")
        sh(git, "checkout", "-q", c1)
        c4 = commit("c4")
        check(gate.unheld_candidates(waiting, c4, tree=clone) == [], "a candidate that holds the waiting one may enter")
        gate.drop_waiting(waiting, c1)
        check(gate.unheld_candidates(waiting, c3, tree=clone) == [], "a candidate that moved or failed waits no more")
        stale = t / "old.txt"
        stale.write_text(c1 + "\n", encoding="utf-8")
        os.utime(stale, (1, 1))
        check(main_push_guard.judge(["x %s refs/heads/main y" % c1], str(stale)) is not None, "an old allowance is refused")
        check(main_push_guard.judge(["x %s refs/heads/main y" % c3], str(t / "none.txt")) is not None, "no allowance file")

        # 7. main()'s rules.
        quiet = ["--work", str(work), "--board", "", "--status", ""]
        # The runner's mark (main moves only through gate.py land): this process stands in for the runner holding its lock.
        (t / "runner.lock").mkdir()
        (t / "runner.lock" / "holder.txt").write_text("landing runner pid %d 2026-11-21T11:00:00\n" % os.getpid(), encoding="utf-8")
        mark = ["--landing-runner", str(os.getpid()), "--runner-lock", str(t / "runner.lock")]
        check(gate.main(["--main"] + quiet) == 2, "the main level needs the tablet named")
        check(gate.main(["--main", "--move", "--only", "rust", "--tablet", "T"] + quiet + mark) == 2, "--move with --only is refused")
        check(gate.main(["--quick", "--move"] + quiet + mark) == 2, "--move with --quick is refused")

        # 8. The status page.
        board.write_text("2026-11-17 18:00 Agent 1 MAIN-MOVED: main a -> b\n2026-11-17 19:00 Planner MAIN-RED: long run\n"
                         "2026-11-17 19:30 Agent 2 ACK to Planner: MAIN-GREEN comes when the long run passes\n",
                         encoding="utf-8")
        page = status.build(work, str(board))
        check("- RED" in page and "Last main gate: 20" in page and "| Agent 1 | E0 gate |" in page
              and "Known failures: " in page and "Agent 2 (mods/*)" in page, "the status page: %s" % page[:600])

    if failures:
        print("gate_levels_test.py: FAILED:\n  " + "\n  ".join(failures))
        return 1
    print("gate_levels_test.py: pass")
    return 0


if __name__ == "__main__":
    sys.exit(main())
