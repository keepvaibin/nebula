"""plan_ahead_test.py: `plan.py ahead`, the next step written while a landing plays (package B16), and what `next`, `status`, `land`,
`start` and `blocked` do with it.

The commands run in a temporary git tree made by plan_fixture (a bare origin, a plan tree, one work folder) with the gate replaced by
a fake whose answers and side effects the test chooses, and the landing queue a temporary file: no game, no build, no network, nothing
of D:/NebulaWork/plan. "N" is the item being landed, "M" the step written ahead of it.

    python -X utf8 -I tools/gate/tests/plan_ahead_test.py
"""
import os
import pathlib
import re
import sys
import unittest
from unittest import mock

HERE = pathlib.Path(__file__).resolve().parent
TREE = HERE.parents[2]
sys.path.insert(0, str(HERE))
sys.path.insert(0, str(TREE / "tools" / "plan"))
import live as lv  # noqa: E402
import plan  # noqa: E402
from plan_fixture import Tree, git_run, ledger_row, ready_a1, run, setup_module, teardown_module, write_steps  # noqa: E402
from plan_rerun_test import failed_verdict, passed_verdict  # noqa: E402

REFUSAL = "ahead is for the time a landing plays: nothing of yours is being landed"
# C.1 does not need A.1; A.2 keeps A.1 from being its package's last step, whose landing is the section's full check, during
# which `ahead` writes no step of another package (mend M-006, plan_section_test)
FIRST_PLAN = (("A.1", "A", "", "open"), ("C.1", "C", "", "open"), ("A.2", "A", "", "open"))
BLOCKED_AFTER = (("A.1", "A", "", "open"), ("B.1", "B", "Z", "open"), ("Z.1", "Z", "", "blocked"))


def setUpModule():
    setup_module()


def tearDownModule():
    teardown_module()


def not_judged_verdict():
    v = plan.Verdict(False, [("tour/d3d12", "TOOL", "result.json is missing")], [("tour/d3d12", "TOOL", "result.json is missing")])
    v.code, v.gate_recorded = 2, True
    return v


class AheadBase(unittest.TestCase):
    """A started A.1 in a temporary tree (the steps of ROWS when a test gives them), a fake gate and a queue file of its own."""
    ROWS = None

    def setUp(self):
        self.t = Tree().__enter__()
        self.addCleanup(self.t.__exit__)
        if self.ROWS:
            write_steps(self.t, self.ROWS)
        self.t.run(["start", "A.1"], expect=0)
        self.folder = self.t.folder("A.1")
        self.calls, self.results, self.hooks = [], [], []
        self.queue = self.t.dir / "queue.tsv"
        for patcher in (mock.patch.object(plan, "LAND", self.fake_land), mock.patch.object(plan, "QUEUE_FILE", str(self.queue)),
                        mock.patch.object(plan, "alive", lambda pid: pid == os.getpid()), mock.patch.object(plan, "JOIN_POLL", 0)):
            patcher.start()
            self.addCleanup(patcher.stop)

    # -- the fake gate: what it was given, a side effect while it "plays", and its answer
    def fake_land(self, folder, branch, tablet, wait, options=(), **kw):
        self.calls.append({"branch": branch, "kw": kw, "options": list(options), "record": lv.read_record(self.t.live, 1),
                           "branch_tip": self.rev("refs/heads/" + branch), "head": self.on()})
        if self.hooks:
            self.hooks.pop(0)()
        return self.results.pop(0) if len(self.results) > 1 else self.results[0]

    # -- looking
    def rev(self, ref):
        return self.t.git("rev-parse", ref, cwd=self.folder)

    def on(self):
        return self.t.git("rev-parse", "--abbrev-ref", "HEAD", cwd=self.folder)

    def status(self):
        return self.t.git("status", "--porcelain", cwd=self.folder)

    def subjects(self, rng):
        return self.t.git("log", "--format=%s", rng, cwd=self.folder).splitlines()

    def is_ancestor(self, old, new):
        return plan.is_ancestor(self.folder, old, new)

    def rebase_going(self):
        gitdir = pathlib.Path(self.folder) / self.t.git("rev-parse", "--git-dir", cwd=self.folder)
        return any((gitdir / n).exists() for n in ("rebase-merge", "rebase-apply"))

    def entry(self):
        return lv.read_ahead(self.t.live, 1)

    # -- running
    def nested(self, args, expect=None):
        """A plan command run while the outer command waits for the gate (the outer command's reading of main is kept)."""
        saved = (dict(plan.MAIN_LANDED), list(plan.MAIN_READ))
        try:
            return self.t.run(args, expect)
        finally:
            plan.MAIN_LANDED.clear()
            plan.MAIN_LANDED.update(saved[0])
            plan.MAIN_READ[:] = saved[1]

    def in_folder(self, args, expect=None):
        """plan.py as an agent runs it: the work folder is the tree the tool reads and writes."""
        return run(args, self.folder, self.t.work, self.t.store, expect, self.t.live)

    def land(self, expect=None):
        return self.t.run(["land", "A.1"], expect)

    # -- states
    def queue_row(self, branch="native/A.1"):
        q = plan.landing.Queue(self.queue)
        q.add([branch], "1", lambda b: "abc")
        q.claim()
        return q.row(branch)

    def plant(self, tip, step="A.1", branch="native/A.1"):
        """A landing in the gate's hands: its queue row runs and its record names `tip`."""
        self.queue_row(branch)
        lv.write_record(self.t.live, 1, step, branch, tip, 424242)

    def unplant(self):
        lv.drop_record(self.t.live, 1)
        self.queue.unlink()

    def failed_landing(self):
        """The ledger the card wants and a failed landing whose state commit stays on the branch (the tool did not get to take it off, as in a
        landing that was cut off: the tool itself takes it off after a failure, which plan_after_trial_test proves): the tip that was handed over."""
        ready_a1(self.t)
        self.results = [failed_verdict()]
        with mock.patch.object(plan, "state_commit_after", lambda *args: None):
            self.assertEqual(self.land()[0], 1)
        self.calls.clear()
        return self.rev("HEAD")

    def dirty(self):
        """An uncommitted change to a tracked file and a file git does not know: the agent's work in progress."""
        (self.folder / "runtime" / "src" / "render" / "a.cpp").write_text("a\nb\nc\nd\nwork in progress\n", encoding="utf-8")
        (self.folder / "loose.txt").write_text("not committed", encoding="utf-8")

    def ahead_made(self):
        """A failed landing of A.1, the landing playing, `ahead` run once (M = A.2, one commit of its own, committed). -> tip."""
        tip = self.failed_landing()
        self.plant(tip)
        self.t.run(["ahead"], expect=0)
        self.m_commit()
        self.unplant()                                    # the landing has been answered (failed)
        return tip

    def m_commit(self, name="m.txt", text="m\n", message="A.2: its work"):
        return self.t.commit("A.2", message, {"runtime/src/render/" + name: text} if "/" not in name else {name: text})

    def commit_n(self, files, message="fix: n"):
        """A commit on the branch of A.1 whatever branch the folder is on (made without touching the working tree)."""
        sha = self.t.git("commit-tree", "-p", "refs/heads/native/A.1", "-m", message, "refs/heads/native/A.1^{tree}", cwd=self.folder)
        self.t.git("update-ref", "refs/heads/native/A.1", sha, cwd=self.folder)
        return sha

    def main_replay(self, branch, unrelated=None):
        """What a landing does when main has moved: an unrelated commit is on main (`unrelated`: {file: text}) and then the
        branch's own commits are put on it again, under other ids. Origin's main moves; returns main's new tip."""
        clone = self.t.dir / "replay"
        git_run("clone", "-q", str(self.t.origin), str(clone))
        if unrelated:
            for name, text in unrelated.items():
                (clone / name).parent.mkdir(parents=True, exist_ok=True)
                (clone / name).write_text(text, encoding="utf-8")
            git_run("add", "-A", cwd=str(clone))
            git_run("commit", "-q", "-m", "unrelated work on main", cwd=str(clone))
        git_run("fetch", "-q", str(self.folder), "refs/heads/%s:refs/heads/replayed" % branch, cwd=str(clone))
        for sha in git_run("rev-list", "--reverse", "origin/main..replayed", cwd=str(clone)).stdout.split():
            git_run("cherry-pick", sha, cwd=str(clone))
        git_run("push", "-q", "origin", "HEAD:main", cwd=str(clone))
        return git_run("rev-parse", "HEAD", cwd=str(clone)).stdout.strip()

    def pass_moving_main(self, branch="native/A.1"):
        """A fake gate's hook: the branch is put on main as it is (a fast forward)."""
        return lambda: git_run("push", "-q", "origin", "%s:main" % branch, cwd=str(self.folder))


