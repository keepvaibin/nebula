"""plan_test.py: tools/plan/plan.py, the step tool of the native plan (package T13).

Two kinds of test. The plan's own files (docs/plan/native: 138 steps, their cards) are read as they are: every step has
its card, every need names something, and ten and more real cards give the rows they must give. The commands run in a
temporary git tree made here (a bare origin, a plan tree, work folders, a small ledger with CRLF line ends) with the
landing step replaced by a fake: no game, no build, no network, not the real ledger, no gate run.

    python -X utf8 -I tools/gate/tests/plan_test.py [--work <folder>]
"""
import contextlib
import io
import os
import pathlib
import shutil
import subprocess
import sys
import tempfile
import textwrap
import unittest
from unittest import mock

HERE = pathlib.Path(__file__).resolve().parent
TREE = HERE.parents[2]
sys.path.insert(0, str(HERE))
sys.path.insert(0, str(TREE / "tools" / "plan"))
sys.path.insert(0, str(TREE / "tools" / "verify"))
import failures  # noqa: E402
import live as lv  # noqa: E402
import plan  # noqa: E402
from plan_fixture import (CARDS, TEMPLATE, Tree, git_run, ledger_row, ledger_text, plan_of, rmtree_force,  # noqa: E402
                          run, setup_module, step, teardown_module)


# ---- helpers ------------------------------------------------------------------------------------------------

def next_id(steps):
    choice = plan.choose_next(steps)
    return choice.step["step"] if choice else None


def setUpModule():
    setup_module()


def tearDownModule():
    teardown_module()


# ---- the plan's real files ----------------------------------------------------------------------------------

class RealPlanTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.steps = plan.read_steps(TREE)

    def test_the_plan_is_138_steps_in_one_order_with_every_step_open_at_the_start(self):
        self.assertEqual(len(self.steps), 138)
        self.assertEqual([int(s["order"]) for s in self.steps], list(range(1, 139)))
        self.assertEqual(self.steps[0]["step"], "S0.1.1")
        self.assertEqual({s["state"] for s in self.steps}, {"open"})
        self.assertEqual({s["commit"] for s in self.steps}, {""})

    def test_every_step_has_its_card_and_every_card_has_its_step(self):
        for s in self.steps:
            card = plan.card_text(TREE, s)
            self.assertIsNotNone(card, s["step"])
            self.assertTrue(card.startswith("### %s " % s["step"]), s["step"])
            self.assertEqual(len([l for l in card.splitlines() if l.startswith("### ")]), 1, s["step"] + " card runs into the next")
            self.assertEqual(len([l for l in card.splitlines() if l.startswith("- **Rows")]), 1, s["step"])
        headings = 0
        for name in ("03-NATIVE-STAGE-0.md", "04-NATIVE-LANE-R.md", "05-NATIVE-LANE-G.md"):
            for line in (plan.native(TREE) / name).read_text(encoding="utf-8").splitlines():
                if line.startswith("### ") and not line.startswith("### Rows of this package"):
                    headings += 1
        self.assertEqual(headings, 138, "a card with no step, or a step with no card")

    def test_every_need_names_steps_and_a_step_never_needs_a_later_one_of_a_different_package_unless_the_plan_says(self):
        ids = {s["step"] for s in self.steps}
        packages = {s["package"] for s in self.steps}
        for s in self.steps:
            for token in [t for t in s["package_needs"].split(",") if t]:
                self.assertTrue(token in packages or token in plan.SPLITS or token == "everything", "%s needs %s" % (s["step"], token))
            needs = plan.need_ids(s, self.steps)
            self.assertNotIn(s["step"], needs)
            self.assertTrue(set(needs) <= ids)
            self.assertEqual(plan.unmet(s, self.steps), [(i, "open") for i in needs])

    def test_the_split_needs_G2a_G2b_G2c_and_everything(self):
        by = {s["step"]: s for s in self.steps}
        self.assertEqual(plan.need_ids(by["G2.8"], self.steps)[:7], ["G2.%d" % n for n in range(1, 8)])
        g28 = set(plan.need_ids(by["G2.8"], self.steps))
        self.assertIn("R5.1", g28)
        self.assertIn("R7.8", g28)
        self.assertNotIn("G2.9", g28)
        self.assertIn("G2.8", plan.need_ids(by["G2.9"], self.steps))
        everything = set(plan.need_ids(by["Z.1"], self.steps))
        self.assertEqual(everything, {s["step"] for s in self.steps if not s["step"].startswith("Z.")})
        self.assertEqual(set(plan.need_ids(by["Z.2"], self.steps)), everything | {"Z.1"}, "the end check follows the profiles")
        self.assertEqual(set(plan.need_ids(by["Z.3"], self.steps)), everything | {"Z.1", "Z.2"})

    def test_next_on_a_fresh_copy_is_S0_1_1_with_its_card(self):
        code, text = run(["next"], TREE, TREE / "none", TREE / "none.tsv")
        self.assertEqual(code, 0)
        lines = text.splitlines()
        self.assertEqual(lines[0], "NEXT: S0.1.1  Land `rows/p022c` and `rows/p022d` as one batch")
        self.assertIn("### S0.1.1 Land `rows/p022c` and `rows/p022d` as one batch", lines)
        self.assertIn("- **Where.** `rows/p022c` is two commits", text)
        self.assertIn("| P-022 | The same per-renderer logic", text)             # the package's row table
        self.assertIn("Closes whole (state fixed, proof naming a test, a search or a measure): P-022", text)
        self.assertIn("start: python -X utf8 -I", text)
        self.assertIn("plan.py land S0.1.1", text)
        self.assertIn("tour3: the 12-stop tour on the three renderers", text)   # the checks of its paths
        self.assertNotIn("### S0.1.2", text, "the card stops at its own end")
        self.assertEqual(self.steps[0]["state"], "open", "next changes nothing")

    # The Rows line, on real cards. Each expectation is what the card's words say (read them in the card file).
    REAL_ROWS = {
        "S0.1.1": (["P-022"], []),
        "S0.1.2": (["P-096", "P-320", "P-321", "P-323"], []),
        "S0.1.3": ([], []),                                              # none of this package's rows
        "S0.2.1": (["P-249", "P-214", "P-261"], ["P-260"]),
        "S0.2.5": (["P-294"], ["P-242"]),                               # "P-294 closes here"; P-242 "(closes at S0.2.6)"
        "S0.2.6": (["N-1068", "P-242"], ["P-293", "P-295", "P-294"]),   # P-294 "(closed at S0.2.5)"
        "S0.3.1": ([], ["N-1053", "N-1054"]),                           # "finds what is left"
        "S0.3.2": (["N-1053", "N-1054"], []),
        "R2.2": (["P-048", "P-056", "P-066", "P-061"], ["P-293"]),       # "The part of P-293 ... stays with R7.8"
        "R2.5": (["P-216"], ["P-255"]),
        "R3.13": (["P-151", "P-157"], []),
        "R4.9": (["P-154"], ["P-155", "P-194"]),                        # (closes); (part: ...); (the site parts)
        "G1.1": (["P-282", "P-279"], ["P-244"]),
        "G2.5": (["I-010", "P-315", "P-316", "P-318", "P-019"], []),    # P-069 is named inside a bracket only
        "G2.8": (["I-003"], []),                                         # "(R5's row; it closes here)"
        "G3.1": (["P-265"], ["P-040", "P-146"]),
        "G3.7": (["N-1083", "P-283", "P-260", "P-040", "P-146", "P-241"], []),
        "G5.3": (["P-248"], ["P-191"]),                                  # three "(part)" and "The row closes here"
        "R7.3": (["P-006", "P-055", "P-031", "P-053", "P-025", "P-063", "P-290", "P-296"], ["P-239", "P-297"]),
        "R7.7": (["P-054", "P-030", "P-049", "P-050", "P-057", "N-1081", "P-305", "P-297"], ["P-028"]),
        "Z.2": ([], []),
    }

    def test_the_rows_line_of_twenty_one_real_cards(self):
        self.assertGreaterEqual(len(self.REAL_ROWS), 10)
        by = {s["step"]: s for s in self.steps}
        for step_id, (closes, advances) in self.REAL_ROWS.items():
            r = plan.card_rows(plan.card_text(TREE, by[step_id]))
            self.assertTrue(r.ok, step_id)
            self.assertEqual((sorted(r.closes), sorted(r.advances)), (sorted(closes), sorted(advances)), step_id)

    def test_every_card_has_a_rows_line_the_parser_can_read_and_a_row_named_once_per_side(self):
        for s in self.steps:
            r = plan.card_rows(plan.card_text(TREE, s))
            self.assertTrue(r.ok, "%s: %s" % (s["step"], r.why))
            self.assertFalse(set(r.closes) & set(r.advances), s["step"])

    def test_every_row_a_card_closes_or_advances_is_in_the_real_ledger_or_a_known_outsider(self):
        ledger = plan.read_ledger((TREE / plan.LEDGER).read_bytes().decode("utf-8"))
        missing = set()
        for s in self.steps:
            r = plan.card_rows(plan.card_text(TREE, s))
            missing |= {i for i in r.closes + r.advances if i not in ledger}
        # P-332 is opened by package T1; P-326 on are rows the plan's own packages open.
        self.assertLessEqual(missing, {"P-332"}, "rows named on cards and absent from the ledger: %s" % sorted(missing))

    def test_the_package_row_table_of_each_real_package(self):
        for s in self.steps:
            if s["package"] == "Z":
                continue
            table = plan.package_table(plan.card_lines(TREE, s), s["package"])
            self.assertIsNotNone(table, s["package"])
            self.assertTrue(table.lstrip().startswith("| Row"), s["package"])
        self.assertIsNone(plan.package_table(plan.card_lines(TREE, self.steps[0]), "S9.9"))

    def test_the_plan_tool_reads_its_own_documents_without_a_bad_reference(self):
        import doc_refs
        allow = doc_refs.read_allow(TREE / "tools" / "verify" / "doc_refs_allow.txt")
        names = [p for p in plan.native(TREE).glob("*.md")]
        self.assertEqual(doc_refs.check_files(names, TREE, allow), [])


