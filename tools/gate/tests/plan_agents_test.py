"""plan_agents_test.py: plan.py with two agents, and the tool-fault record (package T13, items 5 and 7).

next for both agents (the master's order, the helper's fixed list, a claimed package skipped, the pair that must not
overlap, a stale claim), start, claim and land with --agent, and the lines a landing writes into tool-faults.tsv. Plans
are small lists of steps or a temporary git tree with a fake gate: no game, no build, no network, no real live folder.

    python -X utf8 -I tools/gate/tests/plan_agents_test.py [--work <folder>]
"""
import contextlib
import datetime
import io
import os
import pathlib
import subprocess
import sys
import unittest
from unittest import mock

HERE = pathlib.Path(__file__).resolve().parent
TREE = HERE.parents[2]
sys.path.insert(0, str(HERE))
sys.path.insert(0, str(TREE / "tools" / "plan"))
import failures  # noqa: E402
import live as lv  # noqa: E402
import plan  # noqa: E402
from plan_fixture import Tree, plan_of, ready_a1, setup_module, step, teardown_module, write_steps  # noqa: E402


def setUpModule():
    setup_module()


def tearDownModule():
    teardown_module()


def claim(package, agent, branch="", when="2026-11-20T10:00:00"):
    return {"package": package, "agent": str(agent), "time": when, "branch": branch}


def master(steps, claims=(), stale=()):
    c = plan.choose_next(steps, list(claims), set(stale))
    return c.step["step"] if c else None


def helper(steps, claims=(), stale=()):
    c, report = plan.choose_helper(steps, list(claims), set(stale))
    return (c.step["step"] if c else None), dict(report)


# ---- next: the master ---------------------------------------------------------------------------------------

class MasterTest(unittest.TestCase):
    def test_alone_it_follows_the_order_of_steps_tsv(self):
        s = plan_of(step("S1.1", "S1"), step("G6.1", "G6"), step("A.1", "A"))
        self.assertEqual(master(s), "S1.1")
        s[0]["state"] = "landed"
        self.assertEqual(master(s), "G6.1", "with no helper at work, a helper-list package is as good as any other")

    def test_a_package_claimed_by_the_helper_is_skipped_and_one_of_its_own_is_taken(self):
        s = plan_of(step("G6.1", "G6"), step("A.1", "A"))
        self.assertEqual(master(s, [claim("G6", 2)]), "A.1")
        self.assertEqual(master(s, [claim("G6", 1)]), "G6.1")

    def test_while_the_helper_is_active_it_passes_over_helper_packages_not_yet_started_and_takes_them_last(self):
        s = plan_of(step("G6.1", "G6"), step("G7.1", "G7"), step("A.1", "A"), step("B.1", "B"))
        claims = [claim("G7", 2)]
        self.assertEqual(master(s, claims), "A.1", "G6 is the helper's to take and not yet started")
        s[2]["state"] = "landed"
        self.assertEqual(master(s, claims), "B.1")
        s[3]["state"] = "landed"
        choice = plan.choose_next(s, claims, set())
        self.assertEqual(choice.step["step"], "G6.1", "everything else is done: it takes the helper's package")
        self.assertIn("takes package G6 from agent 2's list", choice.notes[0])

    def test_a_helper_package_the_master_has_begun_is_not_passed_over(self):
        s = plan_of(step("G6.1", "G6", state="landed"), step("G6.2", "G6"), step("A.1", "A"))
        self.assertEqual(master(s, [claim("G7", 2)]), "G6.2")

    def test_a_stale_helper_claim_does_not_make_the_master_wait_but_is_still_the_helpers(self):
        s = plan_of(step("G6.1", "G6"), step("G7.1", "G7"))
        self.assertEqual(master(s, [claim("G7", 2)], stale=["G7"]), "G6.1", "G7 is held, and nobody is active")
        self.assertEqual(master(s, [claim("G6", 2)], stale=["G6"]), "G7.1", "a stale claim still holds its package")

    def test_the_master_skips_a_package_whose_needs_are_not_landed(self):
        s = plan_of(step("A.1", "A"), step("B.1", "B", "A"), step("C.1", "C"))
        s[0]["state"] = "started"
        self.assertEqual(master(s), "C.1")

    def test_the_pair_G2a_and_R7_is_never_in_work_together(self):
        s = plan_of(step("G2.1", "G2"), step("G2.2", "G2"), step("R7.1", "R7"), step("A.1", "A"))
        self.assertEqual(master(s), "G2.1")
        s[2]["state"] = "started"                          # R7 is in work
        self.assertEqual(master(s), "A.1", "G2.1 waits")
        s[2]["state"] = "landed"
        self.assertEqual(master(s), "G2.1")
        s[0]["state"] = "started"                          # G2.1 is in work: R7.1 would be next but is of the other
        s[2]["state"] = "open"
        self.assertEqual(master(s), "A.1")

    def test_G2_8_is_not_of_the_pair(self):
        rows = [step("G2.%d" % n, "G2", state="landed" if n < 8 else "open") for n in range(1, 10)]
        rows += [step("R7.1", "R7", state="started")]
        s = plan_of(*rows)
        self.assertEqual(master(s), "G2.8", "only G2.1 to G2.7 are the pair's")

    def test_a_blocked_step_is_reported_before_the_chosen_one(self):
        s = plan_of(step("A.1", "A", state="blocked"), step("B.1", "B"))
        c = plan.choose_next(s)
        self.assertEqual((c.step["step"], [b["step"] for b in c.blocked_before]), ("B.1", ["A.1"]))