# ---- the command --------------------------------------------------------------------------------------------------------

class AheadCommandTest(AheadBase):
    def test_ahead_is_refused_with_no_landing_and_says_what_next_says(self):
        code, text = self.t.run(["ahead"])
        lines = text.splitlines()
        self.assertEqual(code, 1)
        self.assertEqual(lines[0], REFUSAL)
        self.assertTrue(lines[1].startswith("plan.py next says: IN WORK: A.1"), lines[1])
        self.assertFalse(plan.branch_exists(self.folder, "refs/heads/native/A.2"))

    def test_ahead_with_a_landing_makes_the_branch_at_the_records_tip_writes_the_entry_and_prints_the_card(self):
        tip = self.failed_landing()
        self.plant(tip)
        code, text = self.t.run(["ahead"])
        lines = text.splitlines()
        self.assertEqual(code, 0, text)
        self.assertEqual(lines[0], "AHEAD: A.2  Second step")
        self.assertEqual(lines[1], "on %s, the landing of A.1" % tip[:9])
        self.assertIn("=== THE CARD ===", lines)
        self.assertIn("### A.2 Second step", lines)
        self.assertIn("=== WHAT THE LANDING NEEDS IN THE LEDGER ===", lines)
        self.assertIn("=== YOUR COMMANDS (agent 1; folder %s; build folder %s/Nebula-native) ===" % (
            self.folder.as_posix(), plan.BUILD_ROOT), lines)
        self.assertEqual(lines[-1], "Do not land it. When A.1 has landed: plan.py start A.2")
        self.assertEqual(self.rev("refs/heads/native/A.2"), tip, "the branch is at the record's tip")
        self.assertEqual(self.on(), "native/A.2")
        e = self.entry()
        self.assertEqual((e["step"], e["after"], e["base"], e["handed"]), ("A.2", "A.1", tip, tip))
        self.assertTrue(lv.parse_stamp(e["time"]))
        self.assertEqual(list(lv.read_state(self.t.live)), ["A.1"], "no step's state changed")
        self.assertEqual([(c["package"], c["agent"]) for c in lv.read_claims(self.t.live)], [("A", "1")], "no claim was made")
        self.assertEqual(self.t.git("ls-remote", "--heads", "origin", "native/A.2"), "", "nothing was pushed")

    def test_a_second_ahead_changes_nothing(self):
        tip = self.failed_landing()
        self.plant(tip)
        first = self.t.run(["ahead"], expect=0)[1]
        before = (self.entry(), self.rev("refs/heads/native/A.2"), self.status(), self.t.git("reflog", "-1", cwd=self.folder))
        code, again = self.t.run(["ahead"])
        self.assertEqual(code, 0, again)
        self.assertEqual(again, first)
        self.assertEqual((self.entry(), self.rev("refs/heads/native/A.2"), self.status(), self.t.git("reflog", "-1", cwd=self.folder)), before)

    def test_the_first_call_needs_a_clean_folder_on_the_branch_of_the_landing(self):
        tip = self.failed_landing()
        self.plant(tip)
        self.dirty()
        code, text = self.t.run(["ahead"])
        self.assertEqual(code, 1)
        self.assertIn("plan.py: the work folder %s has uncommitted changes. Commit them (or throw away what you do not want), then "
                      "run ahead again." % self.folder.as_posix(), text.splitlines())
        self.assertFalse(plan.branch_exists(self.folder, "refs/heads/native/A.2"))
        self.assertIsNone(self.entry())
        self.t.git("stash", "-u", cwd=self.folder)
        self.t.git("checkout", "-q", "-b", "elsewhere", cwd=self.folder)
        code, text = self.t.run(["ahead"])
        self.assertEqual(code, 1)
        self.assertIn("is on branch elsewhere, not native/A.1", text)
        self.assertFalse(plan.branch_exists(self.folder, "refs/heads/native/A.2"))

    def test_a_branch_that_exists_without_an_entry_is_left_as_it_is(self):
        tip = self.failed_landing()
        self.plant(tip)
        self.t.git("branch", "native/A.2", "origin/main", cwd=self.folder)
        code, text = self.t.run(["ahead"])
        self.assertEqual(code, 1)
        self.assertIn("plan.py: the branch native/A.2 exists already and ahead has no entry for it. Nothing is changed and nothing is "
                      "deleted. Nothing is written ahead this time: wait for the landing.", text.splitlines())
        self.assertEqual(self.rev("refs/heads/native/A.2"), self.rev("origin/main"))

    def test_an_entry_for_another_step_than_the_one_that_follows_is_refused_and_kept(self):
        tip = self.failed_landing()
        self.plant(tip)
        lv.write_ahead(self.t.live, 1, "C.1", "A.1", tip, tip)
        self.t.git("branch", "native/C.1", tip, cwd=self.folder)
        code, text = self.t.run(["ahead"])
        self.assertEqual(code, 1)
        self.assertIn("ahead has native/C.1 written, and the step that follows now is A.2: native/C.1 is kept as it is (nothing is "
                      "deleted)", text)
        self.assertEqual(self.entry()["step"], "C.1")
        self.assertTrue(plan.branch_exists(self.folder, "refs/heads/native/C.1"))
        self.assertFalse(plan.branch_exists(self.folder, "refs/heads/native/A.2"), "no second step ahead")

    def test_the_second_call_after_a_mended_landing_moves_the_commits_of_M_onto_the_new_tip(self):
        old = self.ahead_made()
        m_before = self.subjects("%s..native/A.2" % old)
        self.t.git("checkout", "-q", "native/A.1", cwd=self.folder)
        self.t.commit("A.1", "fix: the repair", {"n.txt": "n\n"})
        self.results = [failed_verdict()]
        self.assertEqual(self.land()[0], 1)
        new = self.rev("HEAD")
        self.assertNotEqual(new, old)
        self.plant(new)
        code, text = self.t.run(["ahead"])
        self.assertEqual(code, 0, text)
        self.assertEqual(text.splitlines()[1], "on %s, the landing of A.1" % new[:9])
        self.assertTrue(self.is_ancestor(new, "native/A.2"), "M stands on the new tip")
        self.assertEqual(self.subjects("%s..native/A.2" % new), m_before, "exactly M's own commits")
        self.assertEqual(self.entry()["base"], new)
        self.assertIn("moved the 1 commits of native/A.2 onto %s" % new[:9], text.splitlines())
        self.assertEqual(self.on(), "native/A.2")
        self.assertFalse(self.rebase_going())

    def test_a_move_that_does_not_apply_is_aborted_and_says_the_command_to_do_by_hand(self):
        old = self.ahead_made()
        m_tip = self.rev("refs/heads/native/A.2")
        self.t.git("checkout", "-q", "native/A.1", cwd=self.folder)
        self.t.commit("A.1", "fix: the same file", {"runtime/src/render/m.txt": "other\n"})
        self.results = [failed_verdict()]
        self.land()
        new = self.rev("HEAD")
        self.plant(new)
        code, text = self.t.run(["ahead"])
        self.assertEqual(code, 1, text)
        self.assertIn("plan.py: the commits of native/A.2 do not apply on top of native/A.1", text)
        self.assertIn("The branch is as it was. Move it by hand: git rebase --onto %s %s native/A.2 (resolve, git rebase --continue), "
                      "then run ahead again." % (new, old), text)
        self.assertFalse(self.rebase_going())
        self.assertEqual(self.rev("refs/heads/native/A.2"), m_tip, "the branch is as it was")
        self.assertEqual(self.on(), "native/A.2", "and the folder is on it, with nothing half done")
        self.assertEqual(self.status(), "")
        self.assertEqual(self.entry()["base"], old, "the entry is as it was")

    def test_a_move_with_uncommitted_changes_is_refused(self):
        old = self.ahead_made()
        self.t.git("checkout", "-q", "native/A.1", cwd=self.folder)
        self.t.commit("A.1", "fix: the repair", {"n.txt": "n\n"})
        self.results = [failed_verdict()]
        self.land()
        self.plant(self.rev("HEAD"))
        self.t.git("checkout", "-q", "native/A.2", cwd=self.folder)
        self.dirty()
        code, text = self.t.run(["ahead"])
        self.assertEqual(code, 1)
        self.assertIn("commit your work on native/A.2 first, then run ahead again", text)
        self.assertEqual(self.entry()["base"], old)

    def test_a_base_that_git_no_longer_has_is_said_and_the_branch_is_named_as_kept(self):
        self.ahead_made()
        lv.write_ahead(self.t.live, 1, "A.2", "A.1", "1234567890abcdef1234567890abcdef12345678", "0" * 40)
        self.t.git("checkout", "-q", "native/A.1", cwd=self.folder)
        self.t.commit("A.1", "fix: moves the tip", {"n.txt": "n\n"})
        self.plant(self.rev("HEAD"))
        code, text = self.t.run(["ahead"])
        self.assertEqual(code, 1)
        self.assertIn("plan.py: the commit 123456789 that native/A.2 was written on is no longer in this repository, so its commits "
                      "cannot be moved. native/A.2 is kept as it is; nothing is deleted.", text.splitlines())
        self.assertTrue(plan.branch_exists(self.folder, "refs/heads/native/A.2"))

    def test_an_entry_of_a_step_the_plan_does_not_have_is_dropped_and_its_branch_is_left_as_it_is(self):
        tip = self.failed_landing()
        self.plant(tip)
        lv.write_ahead(self.t.live, 1, "Z.9", "A.1", tip, tip)
        self.t.git("branch", "native/Z.9", tip, cwd=self.folder)
        code, text = self.t.run(["ahead"])
        self.assertEqual(code, 0, text)
        self.assertIn("the ahead entry of Z.9 is dropped (the plan has no such step); native/Z.9 is left as it is.", text.splitlines())
        self.assertIn("AHEAD: A.2  Second step", text.splitlines())
        self.assertTrue(plan.branch_exists(self.folder, "refs/heads/native/Z.9"))
        self.assertEqual(self.entry()["step"], "A.2")

    def test_an_entry_whose_branch_is_gone_is_written_again_from_the_records_tip(self):
        tip = self.ahead_made()
        self.t.git("checkout", "-q", "native/A.1", cwd=self.folder)
        self.t.git("branch", "-D", "native/A.2", cwd=self.folder)
        self.plant(tip)
        code, text = self.t.run(["ahead"])
        self.assertEqual(code, 0, text)
        self.assertEqual(self.rev("refs/heads/native/A.2"), tip)
        self.assertEqual(self.entry()["base"], tip)

    def test_nothing_can_be_prepared_when_the_list_ends_or_the_next_step_waits_for_one_not_landed(self):
        write_steps(self.t, (("A.1", "A", "", "open"),))
        tip = self.failed_landing()
        self.plant(tip)
        code, text = self.t.run(["ahead"])
        self.assertEqual(code, 1)
        self.assertEqual(text.strip(), "nothing can be prepared ahead: the list is at its end: every step is landed or is the one being landed")
        write_steps(self.t, BLOCKED_AFTER)
        code, text = self.t.run(["ahead"])
        self.assertEqual(code, 1)
        self.assertEqual(text.strip(), "nothing can be prepared ahead: the step after it, B.1, needs Z.1 (blocked), not landed yet")
        self.assertFalse(plan.branch_exists(self.folder, "refs/heads/native/B.1"))