# ---- the card's Rows line: words ----------------------------------------------------------------------------

class RowsWordsTest(unittest.TestCase):
    def read(self, text):
        r = plan.parse_rows(text)
        return sorted(r.closes), sorted(r.advances), sorted(r.mentioned)

    def test_whole_and_a_bare_id_close(self):
        self.assertEqual(self.read("P-001 whole; P-002: whole. P-003 (whole). P-004, P-005 (each whole)"),
                         (["P-001", "P-002", "P-003", "P-004", "P-005"], [], []))
        self.assertEqual(self.read("P-009")[0], ["P-009"])

    def test_part_and_advance_advance(self):
        self.assertEqual(self.read("P-001 (part): the one; P-002 advanced (x); P-003: advances (y). P-004: the talk balloon part.")[1],
                         ["P-001", "P-002", "P-003", "P-004"])
        self.assertEqual(self.read("P-001's collision part (a query); P-002 first part (per-use)")[1], ["P-001", "P-002"])

    def test_closes_here_wins_over_an_earlier_part(self):
        self.assertEqual(self.read("P-248 (part): a. P-248 (part): b. The row closes here, after its punch box part."),
                         (["P-248"], [], []))

    def test_closes_at_another_step_advances(self):
        self.assertEqual(self.read("P-001 closes at R7.2 (x). P-002 (closes at S0.2.6). P-003: path gone (closed at S0.2.5).")[1],
                         ["P-001", "P-002", "P-003"])

    def test_the_word_whole_inside_a_part_is_still_a_part(self):
        self.assertEqual(self.read("P-239's fur part (whole; its fog part is R5.6's)"), ([], ["P-239"], []))

    def test_a_group_shares_the_words_after_it(self):
        self.assertEqual(self.read("P-040 and P-146 advanced (x). P-006, P-055 whole"), (["P-006", "P-055"], ["P-040", "P-146"], []))

    def test_none_lines_name_rows_without_working_them(self):
        self.assertEqual(self.read("none of this package's rows (step 2). P-155 stays open: the disc pictures."), ([], [], ["P-155"]))
        self.assertEqual(self.read("None."), ([], [], []))
        self.assertEqual(self.read("none of this package's rows. It advances P-044's planner test (outside this package)"),
                         ([], ["P-044"], []))
        self.assertEqual(self.read("No row of this package. Closes P-096, P-320 in the ledger."), (["P-096", "P-320"], [], []))

    def test_a_row_named_in_a_bracket_with_no_word_about_it_is_only_named(self):
        self.assertEqual(self.read("P-019 whole (the heat input; its note says the hint's fault is P-069, outside this package)"),
                         (["P-019"], [], ["P-069"]))

    def test_an_unclear_word_advances_rather_than_close(self):
        self.assertEqual(self.read("N-1053: finds what is left.")[1], ["N-1053"])

    def test_a_line_with_no_row_and_no_none_cannot_be_read(self):
        r = plan.parse_rows("see the overview")
        self.assertFalse(r.ok)
        self.assertFalse(plan.card_rows(None).ok)
        self.assertFalse(plan.card_rows("### X.1 t\n\n- **What changes.** x").ok)

    def test_a_card_rows_line_that_continues_on_the_next_line(self):
        r = plan.card_rows("### X.1 t\n- **Rows.** P-001 whole;\n  P-002 advanced.\n- **Needs first.** none")
        self.assertEqual((r.closes, r.advances), (["P-001"], ["P-002"]))


