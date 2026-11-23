"""plan_test.py: tools/plan/plan.py, the step tool of the native plan (package T13).

Two kinds of test. The plan's own files (docs/plan/native: 138 steps, their cards) are read as they are: every step has
its card, every need names something, and ten and more real cards give the rows they must give. The commands run in a
temporary git tree made here (a bare origin, a plan tree, work folders, a small ledger with CRLF line ends) with the
landing step replaced by a fake: no game, no build, no network, not the real ledger, no gate run.

    python -X utf8 -I tools/gate/tests/plan_test.py [--work <folder>]
"""
import contextlib
import datetime
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

_real_read_base = plan.read_base


def open_steps(tree):
    """The plan's own steps as they were at the start: every step open, no commit (plan.read_base with the progress taken out). The real
    steps.tsv changes with every step that lands (T11-B: four of these tests failed once S0.1.1 and S0.1.2 had landed)."""
    return [dict(s, state="open", commit="") for s in _real_read_base(tree)]


def at_the_start():
    """Patches that make the tool read the real plan as it was at the start: the steps open, and no fetch of what origin/main has landed."""
    return (mock.patch.object(plan, "read_base", open_steps), mock.patch.object(plan, "fetch_main_landed", lambda a: None))


class RealPlanTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.steps = open_steps(TREE)

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
        with at_the_start()[0], at_the_start()[1]:
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
        "S0.2.6": (["N-1068", "P-242", "P-340"], ["P-293", "P-295", "P-294"]),   # P-294 "(closed at S0.2.5)"; P-340 "whole" (11-22)
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
            plan.append_under(plan.blocked_path(t.live), "steps", "- 2026-11-20 A.1: the tool broke | first failure line: x")
            code, text = t.run(["next"])
            self.assertEqual(code, 0)
            self.assertIn("BLOCKED: A.1 First step: the tool broke | first failure line: x", text)
            self.assertIn("NEXT: C.1", text)
            self.assertIn("the first later step that does not need its package is C.1", text)

    def test_next_with_nothing_to_do_says_what_is_in_progress(self):
        with Tree() as t:
            for sid in ("A.1", "C.1"):
                plan.set_state(t.tree, sid, "started", agent=2)
            plan.set_state(t.tree, "A.2", "started", agent=2)
            code, text = t.run(["next"])
            self.assertEqual(code, 1)
            self.assertIn("Nothing can start now", text)
            self.assertIn("A.1 started", text)

    def test_next_with_something_in_work_shows_it_again_and_gives_nothing_new(self):
        with Tree() as t:
            t.run(["start", "A.1"], expect=0)
            code, text = t.run(["next"])
            self.assertEqual(code, 0)
            self.assertEqual(text.splitlines()[0], "IN WORK: A.1  First step")
            self.assertIn("This is in work: go on with it, then land it or block it. Nothing new is given while it is.", text)
            self.assertIn("### A.1 First step", text, "its card again")
            self.assertIn("| P-001 | one |", text, "its package's rows again")
            self.assertNotIn("NEXT:", text)
            self.assertNotIn("C.1", text, "no other step is offered")
            self.assertIn("land:  python -X utf8 -I %s land A.1" % (t.tree / "tools" / "plan" / "plan.py").as_posix(), text)
            self.assertIn('block: python -X utf8 -I %s blocked A.1 "<why>"' % (t.tree / "tools" / "plan" / "plan.py").as_posix(), text)
            self.assertNotIn("start: python", text)
            self.assertEqual(plan.find(plan.read_steps(t.tree), "C.1")["state"], "open", "next changes nothing")

    def test_next_after_everything_landed(self):
        with Tree() as t:
            t.landed(*[s["step"] for s in plan.read_steps(t.tree)])
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
            self.assertEqual(folder.name, "Nebula-native")
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

    def test_the_folder_is_made_once_and_used_again(self):
        with Tree() as t:
            t.run(["start", "A.1"], expect=0)
            folder = t.folder()
            git_file = (folder / ".git").read_text(encoding="utf-8")
            # The build beside the sources is kept: a folder the repository ignores keeps what it holds.
            (t.tree / ".git" / "info" / "exclude").write_text("build-here/\n", encoding="utf-8")
            (folder / "build-here").mkdir()
            (folder / "build-here" / "game.obj").write_text("compiled", encoding="utf-8")
            t.commit("A.1")
            t.run(["blocked", "A.1", "waiting"], expect=0)
            code, text = t.run(["start", "C.1"], expect=0)
            self.assertIn("started C.1: branch native/C.1 in %s" % folder.as_posix(), text)
            self.assertEqual((folder / ".git").read_text(encoding="utf-8"), git_file, "the same worktree, not a new one")
            self.assertEqual((folder / "build-here" / "game.obj").read_text(encoding="utf-8"), "compiled", "the build is kept")
            self.assertEqual(t.git("rev-parse", "--abbrev-ref", "HEAD", cwd=folder), "native/C.1")
            self.assertEqual(t.git("rev-parse", "HEAD", cwd=folder), t.git("rev-parse", "origin/main"), "cut from origin/main again")
            listed = [l for l in t.git("worktree", "list", "--porcelain").splitlines() if l.startswith("worktree ")]
            self.assertEqual(len(listed), 2, "the plan tree and the one work folder: %s" % listed)
            self.assertEqual(sorted(p.name for p in t.work.iterdir()), ["Nebula-native"])

    def test_start_refuses_a_folder_with_uncommitted_changes_and_says_what_to_do(self):
        with Tree() as t:
            t.run(["start", "A.1"], expect=0)
            plan.set_state(t.tree, "A.1", "landed")                        # nothing is in work now
            (t.folder() / "loose.txt").write_text("x", encoding="utf-8")
            code, text = t.run(["start", "C.1"])
            self.assertEqual(code, 1)
            self.assertIn("the work folder %s has uncommitted changes. Commit them (or throw away what you do not want), then run "
                          "start again." % t.folder().as_posix(), text)
            self.assertEqual(len(text.strip().splitlines()), 1, "one line")
            self.assertEqual(plan.find(plan.read_steps(t.tree), "C.1")["state"], "open")
            self.assertEqual(t.git("rev-parse", "--abbrev-ref", "HEAD", cwd=t.folder()), "native/A.1")
            (t.folder() / "loose.txt").unlink()
            (t.folder() / "runtime" / "src" / "render" / "a.cpp").write_text("changed\n", encoding="utf-8")   # a tracked file too
            self.assertEqual(t.run(["start", "C.1"])[0], 1)
            t.git("checkout", "--", "runtime", cwd=t.folder())
            t.run(["start", "C.1"], expect=0)

    def test_a_tool_run_from_the_agents_own_folder_writes_blocked_md_to_the_live_folder_and_leaves_the_folder_clean(self):
        with Tree() as t:
            t.run(["start", "A.1"], expect=0)
            folder = t.folder()
            t.commit("A.1")
            code, text = run(["blocked", "A.1", "waiting"], folder, t.work, t.store, 0, t.live)       # the tool of the folder itself
            blocked = plan.blocked_path(t.live)
            self.assertEqual(blocked, t.live / "BLOCKED.md")
            self.assertIn("A.1: waiting", blocked.read_text(encoding="utf-8"))
            self.assertFalse((plan.native(folder) / "BLOCKED.md").exists(), "no file is written into the work folder (T11-B)")
            self.assertEqual(t.git("status", "--porcelain", cwd=folder), "", "the folder stays clean")
            # An older BLOCKED.md of the plan tree (where an earlier version wrote it) is read as well.
            (plan.native(folder) / "BLOCKED.md").write_text("## steps" + chr(10) + "- 2026-11-20 C.1: from the old file" + chr(10), encoding="utf-8")
            self.assertEqual(plan.blocked_reasons(folder, t.live).get("C.1"), "from the old file")
            self.assertTrue(plan.blocked_reasons(folder, t.live).get("A.1", "").startswith("waiting"))
            (plan.native(folder) / "BLOCKED.md").unlink()
            code, text = run(["start", "C.1"], folder, t.work, t.store, 0, t.live)
            self.assertIn("started C.1", text)
            self.assertIn("A.1: waiting", blocked.read_text(encoding="utf-8"), "it stays where it is")
            (folder / "other.txt").write_text("x", encoding="utf-8")                                  # any other file still stops it
            plan.set_state(t.tree, "C.1", "landed")
            self.assertEqual(run(["start", "A.1"], folder, t.work, t.store, None, t.live)[0], 1)

    def test_start_refuses_while_something_is_in_work(self):
        with Tree() as t:
            t.run(["start", "A.1"], expect=0)
            code, text = t.run(["start", "C.1"])
            self.assertEqual(code, 1)
            self.assertEqual(text.strip(), 'plan.py: you have A.1 in work. Land it (plan.py land A.1) or block it '
                                           '(plan.py blocked A.1 "<why>") first.')
            self.assertEqual(plan.find(plan.read_steps(t.tree), "C.1")["state"], "open")
            self.assertEqual(t.git("rev-parse", "--abbrev-ref", "HEAD", cwd=t.folder()), "native/A.1", "the folder was not touched")
            plan.set_state(t.tree, "A.1", "landed")                        # landed: the folder is free
            t.run(["start", "C.1"], expect=0)

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