# ---- what a landing that ends does while the folder is on the step written ahead ----------------------------------------------

class LandFromAheadTest(AheadBase):
    def stale_ahead(self):
        """M was written on the first tip; N was mended and landed again (a second state commit that names a newer code commit), and
        `ahead` was not called since: the folder is on M, whose steps.tsv holds the older code commit. The landing of the new tip plays.
        -> (the new tip, the new code commit)"""
        self.ahead_made()
        self.t.git("checkout", "-q", "native/A.1", cwd=self.folder)
        fix = self.t.commit("A.1", "fix: the repair", {"n.txt": "n\n"})
        self.results = [failed_verdict()]
        with mock.patch.object(plan, "state_commit_after", lambda *args: None):     # the landing is cut off: its state commit stays
            self.land()
        new = self.rev("HEAD")
        fix = self.t.git("rev-parse", "--short=9", "HEAD~1", cwd=self.folder)     # the fix commit as the landing put it on the branch (E1)
        self.assertEqual(self.entry()["handed"], new, "a landing given from the step's branch is the tip the entry remembers")
        self.t.git("checkout", "-q", "native/A.2", cwd=self.folder)
        self.calls.clear()
        self.plant(new)
        return new, fix

    def test_a_cut_land_joined_from_the_ahead_branch_ends_with_the_verdict_and_leaves_the_working_tree_as_it_was(self):
        new, fix = self.stale_ahead()
        self.dirty()
        before = self.status()
        steps_before = (self.folder / "docs/plan/native/steps.tsv").read_bytes()
        self.results = [passed_verdict()]
        code, text = self.in_folder(["land", "A.1"])
        self.assertEqual(code, 0, text)
        self.assertIn("joined the landing of A.1, in the queue since", text)
        self.assertIn("LANDED A.1 as %s" % fix, text, "the code commit the handed tip names")
        self.assertEqual(self.calls[0]["kw"], {"push": False})
        self.assertEqual(self.status(), before, "git status is what it was")
        self.assertEqual((self.folder / "docs/plan/native/steps.tsv").read_bytes(), steps_before, "no file of the tree was written")
        self.assertEqual(self.on(), "native/A.2")
        self.assertNotIn("NOTE:", text, "the commits of M are not 'later commits of the branch'")
        self.assertEqual([r["step"] for r in lv.read_landings(self.t.live)], ["A.1"], "the live folder has the rest")

    def test_the_same_join_on_a_failure_changes_nothing_either(self):
        new, fix = self.stale_ahead()
        self.dirty()
        before = self.status()
        self.results = [failed_verdict()]
        code, text = self.in_folder(["land", "A.1"])
        self.assertEqual(code, 1, text)
        self.assertIn("NOT LANDED: A.1.", text)
        self.assertEqual(self.status(), before)
        self.assertEqual(self.on(), "native/A.2")
        self.assertEqual(self.rev("refs/heads/native/A.1"), self.rev(new + "^"), "the failed landing's state commit is off native/A.1; nothing else moved")

    def test_a_landing_that_passed_while_the_folder_went_to_the_ahead_branch_ends_there_and_leaves_the_tree_as_it_was(self):
        """The command that handed the landing over is not cut: `ahead` switches the folder while it waits, and it ends there."""
        ready_a1(self.t)
        self.results = [passed_verdict()]
        before = []

        def while_it_plays():
            self.nested(["ahead"], 0)
            self.m_commit()
            self.dirty()
            before.append(self.status())

        self.hooks = [while_it_plays]
        code, text = self.in_folder(["land", "A.1"])
        self.assertEqual(code, 0, text)
        self.assertEqual(self.on(), "native/A.2")
        self.assertEqual(self.status(), before[0])
        self.assertEqual(self.calls[0]["head"], "native/A.1", "it was handed over from the step's own branch")

    def test_a_landing_not_judged_is_given_again_from_the_ahead_branch_when_nothing_is_new(self):
        ready_a1(self.t)
        self.results = [not_judged_verdict()]
        self.hooks = [lambda: (self.nested(["ahead"], 0), self.m_commit())]
        self.assertEqual(self.land()[0], 4)
        tip = self.rev("refs/heads/native/A.1")
        self.assertEqual(self.on(), "native/A.2")
        self.dirty()
        before = self.status()
        self.calls.clear()
        self.results = [passed_verdict()]
        code, text = self.in_folder(["land", "A.1"])
        self.assertEqual(code, 0, text)
        self.assertEqual(len(self.calls), 1, "a new landing of the same tip")
        self.assertEqual((self.calls[0]["branch"], self.calls[0]["branch_tip"], self.calls[0]["record"]["tip"], self.calls[0]["kw"]),
                         ("native/A.1", tip, tip, {}))
        self.assertEqual(self.calls[0]["head"], "native/A.2", "handed over from the ahead branch, uncommitted changes and all")
        self.assertIn("landing A.1 again from native/A.2: its tip %s is the one the last landing was given; nothing is committed and "
                      "the folder stays on native/A.2." % tip[:9], text.splitlines())
        self.assertEqual(self.rev("refs/heads/native/A.1"), tip, "nothing was committed")
        self.assertEqual(self.status(), before)

    def test_a_new_commit_on_the_step_needs_the_folder_on_the_step_and_the_refusal_names_both_branches(self):
        ready_a1(self.t)
        self.results = [not_judged_verdict()]
        self.hooks = [lambda: (self.nested(["ahead"], 0), self.m_commit())]
        self.land()
        self.commit_n({}, "fix: a new commit")
        self.calls.clear()
        code, text = self.in_folder(["land", "A.1"])
        self.assertEqual(code, 1)
        self.assertIn("commit your work on native/A.2, then git checkout native/A.1", text)
        self.assertIn("native/A.1", text)
        self.assertEqual(self.calls, [], "the gate was not asked")
        self.assertEqual(self.on(), "native/A.2")

    def test_a_state_commit_that_no_landing_was_given_is_not_handed_over_from_the_ahead_branch(self):
        """The tool made its commit on the step's branch and was cut before the gate had it: the tip looks like the tool's own commit,
        but it is not the one the last landing held."""
        ready_a1(self.t)
        self.results = [not_judged_verdict()]
        self.hooks = [lambda: (self.nested(["ahead"], 0), self.m_commit())]
        self.land()
        handed = self.entry()["handed"]
        self.t.git("checkout", "-q", "native/A.1", cwd=self.folder)
        plan.set_state(self.folder, "A.1", "landed", "deadbeef0", track=False)
        self.t.git("add", "docs/plan/native/steps.tsv", cwd=self.folder)
        self.t.git("commit", "-q", "-m", "Plan: step A.1 landed", cwd=self.folder)
        self.assertTrue(plan.is_state_commit(self.folder, self.rev("HEAD"), "A.1"))
        self.assertNotEqual(self.rev("HEAD"), handed)
        self.t.git("checkout", "-q", "native/A.2", cwd=self.folder)
        self.calls.clear()
        code, text = self.in_folder(["land", "A.1"])
        self.assertEqual(code, 1)
        self.assertIn("commit your work on native/A.2, then git checkout native/A.1", text)
        self.assertEqual(self.calls, [])

    def test_the_state_commit_still_to_be_made_needs_the_step_branch_too(self):
        """Not judged before the tool's own commit existed: the tip is the step's code, so the landing must make its commit there."""
        ready_a1(self.t)
        self.t.git("checkout", "-q", "-b", "native/A.2", cwd=self.folder)
        lv.write_ahead(self.t.live, 1, "A.2", "A.1", self.rev("HEAD"), self.rev("HEAD"))
        code, text = self.in_folder(["land", "A.1"])
        self.assertEqual(code, 1)
        self.assertIn("commit your work on native/A.2, then git checkout native/A.1", text)
        self.assertEqual(self.calls, [])

    def test_land_M_before_it_is_started_is_refused(self):
        tip = self.failed_landing()
        self.plant(tip)
        self.t.run(["ahead"], expect=0)
        code, text = self.t.run(["land", "A.2"])
        self.assertEqual(code, 1)
        self.assertEqual(text.strip(), "step A.2 is prepared ahead, not started: plan.py start A.2")
        self.assertEqual(self.calls, [])