# ---- next: the helper ---------------------------------------------------------------------------------------

class HelperTest(unittest.TestCase):
    def test_its_list_is_fixed_and_in_this_order(self):
        self.assertEqual(plan.AGENT2_LIST, ("G6", "G7", "G8", "G9", "G1", "R10", "R9", "G4"))

    def test_the_first_package_of_its_list_that_can_start(self):
        s = plan_of(step("G1.1", "G1"), step("G7.1", "G7"), step("G6.1", "G6"), step("G8.1", "G8"))
        self.assertEqual(helper(s)[0], "G6.1", "list order, not file order")
        s[2]["state"] = "landed"
        self.assertEqual(helper(s)[0], "G7.1")

    def test_it_is_never_given_a_package_that_is_not_on_its_list(self):
        s = plan_of(step("A.1", "A"), step("R7.1", "R7"), step("Z.1", "Z"))
        choice, report = helper(s)
        self.assertIsNone(choice)
        self.assertTrue(all(why == "is not in the plan" for why in report.values()))

    def test_a_package_landed_or_claimed_by_the_master_is_skipped_so_it_starts_where_the_work_stands(self):
        s = plan_of(step("G6.1", "G6", state="landed"), step("G7.1", "G7"), step("G8.1", "G8"), step("G9.1", "G9"))
        choice, report = helper(s, [claim("G7", 1)])
        self.assertEqual(choice, "G8.1")
        self.assertEqual(report["G6"], "is landed")
        self.assertEqual(report["G7"], "is claimed by agent 1")

    def test_a_package_it_holds_is_done_in_order(self):
        s = plan_of(step("G6.1", "G6", state="started", agent="2"), step("G6.2", "G6"), step("G7.1", "G7"))
        choice, report = helper(s, [claim("G6", 2)])
        self.assertEqual(choice, "G7.1", "G6.2 waits for G6.1; the next package is offered")
        self.assertIn("has step G6.1 started by agent 2", report["G6"])
        s[0]["state"] = "landed"
        self.assertEqual(helper(s, [claim("G6", 2)])[0], "G6.2")

    def test_a_blocked_step_stops_its_package_and_names_the_reason(self):
        s = plan_of(step("G6.1", "G6", state="blocked"), step("G7.1", "G7"))
        s[0]["reason"] = "the card is wrong"
        choice, report = helper(s)
        self.assertEqual(choice, "G7.1")
        self.assertEqual(report["G6"], "has step G6.1 blocked: the card is wrong")

    def test_a_package_whose_needs_are_not_landed_cannot_start_G4_waits_for_the_master(self):
        s = plan_of(step("G3.1", "G3"), step("G3.2", "G3"), step("G4.1", "G4", "G3"))
        choice, report = helper(s)
        self.assertIsNone(choice)
        self.assertIn("waits for G3 (2 not landed, first G3.1)", report["G4"])

    def test_the_pair_holds_for_it_too(self):
        s = plan_of(step("G6.1", "G6"))
        self.assertEqual(helper(s)[0], "G6.1")
        self.assertEqual(plan.pair_conflict(step("G2.1", "G2"), plan_of(step("R7.1", "R7", state="started"), step("G2.1", "G2"))),
                         "R7 is in work, and both change the renderers' frame and present code")
        self.assertEqual(plan.pair_conflict(step("R7.1", "R7"), plan_of(step("G2.3", "G2", state="started"), step("R7.1", "R7"))),
                         "G2a is in work, and both change the renderers' frame and present code")
        self.assertEqual(plan.pair_conflict(step("A.1", "A"), plan_of(step("R7.1", "R7", state="started"))), "")