# ---- order, needs, blocked ----------------------------------------------------------------------------------

class NextTest(unittest.TestCase):
    def test_file_order(self):
        s = plan_of(step("A.1", "A"), step("A.2", "A"), step("B.1", "B"))
        self.assertEqual(next_id(s), "A.1")

    def test_a_started_or_landed_step_is_not_next(self):
        s = plan_of(step("A.1", "A", state="landed"), step("B.1", "B", state="started"), step("C.1", "C"))
        self.assertEqual(next_id(s), "C.1")

    def test_a_step_waits_for_its_package_needs(self):
        s = plan_of(step("A.1", "A"), step("A.2", "A"), step("B.1", "B", needs="A"), step("C.1", "C"))
        s[0]["state"] = "landed"
        self.assertEqual(next_id(s), "A.2")
        s[1]["state"] = "started"
        self.assertEqual(next_id(s), "C.1", "B waits: A.2 is not landed; nothing in A is open")
        s[1]["state"] = "landed"
        self.assertEqual(next_id(s), "B.1")

    def test_a_package_is_done_only_when_all_its_steps_are_landed(self):
        s = plan_of(step("A.1", "A", state="landed"), step("A.2", "A", state="open"), step("B.1", "B", needs="A"))
        self.assertEqual(plan.unmet(s[2], s), [("A.2", "open")])

    def test_several_needs_all_have_to_be_met(self):
        s = plan_of(step("A.1", "A", state="landed"), step("B.1", "B"), step("C.1", "C", needs="A,B"))
        self.assertEqual(plan.unmet(s[2], s), [("B.1", "open")])

    def test_G2a_G2b_G2c_mean_the_numbered_landings(self):
        rows = [step("G2.%d" % n, "G2", state="landed" if n <= 7 else "open") for n in range(1, 10)]
        rows.append(step("X.1", "X", needs="G2a"))
        rows.append(step("X.2", "X", needs="G2b"))
        s = plan_of(*rows)
        self.assertEqual(plan.unmet(s[9], s), [])
        self.assertEqual(plan.unmet(s[10], s), [("G2.8", "open"), ("X.1", "open")], "and X.1: the package's earlier step")
        s[7]["state"] = "landed"
        self.assertEqual(plan.unmet(s[10], s), [("X.1", "open")])
        self.assertEqual(plan.need_ids(step("Y.1", "Y", needs="G2c"), s), ["G2.9"])

    def test_everything_waits_for_every_step_that_does_not_itself_wait_for_everything(self):
        s = plan_of(step("A.1", "A", state="landed"), step("B.1", "B"), step("Z.1", "Z", needs="everything"),
                    step("Z.2", "Z", needs="everything"))
        self.assertEqual(plan.unmet(s[2], s), [("B.1", "open")])
        s[1]["state"] = "landed"
        self.assertEqual(next_id(s), "Z.1")
        s[2]["state"] = "started"
        self.assertEqual(next_id(s), None, "Z.2 follows Z.1")
        self.assertEqual(plan.unmet(s[3], s), [("Z.1", "started")])

    def test_a_package_mates_earlier_steps_are_landed_first(self):
        s = plan_of(step("A.1", "A", state="started"), step("A.2", "A"), step("B.1", "B"))
        self.assertEqual(next_id(s), "B.1", "A.2 is cut from main, which has no A.1 yet")

    def test_the_skip_past_a_blocked_package(self):
        s = plan_of(step("A.1", "A", state="blocked"), step("A.2", "A"), step("B.1", "B", needs="A"), step("C.1", "C", needs="B"),
                    step("D.1", "D"))
        choice = plan.choose_next(s)
        self.assertEqual(choice.step["step"], "D.1", "A.2 is in the blocked package; B and C need it")
        self.assertEqual([b["step"] for b in choice.blocked_before], ["A.1"])

    def test_a_blocked_step_after_the_chosen_one_is_not_reported_before_it(self):
        s = plan_of(step("A.1", "A"), step("B.1", "B", state="blocked"))
        choice = plan.choose_next(s)
        self.assertEqual((choice.step["step"], choice.blocked_before), ("A.1", []))

    def test_nothing_can_start(self):
        s = plan_of(step("A.1", "A", state="started"), step("B.1", "B", needs="A"))
        self.assertIsNone(plan.choose_next(s))
        s = plan_of(step("A.1", "A", state="landed"))
        self.assertIsNone(plan.choose_next(s))

    def test_next_says_a_step_is_blocked_and_names_the_one_that_does_not_depend_on_it(self):
        with Tree() as t:
            plan.set_state(t.tree, "A.1", "blocked")
            plan.append_under(plan.native(t.tree) / "BLOCKED.md", "steps", "- 2026-11-20 A.1: the tool broke | first failure line: x")
            code, text = t.run(["next"])
            self.assertEqual(code, 0)
            self.assertIn("BLOCKED: A.1 First step: the tool broke | first failure line: x", text)
            self.assertIn("NEXT: C.1", text)
            self.assertIn("the first later step that does not need its package is C.1", text)

    def test_next_with_nothing_to_do_says_what_is_in_progress(self):
        with Tree() as t:
            for sid in ("A.1", "C.1"):
                plan.set_state(t.tree, sid, "started")
            plan.set_state(t.tree, "A.2", "started")
            code, text = t.run(["next"])
            self.assertEqual(code, 1)
            self.assertIn("Nothing can start now", text)
            self.assertIn("A.1 started", text)

    def test_next_after_everything_landed(self):
        with Tree() as t:
            for s in plan.read_steps(t.tree):
                plan.set_state(t.tree, s["step"], "landed")
            code, text = t.run(["next"])
            self.assertEqual(code, 0)
            self.assertIn("Every step is landed (5 of 5)", text)

    def test_a_steps_file_that_is_wrong_is_refused(self):
        with Tree() as t:
            path = plan.native(t.tree) / "steps.tsv"
            path.write_text(path.read_text().replace("\topen\t", "\topn\t", 1))
            code, text = t.run(["next"])
            self.assertEqual(code, 2)
            self.assertIn("state 'opn' is none of", text)


# ---- start --------------------------------------------------------------------------------------------------