# ---- start M, blocked N ----------------------------------------------------------------------------------------------------

class StartPreparedTest(AheadBase):
    def passed_with_m(self, replay=None):
        """N lands and passes; M was written ahead (one commit). `replay`: None main is N's tip; else {file: text}, an unrelated commit
        on main and N put on it again under other ids."""
        ready_a1(self.t)
        if replay is None:
            gate = self.pass_moving_main()
        else:
            gate = lambda: self.main_replay("native/A.1", replay)
        self.results = [passed_verdict()]
        self.hooks = [lambda: (self.nested(["ahead"], 0), self.m_commit(), gate())]
        self.assertEqual(self.land()[0], 0)
        self.assertEqual(self.on(), "native/A.2")

    def test_start_M_with_uncommitted_changes_works_when_main_is_N_s_tip_and_nothing_moves(self):
        self.passed_with_m()
        m_tip = self.rev("refs/heads/native/A.2")
        self.dirty()
        before = self.status()
        code, text = self.t.run(["start", "A.2"])
        self.assertEqual(code, 0, text)
        self.assertIn("started A.2: branch native/A.2 in", text)
        self.assertIn("prepared ahead: 1 commits of A.2 are on top of main", text.splitlines())
        self.assertEqual(self.rev("refs/heads/native/A.2"), m_tip, "no rebase")
        self.assertEqual(self.status(), before)
        self.assertEqual(lv.read_state(self.t.live)["A.2"]["state"], "started")
        self.assertIsNone(self.entry(), "the entry is removed")
        self.assertTrue(plan.branch_exists(self.folder, "refs/heads/native/A.1"), "no branch is deleted")
        self.assertIn("=== YOUR COMMANDS", text)

    def test_start_M_moves_exactly_the_commits_of_M_when_main_has_N_under_other_ids(self):
        self.passed_with_m({"other.txt": "unrelated\n"})
        self.t.git("fetch", "-q", "origin", cwd=self.folder)
        self.assertFalse(self.is_ancestor("origin/main", "native/A.2"))
        n_code = self.rev("native/A.2~1")
        code, text = self.t.run(["start", "A.2"])
        self.assertEqual(code, 0, text)
        self.assertIn("prepared ahead: 1 commits of A.2 are on top of main", text.splitlines())
        self.assertTrue(self.is_ancestor("origin/main", "native/A.2"))
        self.assertEqual(self.subjects("origin/main..native/A.2"), ["A.2: its work"])
        subjects = self.subjects("native/A.2")
        self.assertEqual(subjects.count("A.1: the code"), 1, "none of N twice")
        self.assertEqual(subjects.count("Plan: step A.1 landed"), 1)
        for name, text_of in (("runtime/src/render/m.txt", "m\n"), ("other.txt", "unrelated\n"), ("runtime/src/render/a.cpp", "a\nb\nc\nd\n")):
            self.assertEqual(self.t.git("show", "native/A.2:" + name, cwd=self.folder).replace("\r", ""), text_of.strip("\n"), name)
        self.assertNotEqual(self.rev("native/A.2~1"), n_code)
        self.assertEqual(self.status(), "")
        self.assertIsNone(self.entry())

    def test_a_move_that_has_to_happen_needs_a_clean_folder_and_a_conflict_is_aborted(self):
        self.passed_with_m({"runtime/src/render/m.txt": "main wrote this first\n"})
        m_tip = self.rev("refs/heads/native/A.2")
        base = self.entry()["base"]
        self.dirty()
        code, text = self.t.run(["start", "A.2"])
        self.assertEqual(code, 1)
        self.assertIn("has uncommitted changes, and the commits of native/A.2 have to be moved onto origin/main: commit them first "
                      "(git add, git commit), then run start again.", text)
        self.t.git("stash", "-u", cwd=self.folder)
        code, text = self.t.run(["start", "A.2"])
        self.assertEqual(code, 1, text)
        self.assertIn("plan.py: the commits of native/A.2 do not apply on top of origin/main", text)
        self.assertIn("The branch is as it was. Move it by hand: git rebase --onto origin/main %s native/A.2 (resolve, git rebase "
                      "--continue), then run start again." % base, text)
        self.assertFalse(self.rebase_going())
        self.assertEqual(self.rev("refs/heads/native/A.2"), m_tip)
        self.assertEqual(self.status(), "")
        self.assertNotIn("A.2", lv.read_state(self.t.live), "not started")
        self.assertEqual(self.entry()["step"], "A.2", "the entry stays")

    def test_start_of_a_repair_that_came_first_keeps_the_ahead_branch_and_start_M_moves_it_onto_main_later(self):
        self.passed_with_m()
        m_tip = self.rev("refs/heads/native/A.2")
        lv.add_repair(self.t.live, "nightly", "fail", "tour/d3d11", renderer="d3d11", evidence="x", note="stop 25 differs")
        code, text = self.t.run(["start", "R-001"])
        self.assertEqual(code, 0, text)
        self.assertEqual(self.on(), "native/repair-001")
        self.assertEqual(self.rev("refs/heads/native/A.2"), m_tip, "the ahead branch is kept")
        self.assertEqual(self.entry()["step"], "A.2")
        self.t.commit("A.1", "R-001: the repair", {"runtime/src/render/r.txt": "r\n"})
        self.results = [passed_verdict()]
        self.hooks = [self.pass_moving_main("native/repair-001")]
        self.assertEqual(self.t.run(["land", "R-001"])[0], 0)
        code, text = self.t.run(["start", "A.2"])
        self.assertEqual(code, 0, text)
        self.assertTrue(self.is_ancestor("origin/main", "native/A.2"))
        self.assertEqual(self.subjects("origin/main..native/A.2"), ["A.2: its work"])
        self.assertIn("prepared ahead: 1 commits of A.2 are on top of main", text.splitlines())