# ---- the commands, in a temporary tree ------------------------------------------------------------------------

class CommandsTest(unittest.TestCase):
    def setUp(self):
        self.t = Tree().__enter__()
        self.addCleanup(self.t.__exit__)

    def plan(self, *rows):
        write_steps(self.t, rows)

    def test_next_agent_2_on_a_plan_where_its_list_is_landed_says_so_plainly(self):
        self.plan(*[("%s.1" % p, p, "", "landed") for p in plan.AGENT2_LIST], ("A.1", "A", "", "open"))
        code, text = self.t.run(["next", "--agent", "2"])
        self.assertEqual(code, 1)
        self.assertIn("Agent 2's list is finished: G6, G7, G8, G9, G1, R10, R9, G4 are all landed. Stop and report to agent 1. "
                      "Do not take any other package.", text)
        self.assertNotIn("NEXT:", text)

    def test_next_agent_2_when_nothing_on_the_list_can_start_names_each_package(self):
        self.plan(("G3.1", "G3", "", "open"), ("G4.1", "G4", "G3", "open"), ("G6.1", "G6", "G3", "open"))
        code, text = self.t.run(["next", "--agent", "2"])
        self.assertEqual(code, 1)
        self.assertIn("Nothing on agent 2's list can start now. Stop and report to agent 1; do not take any other package.", text)
        self.assertIn("  G4 waits for G3 (1 not landed, first G3.1)", text)
        self.assertIn("  G6 waits for G3", text)
        self.assertIn("  G7 is not in the plan", text)

    def test_next_agent_2_prints_the_step_and_its_commands_with_the_agent(self):
        self.plan(("A.1", "A", "", "open"), ("G6.1", "G6", "", "open"))
        code, text = self.t.run(["next", "--agent", "2"])
        self.assertEqual(code, 0)
        self.assertTrue(text.startswith("NEXT: G6.1  Title of G6.1\n"), text[:80])
        self.assertIn("plan.py start G6.1 --agent 2", text)
        self.assertIn("plan.py land G6.1 --agent 2", text)

    def test_next_agent_1_is_the_default_and_skips_what_agent_2_holds(self):
        self.plan(("G6.1", "G6", "", "open"), ("A.1", "A", "", "open"))
        self.t.run(["start", "G6.1", "--agent", "2"], expect=0)
        for args in (["next"], ["next", "--agent", "1"]):
            code, text = self.t.run(args)
            self.assertEqual(code, 0)
            self.assertTrue(text.startswith("NEXT: A.1"), text[:60])
            self.assertIn("plan.py start A.1\n", text)
        code, text = self.t.run(["next", "--agent", "2"])
        self.assertEqual(code, 1, "its own started step is the only thing in its list")
        self.assertIn("G6 has step G6.1 started by agent 2: land it first", text)

    def test_a_stale_claim_is_reported_by_next_and_status_and_not_taken_without_the_flag(self):
        self.plan(("G6.1", "G6", "", "open"), ("G6.2", "G6", "", "open"), ("A.1", "A", "", "open"))
        self.t.run(["start", "G6.1", "--agent", "2"], expect=0)
        old = datetime.datetime.now() - datetime.timedelta(hours=60)
        lv.put_claim(self.t.live, "G6", 2, "native/G6.1", old)
        with mock.patch.object(plan, "branch_time", lambda tree, branch: old.timestamp()):
            code, text = self.t.run(["next"])
            self.assertIn("STALE: package G6 is claimed by agent 2 (no commit on native/G6.1 for 60 hours). Take it over with: "
                          "plan.py claim G6 --take-stale --agent 1", text)
            status = self.t.run(["status"], expect=0)[1]
            self.assertIn("- G6: agent 2 since", status)
            self.assertIn("STALE (no commit for 48 hours): plan.py claim G6 --take-stale", status)
            code, text = self.t.run(["claim", "G6"])
            self.assertEqual(code, 1)
            self.assertIn("the claim is stale; take it with --take-stale", text)
            code, text = self.t.run(["start", "G6.2"])
            self.assertEqual(code, 1)
            self.assertIn("is held by agent 2 (the claim is stale: plan.py claim G6 --take-stale --agent 1)", text)
            code, text = self.t.run(["claim", "G6", "--take-stale"], expect=0)
            self.assertIn("package G6 is claimed by agent 1", text)
            self.assertEqual(lv.claim_of(lv.read_claims(self.t.live), "G6")["agent"], "1")
            self.assertEqual(plan.find(plan.read_steps(self.t.tree), "G6.1")["agent"], "1",
                             "the step started on the stale branch is now the new holder's; its branch is as it was")

    def test_a_claim_that_is_not_stale_is_not_taken_even_with_the_flag(self):
        self.plan(("G6.1", "G6", "", "open"))
        self.t.run(["start", "G6.1", "--agent", "2"], expect=0)
        code, text = self.t.run(["claim", "G6", "--take-stale"])
        self.assertEqual(code, 1)
        self.assertIn("held by agent 2", text)
        self.assertIn("not stale", text)

    def test_the_real_branch_time_is_read_from_git(self):
        self.plan(("G6.1", "G6", "", "open"))
        self.t.run(["start", "G6.1", "--agent", "2"], expect=0)
        folder = self.t.folder("G6.1")
        (folder / "x.txt").write_text("x", encoding="utf-8")
        self.t.git("add", "-A", cwd=folder)
        with mock.patch.dict(os.environ, {"GIT_COMMITTER_DATE": "2020-01-02T03:04:05+00:00", "GIT_AUTHOR_DATE": "2020-01-02T03:04:05+00:00"}):
            self.t.git("commit", "-m", "old", cwd=folder)
        self.assertEqual(plan.branch_time(self.t.tree, "native/G6.1"), 1577934245)
        self.assertIsNone(plan.branch_time(self.t.tree, "native/nowhere"))
        claims = lv.read_claims(self.t.live)
        claims[0]["time"] = "2020-01-01T00:00:00"
        self.assertEqual(plan.stale_of(self.t.tree, claims), {"G6"})
        claims[0]["time"] = lv.stamp()
        self.assertEqual(plan.stale_of(self.t.tree, claims), set(), "a claim made today is live even when its branch's last commit is old")

    def test_start_by_agent_2_claims_the_package_and_marks_the_step_with_the_agent(self):
        self.plan(("G6.1", "G6", "", "open"))
        code, text = self.t.run(["start", "G6.1", "--agent", "2"], expect=0)
        self.assertIn("package G6 is claimed by agent 2", text)
        c = lv.claim_of(lv.read_claims(self.t.live), "G6")
        self.assertEqual((c["agent"], c["branch"]), ("2", "native/G6.1"))
        self.assertEqual(lv.read_state(self.t.live)["G6.1"]["agent"], "2")

    def test_agent_2_is_refused_a_package_that_is_not_on_its_list(self):
        self.plan(("A.1", "A", "", "open"), ("R7.1", "R7", "", "open"))
        for sid in ("A.1", "R7.1"):
            code, text = self.t.run(["start", sid, "--agent", "2"])
            self.assertEqual(code, 1)
            self.assertIn("is not on agent 2's list (G6, G7, G8, G9, G1, R10, R9, G4); agent 2 is never given another package", text)
            self.assertFalse(self.t.folder(sid).exists())
        self.assertEqual(self.t.run(["claim", "A", "--agent", "2"])[0], 1)

    def test_start_is_refused_for_a_package_another_agent_holds_and_nothing_is_changed(self):
        self.plan(("G6.1", "G6", "", "open"), ("G6.2", "G6", "", "open"))
        self.t.run(["start", "G6.1", "--agent", "2"], expect=0)
        code, text = self.t.run(["start", "G6.2", "--agent", "1"])
        self.assertEqual(code, 1)
        self.assertIn("package G6 is held by agent 2", text)
        self.assertEqual(plan.find(plan.read_steps(self.t.tree), "G6.2")["state"], "open")

    def test_land_is_refused_to_the_agent_that_does_not_hold_the_package_or_did_not_start_the_step(self):
        self.plan(("G6.1", "G6", "", "open"))
        self.t.run(["start", "G6.1", "--agent", "2"], expect=0)
        code, text = self.t.run(["land", "G6.1", "--agent", "1"])
        self.assertEqual(code, 1)
        self.assertIn("package G6 is held by agent 2", text)
        lv.drop_claim(self.t.live, "G6")
        code, text = self.t.run(["land", "G6.1", "--agent", "1"])
        self.assertEqual(code, 1)
        self.assertIn("step G6.1 was started by agent 2, not agent 1", text)

    def test_the_started_limit_is_per_agent(self):
        self.plan(*[("S%d.1" % i, "S%d" % i, "", "open") for i in range(1, 4)], ("A.1", "A", "", "open"), ("G6.1", "G6", "", "open"))
        for i in range(1, 4):
            self.t.run(["start", "S%d.1" % i], expect=0)
        code, text = self.t.run(["start", "A.1"])
        self.assertEqual(code, 1)
        self.assertIn("3 steps are started and not landed (S1.1, S2.1, S3.1)", text)
        self.t.run(["start", "G6.1", "--agent", "2"], expect=0)

    def test_the_pair_is_refused_at_start(self):
        self.plan(("G2.1", "G2", "", "open"), ("R7.1", "R7", "", "open"))
        self.t.run(["start", "R7.1"], expect=0)
        code, text = self.t.run(["start", "G2.1"])
        self.assertEqual(code, 1)
        self.assertIn("waits: R7 is in work", text)
        self.assertEqual(self.t.run(["next"])[1].count("NEXT:"), 0, "next gives neither: R7.1 is started and G2.1 waits for it")

    def test_start_by_the_master_claims_the_package(self):
        self.plan(("A.1", "A", "", "open"), ("A.2", "A", "", "open"))
        self.t.run(["start", "A.1"], expect=0)
        self.assertEqual(lv.claim_of(lv.read_claims(self.t.live), "A")["agent"], "1")

    def test_claim_by_hand_and_its_refusals(self):
        self.plan(("G7.1", "G7", "", "open"), ("G6.1", "G6", "", "landed"))
        self.assertEqual(self.t.run(["claim", "Q"])[0], 1)
        self.assertIn("is landed", self.t.run(["claim", "G6", "--agent", "2"])[1])
        self.t.run(["claim", "G7", "--agent", "2"], expect=0)
        self.assertEqual(lv.claim_of(lv.read_claims(self.t.live), "G7")["agent"], "2")
        self.t.run(["claim", "G7", "--agent", "2"], expect=0)               # again: renewed
        self.assertEqual(len(lv.read_claims(self.t.live)), 1)
        self.assertIn("held by agent 2", self.t.run(["claim", "G7"])[1])

    def test_a_claim_is_dropped_when_the_package_has_landed_and_kept_while_it_has_steps_left(self):
        cards = plan.native(self.t.tree) / "03-X.md"
        cards.write_text("\n".join(["### A.1 First step", "", "- **Rows.** None.", "", "### A.2 Second step", "", "- **Rows.** None.", ""]),
                         encoding="utf-8")
        self.t.git("add", "-A")
        self.t.git("commit", "-m", "cards with no rows")
        self.t.git("push", "origin", "main")
        self.t.run(["start", "A.1"], expect=0)
        with mock.patch.object(plan, "LAND", lambda *a: plan.Verdict(True, [("c", "PASS", "ok")])):
            self.t.run(["land", "A.1"], expect=0)
            self.assertEqual(lv.claim_of(lv.read_claims(self.t.live), "A")["agent"], "1", "A.2 is left: the claim is renewed")
            self.t.run(["start", "A.2"], expect=0)
            self.assertEqual(plan.find(plan.read_steps(self.t.tree), "A.2")["state"], "started")
        self.assertEqual(lv.read_landings(self.t.live)[0]["agent"], "1")

    def test_status_shows_both_agents_next_steps_and_the_claims(self):
        self.plan(("G6.1", "G6", "", "open"), ("A.1", "A", "", "open"))
        self.t.run(["start", "G6.1", "--agent", "2"], expect=0)
        text = self.t.run(["status"], expect=0)[1]
        self.assertIn("- G6.1 Title of G6.1: agent 2, branch native/G6.1", text)
        self.assertIn("## Next (agent 1)\n\n- A.1 Title of A.1", text)
        self.assertIn("## Next (agent 2)\n\n- nothing on agent 2's list can start now", text)
        self.assertIn("- G6: agent 2 since", text)

    def test_the_commands_take_the_agent_in_the_documented_place(self):
        parsed = plan.parser().parse_args(["next", "--agent", "2"])
        self.assertEqual(parsed.agent, 2)
        self.assertEqual(plan.parser().parse_args(["land", "A.1"]).agent, 1)
        with contextlib.redirect_stderr(io.StringIO()), self.assertRaises(SystemExit):
            plan.parser().parse_args(["next", "--agent", "3"])