class StartTest(unittest.TestCase):
    def test_start_makes_the_branch_and_the_work_folder_and_sets_the_state(self):
        with Tree() as t:
            code, text = t.run(["start", "A.1"], expect=0)
            self.assertIn("started A.1: branch native/A.1", text)
            folder = t.folder("A.1")
            self.assertEqual(folder.name, "N-A.1")
            self.assertTrue((folder / "docs" / "plan" / "native" / "steps.tsv").is_file())
            self.assertEqual(t.git("rev-parse", "--abbrev-ref", "HEAD", cwd=folder), "native/A.1")
            self.assertEqual(t.git("rev-parse", "HEAD", cwd=folder), t.git("rev-parse", "origin/main"))
            self.assertEqual(plan.find(plan.read_steps(t.tree), "A.1")["state"], "started")
            self.assertEqual(plan.find(plan.read_steps(t.tree), "A.2")["state"], "open")

    def test_start_cuts_the_branch_from_mains_newest_commit(self):
        with Tree() as t:
            other = t.dir / "other"
            t.git("clone", str(t.origin), str(other), cwd=t.dir)
            (other / "new.txt").write_text("x", encoding="utf-8")
            t.git("add", "-A", cwd=other)
            t.git("commit", "-m", "newer", cwd=other)
            t.git("push", "origin", "HEAD:main", cwd=other)
            t.run(["start", "A.1"], expect=0)
            self.assertTrue((t.folder("A.1") / "new.txt").is_file())

    def test_start_refuses_the_fourth_started_step(self):
        steps = [("S%d.1" % i, "P%d" % i) for i in range(1, 6)]
        with Tree() as t:
            n = plan.native(t.tree)
            (n / "steps.tsv").write_text("\t".join(plan.STEP_COLUMNS) + "\n" + "".join(
                "\t".join([str(i + 1), sid, pkg, "t", "", "03-X.md", "open", ""]) + "\n" for i, (sid, pkg) in enumerate(steps)),
                encoding="utf-8")
            for sid, _ in steps[:3]:
                t.run(["start", sid], expect=0)
            code, text = t.run(["start", "S4.1"])
            self.assertEqual(code, 1)
            self.assertIn("3 steps are started and not landed (S1.1, S2.1, S3.1)", text)
            self.assertEqual(plan.find(plan.read_steps(t.tree), "S4.1")["state"], "open")
            self.assertFalse(t.folder("S4.1").exists())
            plan.set_state(t.tree, "S1.1", "landed")
            t.run(["start", "S4.1"], expect=0)

    def test_start_refuses_a_step_whose_needs_are_not_landed(self):
        with Tree() as t:
            code, text = t.run(["start", "B.1"])
            self.assertEqual(code, 1)
            self.assertIn("needs first: A.1 (open), A.2 (open)", text)
            self.assertFalse(t.folder("B.1").exists())

    def test_start_refuses_a_step_twice_and_an_unknown_step(self):
        with Tree() as t:
            t.run(["start", "A.1"], expect=0)
            self.assertEqual(t.run(["start", "A.1"])[0], 1)
            self.assertEqual(t.run(["start", "Q.9"])[0], 1)

    def test_a_blocked_step_can_be_started_again_on_its_branch(self):
        with Tree() as t:
            t.run(["start", "A.1"], expect=0)
            t.commit("A.1")
            t.run(["blocked", "A.1", "waiting"], expect=0)
            shutil.rmtree(t.folder("A.1"))
            t.git("worktree", "prune")
            t.run(["start", "A.1"], expect=0)
            self.assertEqual(t.git("log", "-1", "--format=%s", cwd=t.folder("A.1")), "work", "the branch keeps its commit")


# ---- land ---------------------------------------------------------------------------------------------------