class LandBase(unittest.TestCase):
    def setUp(self):
        self.t = Tree().__enter__()
        self.addCleanup(self.t.__exit__)
        self.t.run(["start", "A.1"], expect=0)
        self.calls = []
        self.verdict = plan.Verdict(True, [("repo/code", "PASS", "ok"), ("tour/d3d12", "PASS", "ok")])
        patcher = mock.patch.object(plan, "LAND", self.fake_land)
        patcher.start()
        self.addCleanup(patcher.stop)

    def fake_land(self, folder, branch, tablet, wait, *rest):
        self.calls.append((pathlib.Path(folder), branch, tablet, wait))
        self.options = list(rest[0]) if rest else []
        return self.verdict

    def ready(self, p001=("fixed", "tests/render/a_test 1-9 fails before"), p002_note="advanced 11-20: half done"):
        """The ledger the card wants, committed on the branch."""
        self.t.write_ledger([ledger_row("P-001", p001[0], p001[1]), ledger_row("P-002", note=p002_note), ledger_row("P-003"),
                             ledger_row("P-004")], tree=self.t.folder("A.1"))
        return self.t.commit("A.1", "A.1: the code", {"runtime/src/render/a.cpp": "a\nb\nc\nd\n"})

    def land(self, expect=None):
        return self.t.run(["land", "A.1"], expect)