class StartOtherTest(AheadBase):
    ROWS = (("A.1", "A", "", "open"), ("A.2", "A", "", "open"), ("C.1", "C", "", "open"))

    def passed_with_m(self):
        ready_a1(self.t)
        self.results = [passed_verdict()]
        self.hooks = [lambda: (self.nested(["ahead"], 0), self.m_commit(), self.pass_moving_main()())]
        self.assertEqual(self.land()[0], 0)

    def test_start_of_anything_else_works_as_today_when_the_folder_is_clean_and_the_entry_stays(self):
        self.passed_with_m()
        self.dirty()
        code, text = self.t.run(["start", "C.1"])
        self.assertEqual(code, 1)
        self.assertIn("has uncommitted changes. Commit them (or throw away what you do not want), then run start again.", text)
        self.t.git("stash", "-u", cwd=self.folder)
        code, text = self.t.run(["start", "C.1"])
        self.assertEqual(code, 0, text)
        self.assertEqual(self.on(), "native/C.1")
        self.assertEqual(self.entry()["step"], "A.2")
        self.assertTrue(plan.branch_exists(self.folder, "refs/heads/native/A.2"))


class GivenUpTest(AheadBase):
    ROWS = FIRST_PLAN

    def test_blocked_N_from_the_ahead_branch_works_clean_and_is_refused_with_changes_and_start_M_does_without_N(self):
        tip = self.failed_landing()
        self.plant(tip)
        self.t.run(["ahead"], expect=0)
        self.assertEqual(self.entry()["step"], "C.1", "C.1 does not need A.1")
        self.m_commit()
        self.unplant()
        self.dirty()
        code, text = self.t.run(["blocked", "A.1", "the replay differs"])
        self.assertEqual(code, 1)
        self.assertIn("Commit your work on native/C.1 first (git add, git commit), then run blocked again.", text)
        self.assertNotIn("A.1", [k for k, v in lv.read_state(self.t.live).items() if v["state"] == "blocked"])
        self.t.git("stash", "-u", cwd=self.folder)
        code, text = self.t.run(["blocked", "A.1", "the replay differs"])
        self.assertEqual(code, 0, text)
        self.assertEqual(lv.read_state(self.t.live)["A.1"]["state"], "blocked")
        self.assertNotEqual(self.t.git("ls-remote", "--heads", "origin", "native/A.1"), "", "N's branch went to origin")
        self.assertEqual(self.entry()["step"], "C.1", "the entry stays")
        self.assertTrue(plan.branch_exists(self.folder, "refs/heads/native/C.1"))
        self.assertEqual(self.on(), "native/C.1")
        self.assertIn("NEXT: C.1", self.t.run(["next"], expect=0)[1].splitlines()[0])
        code, text = self.t.run(["start", "C.1"])
        self.assertEqual(code, 0, text)
        self.assertEqual(self.subjects("origin/main..native/C.1"), ["A.2: its work"], "M's commits only: none of N's")
        self.assertEqual(self.t.git("show", "native/C.1:runtime/src/render/a.cpp", cwd=self.folder).replace("\r", ""), "a\nb\nc")
        self.assertIn("prepared ahead: 1 commits of C.1 are on top of main", text.splitlines())

    def test_a_blocked_N_is_taken_up_again_once_main_holds_the_mend_and_M_follows_it(self):
        """T11-B (step S0.1.3): the commands of AGENT.md section 13, last part: start N, git rebase origin/main, land N, ahead."""
        tip = self.failed_landing()
        self.plant(tip)
        self.t.run(["ahead"], expect=0)
        self.m_commit(message="C.1: its work")
        self.unplant()
        self.t.run(["blocked", "A.1", "tool: the census refused it"], expect=0)
        clone = self.t.dir / "mend"                                   # the mend lands on main: a commit that is not the step's
        git_run("clone", "-q", str(self.t.origin), str(clone))
        (clone / "mend.txt").write_text("the mend" + chr(10), encoding="utf-8")
        git_run("add", "-A", cwd=str(clone))
        git_run("commit", "-q", "-m", "the mend", cwd=str(clone))
        git_run("push", "-q", "origin", "HEAD:main", cwd=str(clone))
        code, text = self.t.run(["start", "A.1"])
        self.assertEqual(code, 0, text)
        self.assertEqual(lv.read_state(self.t.live)["A.1"]["state"], "started")
        self.assertEqual(self.on(), "native/A.1")
        self.assertEqual(self.entry()["step"], "C.1", "the step written ahead keeps its entry")
        self.t.git("fetch", "origin", cwd=self.folder)
        self.t.git("rebase", "origin/main", cwd=self.folder)
        self.assertTrue(self.is_ancestor("origin/main", "HEAD"), "the step stands on the mend")
        new = self.rev("HEAD")
        self.assertNotEqual(new, tip)
        self.results = [passed_verdict()]
        self.hooks = [lambda: (self.nested(["ahead"], 0), self.pass_moving_main()())]
        code, text = self.land()
        self.assertEqual(code, 0, text)
        self.assertTrue(self.is_ancestor(new, "native/C.1"), "M's commits were moved onto N's new tip by ahead")
        self.assertEqual(self.subjects("%s..native/C.1" % new), ["C.1: its work"])

    def test_a_kept_N_whose_branch_was_rebased_gets_M_moved_onto_its_new_tip_by_ahead(self):
        old = self.failed_landing()
        self.plant(old)
        self.t.run(["ahead"], expect=0)
        self.m_commit(message="C.1: its work")
        self.unplant()
        lv.add_repair(self.t.live, "github", "fail", "github/windows", evidence="x", note="a job is red")
        code, text = self.t.run(["start", "R-001"])
        self.assertEqual(code, 0, text)
        self.assertIn("kept A.1 on its branch native/A.1", text)
        self.assertEqual(self.entry()["step"], "C.1", "the entry stays")
        self.t.commit("A.1", "R-001: the repair", {"runtime/src/render/r.txt": "r\n"})
        self.results = [passed_verdict()]
        self.hooks = [self.pass_moving_main("native/repair-001")]
        self.assertEqual(self.t.run(["land", "R-001"])[0], 0)
        code, text = self.t.run(["start", "A.1"])
        self.assertEqual(code, 0, text)
        self.assertNotEqual(self.rev("refs/heads/native/A.1"), old, "N was put on the new main")
        self.assertFalse(self.is_ancestor(old, "native/A.1"))
        self.results = [failed_verdict()]
        self.hooks = [lambda: self.nested(["ahead"], 0)]
        self.assertEqual(self.land()[0], 1)
        new = self.rev("refs/heads/native/A.1")
        self.assertEqual(self.on(), "native/C.1")
        self.assertTrue(self.is_ancestor(new, "native/C.1"), "M stands on N's new tip")
        self.assertFalse(self.is_ancestor(old, "native/C.1"), "and not on the old one")
        self.assertEqual(self.subjects("%s..native/C.1" % new), ["C.1: its work"])
        self.assertEqual(self.subjects("native/C.1").count("A.1: the code"), 1)
        self.assertEqual(self.entry()["base"], new)