class LandTest(unittest.TestCase):
    def setUp(self):
        self.t = Tree().__enter__()
        self.addCleanup(self.t.__exit__)
        self.t.run(["start", "A.1"], expect=0)
        self.calls = []
        self.verdict = plan.Verdict(True, [("repo/code", "PASS", "ok"), ("tour/d3d12", "PASS", "ok")])
        patcher = mock.patch.object(plan, "LAND", self.fake_land)
        patcher.start()
        self.addCleanup(patcher.stop)

    def fake_land(self, folder, branch, tablet, wait):
        self.calls.append((pathlib.Path(folder), branch, tablet, wait))
        return self.verdict

    def ready(self, p001=("fixed", "tests/render/a_test 1-9 fails before"), p002_note="advanced 11-20: half done"):
        """The ledger the card wants, committed on the branch."""
        self.t.write_ledger([ledger_row("P-001", p001[0], p001[1]), ledger_row("P-002", note=p002_note), ledger_row("P-003"),
                             ledger_row("P-004")], tree=self.t.folder("A.1"))
        return self.t.commit("A.1", "A.1: the code", {"runtime/src/render/a.cpp": "a\nb\nc\nd\n"})

    def land(self, expect=None):
        return self.t.run(["land", "A.1"], expect)

    def test_a_ready_step_lands_and_marks_itself_landed_in_its_own_commit(self):
        code_commit = self.ready()
        code, text = self.land(expect=0)
        self.assertEqual(len(self.calls), 1)
        folder, branch, tablet, wait = self.calls[0]
        self.assertEqual((folder, branch, tablet, wait), (self.t.folder("A.1"), "native/A.1", plan.TABLET, plan.TABLET_WAIT))
        self.assertIn("LANDED A.1 as %s" % code_commit, text)
        self.assertIn("Verdict by kind: 2 PASS", text)
        folder_row = plan.find(plan.read_steps(folder), "A.1")
        self.assertEqual((folder_row["state"], folder_row["commit"]), ("landed", code_commit))
        self.assertEqual(self.t.git("log", "-1", "--format=%s", cwd=folder), "Plan: step A.1 landed")
        self.assertEqual(self.t.git("rev-parse", "--short=9", "HEAD~1", cwd=folder), code_commit)
        self.assertEqual(self.t.git("show", "HEAD", "--name-only", "--format=", cwd=folder), "docs/plan/native/steps.tsv")
        tree_row = plan.find(plan.read_steps(self.t.tree), "A.1")
        self.assertEqual((tree_row["state"], tree_row["commit"]), ("landed", code_commit))
        landing = lv.read_landings(self.t.live)[0]
        self.assertEqual((landing["step"], landing["commit"], landing["agent"]), ("A.1", code_commit, "1"))

    def test_the_landing_message_has_no_attribution(self):
        self.ready()
        self.land(expect=0)
        message = self.t.git("log", "-1", "--format=%B", cwd=self.t.folder("A.1"))
        for word in ("Co-Authored-By", "Claude", "Session", "Agent-Lane"):
            self.assertNotIn(word, message)

    def test_refuses_a_row_the_card_closes_that_is_not_fixed(self):
        self.ready(p001=("open", ""))
        code, text = self.land()
        self.assertEqual(code, 1)
        self.assertIn("P-001: the card closes it whole, and its state is 'open' (want fixed)", text)
        self.assertEqual(self.calls, [], "the gate was not asked")
        self.assertEqual(plan.find(plan.read_steps(self.t.tree), "A.1")["state"], "started")

    def test_refuses_a_fixed_row_whose_proof_names_no_test_search_or_measure(self):
        self.ready(p001=("fixed", "it works now"))
        code, text = self.land()
        self.assertEqual(code, 1)
        self.assertIn("P-001: fixed, but its proof names no test, search or measure", text)

    def test_accepts_each_kind_of_proof(self):
        for proof in ("tests/a/b_test.py", "ctest galaxy_x_tests", "a whole-tree search for the old name found none",
                      "grep -r old src: 0 hits", "measure: 3.2 ms before, 1.1 ms after", "replays 102/102"):
            self.assertEqual(plan.ledger_problems(plan.parse_rows("P-001 whole"), plan.read_ledger(
                ledger_text([ledger_row("P-001", "fixed", proof)]))), [], proof)

    def test_refuses_an_advanced_row_with_no_dated_note(self):
        self.ready(p002_note="half done")
        code, text = self.land()
        self.assertEqual(code, 1)
        self.assertIn("P-002: the card advances it, and its note has no date", text)

    def test_refuses_an_advanced_row_whose_note_is_the_one_main_already_has(self):
        self.ready(p002_note="old 11-01")
        code, text = self.land()
        self.assertEqual(code, 1)
        self.assertIn("P-002: the card advances it, and its note is the one main already has", text)

    def test_refuses_a_row_that_is_not_in_the_ledger(self):
        self.t.write_ledger([ledger_row("P-002", note="x 11-20")], tree=self.t.folder("A.1"))
        self.t.commit("A.1", "x", {"runtime/src/render/a.cpp": "z\n"})
        code, text = self.land()
        self.assertEqual(code, 1)
        self.assertIn("P-001: the card says it is closed here, and the ledger has no such row", text)

    def test_refuses_a_file_that_does_not_exist_named_in_a_changed_document(self):
        self.ready()
        self.t.commit("A.1", "docs", {"docs/note.md": "See `runtime/src/render/a.cpp:3` and `runtime/src/render/made_up.cpp`.\n"})
        code, text = self.land()
        self.assertEqual(code, 1)
        self.assertIn("docs/note.md:1: runtime/src/render/made_up.cpp: no such file in the tree", text)
        self.assertNotIn("a.cpp:3:", text)
        self.assertEqual(self.calls, [])

    def test_refuses_a_line_past_the_end_in_a_changed_document(self):
        self.ready()
        self.t.commit("A.1", "docs", {"docs/note.md": "`runtime/src/render/a.cpp:40`\n"})
        code, text = self.land()
        self.assertEqual(code, 1)
        self.assertIn("line 40 is past the end (the file has 4 lines)", text)

    def test_a_document_it_did_not_change_is_not_read(self):
        (self.t.folder("A.1") / "docs").mkdir(exist_ok=True)
        self.ready()
        self.land(expect=0)           # the plan's own cards name `rows/branch`, a path with no extension: never a reference

    def test_refuses_a_file_that_does_not_exist_named_in_a_commit_message(self):
        self.ready()
        self.t.commit("A.1", "Fix the thing in runtime/src/render/nowhere.cpp:12", {"x.txt": "x"})
        code, text = self.land()
        self.assertEqual(code, 1)
        self.assertIn("the commit messages:1: runtime/src/render/nowhere.cpp:12: no such file in the tree", text)
        self.assertEqual(self.calls, [])

    def test_the_allow_list_of_the_branch_lets_a_known_outside_reference_through(self):
        self.ready()
        self.t.commit("A.1", "allow", {"tools/verify/doc_refs_allow.txt": "docs/note.md :: x/y/z.cpp\n",
                                       "docs/note.md": "`x/y/z.cpp`\n"})
        self.land(expect=0)

    def test_refuses_uncommitted_changes_and_a_missing_folder_and_a_step_not_started(self):
        self.ready()
        (self.t.folder("A.1") / "loose.txt").write_text("x", encoding="utf-8")
        code, text = self.land()
        self.assertEqual(code, 1)
        self.assertIn("uncommitted changes", text)
        self.assertEqual(self.t.run(["land", "A.2"])[0], 1)
        shutil.rmtree(self.t.folder("A.1"))
        self.assertIn("is missing", self.land()[1])

    def test_a_card_whose_rows_cannot_be_read_is_the_plans_fault_and_says_so(self):
        n = plan.native(self.t.folder("A.1")) / "03-X.md"
        n.write_text(n.read_text().replace("- **Rows.** P-001: whole. P-002 (part): the half.", "- **Rows.** see the overview"))
        self.t.git("commit", "-am", "break the card", cwd=self.t.folder("A.1"))
        code, text = self.land()
        self.assertEqual(code, 1)
        self.assertIn("could not be read", text)
        self.assertIn("plan.py tool", text)

    def test_a_failed_landing_is_not_landed_takes_its_state_commit_back_and_is_counted(self):
        code_commit = self.ready()
        self.verdict = plan.Verdict(False, [("repo/code", "PASS", "ok"), ("tour/d3d12", "FAIL", "replay step 0 differs")],
                                    [("tour/d3d12", "FAIL", "replay step 0 differs")])
        code, text = self.land()
        self.assertEqual(code, 1)
        self.assertIn("NOT LANDED: A.1. Verdict by kind: 1 FAIL, 1 PASS", text)
        self.assertIn("FAIL  tour/d3d12: replay step 0 differs", text)
        self.assertIn("1 time in a row", text)
        folder = self.t.folder("A.1")
        self.assertEqual(self.t.git("rev-parse", "--short=9", "HEAD", cwd=folder), code_commit, "the state commit is gone")
        self.assertEqual(plan.find(plan.read_base(folder), "A.1")["state"], "open", "the branch is as it was cut from main")
        self.assertEqual(plan.find(plan.read_steps(self.t.tree), "A.1")["state"], "started")
        self.assertEqual(lv.read_landings(self.t.live), [])
        self.assertEqual(failures.Store(self.t.store).count("native/A.1"), 1)

    def test_the_third_same_failure_exits_with_code_three_and_the_stop_sentence(self):
        self.ready()
        self.verdict = plan.Verdict(False, [("tour/d3d12", "FAIL", "replay differs")], [("tour/d3d12", "FAIL", "replay differs")])
        self.assertEqual(self.land()[0], 1)
        self.assertEqual(self.land()[0], 1)
        code, text = self.land()
        self.assertEqual(code, 3)
        self.assertIn("Same failure three times. Stop retrying. Run plan.py blocked, then plan.py next.", text)
        self.assertEqual(len(self.calls), 3)

    def test_a_different_failure_in_between_starts_the_count_again(self):
        self.ready()
        same = plan.Verdict(False, [], [("tour/d3d12", "FAIL", "replay differs")])
        other = plan.Verdict(False, [], [("unit", "FAIL", "a test failed")])
        for verdict, want in ((same, 1), (same, 1), (other, 1), (same, 1), (same, 1)):
            self.verdict = verdict
            self.assertEqual(self.land()[0], want)

    def test_a_tool_result_never_counts_and_prints_what_to_do(self):
        self.ready()
        self.verdict = plan.Verdict(False, [("harness/self", "TOOL", "result.json is missing")],
                                    [("harness/self", "TOOL", "result.json is missing")])
        for _ in range(3):
            code, text = self.land()
            self.assertEqual(code, 4)
            self.assertIn("this is a tool fault: report it in BLOCKED.md under 'tools' and take the next step; do not debug the tool", text)
            self.assertNotIn("Same failure three times", text)
        self.assertEqual(failures.Store(self.t.store).count("native/A.1"), 0)

    def test_an_env_result_never_counts(self):
        self.ready()
        self.verdict = plan.Verdict(False, [("start/d3d11", "ENV", "D: is under its floor")], [("start/d3d11", "ENV", "D: is under its floor")])
        for _ in range(3):
            self.assertEqual(self.land()[0], 4)
        self.assertEqual(failures.Store(self.t.store).count("native/A.1"), 0)

    def test_a_pass_clears_the_count(self):
        self.ready()
        self.verdict = plan.Verdict(False, [], [("tour/d3d12", "FAIL", "replay differs")])
        self.land()
        self.land()
        self.verdict = plan.Verdict(True, [("tour/d3d12", "PASS", "ok")])
        self.land(expect=0)
        self.assertEqual(failures.Store(self.t.store).count("native/A.1"), 0)

    def test_the_gate_is_given_the_branch_the_tablet_and_the_work_folder(self):
        self.ready()
        self.t.run(["--tablet", "SERIAL1", "--tablet-wait", "7", "land", "A.1"], expect=0) if False else None
        out = io.StringIO()
        with contextlib.redirect_stdout(out):
            plan.main(["--tree", str(self.t.tree), "--work-root", str(self.t.work), "--failures", str(self.t.store),
                       "--tablet", "SERIAL1", "--tablet-wait", "7", "land", "A.1"])
        self.assertEqual(self.calls[0][2:], ("SERIAL1", 7.0))