class LandTest(LandBase):
    def test_github_again_is_handed_to_the_gate_only_when_asked(self):
        self.ready()
        self.verdict = plan.Verdict(False, [], [("tour/d3d12", "FAIL", "replay differs")])   # a failed landing can be asked again
        self.t.run(["land", "A.1"], None)
        self.assertNotIn("--github-again", self.options, "without the switch nothing changes")
        self.t.run(["land", "A.1", "--github-again"], None)
        self.assertIn("--github-again", self.options)
        self.assertEqual(plan.gate_options(type("A", (), dict(live="L", failures="F", github_again=True))(), "1")[-1], "--github-again")

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
        # The allow-list is a place that can make a check see less: the card names the file, and the strictness log has its line.
        card = (CARDS + "\n### D.1 Last step\n\n- **Rows.** None.\n").replace(
            "`runtime/src/render/a.cpp`, ", "`runtime/src/render/a.cpp`, `tools/verify/doc_refs_allow.txt`, ")
        line = "\t".join(["2026-11-22", "tools/verify/doc_refs_allow.txt", "one outside reference", "a test", "plan_test", "A.1", "-"])
        self.t.commit("A.1", "allow", {"tools/verify/doc_refs_allow.txt": "docs/note.md :: x/y/z.cpp\n",
                                       "docs/note.md": "`x/y/z.cpp`\n", "docs/plan/native/03-X.md": card,
                                       "docs/plan/strictness-log.tsv": "\t".join(plan.strictness.LOG_COLUMNS) + "\n" + line + "\n"})
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
        self.assertIn("plan.py blocked A.1 \"tool: the card's Rows line cannot be read\", then plan.py next.", text)

    def test_a_failed_landing_is_not_landed_keeps_its_state_commit_and_is_counted(self):
        code_commit = self.ready()
        self.verdict = plan.Verdict(False, [("repo/code", "PASS", "ok"), ("tour/d3d12", "FAIL", "replay step 0 differs")],
                                    [("tour/d3d12", "FAIL", "replay step 0 differs")])
        code, text = self.land()
        self.assertEqual(code, 1)
        self.assertIn("NOT LANDED: A.1. Verdict by kind: 1 FAIL, 1 PASS", text)
        self.assertIn("FAIL  tour/d3d12: replay step 0 differs", text)
        self.assertIn("1 time in a row", text)
        folder = self.t.folder("A.1")
        self.assertEqual(self.t.git("rev-parse", "--short=9", "HEAD~1", cwd=folder), code_commit, "nothing is undone: the state commit stays")
        self.assertEqual(plan.find(plan.read_base(folder), "A.1")["state"], "landed", "the branch holds the state commit")
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
            self.assertIn("this is a tool fault, not the change's: it does not count as a failure; give the landing once more. Not "
                          "judged a second time: stop (an agent of the plan: plan.py blocked <ID> \"tool: <the fault's first line>\", "
                          "then plan.py next). Do not debug the tool.", text)
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
    assert args[0] == "land", args
    branch = args[1]
    if mode == "crash":
        sys.exit(7)
    if mode == "silent":
        sys.exit(0)
    if mode == "usage":
        print("gate.py: land takes branches to put in the queue, or --queue to run what waits: one of the two", file=sys.stderr)
        print("VERDICT not-judged")
        sys.exit(2)
    print("gate.py: run 1 on %s: repo,build (abc)" % branch)
    print("repo/code                          pass        1.0 s  ok")
    if mode == "ok":
        print("tablet/prepare                     owed        0.0 s  the tablet is away")
        print("unit                               known       2.0 s  1 known failure")
        print("tour/d3d12                         pass       41.5 s  12 stops")
        print("LAND %s landed main now has it as 0123456789" % branch)
        print("KIND env tablet/prepare - :: the tablet does not answer adb [owed]")
        print("KIND main tour/d3d12 001-a :: fails on main's build too")
        print("KIND flake tour/d3d12 002-b :: once")
        print("VERDICT pass")
        sys.exit(0)
    if mode in ("fail", "stop"):
        print("tour/d3d12                         FAIL       41.5 s  replay step 0: 0873f048 against 521573f1")
        print("LAND %s failed tour/d3d12: replay step 0: 0873f048 against 521573f1" % branch)
        print("Same failure three times. Stop retrying. Run plan.py blocked, then plan.py next.") if mode == "stop" else None
        print("KIND fail tour/d3d12 025-EggStarGalaxy-4 :: replay step 0: 0873f048 against 521573f1")
        print("KIND fail unit/game - :: known one [known (P-9)]")
        print("VERDICT fail")
        sys.exit(1 if mode == "fail" else 3)
    if mode == "tool":
        print("LAND %s waiting not judged: tool tour/d3d12: result.json is missing" % branch)
        print("KIND tool harness/self - :: result.json is missing")
        print("KIND env start/d3d11 - :: D: is under its floor")
        print("VERDICT not-judged")
        sys.exit(2)
    if mode == "conflict":
        print("LAND %s conflict does not apply on origin/main: runtime/src/a.cpp" % branch)
        print("VERDICT not-judged")
        sys.exit(2)
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
            return plan.land_with_gate(self.folder, "native/A.1", "SER", 30, ["--agent", "2"])

    def push(self):
        self.t.git("commit", "--allow-empty", "-m", "c", cwd=self.folder)

    def test_the_gate_rows_are_read_by_kind(self):
        rows = plan.read_gate_output(["repo/code   pass   1.0 s  ok\n", "x  FAIL  2.5 s  boom: first line\n", "t  owed  0.0 s  away\n",
                                      "not a row\n", "a/b c  TOOL  0.1 s  d\n", "p  NEW  3.0 s  new picture\n", "f  FLAKE  3.0 s  once"])
        self.assertEqual([(c, k) for c, k, _, _ in rows], [("repo/code", "PASS"), ("x", "FAIL"), ("t", "ENV"), ("p", "NEW"), ("f", "FLAKE")])
        self.assertEqual(rows[1][2], "boom: first line")

    def test_one_call_of_gate_land_with_the_tablet_and_the_options(self):
        self.push()
        v = self.land()
        self.assertTrue(v.passed)
        self.assertEqual(self.calls.read_text().splitlines(), ["land native/A.1 --tablet SER --tablet-wait 30 --agent 2"],
                         "one call, in place of the quick gate and the main gate")
        self.assertEqual(v.counts(), {"PASS": 3, "ENV": 1})
        self.assertEqual(v.failures, [], "an owed check, a main line and a flake are not failures")
        self.assertEqual((v.code, v.commit, v.gate_recorded), (0, "0123456789", True))
        self.assertEqual(self.t.git("rev-parse", "native/A.1", cwd=self.t.origin), self.t.git("rev-parse", "HEAD", cwd=self.folder),
                         "the branch is on origin for the gate")

    def test_a_repairs_check_goes_to_the_gate_as_also(self):
        self.push()
        with contextlib.redirect_stdout(io.StringIO()):
            v = plan.land_with_gate(self.folder, "native/A.1", "SER", 30, ["--agent", "1", "--also", "tour/d3d11"])
        self.assertTrue(v.passed)
        self.assertEqual(self.calls.read_text().splitlines(),
                         ["land native/A.1 --tablet SER --tablet-wait 30 --agent 1 --also tour/d3d11"],
                         "the fake gate got the branch, the tablet and --also with the check")

    def test_a_fail_is_read_from_its_kind_lines_and_a_known_one_is_not_counted(self):
        self.mode.write_text("fail", encoding="utf-8")
        v = self.land()
        self.assertFalse(v.passed)
        self.assertEqual(v.code, 1)
        self.assertEqual(v.failures, [("tour/d3d12", "FAIL", "replay step 0: 0873f048 against 521573f1")])

    def test_exit_code_three_is_the_stop(self):
        self.mode.write_text("stop", encoding="utf-8")
        v = self.land()
        self.assertEqual((v.passed, v.code), (False, 3))

    def test_not_judged_is_kind_tool_or_env_and_never_refused(self):
        self.mode.write_text("tool", encoding="utf-8")
        v = self.land()
        self.assertEqual((v.passed, v.code), (False, 2))
        self.assertEqual(v.failures, [("harness/self", "TOOL", "result.json is missing"), ("start/d3d11", "ENV", "D: is under its floor")])

    def test_a_branch_that_does_not_apply_is_not_judged(self):
        self.mode.write_text("conflict", encoding="utf-8")
        v = self.land()
        self.assertEqual(v.code, 2)
        self.assertEqual(v.failures, [("land", "NOT-JUDGED", "conflict does not apply on origin/main: runtime/src/a.cpp")])

    def test_a_usage_refusal_of_the_gate_is_not_judged_with_its_words(self):
        self.mode.write_text("usage", encoding="utf-8")
        v = self.land()
        self.assertEqual(v.code, 2)
        self.assertEqual(v.failures[0][0], "land")

    def test_a_gate_that_dies_or_says_nothing_is_a_tool_fault(self):
        for mode, word in (("crash", "exited 7"), ("silent", "exited 0")):
            self.mode.write_text(mode, encoding="utf-8")
            v = self.land()
            self.assertEqual(v.failures, [("gate.py", "TOOL", "%s with no VERDICT line" % word)], mode)
            self.assertEqual(v.code, 2)
            outcome = failures.Store(self.t.store).record("native/A.1", v.failures)
            self.assertEqual((outcome.counted, outcome.exit_code), (False, 4))

    def test_a_branch_origin_refuses_is_a_failure_not_a_crash(self):
        shutil.rmtree(self.t.origin, ignore_errors=True)
        v = self.land()
        self.assertEqual((v.passed, v.failures[0][:2]), (False, ("git/push", "FAIL")))