# ---- a repair is the item in the gate's hands --------------------------------------------------------------------------------

class RepairLandingTest(AheadBase):
    def test_ahead_during_the_landing_of_a_repair_moves_the_commits_of_M_written_before_onto_the_repairs_tip(self):
        ready_a1(self.t)
        self.results = [passed_verdict()]
        self.hooks = [lambda: (self.nested(["ahead"], 0), self.m_commit(), self.pass_moving_main()())]
        self.assertEqual(self.land()[0], 0)
        self.t.git("checkout", "-q", "native/A.2", cwd=self.folder)
        lv.add_repair(self.t.live, "nightly", "fail", "tour/d3d11", renderer="d3d11", evidence="x", note="stop 25 differs")
        self.t.run(["start", "R-001"], expect=0)
        self.t.commit("A.1", "R-001: the repair", {"runtime/src/render/r.txt": "r\n"})
        seen = []

        def play():
            seen.append(self.nested(["ahead"], 0)[1])

        self.results = [failed_verdict()]
        self.hooks = [play]
        self.assertEqual(self.t.run(["land", "R-001"])[0], 1)
        r_tip = self.rev("refs/heads/native/repair-001")
        self.assertEqual(seen[0].splitlines()[:2], ["AHEAD: A.2  Second step", "on %s, the landing of R-001" % r_tip[:9]])
        self.assertTrue(self.is_ancestor(r_tip, "native/A.2"))
        self.assertEqual(self.subjects("%s..native/A.2" % r_tip), ["A.2: its work"])
        e = self.entry()
        self.assertEqual((e["step"], e["after"], e["base"]), ("A.2", "R-001", r_tip))

    def test_ahead_during_a_repair_landing_while_a_step_is_kept_says_that_step_comes_first(self):
        tip = self.failed_landing()
        lv.add_repair(self.t.live, "github", "fail", "github/windows", evidence="x", note="a job is red")
        self.t.run(["start", "R-001"], expect=0)
        self.t.commit("A.1", "R-001: the repair", {"runtime/src/render/r.txt": "r\n"})
        self.results = [failed_verdict()]
        seen = []
        self.hooks = [lambda: seen.append(self.nested(["ahead"]))]
        self.t.run(["land", "R-001"])
        code, text = seen[0]
        self.assertEqual(code, 1)
        self.assertEqual(text.strip(), "nothing can be prepared ahead: step A.1 is kept on its branch and comes first after the repair")
        self.assertFalse(plan.branch_exists(self.folder, "refs/heads/native/A.2"))

    def test_ahead_during_a_repair_landing_with_no_entry_makes_the_branch_of_the_step_that_comes_after_it(self):
        lv.add_repair(self.t.live, "nightly", "fail", "tour/d3d11", renderer="d3d11", evidence="x", note="stop 25 differs")
        self.t.run(["blocked", "A.1", "first"], expect=0)
        self.t.run(["start", "R-001"], expect=0)
        self.t.commit("A.1", "R-001: the repair", {"runtime/src/render/r.txt": "r\n"})
        self.results = [failed_verdict()]
        seen = []
        self.hooks = [lambda: seen.append(self.nested(["ahead"]))]
        self.t.run(["land", "R-001"])
        code, text = seen[0]
        r_tip = self.rev("refs/heads/native/repair-001")
        self.assertEqual(code, 0, text)
        self.assertEqual(text.splitlines()[:2], ["AHEAD: C.1  Free step", "on %s, the landing of R-001" % r_tip[:9]])
        self.assertEqual(self.rev("refs/heads/native/C.1"), r_tip)


