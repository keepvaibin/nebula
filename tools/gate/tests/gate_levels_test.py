"""gate_levels_test.py: the gate's levels and rules of docs/plan/04-GATE-AND-MAIN.md, without the game (N-1039).

1. The two lists: the tree's own read cleanly; a line whose ledger row is closed or missing, a level that is none of the
   four, a test listed twice, a check in both lists, a wrong header, are each refused.
2. A check's state: a failure only in listed tests is known; a listed test that passes fails; a failure outside the
   list fails; a check whose runs were not all reported cannot be known; a planned check that runs fails; an owed
   check does not fail the run; planned checks of the level are reported; history.tsv gets one line per run.
3. The levels (a dry run of each): quick has no tablet, GitHub or soak; main has the tablet's short part and GitHub and
   no soak; long has the soak, the sanitizer tour and every disc on every renderer, and no GitHub; final has both.
   --only keeps a check and what it needs.
4. The tablet's part is owed, not failed, when the tablet does not answer.
5. GitHub: green in any run counts, a red job ends the wait at once and fails, a job still running is waited for up
   to the limit.
6. Main moves only through the gate: in a scratch repository with the guard as its pre-push hook, a plain push to main
   and a deletion of main are refused, another branch's push goes, move_main moves main and writes MAIN-MOVED, and a
   main that moved during the gate is not overwritten.
7. The rules main() enforces before it starts: the main level needs the tablet named, --move goes with --main alone.
8. The status page names main, its colour, the runs, the ledger counts and the lists.
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
        check("030-FactoryGalaxy-4" in known.get("tour/d3d12", {}) and "pictures/tablet" in planned,
              "the first content: P-122 on the tours, the tablet's pictures planned")
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

        # 3. The levels.
        names = {}
        for level in gate.LEVELS:
            dry = gate.run_checks(args_for("--" + level, "--tablet", "T", "--work", str(work)), known, planned, dry=True)
            names[level] = set(dry.needs)
            if level == "main":
                main_needs = dry.needs
        q, m, lg, f = names["quick"], names["main"], names["long"], names["final"]
        check("tour/d3d12" in q and not any(n.startswith(("tablet/", "tour/tablet", "github", "soak")) for n in q),
              "quick: no tablet, GitHub or soak: %s" % sorted(q))
        check({"tour/vulkan", "tour/d3d11", "replays/tablet", "tour/tablet-pc", "github", "build/android",
               "tour-RMGP01/d3d12", "pictures/tablet"} <= m and "soak" not in m and "tour-RMGP01/vulkan" not in m,
              "main: %s" % sorted(m))
        check({"soak", "tour-asan/d3d12", "tour-RMGJ01/vulkan", "tour-RMGP01/d3d11", "tour/tablet", "coverage"} <= lg
              and "github" not in lg and "unit/windows" not in lg and "pictures/tablet" not in lg, "long: %s" % sorted(lg))
        check((m | lg) <= f | {"pictures/tablet"} and "measure/full" in f, "final has main's and long's: %s" % sorted((m | lg) - f))
        chosen = gate.select(main_needs, ["replays/vulkan"])
        check(chosen == {"replays/vulkan", "tour/vulkan", "tour/d3d12", "build/windows"}, "--only and its needs: %s" % sorted(chosen))
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
        check([r["state"] for r in g3.results] == ["owed"] * len(gate.TABLET_CHECKS)
              and [r["check"] for r in g3.results] == list(gate.TABLET_CHECKS), "a tablet away owes its whole part")

        # 5. GitHub.
        green = [{"name": n, "status": "completed", "conclusion": "success"} for n in gate.GITHUB_JOBS]
        check(gate.judge_jobs(green)[:2] == (True, True), "all green")
        again = green[:1] + [{"name": gate.GITHUB_JOBS[0], "status": "completed", "conclusion": "failure"}] + green[1:]
        check(gate.judge_jobs(again)[:2] == (True, True), "green in any run counts")
        red = green[:-1] + [{"name": gate.GITHUB_JOBS[-1], "status": "completed", "conclusion": "failure"}]
        done, ok, detail = gate.judge_jobs(red + [{"name": "Mac", "status": "in_progress", "conclusion": None}][:0])
        check(done and not ok and "failure" in detail, "a red job fails: %s" % detail)
        running = green[:-1] + [{"name": gate.GITHUB_JOBS[-1], "status": "in_progress", "conclusion": None}]
        check(gate.judge_jobs(running)[:2] == (False, False), "a running job is waited for")
        check(gate.judge_jobs(green[:-1])[:2] == (False, False), "a job not started is waited for")
        now = [0.0]
        answers = iter([running, running, green])
        fn = gate.github_check(g2, 15, jobs_of=lambda sha: next(answers), sleep=lambda s: now.__setitem__(0, now[0] + s),
                               clock=lambda: now[0])
        ok, detail = fn(t / "gh.log")
        check(ok and now[0] == 120.0, "waits while running, then green: %s after %s s" % (detail, now[0]))
        now[0] = 0.0
        fn = gate.github_check(g2, 15, jobs_of=lambda sha: running, sleep=lambda s: now.__setitem__(0, now[0] + s),
                               clock=lambda: now[0])
        ok, detail = fn(t / "gh2.log")
        check(not ok and "after 15 minutes" in detail and now[0] >= 900, "the wait has a limit: %s" % detail)

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
        check(r.returncode != 0 and "main moves only through" in r.stderr, "a plain push to main is refused: %s" % r.stderr[-200:])
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
        stale = t / "old.txt"
        stale.write_text(c1 + "\n", encoding="utf-8")
        os.utime(stale, (1, 1))
        check(main_push_guard.judge(["x %s refs/heads/main y" % c1], str(stale)) is not None, "an old allowance is refused")
        check(main_push_guard.judge(["x %s refs/heads/main y" % c3], str(t / "none.txt")) is not None, "no allowance file")

        # 7. main()'s rules.
        quiet = ["--work", str(work), "--board", "", "--status", ""]
        check(gate.main(["--main"] + quiet) == 2, "the main level needs the tablet named")
        check(gate.main(["--main", "--move", "--only", "rust", "--tablet", "T"] + quiet) == 2, "--move with --only is refused")
        check(gate.main(["--quick", "--move"] + quiet) == 2, "--move with --quick is refused")

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