# ---- the landing step: the gate's own program ----------------------------------------------------------------

FAKE_GATE = textwrap.dedent('''\
    import pathlib, sys
    args = sys.argv[1:]
    log = pathlib.Path(__file__).with_name("calls.txt")
    log.write_text((log.read_text() if log.exists() else "") + " ".join(args) + "\\n")
    mode = pathlib.Path(__file__).with_name("mode.txt").read_text().strip()
    if args[0] == "--quick" and mode == "quick-fails":
        print("repo/code                          pass        1.0 s  ok")
        print("tour/d3d12                         FAIL       41.5 s  replay step 0: 0873f048 against 521573f1")
        print("gate quick abc: FAIL")
        sys.exit(1)
    if mode == "refused" and args[0] == "--main":
        print("gate.py: main (abc) is not an ancestor of this commit: rebase on it first", file=sys.stderr)
        sys.exit(2)
    if mode == "crash":
        sys.exit(7)
    print("repo/code                          pass        1.0 s  ok")
    print("tablet/short                       owed        0.0 s  the tablet is away")
    print("unit                               known       2.0 s  1 known failure")
    print("tour/d3d12                         pass       41.5 s  12 stops")
    sys.exit(0)
    ''')


class GateStepTest(unittest.TestCase):
    def setUp(self):
        self.t = Tree().__enter__()
        self.addCleanup(self.t.__exit__)
        self.t.run(["start", "A.1"], expect=0)
        self.folder = self.t.folder("A.1")
        gate = self.folder / "tools" / "gate"
        gate.mkdir(parents=True)
        (gate / "gate.py").write_text(FAKE_GATE, encoding="utf-8")
        self.mode = gate / "mode.txt"
        self.mode.write_text("ok", encoding="utf-8")
        self.calls = gate / "calls.txt"

    def land(self):
        with contextlib.redirect_stdout(io.StringIO()):
            return plan.land_with_gate(self.folder, "native/A.1", "SER", 30)

    def test_the_gate_rows_are_read_by_kind(self):
        rows = plan.read_gate_output(["repo/code   pass   1.0 s  ok\n", "x  FAIL  2.5 s  boom: first line\n", "t  owed  0.0 s  away\n",
                                      "not a row\n", "a/b c  TOOL  0.1 s  d\n", "p  NEW  3.0 s  new picture\n", "f  FLAKE  3.0 s  once"])
        self.assertEqual([(c, k) for c, k, _, _ in rows], [("repo/code", "PASS"), ("x", "FAIL"), ("t", "ENV"), ("p", "NEW"), ("f", "FLAKE")])
        self.assertEqual(rows[1][2], "boom: first line")

    def test_quick_then_main_with_the_move_and_the_tablet(self):
        self.t.git("commit", "--allow-empty", "-m", "c", cwd=self.folder)
        v = self.land()
        self.assertTrue(v.passed)
        self.assertEqual(self.calls.read_text().splitlines(), ["--quick", "--main --move --tablet SER --tablet-wait 30"])
        self.assertEqual(v.counts(), {"PASS": 6, "ENV": 2})
        self.assertEqual(v.failures, [], "an owed check is not a failure")
        self.assertEqual(self.t.git("rev-parse", "native/A.1", cwd=self.t.origin), self.t.git("rev-parse", "HEAD", cwd=self.folder),
                         "the branch is on origin for the gate")

    def test_the_main_gate_is_not_started_when_quick_fails(self):
        self.mode.write_text("quick-fails", encoding="utf-8")
        v = self.land()
        self.assertFalse(v.passed)
        self.assertEqual(self.calls.read_text().splitlines(), ["--quick"])
        self.assertEqual(v.failures, [("tour/d3d12", "FAIL", "replay step 0: 0873f048 against 521573f1")])

    def test_a_refusal_of_the_gate_is_a_failure_with_its_words(self):
        self.mode.write_text("refused", encoding="utf-8")
        v = self.land()
        self.assertEqual(v.failures, [("gate.py", "FAIL", "main (abc) is not an ancestor of this commit: rebase on it first")])

    def test_a_gate_that_dies_without_a_failing_check_is_a_tool_fault(self):
        self.mode.write_text("crash", encoding="utf-8")
        v = self.land()
        self.assertEqual(v.failures, [("gate.py", "TOOL", "exited 7 with no failing check in its output")])
        outcome = failures.Store(self.t.store).record("native/A.1", v.failures)
        self.assertEqual((outcome.counted, outcome.exit_code), (False, 4))

    def test_a_branch_origin_refuses_is_a_failure_not_a_crash(self):
        shutil.rmtree(self.t.origin, ignore_errors=True)
        v = self.land()
        self.assertEqual((v.passed, v.failures[0][:2]), (False, ("git/push", "FAIL")))


# ---- blocked, tool, status ----------------------------------------------------------------------------------