class GateRecordedTest(LandBase):
    """What the step tool does when the gate stored the failure and wrote the faults itself (gate.py land's exit codes)."""

    def verdict_of(self, code, failures_):
        v = plan.Verdict(code == 0, [], failures_)
        v.code, v.gate_recorded = code, True
        return v

    def test_a_failed_landing_is_not_counted_twice(self):
        self.ready()
        self.verdict = self.verdict_of(1, [("tour/d3d12", "FAIL", "replay differs")])
        self.assertEqual(self.land()[0], 1)
        self.assertEqual(failures.Store(self.t.store).count("native/A.1"), 0, "the gate stores it, not the step tool")

    def test_exit_three_is_three_and_not_judged_is_four(self):
        self.ready()
        self.verdict = self.verdict_of(3, [("tour/d3d12", "FAIL", "replay differs")])
        code, text = self.land()
        self.assertEqual(code, 3)
        self.assertIn("Same failure three times. Stop retrying.", text)
        self.verdict = self.verdict_of(2, [("harness/self", "TOOL", "result.json is missing")])
        code, text = self.land()
        self.assertEqual(code, 4)
        self.assertEqual(lv.read_faults(self.t.live), [], "the gate wrote the fault line, not this tool")
        self.verdict = self.verdict_of(2, [("land", "NOT-JUDGED", "conflict does not apply on origin/main: a.cpp")])
        self.assertEqual(self.land()[0], 4)

    def test_the_commit_main_has_is_printed(self):
        self.ready()
        v = plan.Verdict(True, [("c", "PASS", "ok")])
        v.code, v.commit, v.gate_recorded = 0, "0123456789", True
        self.verdict = v
        self.assertIn("(main has it as 0123456789)", self.land(expect=0)[1])


# ---- blocked, tool, status ----------------------------------------------------------------------------------

class BlockedTest(unittest.TestCase):
    def test_blocked_sets_the_state_and_writes_the_reason_and_the_failures_first_line(self):
        with Tree() as t:
            failures.Store(t.store).record("native/A.1", [("tour/d3d12", "FAIL", "replay differs at step 0")])
            code, text = t.run(["blocked", "A.1", "the replay differs\nand I cannot say why"], expect=0)
            self.assertEqual(plan.find(plan.read_steps(t.tree), "A.1")["state"], "blocked")
            blocked = (plan.blocked_path(t.live)).read_text(encoding="utf-8")
            self.assertRegex(blocked, r"- \d{4}-\d\d-\d\d A\.1: the replay differs and I cannot say why \| first failure line: "
                                      r"tour/d3d12 \| replay differs at step 0")
            self.assertEqual(next_id(plan.read_steps(t.tree)), "C.1", "A.2 is in the blocked package, B.1 needs it")
            self.assertEqual(plan.blocked_reasons(t.tree)["A.1"][:20], "the replay differs a")

    def test_blocked_pushes_the_branch_and_frees_the_folder(self):
        with Tree() as t:
            t.run(["start", "A.1"], expect=0)
            sha = t.commit("A.1")
            self.assertEqual(t.git("ls-remote", "origin", "refs/heads/native/A.1"), "", "not on origin before")
            code, text = t.run(["blocked", "A.1", "stuck"], expect=0)
            self.assertTrue(t.git("ls-remote", "origin", "refs/heads/native/A.1").startswith(
                t.git("rev-parse", "native/A.1")), "the branch is on origin, as the folder has it")
            self.assertTrue(t.git("rev-parse", "native/A.1").startswith(sha))
            self.assertEqual(plan.find(plan.read_steps(t.tree), "A.1")["state"], "blocked")
            t.run(["start", "C.1"], expect=0)                                   # the folder is free for the next start
            self.assertEqual(t.git("rev-parse", "--abbrev-ref", "HEAD", cwd=t.folder()), "native/C.1")

    def test_blocked_refuses_while_the_folder_has_uncommitted_changes(self):
        with Tree() as t:
            t.run(["start", "A.1"], expect=0)
            (t.folder() / "unfinished.txt").write_text("x", encoding="utf-8")
            code, text = t.run(["blocked", "A.1", "stuck"])
            self.assertEqual(code, 1)
            self.assertIn("has uncommitted changes. Commit your unfinished work first", text)
            self.assertEqual(plan.find(plan.read_steps(t.tree), "A.1")["state"], "started", "nothing was changed")
            self.assertEqual(t.git("ls-remote", "origin", "refs/heads/native/A.1"), "", "nothing was pushed")
            self.assertFalse((plan.blocked_path(t.live)).exists())
            (t.folder() / "unfinished.txt").unlink()
            t.run(["blocked", "A.1", "stuck"], expect=0)

    def test_blocked_with_no_failure_recorded(self):
        with Tree() as t:
            t.run(["blocked", "A.1", "why"], expect=0)
            self.assertIn("first failure line: (no failure recorded)", (plan.blocked_path(t.live)).read_text(encoding="utf-8"))

    def test_blocked_refuses_an_unknown_and_a_landed_step(self):
        with Tree() as t:
            self.assertEqual(t.run(["blocked", "Q.1", "x"])[0], 1)
            t.landed("A.1")
            self.assertEqual(t.run(["blocked", "A.1", "x"])[0], 1)

    def test_two_blocked_steps_and_a_tool_fault_go_in_their_own_sections(self):
        with Tree() as t:
            t.run(["blocked", "A.1", "one"], expect=0)
            t.run(["tool", "harness/self wrote no result.json"], expect=0)
            t.run(["blocked", "C.1", "two"], expect=0)
            t.run(["tool", "second fault"], expect=0)
            text = (plan.blocked_path(t.live)).read_text(encoding="utf-8")
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