# ---- next, status, the page ----------------------------------------------------------------------------------------------------

class NextAndStatusTest(AheadBase):
    def test_next_names_the_step_that_could_be_written_ahead_and_then_the_one_that_is(self):
        tip = self.failed_landing()
        self.plant(tip)
        lines = self.t.run(["next"], expect=0)[1].splitlines()
        self.assertTrue(lines[0].startswith("LANDING RUNNING: A.1"), lines[0])
        self.assertEqual(lines[1], "AHEAD POSSIBLE: A.2 Second step; plan.py ahead")
        self.t.run(["ahead"], expect=0)
        self.m_commit()
        self.m_commit("m2.txt", "m2\n", "A.2: more")
        lines = self.t.run(["next"], expect=0)[1].splitlines()
        self.assertEqual(lines[1], "AHEAD: A.2  Second step (branch native/A.2, 2 commits)")

    def test_next_says_nothing_when_no_step_can_follow_and_nothing_when_no_landing_runs(self):
        write_steps(self.t, (("A.1", "A", "", "open"),))
        tip = self.failed_landing()
        self.plant(tip)
        text = self.t.run(["next"], expect=0)[1]
        self.assertNotIn("AHEAD", text)
        self.unplant()
        write_steps(self.t, (("A.1", "A", "", "open"), ("A.2", "A", "", "open")))
        self.assertNotIn("AHEAD", self.t.run(["next"], expect=0)[1], "no landing is running")

    def test_next_gives_the_prepared_step_with_the_line_under_its_head(self):
        ready_a1(self.t)
        self.results = [passed_verdict()]
        self.hooks = [lambda: (self.nested(["ahead"], 0), self.m_commit(), self.pass_moving_main()())]
        self.land()
        lines = self.t.run(["next"], expect=0)[1].splitlines()
        self.assertEqual(lines[0], "NEXT: A.2  Second step")
        self.assertEqual(lines[1], "PREPARED AHEAD: branch native/A.2, 1 commits; plan.py start A.2 takes it up")

    def test_status_has_one_line_for_a_step_written_ahead(self):
        tip = self.failed_landing()
        self.plant(tip)
        self.assertNotIn("Written ahead", self.t.run(["status"], expect=0)[1])
        self.t.run(["ahead"], expect=0)
        self.m_commit()
        text = self.t.run(["status"], expect=0)[1]
        self.assertIn("## Written ahead (1)\n\n- A.2 Second step: branch native/A.2, 1 commits, on top of A.1 (base %s); plan.py start A.2 "
                      "takes it up once A.1 has landed\n" % tip[:9], text)

    def test_status_shows_no_second_agent_that_never_claimed_anything(self):
        text = self.t.run(["status"], expect=0)[1]
        self.assertNotIn("agent 2", text)
        self.assertIn("## Next (agent 1)", text)