class BlockedTest(unittest.TestCase):
    def test_blocked_sets_the_state_and_writes_the_reason_and_the_failures_first_line(self):
        with Tree() as t:
            failures.Store(t.store).record("native/A.1", [("tour/d3d12", "FAIL", "replay differs at step 0")])
            code, text = t.run(["blocked", "A.1", "the replay differs\nand I cannot say why"], expect=0)
            self.assertEqual(plan.find(plan.read_steps(t.tree), "A.1")["state"], "blocked")
            blocked = (plan.native(t.tree) / "BLOCKED.md").read_text(encoding="utf-8")
            self.assertRegex(blocked, r"- \d{4}-\d\d-\d\d A\.1: the replay differs and I cannot say why \| first failure line: "
                                      r"tour/d3d12 \| replay differs at step 0")
            self.assertEqual(next_id(plan.read_steps(t.tree)), "C.1", "A.2 is in the blocked package, B.1 needs it")
            self.assertEqual(plan.blocked_reasons(t.tree)["A.1"][:20], "the replay differs a")

    def test_blocked_with_no_failure_recorded(self):
        with Tree() as t:
            t.run(["blocked", "A.1", "why"], expect=0)
            self.assertIn("first failure line: (no failure recorded)", (plan.native(t.tree) / "BLOCKED.md").read_text(encoding="utf-8"))

    def test_blocked_refuses_an_unknown_and_a_landed_step(self):
        with Tree() as t:
            self.assertEqual(t.run(["blocked", "Q.1", "x"])[0], 1)
            plan.set_state(t.tree, "A.1", "landed")
            self.assertEqual(t.run(["blocked", "A.1", "x"])[0], 1)

    def test_two_blocked_steps_and_a_tool_fault_go_in_their_own_sections(self):
        with Tree() as t:
            t.run(["blocked", "A.1", "one"], expect=0)
            t.run(["tool", "harness/self wrote no result.json"], expect=0)
            t.run(["blocked", "C.1", "two"], expect=0)
            t.run(["tool", "second fault"], expect=0)
            text = (plan.native(t.tree) / "BLOCKED.md").read_text(encoding="utf-8")
            tools_at, steps_at = text.index("## tools"), text.index("## steps")
            self.assertLess(tools_at, text.index("harness/self wrote no result.json"))
            self.assertLess(text.index("second fault"), steps_at)
            self.assertGreater(text.index("A.1: one"), steps_at)
            self.assertGreater(text.index("C.1: two"), text.index("A.1: one"))
            self.assertEqual(plan.find(plan.read_steps(t.tree), "A.1")["state"], "blocked")
            self.assertEqual(len(plan.tool_notes(t.tree)), 2)

    def test_a_tool_fault_does_not_block_the_step(self):
        with Tree() as t:
            t.run(["tool", "x"], expect=0)
            self.assertEqual({s["state"] for s in plan.read_steps(t.tree)}, {"open"})


class StatusTest(unittest.TestCase):
    def test_status_writes_the_file_and_prints_it(self):
        with Tree() as t:
            t.run(["start", "A.1"], expect=0)
            plan.set_state(t.tree, "C.1", "blocked")
            plan.append_under(plan.native(t.tree) / "BLOCKED.md", "steps", "- 2026-11-20 C.1: the card is wrong | first failure line: x")
            plan.set_state(t.tree, "A.2", "landed", "abc123def")
            lv.add_landing(t.live, "A.2", "abc123def", 600)
            code, text = t.run(["status"], expect=0)
            written = (plan.native(t.tree) / "STATUS.md").read_text(encoding="utf-8")
            self.assertEqual(written, text)
            self.assertIn("Steps landed: 1 of 5.", text)
            self.assertIn("## Started (1, at most 3 for one agent)", text)
            self.assertIn("- A.1 First step: agent 1, branch native/A.1 in %s" % t.folder("A.1").as_posix(), text)
            self.assertIn("- C.1 Free step: the card is wrong | first failure line: x", text)
            self.assertIn("- A.2: commit abc123def,", text)
            self.assertIn(", 600 s", text)
            self.assertIn("## Next", text)
            self.assertIn("nothing can start now; steps in progress: A.1", text)

    def test_status_names_the_next_step_and_the_blocked_one_it_skips(self):
        with Tree() as t:
            plan.set_state(t.tree, "A.1", "blocked")
            text = t.run(["status"], expect=0)[1]
            self.assertIn("- C.1 Free step (plan.py next prints its card)", text)
            self.assertIn("- skipping blocked: A.1", text)

    def test_only_the_last_five_landings_are_listed_newest_first(self):
        with Tree() as t:
            for i in range(7):
                lv.add_landing(t.live, "S%d" % i, "c%d" % i, i)
            text = t.run(["status"], expect=0)[1]
            listed = [l for l in text.splitlines() if l.startswith("- S")]
            self.assertEqual([l.split(":")[0] for l in listed], ["- S6", "- S5", "- S4", "- S3", "- S2"])

    def test_status_of_a_fresh_plan(self):
        with Tree() as t:
            text = t.run(["status"], expect=0)[1]
            self.assertIn("Steps landed: 0 of 5.", text)
            self.assertIn("- none yet", text)
            self.assertIn("- A.1 First step", text)

    def test_status_of_the_real_plan_says_138(self):
        code, text = run(["status"], TREE, TREE / "none", TREE / "none.tsv") if False else (0, plan.status_text(TREE))
        self.assertIn("Steps landed: 0 of 138.", text)
        self.assertIn("- S0.1.1 Land `rows/p022c` and `rows/p022d` as one batch", text)


