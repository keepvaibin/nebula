"""plan_mend_scope_test.py: an open mend holds only the steps whose files it touches (mend M-016, the owner's order of 11-28; docs/plan/native/AGENT.md
section 9), on the temporary git tree of plan_fixture.

    python -X utf8 -I tools/gate/tests/plan_mend_scope_test.py [--work FOLDER]

No game, no build, no network; the landing is a fake; nothing of D:/NebulaWork/plan is touched. Each test was seen to fail with the thing it
guards broken (named in a phrase after the test).
  clash_*                                 the rule on paths: the same file, a folder, a bare name, one tool's folder, the self-test of a tool file
    clash_is_paths_and_prefixes_and_one_tool_folder   a prefix, a file, the tests of a tool's file or two files of one tool folder did not meet;
                                          files of two tool folders, or of crates/ against tools/, met
  a_step_that_touches_a_file_of_the_mend_waits   `next`, `start`, `land` or `stack` gave a held step, or `ahead` wrote it
  a_step_that_touches_none_goes_on          `next`, `start`, `stack` or `land` of a step that touches none of the mend's files answered WAIT
  an_empty_or_unreadable_file_list_holds_every_step   a mend whose sheet names no file (or has no sheet) let a step go on
  a_prefix_holds_the_steps_that_change_a_file_under_it   a mend naming `tools/gate/` let a step that changes tools/gate/x.py land, or held one that changes crates/
  the_helpers_branch_is_read   the files `mend/M-00N` changes were not counted (the sheet's list empty)
  both_agents                             the second agent's `start` or `next` was held by a mend that touches none of its files, or let through one that does
  a_repair_is_held                        a repair went on while a mend was open
  the_mend_closes_as_before_and_a_second_one_is_refused   `mend close` or a second `mend open` changed
  the_gates_work_option_is_not_given_to_unittest   the gate's `--work FOLDER` reached unittest, which refused it (the first landing of this mend)
"""
import pathlib
import sys
import unittest
from unittest import mock

HERE = pathlib.Path(__file__).resolve().parent
TREE = HERE.parents[2]
sys.path.insert(0, str(HERE))
sys.path.insert(0, str(TREE / "tools" / "plan"))
import live as lv  # noqa: E402
import plan  # noqa: E402
from plan_fixture import Tree, moving_land, setup_module, teardown_module  # noqa: E402


def setUpModule():
    setup_module()


def tearDownModule():
    teardown_module()


CARDS = """# Cards

## S Package S

### S.1 First step

- **Where.** {s1}
- **Rows.** none of this package's rows.

### S.2 Second step

- **Where.** {s2}
- **Rows.** none of this package's rows.

### S.3 Third step

{s3}- **Rows.** none of this package's rows.

### Rows of this package

| Row | Today |
| --- | --- |
| P-001 | one |

---

## G6 Package G6

### G6.1 Helper step

- **Where.** {g1}
- **Rows.** none of this package's rows.

### G6.2 Helper second

- **Where.** {g2}
- **Rows.** none of this package's rows.

### Rows of this package

| Row | Today |
| --- | --- |
| P-004 | four |

---

## T Package T

### T.1 Free step

- **Where.** {t1}
- **Rows.** none of this package's rows.

### Rows of this package

| Row | Today |
| --- | --- |
| P-005 | five |
"""
STEPS = [("S.1", "S"), ("S.2", "S"), ("S.3", "S"), ("G6.1", "G6"), ("G6.2", "G6"), ("T.1", "T")]
RENDER, OTHER = "runtime/src/render/a.cpp", "runtime/src/other/b.cpp"