# ---- repairs ------------------------------------------------------------------------------------------------

def add_repair(t, check="tour/d3d11", renderer="d3d11", kind="fail", source="nightly", note="stop 025 differs", **more):
    """`plan.py repair add` with the evidence folder given; returns the id it prints."""
    args = ["repair", "add", "--source", source, "--kind", kind, "--check", check, "--renderer", renderer,
            "--evidence", "D:/NebulaWork/build/gate-runs/20261121-nightly", "--note", note]
    for key, value in more.items():
        args += ["--" + key, value]
    code, text = t.run(args, expect=0)
    return text.strip()


class RepairAddTest(unittest.TestCase):
    def test_a_new_repair_is_one_line_in_the_live_folder_and_its_id_is_printed(self):
        with Tree() as t:
            self.assertEqual(add_repair(t, step="A.1", commit="abc123456", row="P-001"), "R-001", "only the id is printed")
            path = t.live / "repairs.tsv"
            lines = path.read_text(encoding="utf-8").splitlines()
            self.assertEqual(lines[0], "id\topened\tsource\tkind\tcheck\trenderer\tstep\tcommit\tevidence\trow\tstate\tclosed\tseen\tnote")
            self.assertEqual(len(lines), 2)
            r = lv.read_repairs(t.live)[0]
            self.assertRegex(r["opened"], r"^\d{4}-\d\d-\d\dT\d\d:\d\d:\d\d$")
            self.assertEqual((r["id"], r["source"], r["kind"], r["check"], r["renderer"], r["step"], r["commit"], r["row"], r["state"],
                              r["closed"], r["seen"], r["note"]),
                             ("R-001", "nightly", "fail", "tour/d3d11", "d3d11", "A.1", "abc123456", "P-001", "open", "-", "1",
                              "stop 025 differs"))
            self.assertEqual(r["evidence"], "D:/NebulaWork/build/gate-runs/20261121-nightly")
            self.assertEqual(add_repair(t, check="unit/game", renderer="-", kind="stress", source="stress"), "R-002")
            second = lv.read_repairs(t.live)[1]
            self.assertEqual((second["renderer"], second["step"], second["commit"], second["row"]), ("-", "-", "-", "-"),
                             "what is not given is a dash")

    def test_the_same_check_and_renderer_is_counted_and_not_written_twice(self):
        with Tree() as t:
            self.assertEqual(add_repair(t), "R-001")
            self.assertEqual(add_repair(t, note="again"), "R-001", "the existing id is printed")
            rows = lv.read_repairs(t.live)
            self.assertEqual(len(rows), 1)
            self.assertEqual(rows[0]["seen"], "2")
            self.assertIn("stop 025 differs; seen again %s" % datetime.date.today().isoformat(), rows[0]["note"])
            self.assertEqual(add_repair(t), "R-001")
            self.assertEqual(lv.read_repairs(t.live)[0]["seen"], "3")
            self.assertEqual(add_repair(t, renderer="vulkan"), "R-002", "another renderer is another repair")
            self.assertEqual(add_repair(t, check="tour/d3d12"), "R-003", "another check too")
            for state in ("started", "set-aside"):                                  # still counted in these states
                lv.set_repair(t.live, "R-001", state)
                self.assertEqual(add_repair(t), "R-001", state)
            self.assertEqual(lv.read_repairs(t.live)[0]["seen"], "5")
            lv.set_repair(t.live, "R-001", "landed", "0123456789")                  # landed: the fault is back, a new repair
            self.assertEqual(add_repair(t), "R-004")

    def test_a_wrong_command_is_exit_2_and_a_note_with_a_tab_stays_one_line(self):
        with Tree() as t:
            with contextlib.redirect_stderr(io.StringIO()):
                for args in (["repair", "add", "--source", "bad", "--kind", "fail", "--check", "c", "--evidence", "e", "--note", "n"],
                             ["repair", "add", "--source", "nightly", "--kind", "bad", "--check", "c", "--evidence", "e", "--note", "n"],
                             ["repair", "add", "--source", "nightly", "--kind", "fail", "--evidence", "e", "--note", "n"],
                             ["repair"]):
                    with self.assertRaises(SystemExit) as caught:
                        t.run(args)
                    self.assertEqual(caught.exception.code, 2, args)
            add_repair(t, note="a\tb\nc")
            self.assertEqual(len((t.live / "repairs.tsv").read_text(encoding="utf-8").splitlines()), 2)
            self.assertEqual(lv.read_repairs(t.live)[0]["note"], "a b c")

    def test_repair_list_prints_them_open_ones_first(self):
        with Tree() as t:
            self.assertIn("No repairs.", t.run(["repair", "list"], expect=0)[1])
            add_repair(t, check="a/stress", kind="stress", source="stress")
            add_repair(t, check="b/fail")
            add_repair(t, check="c/landed")
            add_repair(t, check="d/fail", step="A.1", commit="abc")
            lv.set_repair(t.live, "R-003", "landed", "0123456789")
            text = t.run(["repair", "list"], expect=0)[1]
            order = [text.index(x) for x in ("R-002", "R-004", "R-001", "R-003")]
            self.assertEqual(order, sorted(order), "open fail repairs, the other open ones, then landed: " + text)
            self.assertIn("R-004  open", text)
            self.assertIn("traced to step A.1 commit abc", text)
            self.assertIn("closed by 0123456789", text)