# ---- the tool-fault record ------------------------------------------------------------------------------------

class FaultRecordTest(unittest.TestCase):
    def setUp(self):
        self.t = Tree().__enter__()
        self.addCleanup(self.t.__exit__)
        self.t.run(["start", "A.1"], expect=0)
        ready_a1(self.t)
        self.verdict = None
        patcher = mock.patch.object(plan, "LAND", lambda *a: self.verdict)
        patcher.start()
        self.addCleanup(patcher.stop)

    def land(self, extra=()):
        return self.t.run(["land", "A.1", *extra])

    def tool(self, check="harness/self", detail="result.json is missing", lines=()):
        return plan.Verdict(False, [(check, "TOOL", detail)], [(check, "TOOL", detail)], list(lines))

    def test_a_tool_result_is_written_at_once_with_the_command_to_say_what_it_stopped(self):
        self.verdict = self.tool(lines=["... D:\\NebulaWork\\build\\gate-runs\\20261120-101500-abc-quick\\harness-self.log ...\n"])
        code, text = self.land()
        self.assertEqual(code, 4)
        rows = lv.read_faults(self.t.live)
        self.assertEqual(len(rows), 1)
        r = rows[0]
        self.assertEqual((r["agent"], r["step"], r["kind"], r["check"], r["first_error"], r["could_not"]),
                         ("1", "A.1", "TOOL", "harness/self", "result.json is missing", ""))
        self.assertEqual(r["run_folder"], "D:\\NebulaWork\\build\\gate-runs\\20261120-101500-abc-quick")
        self.assertRegex(r["time"], r"^\d{4}-\d\d-\d\dT\d\d:\d\d:\d\d$")
        self.assertIn("Written to ", text)
        self.assertIn("tool-faults.tsv (1 line)", text)
        self.assertIn('plan.py toolfault --last --could-not "<one sentence>"', text)
        self.assertIn("this is a tool fault: report it in BLOCKED.md under 'tools' and take the next step; do not debug the tool", text)

    def test_the_line_is_never_in_the_repository_or_a_landing(self):
        self.verdict = self.tool()
        self.land()
        self.assertEqual(self.t.git("status", "--short"), "")
        self.assertEqual(self.t.git("status", "--short", cwd=self.t.folder("A.1")), "")
        self.assertEqual(self.t.git("ls-files", "--", "*tool-faults*", cwd=self.t.folder("A.1")), "")
        self.assertTrue((self.t.live / "tool-faults.tsv").is_file())

    def test_an_env_result_is_written_too_and_a_fail_is_not(self):
        self.verdict = plan.Verdict(False, [], [("start/d3d11", "ENV", "D: is under its floor"), ("tour/d3d12", "FAIL", "replay differs")])
        self.assertEqual(self.land()[0], 1, "a real failure beside it counts, as before")
        rows = lv.read_faults(self.t.live)
        self.assertEqual([(r["kind"], r["check"]) for r in rows], [("ENV", "start/d3d11")])

    def test_a_failure_alone_writes_no_line(self):
        self.verdict = plan.Verdict(False, [], [("tour/d3d12", "FAIL", "replay differs")])
        text = self.land()[1]
        self.assertEqual(lv.read_faults(self.t.live), [])
        self.assertNotIn("toolfault", text)

    def test_a_pass_writes_no_line(self):
        self.verdict = plan.Verdict(True, [("tablet/short", "ENV", "owed: the tablet is away")], [])
        self.assertEqual(self.land()[0], 0)
        self.assertEqual(lv.read_faults(self.t.live), [])

    def test_the_same_fault_three_times_is_one_signature_with_count_three_and_never_stops_the_step(self):
        self.verdict = self.tool()
        for _ in range(3):
            self.assertEqual(self.land()[0], 4)
        groups = lv.fault_groups(lv.read_faults(self.t.live))
        self.assertEqual([(g["count"], g["check"]) for g in groups], [(3, "harness/self")])
        self.assertEqual(failures.Store(self.t.store).count("native/A.1"), 0)

    def test_toolfault_last_could_not_sets_the_sentence(self):
        self.verdict = self.tool()
        self.land()
        code, text = self.t.run(["toolfault", "--last", "--could-not", "land the first   step\nand go on"], expect=0)
        self.assertIn("could not: land the first step and go on", text)
        self.assertEqual(lv.read_faults(self.t.live)[0]["could_not"], "land the first step and go on")

    def test_toolfault_needs_last_and_a_fault_of_that_agent(self):
        self.assertEqual(self.t.run(["toolfault", "--could-not", "x"])[0], 1)
        code, text = self.t.run(["toolfault", "--last", "--could-not", "x"])
        self.assertEqual(code, 1)
        self.assertIn("agent 1 has no tool fault recorded", text)

    def test_the_agent_is_written_and_the_command_names_it(self):
        lv.append_fault(self.t.live, 2, "G6.1", "ENV", "start/vk", "no device")
        self.t.run(["toolfault", "--last", "--could-not", "start Vulkan", "--agent", "2"], expect=0)
        rows = lv.read_faults(self.t.live)
        self.assertEqual((rows[0]["agent"], rows[0]["could_not"]), ("2", "start Vulkan"))

    def test_plan_tool_writes_a_line_too(self):
        self.t.run(["tool", "the gate printed no   result"], expect=0)
        r = lv.read_faults(self.t.live)[0]
        self.assertEqual((r["kind"], r["check"], r["first_error"], r["step"]), ("TOOL", "(by hand)", "the gate printed no result", ""))
        self.assertIn("the gate printed no result", (plan.native(self.t.tree) / "BLOCKED.md").read_text(encoding="utf-8"))


def main():
    result = unittest.main(argv=[sys.argv[0]], exit=False, verbosity=1).result
    print("plan_agents_test: %s (%d tests)" % ("pass" if result.wasSuccessful() else "FAIL", result.testsRun))
    return 0 if result.wasSuccessful() else 1


if __name__ == "__main__":
    sys.exit(main())