class Base(unittest.TestCase):
    def setUp(self):
        self.t = Tree().__enter__()
        self.addCleanup(self.t.__exit__)
        for patcher in (mock.patch.object(plan, "LAND", moving_land()), ):
            patcher.start()
            self.addCleanup(patcher.stop)

    def write_plan(self, s1=RENDER, s2=OTHER, s3=None, g1="tools/measure/m.py", g2=RENDER, t1=OTHER):
        """Six steps (S.1 to S.3, G6.1, G6.2 and T.1) whose cards' Where lines name these paths (`s3` None: no Where line), pushed to main."""
        n = plan.native(self.t.tree)
        rows = [(str(i), sid, pkg, "Title of " + sid, "", "03-X.md", "open", "") for i, (sid, pkg) in enumerate(STEPS, 1)]
        (n / "steps.tsv").write_text("\t".join(plan.STEP_COLUMNS) + "\n" + "".join("\t".join(r) + "\n" for r in rows), encoding="utf-8")
        where = lambda *paths: ", ".join("`%s`" % p for p in paths)   # noqa: E731
        (n / "03-X.md").write_text(CARDS.format(s1=where(s1), s2=where(s2), g1=where(g1), g2=where(g2), t1=where(t1),
                                                s3="- **Where.** %s\n" % where(s3) if s3 else ""), encoding="utf-8")
        (self.t.tree / "tools" / "measure").mkdir(parents=True, exist_ok=True)
        (self.t.tree / "tools" / "measure" / "m.py").write_text("m\n", encoding="utf-8")
        (self.t.tree / "tools" / "gate").mkdir(parents=True, exist_ok=True)
        (self.t.tree / "tools" / "gate" / "pace.py").write_text("p\n", encoding="utf-8")
        (self.t.tree / "runtime" / "src" / "other").mkdir(parents=True, exist_ok=True)
        (self.t.tree / "runtime" / "src" / "other" / "b.cpp").write_text("b\n", encoding="utf-8")
        self.t.push_plan_files("plan")

    def open_mend(self, files, step=None):
        """A mend whose sheet names `files` (None: the sheet is as `mend open` made it, with no file)."""
        self.t.run(["mend", "open", *([step] if step else []), "a fault"], expect=0)
        if files is not None:
            sheet = lv.sheet_path(self.t.live, "M-001")
            body = sheet.read_text(encoding="utf-8")
            heading = "## The files I believe are at fault\n\n(path and function, or `none known`)"
            self.assertIn(heading, body)
            sheet.write_text(body.replace(heading, heading.replace("(path and function, or `none known`)", files)), encoding="utf-8")

    def waits(self, args):
        code, text = self.t.run(args)
        self.assertEqual(code, 1 if args[0] != "next" else 0, (args, text))
        self.assertIn("WAIT: mend M-001 is open: a fault", text, args)
        return text

    def goes(self, args):
        code, text = self.t.run(args)
        self.assertNotIn("WAIT: mend", text, (args, text))
        self.assertEqual(code, 0, (args, text))
        return text


class ClashTest(unittest.TestCase):
    def test_clash_is_paths_and_prefixes_and_one_tool_folder(self):
        c = plan.mend_clash
        self.assertEqual(c(["tools/gate/pace.py"], ["tools/gate/pace.py"]), ["tools/gate/pace.py"], "the same file")
        self.assertEqual(c(["tools/gate"], ["tools/gate/x.py"]), ["tools/gate"], "a folder holds the file")
        self.assertEqual(c(["tools/gate/x.py"], ["tools/gate"]), ["tools/gate/x.py"], "a card names the folder")
        self.assertEqual(c(["pace.py"], ["tools/gate/pace.py"]), ["pace.py"], "the bare name")
        self.assertEqual(c(["tools/gate/pace.py"], ["tools/gate/tests/pace_test.py"]), ["tools/gate/pace.py"], "the self-test of the file")
        self.assertEqual(c(["tools/gate/pace.py"], ["tools/gate/other.py"]), ["tools/gate/pace.py"], "one tool's folder")
        self.assertEqual(c(["tools/plan/plan.py"], ["tools/gate/tests/plan_mend_test.py"]), ["tools/plan/plan.py"], "a self-test named after the file")
        self.assertEqual(c(["tools/gate/pace.py"], ["tools/measure/m.py"]), [], "another tool's folder")
        self.assertEqual(c(["tools/gate/pace.py"], ["crates/x/lib.rs"]), [])
        self.assertEqual(c(["tools/gate/pace.py"], ["tools/gate/tests/other_test.py", "docs/plan/ledger.tsv"]), ["tools/gate/pace.py"])
        self.assertEqual(c(["docs/plan/native/AGENT.md"], ["docs/plan/ledger.tsv"]), [], "docs: the file only, never the folder's other files")
        self.assertEqual(c(["tools/gate/pace.py"], ["tools/gate/pacemaker.py"]), ["tools/gate/pace.py"], "same folder")
        self.assertEqual(c(["tools/foo.py"], ["tools/bar.py"]), [], "a file directly in tools/ is no folder")