class RepairNextTest(unittest.TestCase):
    def test_next_gives_the_oldest_open_fail_repair_before_any_step_and_prints_its_card(self):
        with Tree() as t:
            add_repair(t, check="tour/d3d11", renderer="d3d11", step="A.1", commit="abc123456", row="P-001")
            add_repair(t, check="unit/game", renderer="-")
            code, text = t.run(["next"])
            self.assertEqual(code, 0)
            plan_py = (t.tree / "tools" / "plan" / "plan.py").as_posix()
            opened = lv.read_repairs(t.live)[0]["opened"]
            self.assertEqual(text.splitlines(), [
                "REPAIR: R-001  (nightly, kind fail)",
                "What failed: tour/d3d11 on d3d11",
                "Seen: 1 time; opened %s." % opened,
                "Evidence: D:/NebulaWork/build/gate-runs/20261121-nightly",
                "Traced to: step A.1 commit abc123456",
                "Ledger row: P-001",
                "Note: stop 025 differs",
                "",
                plan.REPAIR_RULES % "R-001",
                "",
                "=== COMMANDS ===",
                "start: python -X utf8 -I %s start R-001" % plan_py,
                "land:  python -X utf8 -I %s land R-001" % plan_py,
                'block: python -X utf8 -I %s blocked R-001 "<why>"' % plan_py,
                "after it: A.1 First step; read it with `plan.py card A.1`"])
            self.assertNotIn("NEXT:", text)

    def test_the_oldest_comes_first_and_an_untraced_repair_says_so(self):
        with Tree() as t:
            add_repair(t, check="first/fail", renderer="-")
            add_repair(t, check="second/fail", renderer="-")
            text = t.run(["next"], expect=0)[1]
            self.assertTrue(text.startswith("REPAIR: R-001"), text[:40])
            self.assertIn("What failed: first/fail\n", text)
            self.assertIn("Traced to: no landing (not traced)", text)
            self.assertIn("Ledger row: none", text)
            lv.set_repair(t.live, "R-001", "set-aside")
            self.assertTrue(t.run(["next"], expect=0)[1].startswith("REPAIR: R-002"), "the next oldest open one")

    def test_stress_and_worse_repairs_never_come_first_and_never_block(self):
        with Tree() as t:
            add_repair(t, check="soak", renderer="-", kind="stress", source="stress")
            add_repair(t, check="measure/route", renderer="-", kind="worse", source="measure")
            text = t.run(["next"], expect=0)[1]
            self.assertTrue(text.startswith("NEXT: A.1"), "the steps go on: " + text[:40])
            t.run(["start", "A.1"], expect=0)                               # and a step can be started with them open
            self.assertTrue(t.run(["next"], expect=0)[1].startswith("IN WORK: A.1"))
            plan.set_state(t.tree, "A.1", "landed")
            add_repair(t, check="tour/d3d12", renderer="d3d12")
            self.assertTrue(t.run(["next"], expect=0)[1].startswith("REPAIR: R-003"), "a fail repair, added later, comes first")

    def test_agent_2_is_not_given_a_repair(self):
        with Tree() as t:
            add_repair(t)
            self.assertNotIn("REPAIR:", t.run(["next", "--agent", "2"])[1])
            code, text = t.run(["start", "R-001", "--agent", "2"])
            self.assertEqual((code, text.strip()), (1, "plan.py: repairs are agent 1's work"))
            self.assertEqual(lv.read_repairs(t.live)[0]["state"], "open")