# ---- the whole loop ---------------------------------------------------------------------------------------------------------

class WholeLoopTest(AheadBase):
    def test_start_N_land_N_ahead_a_commit_on_M_the_gate_passes_start_M_land_M(self):
        ready_a1(self.t)
        n_code = self.rev("HEAD")
        self.results = [passed_verdict("1111111111")]
        out = []

        def while_it_plays():
            out.append(self.nested(["ahead"], 0)[1])
            self.t.write_ledger([ledger_row("P-001", "fixed", "tests/render/a_test 1-9 fails before"),
                                 ledger_row("P-002", note="advanced 11-20: half done"),
                                 ledger_row("P-003", "fixed", "tests/render/b_test 1-9 fails before"), ledger_row("P-004")],
                                tree=self.folder)
            self.m_commit()
            self.pass_moving_main()()

        self.hooks = [while_it_plays]
        code, text = self.land()
        self.assertEqual(code, 0, text)
        self.assertIn("LANDED A.1 as", text)
        self.assertEqual(self.on(), "native/A.2")
        self.assertEqual(self.t.run(["start", "A.2"], expect=0)[1].splitlines()[0].split(":")[0], "started A.2")
        self.results = [passed_verdict("2222222222")]
        self.hooks = [self.pass_moving_main("native/A.2")]
        code, text = self.land_m()
        self.assertEqual(code, 0, text)
        main = plan.parse_base(self.t.git("show", "origin/main:docs/plan/native/steps.tsv", cwd=self.folder), "origin/main")
        row = lambda sid: plan.find(main, sid)
        self.assertEqual((row("A.1")["state"], row("A.2")["state"]), ("landed", "landed"))
        self.assertEqual(row("A.1")["commit"], n_code[:9], "A.1's code commit")
        m_code = self.t.git("log", "--format=%h", "--abbrev=9", "-1", "--skip=1", "origin/main", cwd=self.folder)
        self.assertEqual(row("A.2")["commit"], m_code, "A.2's code commit")
        subjects = self.subjects("origin/main")
        order = [subjects.index(s) for s in ("Plan: step A.2 landed", "A.2: its work", "Plan: step A.1 landed", "A.1: the code")]
        self.assertEqual(order, sorted(order), "in order: A.1 before A.2 on main")

    def land_m(self):
        return self.t.run(["land", "A.2"])


# ---- the four lines that told the agent to do what its rules forbid -------------------------------------------------------------

class AdviceLinesTest(AheadBase):
    def test_tool_says_nothing_is_blocked_by_it(self):
        code, text = self.t.run(["tool", "the runner broke"])
        self.assertEqual(code, 0)
        self.assertIn("Nothing is blocked by it: go on as your rules say; do not debug the tool.", text)
        self.assertNotIn("Take the next step", text)

    def test_a_not_judged_landing_of_the_gate_says_to_give_the_same_command_again(self):
        text = (TREE / "tools" / "gate" / "landing.py").read_text(encoding="utf-8")
        self.assertIn("Fix what the line above says, then give the same landing command again.", text)
        self.assertNotIn("then run gate.py land again", text)


def test_arguments(argv):
    """What unittest is given: the gate starts every self-test with `--work <folder>`, which is none of unittest's; the rest (-k, names)
    is kept so that one test can be run alone."""
    out, skip = [argv[0]], False
    for arg in argv[1:]:
        if skip:
            skip = False
        elif arg == "--work":
            skip = True
        else:
            out.append(arg)
    return out


class ArgumentsTest(unittest.TestCase):
    def test_the_gates_work_option_is_not_given_to_unittest(self):
        self.assertEqual(test_arguments(["t.py", "--work", "D:/x", "-k", "a"]), ["t.py", "-k", "a"])
        self.assertEqual(test_arguments(["t.py"]), ["t.py"])


if __name__ == "__main__":
    result = unittest.main(argv=test_arguments(sys.argv), exit=False, verbosity=1).result
    print("plan_ahead_test: %s (%d tests)" % ("pass" if result.wasSuccessful() else "FAIL", result.testsRun))
    sys.exit(0 if result.wasSuccessful() else 1)