class StateFilesTest(unittest.TestCase):
    def test_started_and_blocked_are_in_the_live_folder_not_in_steps_tsv_or_the_work_tree(self):
        with Tree() as t:
            before = (plan.native(t.tree) / "steps.tsv").read_bytes()
            plan.set_state(t.tree, "A.1", "started", agent=2)
            plan.set_state(t.tree, "C.1", "blocked", reason="the card is wrong")
            self.assertEqual((plan.native(t.tree) / "steps.tsv").read_bytes(), before)
            self.assertEqual({s["step"]: s["state"] for s in plan.read_steps(t.tree)},
                             {"A.1": "started", "A.2": "open", "B.1": "open", "C.1": "blocked", "D.1": "open"})
            state = lv.read_state(t.live)
            self.assertEqual((state["A.1"]["state"], state["A.1"]["agent"]), ("started", "2"))
            self.assertEqual((state["C.1"]["agent"], state["C.1"]["reason"]), ("1", "the card is wrong"))
            self.assertEqual(t.git("status", "--short"), "", "the work tree has nothing new in it")
            self.assertTrue((t.live / "state.tsv").is_file())
            by = {s["step"]: s for s in plan.read_steps(t.tree)}
            self.assertEqual((by["A.1"]["agent"], by["C.1"]["reason"], by["A.2"]["agent"]), ("2", "the card is wrong", ""))
            plan.set_state(t.tree, "A.1", "open")
            self.assertEqual(sorted(lv.read_state(t.live)), ["C.1"])

    def test_landed_goes_into_steps_tsv_with_the_commit_and_leaves_the_live_state(self):
        with Tree() as t:
            plan.set_state(t.tree, "A.1", "started")
            plan.set_state(t.tree, "A.1", "landed", "abcdef123")
            row = plan.find(plan.read_base(t.tree), "A.1")
            self.assertEqual((row["state"], row["commit"]), ("landed", "abcdef123"))
            self.assertEqual(lv.read_state(t.live), {})
            self.assertIn("A.1\tA\tFirst step\t\t03-X.md\tlanded\tabcdef123\n", (plan.native(t.tree) / "steps.tsv").read_text())

    def test_the_work_folders_state_commit_leaves_the_live_folder_alone(self):
        with Tree() as t:
            plan.set_state(t.tree, "A.1", "started")
            plan.set_state(t.tree, "A.1", "landed", "abcdef123", track=False)
            self.assertEqual(list(lv.read_state(t.live)), ["A.1"])

    def test_a_live_state_never_hides_a_landed_step(self):
        with Tree() as t:
            plan.set_state(t.tree, "A.1", "started")
            path = plan.native(t.tree) / "steps.tsv"
            path.write_text(path.read_text().replace("\topen\t\n", "\tlanded\tfeed\n", 1))     # main landed it meanwhile
            self.assertEqual(plan.find(plan.read_steps(t.tree), "A.1")["state"], "landed")

    def test_an_unknown_step_is_refused(self):
        with Tree() as t:
            with self.assertRaises(KeyError):
                plan.set_state(t.tree, "Q.9", "started")

    def test_two_work_folders_see_the_same_state(self):
        with Tree() as t:
            second = t.dir / "tree2"
            shutil.copytree(t.tree, second)
            plan.set_state(t.tree, "A.1", "started", agent=1)
            self.assertEqual(plan.find(plan.read_steps(second), "A.1")["state"], "started")

    def test_the_repository_ignores_the_status_page_and_nothing_else_of_it(self):
        text = (TREE / ".gitignore").read_text(encoding="utf-8")
        self.assertIn("/docs/plan/native/STATUS.md", text)
        for name in ("state.tsv", "landings.tsv", "BLOCKED.md"):
            self.assertNotIn(name, text)


# ---- the checks a step's paths will run ---------------------------------------------------------------------

class ScopeTest(unittest.TestCase):
    """The checks a step's paths run: tools/gate/scope.tsv (scope_test.py tests the table itself); here, how the plan tool
    prints them."""
    table = plan.scope.read_table()

    def checks(self, *paths):
        return plan.checks_for(list(paths), self.table)

    def test_documents_only(self):
        out = self.checks("docs/plan/ledger.tsv", "README.md", "docs/x/y.md")
        self.assertEqual(out[0][:5], "repo:")
        self.assertEqual(out[-1], "checks: repo/*")

    def test_the_union_of_several_rows(self):
        out = self.checks("native/petari/a.cpp", "runtime/src/render/a.cpp", "crates/nebula-import/src/x.rs")
        self.assertEqual([l.split(":")[0] for l in out[:-1]], ["repo", "build", "unit", "start", "engine1", "engine3", "tour1", "tour3", "import"])
        self.assertIn("tour/vulkan", out[-1])

    def test_a_path_no_row_names_runs_everything(self):
        out = self.checks("installer/build.ps1")
        self.assertTrue(out[0].startswith("full: everything the main gate runs today"))
        self.assertIn("installer/build.ps1", out[0])
        self.assertEqual(len(out), 1)

    def test_no_path_runs_everything(self):
        self.assertIn("the card names no path", self.checks()[0])

    def test_the_gate_scope_file_is_read_from_the_plan_tree_and_this_trees_when_it_is_not_there(self):
        with tempfile.TemporaryDirectory() as d:
            self.assertEqual(len(plan.read_scope(d).rows), len(self.table.rows))
            (pathlib.Path(d) / "tools" / "gate").mkdir(parents=True)
            scope_file = pathlib.Path(d) / "tools" / "gate" / "scope.tsv"
            scope_file.write_text("mods = off" + chr(10) + "pattern" + chr(9) + "groups" + chr(10) + "x/**" + chr(9) + "tools" + chr(10), encoding="utf-8")
            self.assertEqual([r[:2] for r in plan.read_scope(d).rows], [("x/**", ("tools",))])
            scope_file.write_text("nonsense" + chr(10), encoding="utf-8")
            with self.assertRaises(plan.scope.ScopeError):
                plan.read_scope(d)

    def test_a_cards_paths_are_the_ones_in_the_tree(self):
        with Tree() as t:
            card = plan.card_text(t.tree, plan.find(plan.read_steps(t.tree), "A.1"))
            self.assertEqual(plan.card_paths(card, t.tree), ["runtime/src/render/a.cpp", "docs/plan/ledger.tsv"])


# ---- the ledger ---------------------------------------------------------------------------------------------

class LedgerTest(unittest.TestCase):
    def test_the_ledger_is_read_with_crlf_and_a_cell_with_a_unicode_line_break(self):
        text = ledger_text([ledger_row("P-001", note="a\u2028b"), ledger_row("P-002")])
        rows = plan.read_ledger(text)
        self.assertEqual(rows["P-001"]["note"], "a\u2028b")
        self.assertEqual(list(rows), ["P-001", "P-002"])
        self.assertEqual(plan.read_ledger(text.replace("\r\n", "\n")).keys(), rows.keys())

    def test_a_ledger_with_a_wrong_row_is_refused(self):
        with self.assertRaises(ValueError):
            plan.read_ledger("id\tagent\tpackage\tstate\tproof\ttitle\tnote\r\nP-001\tx\r\n")
        with self.assertRaises(ValueError):
            plan.read_ledger("id\tstate\r\n")

    def test_the_real_ledger_reads(self):
        rows = plan.read_ledger((TREE / plan.LEDGER).read_bytes().decode("utf-8"))
        self.assertGreater(len(rows), 600)
        self.assertEqual(rows["M-001"]["state"], "fixed")

    def test_dates(self):
        for ok in ("fixed 11-17", "2026-11-20 done", "on 11-20, after"):
            self.assertTrue(plan.DATE.search(ok), ok)
        for bad in ("version 1.2", "no date", "13-45", "P-151", "store 39"):
            self.assertFalse(plan.DATE.search(bad), bad)


def main():
    result = unittest.main(argv=[sys.argv[0]], exit=False, verbosity=1).result
    print("plan_test: %s (%d tests)" % ("pass" if result.wasSuccessful() else "FAIL", result.testsRun))
    return 0 if result.wasSuccessful() else 1


if __name__ == "__main__":
    sys.exit(main())