class RepairWorkTest(unittest.TestCase):
    def setUp(self):
        self.t = Tree().__enter__()
        self.addCleanup(self.t.__exit__)
        self.gate_calls = []
        self.verdict = plan.Verdict(True, [("repo/code", "PASS", "ok"), ("tour/d3d11", "PASS", "ok")])
        self.verdict.commit = "0123456789"
        patcher = mock.patch.object(plan, "LAND", self.fake_land)
        patcher.start()
        self.addCleanup(patcher.stop)
        self.assertEqual(add_repair(self.t), "R-001")

    def fake_land(self, folder, branch, tablet, wait, *rest):
        self.gate_calls.append({"folder": pathlib.Path(folder), "branch": branch, "options": list(rest[0]) if rest else []})
        return self.verdict

    def started(self):
        self.t.run(["start", "R-001"], expect=0)
        return self.t.commit("R-001", "the fix")

    def test_start_makes_the_branch_for_the_repair_in_the_agents_folder(self):
        code, text = self.t.run(["start", "R-001"], expect=0)
        folder = self.t.folder()
        first, _, block = text.partition("\n\n")
        self.assertEqual(first.strip(), "started R-001: branch native/repair-001 in %s" % folder.as_posix())
        self.assertTrue(block.startswith("=== YOUR COMMANDS (agent 1; "), "the agent's commands follow (brief T10 part B): %r" % block[:60])
        self.assertEqual(self.t.git("rev-parse", "--abbrev-ref", "HEAD", cwd=folder), "native/repair-001")
        self.assertEqual(self.t.git("rev-parse", "HEAD", cwd=folder), self.t.git("rev-parse", "origin/main"))
        self.assertEqual(lv.read_repairs(self.t.live)[0]["state"], "started")
        self.assertEqual(lv.read_state(self.t.live)["R-001"]["agent"], "1")
        again = self.t.run(["start", "R-001"])
        self.assertEqual((again[0], again[1].strip()), (1, "plan.py: repair R-001 is already started"))
        self.assertEqual(self.t.run(["start", "R-009"])[0], 1)
        # one thing at a time: the repair is in work, so no step is started, and next shows the repair again
        code, text = self.t.run(["start", "A.1"])
        self.assertEqual(code, 1)
        self.assertIn('you have R-001 in work. Land it (plan.py land R-001) or block it (plan.py blocked R-001 "<why>") first.', text)
        again = self.t.run(["next"], expect=0)[1]
        self.assertTrue(again.startswith("IN WORK: R-001"), again[:40])
        self.assertIn("This is in work: go on with it, then land it or block it.", again)
        self.assertIn("Read the evidence first.", again)
        self.assertNotIn("start: python", again)

    def test_land_hands_the_branch_to_the_gate_with_also_and_on_a_pass_sets_the_repair_landed(self):
        code_commit = self.started()
        code, text = self.t.run(["land", "R-001"], expect=0)
        call = self.gate_calls[0]
        self.assertEqual((call["folder"], call["branch"]), (self.t.folder(), "native/repair-001"))
        self.assertEqual(call["options"][-2:], ["--also", "tour/d3d11"], "the repair's check goes to the gate as --also")
        self.assertEqual(call["options"][:2], ["--agent", "1"])
        self.assertIn("LANDED R-001 as %s (main has it as 0123456789)" % code_commit, text)
        r = lv.read_repairs(self.t.live)[0]
        self.assertEqual((r["state"], r["closed"]), ("landed", "0123456789"))
        self.assertNotIn("R-001", lv.read_state(self.t.live), "no longer in work")
        self.assertEqual(self.t.git("log", "-1", "--format=%s", cwd=self.t.folder()), "the fix", "no state commit for a repair")
        self.assertEqual(lv.read_landings(self.t.live)[0]["step"], "R-001")
        self.assertTrue(self.t.run(["next"], expect=0)[1].startswith("NEXT: A.1"), "the steps go on")
        self.assertEqual({s["state"] for s in plan.read_steps(self.t.tree)}, {"open"}, "steps.tsv is not touched by a repair")

    def test_the_closing_commit_is_the_branchs_when_the_gate_does_not_say_where_main_has_it(self):
        code_commit = self.started()
        self.verdict.commit = ""
        self.t.run(["land", "R-001"], expect=0)
        self.assertEqual(lv.read_repairs(self.t.live)[0]["closed"], code_commit)

    def test_the_ledger_refusals_of_a_step_do_not_apply_to_a_repair(self):
        self.started()
        self.t.run(["land", "R-001"], expect=0)                              # no ledger row named: nothing to say in the ledger

    def test_a_repair_that_names_a_ledger_row_needs_a_dated_note_or_a_fixed_row_with_a_proof(self):
        add_repair(self.t, check="unit/game", renderer="-", row="P-001")
        self.t.run(["start", "R-001"], expect=0)
        self.t.run(["blocked", "R-001", "later"], expect=0)
        self.t.run(["start", "R-002"], expect=0)
        folder = self.t.folder()
        for rows, wanted in (
                ([ledger_row("P-001")], "must be fixed with a proof that names a test, a search or a measure, or its note must have a date"),
                ([ledger_row("P-001", "fixed", "looked at it")], "must be fixed with a proof"),
                ([ledger_row("P-009")], "the ledger has no such row")):
            self.t.write_ledger(rows, tree=folder)
            self.t.git("commit", "-am", "ledger", cwd=folder)
            code, text = self.t.run(["land", "R-002"])
            self.assertEqual(code, 1)
            self.assertIn("repair R-002 does not land yet. The ledger (docs/plan/ledger.tsv) must say:", text)
            self.assertIn(wanted, text)
        self.assertEqual(self.gate_calls, [], "nothing reached the gate")
        self.t.write_ledger([ledger_row("P-001", note="found 11-21: the seam differs")], tree=folder)
        self.t.git("commit", "-am", "dated note", cwd=folder)
        self.t.run(["land", "R-002"], expect=0)
        self.assertEqual(self.gate_calls[0]["options"][-2:], ["--also", "unit/game"])

    def test_a_fixed_row_with_a_proof_lets_it_land_too(self):
        add_repair(self.t, check="unit/game", renderer="-", row="P-001")
        self.t.run(["start", "R-001"], expect=0)
        self.t.run(["blocked", "R-001", "later"], expect=0)
        self.t.run(["start", "R-002"], expect=0)
        self.t.write_ledger([ledger_row("P-001", "fixed", "tests/unit/seam_test fails before")], tree=self.t.folder())
        self.t.git("commit", "-am", "fixed", cwd=self.t.folder())
        self.t.run(["land", "R-002"], expect=0)

    def test_a_failed_landing_leaves_the_repair_started_and_the_third_same_failure_stops(self):
        self.started()
        head = self.t.git("rev-parse", "HEAD", cwd=self.t.folder())
        self.verdict = plan.Verdict(False, [("tour/d3d11", "FAIL", "stop 025 differs")], [("tour/d3d11", "FAIL", "stop 025 differs")])
        code, text = self.t.run(["land", "R-001"])
        self.assertEqual(code, 1)
        self.assertIn("NOT LANDED: R-001.", text)
        self.assertEqual(self.t.git("rev-parse", "HEAD", cwd=self.t.folder()), head, "no state commit was made, none taken off")
        self.assertEqual(lv.read_repairs(self.t.live)[0]["state"], "started")
        self.assertEqual(self.t.run(["land", "R-001"])[0], 1)
        code, text = self.t.run(["land", "R-001"])
        self.assertEqual(code, 3)
        self.assertIn("Same failure three times. Stop retrying. Run plan.py blocked, then plan.py next.", text)
        self.assertEqual(failures.Store(self.t.store).count("native/repair-001"), 3)

    def test_a_tool_result_for_a_repair_is_a_tool_fault_and_exit_4(self):
        self.started()
        self.verdict = plan.Verdict(False, [("harness/self", "TOOL", "result.json is missing")], [("harness/self", "TOOL", "result.json is missing")])
        code, text = self.t.run(["land", "R-001"])
        self.assertEqual(code, 4)
        self.assertEqual([(r["step"], r["kind"]) for r in lv.read_faults(self.t.live)], [("R-001", "TOOL")])

    def test_land_refuses_a_repair_not_started_a_dirty_folder_and_another_branch(self):
        self.assertEqual(self.t.run(["land", "R-001"])[0], 1, "not started")
        self.assertEqual(self.t.run(["land", "R-009"])[0], 1, "no such repair")
        self.started()
        (self.t.folder() / "loose.txt").write_text("x", encoding="utf-8")
        code, text = self.t.run(["land", "R-001"])
        self.assertEqual(code, 1)
        self.assertIn("uncommitted changes", text)
        (self.t.folder() / "loose.txt").unlink()
        self.t.git("checkout", "-b", "elsewhere", cwd=self.t.folder())
        code, text = self.t.run(["land", "R-001"])
        self.assertEqual(code, 1)
        self.assertIn("is on branch elsewhere, not native/repair-001", text)
        self.assertEqual(self.gate_calls, [])

    def test_blocked_sets_the_repair_aside_writes_blocked_md_pushes_the_branch_and_next_goes_on_with_the_steps(self):
        sha = self.started()
        code, text = self.t.run(["blocked", "R-001", "the test tool is at fault: it\nreads the wrong pak"], expect=0)
        self.assertEqual(text.strip(), "set aside R-001. Run plan.py next.")
        r = lv.read_repairs(self.t.live)[0]
        self.assertEqual(r["state"], "set-aside")
        self.assertNotIn("R-001", lv.read_state(self.t.live))
        blocked = (plan.blocked_path(self.t.live)).read_text(encoding="utf-8")
        self.assertRegex(blocked, r"## repairs\n- \d{4}-\d\d-\d\d R-001: the test tool is at fault: it reads the wrong pak \| first failure line: "
                                  r"\(no failure recorded\)")
        self.assertTrue(self.t.git("ls-remote", "origin", "refs/heads/native/repair-001").startswith(self.t.git("rev-parse", "native/repair-001")))
        self.assertTrue(self.t.git("rev-parse", "native/repair-001").startswith(sha))
        text = self.t.run(["next"], expect=0)[1]
        self.assertTrue(text.startswith("NEXT: A.1"), "next goes on with the steps: " + text[:40])
        # the same fault found again is counted on the set-aside repair, and does not come first again
        self.assertEqual(add_repair(self.t), "R-001")
        self.assertEqual(lv.read_repairs(self.t.live)[0]["seen"], "2")
        self.assertTrue(self.t.run(["next"], expect=0)[1].startswith("NEXT: A.1"))

    def test_blocked_refuses_a_repair_while_the_folder_has_uncommitted_changes(self):
        self.started()
        (self.t.folder() / "unfinished.txt").write_text("x", encoding="utf-8")
        code, text = self.t.run(["blocked", "R-001", "why"])
        self.assertEqual(code, 1)
        self.assertIn("Commit your unfinished work first", text)
        self.assertEqual(lv.read_repairs(self.t.live)[0]["state"], "started")
        self.assertEqual(self.t.run(["blocked", "R-009", "why"])[0], 1)

    def test_a_set_aside_repair_can_be_started_again(self):
        self.started()
        self.t.run(["blocked", "R-001", "why"], expect=0)
        self.t.run(["start", "R-001"], expect=0)
        self.assertEqual(lv.read_repairs(self.t.live)[0]["state"], "started")
        self.assertEqual(self.t.git("log", "-1", "--format=%s", cwd=self.t.folder()), "the fix", "the branch keeps its commit")

    def test_status_lists_a_started_repair_in_work_and_the_repairs_section(self):
        add_repair(self.t, check="soak", renderer="-", kind="stress", source="stress")
        self.started()
        text = self.t.run(["status"], expect=0)[1]
        self.assertIn("- R-001 repair of tour/d3d11 on d3d11: agent 1, branch native/repair-001 in %s" % self.t.folder().as_posix(), text)
        self.assertIn("## Repairs (0 open of kind fail, 2 in all)", text)
        self.assertIn("- R-001 started fail: tour/d3d11 on d3d11, not traced to a landing, seen 1x", text)
        self.assertIn("- R-002 open stress: soak, not traced to a landing, seen 1x", text)



class StatusTest(unittest.TestCase):
    def test_status_writes_the_file_and_prints_it(self):
        with Tree() as t:
            t.run(["start", "A.1"], expect=0)
            plan.set_state(t.tree, "C.1", "blocked")
            plan.append_under(plan.blocked_path(t.live), "steps", "- 2026-11-20 C.1: the card is wrong | first failure line: x")
            t.landed("A.2", commit="abc123def")
            lv.add_landing(t.live, "A.2", "abc123def", 600)
            code, text = t.run(["status"], expect=0)
            written = (plan.native(t.tree) / "STATUS.md").read_text(encoding="utf-8")
            self.assertEqual(written, text)
            self.assertIn("Steps landed: 1 of 5.", text)
            self.assertIn("## In work (1, one thing for each agent)", text)
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
        with at_the_start()[0], at_the_start()[1]:
            text = plan.status_text(TREE)
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
        self.assertEqual([l.split(":")[0] for l in out[:-1]], ["repo", "build", "unit", "start", "engine1", "engine3", "tour1", "tour3", "validate", "import", "android", "github", "measure_pc", "measure_tablet"])
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