class ScopeTest(Base):
    def test_a_step_that_touches_a_file_of_the_mend_waits(self):
        self.write_plan()
        self.open_mend("`%s`" % RENDER)
        text = self.waits(["next"])
        self.assertIn("The mend touches %s, which step S.1 touches" % RENDER, text)
        self.assertNotIn("=== COMMANDS", text)
        self.assertIn("mend close M-001", text)
        self.assertIn("The mend touches %s, which step S.1 touches" % RENDER, self.waits(["start", "S.1"]))
        self.assertEqual(plan.find(plan.read_steps(self.t.tree, self.t.live), "S.1")["state"], "open")

    def test_a_step_that_touches_none_goes_on(self):
        self.write_plan(s1=OTHER, s2="tools/measure/m.py")
        self.open_mend("`%s`" % RENDER)
        text = self.goes(["next"])
        self.assertIn("Mend M-001 is open: S.1 touches none of its files, so it goes on", text)
        self.assertIn("NEXT: S.1", text)
        self.goes(["start", "S.1"])
        self.t.commit("S.1", "S.1: work", {OTHER: "b\nc\n"})
        text = self.goes(["land", "S.1"])
        self.assertEqual(plan.find(plan.read_steps(self.t.tree, self.t.live), "S.1")["state"], "landed", text)

    def test_land_reads_the_files_the_commits_change(self):
        """The card names a file the mend does not touch; the commits change one it does: `land` holds it."""
        self.write_plan(s1=OTHER)
        self.open_mend("`%s`" % RENDER)
        self.goes(["start", "S.1"])
        self.t.commit("S.1", "S.1: work", {RENDER: "a\nb\nc\nd\n"})
        self.waits(["land", "S.1"])
        self.assertEqual(plan.find(plan.read_steps(self.t.tree, self.t.live), "S.1")["state"], "started")

    def test_a_step_in_work_that_touches_the_mend_is_held_in_next_and_the_free_one_after_it_is_not(self):
        self.write_plan()
        self.goes(["start", "S.1"])
        self.open_mend("`%s`" % RENDER, step="S.1")
        text = self.waits(["next"])
        self.assertIn("The mend touches %s, which step S.1 touches" % RENDER, text)
        self.waits(["land", "S.1"])

    def test_stack_holds_a_step_that_touches_the_mend(self):
        self.write_plan(s1=OTHER, s2=RENDER, s3="tools/measure/m.py")
        self.goes(["start", "S.1"])
        self.t.commit("S.1", "S.1: work", {OTHER: "bcd"})
        self.open_mend("`%s`" % RENDER, step="S.1")
        text = self.waits(["stack"])                                  # S.2 touches the mend's file
        self.assertIn("The mend touches %s, which step S.2 touches" % RENDER, text)
        self.assertFalse(plan.branch_exists(self.t.folder(), "refs/heads/native/S.2"), "nothing was written")

    def test_stack_writes_a_step_that_touches_none(self):
        self.write_plan(s1=OTHER, s2="tools/measure/m.py", s3=RENDER)
        self.goes(["start", "S.1"])
        self.t.commit("S.1", "S.1: work", {OTHER: "bcd"})
        self.open_mend("`%s`" % RENDER, step="S.1")
        text = self.goes(["stack"])
        self.assertIn("STACKED: S.2", text)
        self.assertTrue(plan.branch_exists(self.t.folder(), "refs/heads/native/S.2"))

    def test_an_empty_or_unreadable_file_list_holds_every_step(self):
        self.write_plan(s1=OTHER, s2="tools/measure/m.py")
        self.open_mend(None)                                          # the sheet as `mend open` made it: no file named
        for args in (["next"], ["start", "S.1"], ["start", "S.2"], ["land", "S.1"], ["stack"]):
            text = self.waits(args)
            self.assertIn("names no file yet", text, args)
        lv.sheet_path(self.t.live, "M-001").unlink()                  # no sheet at all
        for args in (["next"], ["start", "S.2"], ["land", "S.1"]):
            self.assertIn("names no file yet", self.waits(args), args)

    def test_a_prefix_holds_the_steps_that_change_a_file_under_it(self):
        self.write_plan(s1="runtime/src/other", s2="tools/measure/m.py")
        self.open_mend("`tools/gate/`")
        self.goes(["start", "S.1"])
        self.t.commit("S.1", "S.1: work", {"crates/x/lib.rs": "x\n"})
        self.goes(["land", "S.1"])                                    # crates/ against tools/gate/: goes on
        self.goes(["start", "S.2"])
        self.t.commit("S.2", "S.2: work", {"tools/gate/x.py": "x\n"})
        self.waits(["land", "S.2"])                                   # tools/gate/x.py: held
        self.t.git("reset", "-q", "--hard", "HEAD~1", cwd=self.t.folder())
        self.t.commit("S.2", "S.2: its tests", {"tools/gate/tests/pace_test.py": "x\n"})
        self.assertIn("tools/gate", self.waits(["land", "S.2"]))

    def test_a_mend_that_names_a_tool_file_holds_its_selftest_and_its_folder(self):
        self.write_plan(s1="tools/gate/pace.py", s2="tools/measure/m.py")
        self.open_mend("`tools/gate/pace.py`")
        self.waits(["start", "S.1"])
        self.goes(["start", "T.1"])
        self.t.commit("T.1", "T.1: work", {"tools/gate/tests/pace_test.py": "x\n"})
        self.waits(["land", "T.1"])

    def test_the_helpers_branch_is_read(self):
        """The sheet names nothing; the commit of mend/M-001 changes a file: only the steps that touch that file are held."""
        self.write_plan(s1=OTHER, s2=RENDER)
        self.open_mend(None)
        self.t.git("checkout", "-q", "-b", "mend/M-001", "origin/main")
        (self.t.tree / "runtime" / "src" / "render" / "a.cpp").write_text("a\nb\nc\nmended\n", encoding="utf-8")
        self.t.git("commit", "-q", "-am", "the mend")
        self.t.git("checkout", "-q", "main")
        self.goes(["start", "S.1"])
        self.t.run(["blocked", "S.1", "to free the folder"], expect=0)
        self.waits(["start", "S.2"])

    def test_both_agents(self):
        self.write_plan()
        self.open_mend("`%s`" % RENDER)
        self.goes(["start", "G6.1", "--agent", "2"])                  # tools/measure/m.py: goes on
        self.waits(["start", "G6.2", "--agent", "2"])                 # RENDER: held
        self.waits(["start", "S.1"])                                  # agent 1, RENDER: held
        self.goes(["start", "T.1"])                                   # agent 1, OTHER: goes on

    def test_next_of_the_second_agent(self):
        self.write_plan(g1=RENDER, g2="tools/measure/m.py")
        self.open_mend("`%s`" % RENDER)
        self.assertIn("The mend touches %s, which step G6.1 touches" % RENDER, self.waits(["next", "--agent", "2"]))
        self.write_plan(g1="tools/measure/m.py")
        text = self.goes(["next", "--agent", "2"])
        self.assertIn("NEXT: G6.1", text)

    def test_a_repair_is_held(self):
        self.write_plan(s1=OTHER, s2="tools/measure/m.py")
        lv.add_repair(self.t.live, "nightly", "fail", "build/windows-asan", evidence="x", note="the build refuses the folder")
        self.open_mend("`%s`" % RENDER)
        text = self.waits(["start", "R-001"])
        self.assertIn("R-001 is a repair", text)

    def test_the_mend_closes_as_before_and_a_second_one_is_refused(self):
        self.write_plan(s1=OTHER)
        self.open_mend("`%s`" % RENDER)
        code, text = self.t.run(["mend", "open", "another fault"])
        self.assertEqual(code, 1)
        self.assertIn("mend M-001 is open", text)
        (self.t.tree / "tools" / "verify" / "doc_refs_allow.txt").write_text("# a mend\n", encoding="utf-8")
        self.t.push_plan_files("mend")
        sha = self.t.git("rev-parse", "HEAD")
        self.t.run(["mend", "close", "M-001", sha], expect=0)
        text = self.goes(["next"])
        self.assertNotIn("Mend M-001 is open", text)


def test_names(args):
    """The test names asked for on the command line; the harness's own option --work FOLDER (the gate starts every self-test with it) is not one."""
    out, skip = [], False
    for a in args:
        if skip:
            skip = False
        elif a == "--work":
            skip = True
        elif not a.startswith("--work="):
            out.append(a)
    return out


class RunnerTest(unittest.TestCase):
    def test_the_gates_work_option_is_not_given_to_unittest(self):
        self.assertEqual(test_names(["--work", "D:/x", "ScopeTest"]), ["ScopeTest"])
        self.assertEqual(test_names(["--work=D:/x"]), [])
        self.assertEqual(test_names(["-k", "a"]), ["-k", "a"])


def main():
    result = unittest.main(argv=[sys.argv[0]] + test_names(sys.argv[1:]), exit=False, verbosity=1).result
    print("plan_mend_scope_test: %s (%d tests)" % ("pass" if result.wasSuccessful() else "FAIL", result.testsRun))
    return 0 if result.wasSuccessful() else 1


if __name__ == "__main__":
    sys.exit(main())
