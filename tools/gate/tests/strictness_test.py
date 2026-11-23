"""strictness_test.py: tools/gate/strictness.py on small git repositories whose changes are known (brief U, section 7).

    python -X utf8 -I tools/gate/tests/strictness_test.py [--work FOLDER]

Each test was seen to fail with the thing it guards broken (named in a phrase after the test).
  a_line_added_to_a_known_list_needs_its_log_line    the list line landed with no log line; a log line for another file counted
  a_changed_line_of_an_existing_test_needs_its_line  an expected value changed in an existing self-test with no line; an addition was refused
  additions_to_tests_are_free                        a new test function, a new test file
  a_deleted_test_file_is_a_change                    a self-test file deleted
  a_block_is_judged_by_its_own_lines                 a constant of kinds.py changed (refused); a line elsewhere in the file (free); one
                                                     element added to a multi-line tuple (refused)
  the_log_only_grows                                 a removed or changed line of the log; a line with six cells
  white_space_is_not_a_change                        a reformatted line; a blank line added
  a_mend_branch_may_not_change_game_source           runtime/ source in a mend/* branch (refused); CMake, a script, a tool (free); the same
                                                     file in a native/* branch (not this rule's)
  a_comma_after_the_last_name_is_not_a_removal       a test added after the last, so the old last line gains a comma (free); the last,
                                                     a middle and a changed name taken out (refused); a comma beside a real removal
                                                     (the removal counts); seen failing with the comma filter off, and with it free for all
  the_guard_guards_itself                            a line taken out of strictness.tsv; a change of strictness.py
  the_real_table                                     every row parses; every block names a place that exists; every named file is in a
                                                     scope row that runs `repo` (so the check runs where it must)
  check_branches_judges_each_branch_by_its_tip       a covered branch judged from its own commits, not from HEAD
"""
import os
import pathlib
import shutil
import subprocess
import sys
import tempfile
import unittest
from unittest import mock

HERE = pathlib.Path(__file__).resolve().parent
TREE = HERE.parents[2]
sys.path.insert(0, str(TREE / "tools" / "gate"))
import scope  # noqa: E402
import strictness  # noqa: E402

GIT = strictness.GIT if pathlib.Path(strictness.GIT).exists() else "git"
LOG_HEAD = "\t".join(strictness.LOG_COLUMNS) + "\n"
KINDS = ("FLAKE_DAYS = 7\nFLAKES_TO_SET_ASIDE = 3\nSLOW = 80\n\ndef f():\n    return 1\n")
LONG_RESULTS = ('RENDERERS = ("a",)\nNEVER_TRACED = ("build/windows-asan", "unit/game-asan",\n                "soak", "measure/*")\n'
                'TRACEABLE = ("engine/*",)\n')


def log_line(file, date="2026-11-22"):
    return "\t".join([date, file, "what", "why", "doc 1", "M-001", "-"]) + "\n"


class Repo:
    def __init__(self):
        self.dir = pathlib.Path(tempfile.mkdtemp(prefix="strictness-")).resolve()
        self.config = self.dir / "gitconfig"
        self.config.write_text("", encoding="utf-8")
        self.root = self.dir / "r"
        self.root.mkdir()
        self.env = mock.patch.dict(os.environ, {"GIT_CONFIG_GLOBAL": str(self.config), "GIT_CONFIG_NOSYSTEM": "1", "GIT_AUTHOR_NAME": "t",
                                                "GIT_AUTHOR_EMAIL": "t@example.com", "GIT_COMMITTER_NAME": "t", "GIT_COMMITTER_EMAIL": "t@example.com"})
        self.env.start()
        self.git("init", "-q", "-b", "main")
        self.write({"tools/gate/known-failures.tsv": "check\ttest\tledger row\tfirst seen\twhat\n",
                    "tools/gate/tests/foo_test.py": "def test():\n    assert value == 60\n",
                    "tools/gate/kinds.py": KINDS, "tools/gate/long_results.py": LONG_RESULTS,
                    "docs/plan/strictness-log.tsv": LOG_HEAD + log_line("tools/verify/console_census.py"),
                    "tools/gate/strictness.tsv": (TREE / "tools" / "gate" / "strictness.tsv").read_text(encoding="utf-8"),
                    "tools/gate/strictness.py": "# the check\n", "runtime/src/a.cpp": "a\n", "CMakeLists.txt": "# build\n",
                    "native/CMakeLists.txt": "# build\n", "tools/other.py": "x = 1\n"})
        self.git("add", "-A")
        self.git("commit", "-q", "-m", "start")
        self.git("update-ref", "refs/remotes/origin/main", "HEAD")

    def git(self, *args):
        r = subprocess.run([GIT, "-C", str(self.root), *args], capture_output=True, text=True, encoding="utf-8")
        assert r.returncode == 0, (args, r.stderr)
        return r.stdout.strip()

    def write(self, files):
        for name, text in files.items():
            path = self.root / name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text(text, encoding="utf-8", newline="\n")

    def commit(self, files, branch=None):
        if branch:
            self.git("checkout", "-q", "-B", branch, "origin/main")
        self.write(files)
        self.git("add", "-A")
        self.git("commit", "-q", "-m", "change")

    def delete(self, name):
        self.git("rm", "-q", name)
        self.git("commit", "-q", "-m", "delete")

    def problems(self, branch=""):
        return strictness.problems_for(self.root, "origin/main", "HEAD", branch, rows=strictness.read_table())

    def close(self):
        self.env.stop()
        def unlock(func, p, _exc):
            os.chmod(p, 0o700)
            func(p)
        shutil.rmtree(self.dir, onerror=unlock)


class StrictnessTest(unittest.TestCase):
    def setUp(self):
        self.r = Repo()
        self.addCleanup(self.r.close)

    def read(self, name):
        return (self.r.root / name).read_text(encoding="utf-8")

    def test_a_line_added_to_a_known_list_needs_its_log_line(self):
        r = self.r
        r.commit({"tools/gate/known-failures.tsv": self.read("tools/gate/known-failures.tsv") + "unit/windows\tx\tP-001\tabc\tbroken\n"})
        lines = r.problems()
        self.assertTrue(lines and "tools/gate/known-failures.tsv" in lines[0], lines)
        self.assertIn("strictness-log.tsv", " ".join(lines))
        # a line for another file does not cover it
        r.commit({"docs/plan/strictness-log.tsv": self.read("docs/plan/strictness-log.tsv") + log_line("tools/gate/planned.tsv")})
        self.assertTrue(r.problems(), "the log line names another file")
        r.commit({"docs/plan/strictness-log.tsv": self.read("docs/plan/strictness-log.tsv") + log_line("tools/gate/known-failures.tsv")})
        self.assertEqual(r.problems(), [])

    def test_a_removed_line_of_a_list_that_only_adds_is_free(self):
        r = self.r
        r.commit({"tools/gate/known-failures.tsv": "check\ttest\tledger row\tfirst seen\twhat\nunit/windows\tx\tP-001\tabc\tbroken\n"})
        r.git("update-ref", "refs/remotes/origin/main", "HEAD")
        r.commit({"tools/gate/known-failures.tsv": "check\ttest\tledger row\tfirst seen\twhat\n"})
        self.assertEqual(r.problems(), [], "a fixed fault takes its line out: the check sees more")

    def test_a_changed_line_of_an_existing_test_needs_its_line(self):
        r = self.r
        r.commit({"tools/gate/tests/foo_test.py": "def test():\n    assert value == 61\n"})
        lines = r.problems()
        self.assertTrue(lines and "tools/gate/tests/foo_test.py" in lines[0], lines)
        self.assertIn("- 2: assert value == 60", " ".join(lines))
        r.commit({"docs/plan/strictness-log.tsv": self.read("docs/plan/strictness-log.tsv") + log_line("tools/gate/tests/foo_test.py")})
        self.assertEqual(r.problems(), [])

    def test_additions_to_tests_are_free(self):
        r = self.r
        r.commit({"tools/gate/tests/foo_test.py": "def test():\n    assert value == 60\n\ndef test_more():\n    assert other == 1\n",
                  "tools/gate/tests/new_test.py": "def test():\n    pass\n"})
        self.assertEqual(r.problems(), [])

    def test_a_deleted_test_file_is_a_change(self):
        r = self.r
        r.git("checkout", "-q", "-B", "work", "origin/main")
        r.delete("tools/gate/tests/foo_test.py")
        lines = r.problems()
        self.assertTrue(lines and "tools/gate/tests/foo_test.py" in lines[0], lines)

    def test_a_block_is_judged_by_its_own_lines(self):
        r = self.r
        r.commit({"tools/gate/kinds.py": KINDS.replace("FLAKES_TO_SET_ASIDE = 3", "FLAKES_TO_SET_ASIDE = 5")})
        self.assertTrue(r.problems(), "the constant that sets a test aside")
        r.commit({"tools/gate/kinds.py": KINDS.replace("SLOW = 80", "SLOW = 70").replace("return 1", "return 2")}, branch="b2")
        self.assertEqual(r.problems(), [], "a line outside the blocks is free")
        r.commit({"tools/gate/long_results.py": LONG_RESULTS.replace('"soak", "measure/*")', '"soak", "measure/*", "tablet/*")')}, branch="b3")
        lines = r.problems()
        self.assertTrue(lines and "tools/gate/long_results.py" in lines[0], "an element added in the second line of the tuple: %s" % lines)
        r.commit({"tools/gate/long_results.py": LONG_RESULTS.replace('TRACEABLE = ("engine/*",)', 'TRACEABLE = ("engine/*", "tour/*")')}, branch="b4")
        self.assertEqual(r.problems(), [], "another name of the same file")

    def test_the_log_only_grows(self):
        r = self.r
        r.commit({"docs/plan/strictness-log.tsv": LOG_HEAD})
        lines = r.problems()
        self.assertTrue(lines and "The log only grows" in lines[0], lines)
        r.commit({"docs/plan/strictness-log.tsv": self.read("docs/plan/strictness-log.tsv") + "2026-11-22\ttools/x.py\tonly\tfour\tcells\n"},
                 branch="b2")
        lines = r.problems()
        self.assertTrue(any("cells" in l for l in lines), lines)
        r.commit({"docs/plan/strictness-log.tsv": self.read("docs/plan/strictness-log.tsv") + log_line("tools/other.py", date="yesterday")},
                 branch="b3")
        self.assertTrue(any("YYYY-MM-DD" in l for l in r.problems()))

    def test_white_space_is_not_a_change(self):
        r = self.r
        r.commit({"tools/gate/tests/foo_test.py": "def test():\n\n    assert   value  ==  60\n\n"})
        self.assertEqual(r.problems(), [])

    def test_a_mend_branch_may_not_change_game_source(self):
        r = self.r
        r.commit({"runtime/src/a.cpp": "a\nb\n"}, branch="mend/M-001")
        lines = r.problems("mend/M-001")
        self.assertTrue(lines and "runtime/src/a.cpp" in lines[0], lines)
        self.assertEqual(r.problems("native/S0.1.1"), [], "the rule is the mend branches'")
        r.commit({"native/CMakeLists.txt": "# build 2\n", "CMakeLists.txt": "# b\n", "tools/other.py": "x = 2\n",
                  "platform/android/build_apk.ps1": "# script\n", "crates/x/Cargo.toml": "[package]\n"}, branch="mend/M-002")
        self.assertEqual(r.problems("mend/M-002"), [], "build files and tools are a mend's")
        r.commit({"platform/sdl/host.cpp": "x\n"}, branch="mend/M-003")
        self.assertTrue(r.problems("mend/M-003"))

    def test_a_step_with_pictures_change_adds_no_line_for_the_approved_pictures(self):
        r = self.r
        r.commit({"PICTURES-CHANGE.txt": "tour/010-CannonFleetGalaxy-1 * all :: the sky is another blue\n"})
        self.assertEqual(r.problems("native/S0.2.1"), [], "the declaration is the step's; the gate approves the pictures itself, after its checks")
        r.commit({"tests/pictures/approved.tsv": "# a row put in by hand\n"}, branch="b2")
        lines = r.problems("native/S0.2.1")
        self.assertTrue(lines and "tests/pictures/approved.tsv" in lines[0], "a hand-made change of the approved pictures needs its line: %s" % lines)

    SUITE = '{\n  "harness": 1,\n  "tests": [\n    "boot",\n    "start",\n    "end"\n  ]\n}\n'

    def with_suite(self):
        r = self.r
        r.write({"tests/engine/suite.json": self.SUITE})
        r.git("add", "-A")
        r.git("commit", "-q", "-m", "base with a suite")
        r.git("update-ref", "refs/remotes/origin/main", "HEAD")

    def test_a_comma_after_the_last_name_is_not_a_removal(self):
        r = self.r
        self.with_suite()
        # A test added after the last: the old last line gains a comma. The name is still in the list.
        r.commit({"tests/engine/suite.json": self.SUITE.replace('    "end"\n', '    "end",\n    "new"\n')})
        self.assertEqual(r.problems(), [], "the comma is no removed test")
        # The last test taken out: the comma of the one before goes too; the removed name is refused.
        r.commit({"tests/engine/suite.json": self.SUITE.replace('    "start",\n    "end"\n', '    "start"\n')}, branch="b2")
        lines = r.problems()
        self.assertTrue(lines and "tests/engine/suite.json" in lines[0] and "1 removed line" in lines[0], lines)
        self.assertIn('"end"', " ".join(lines), "the removed name is the one named")
        # A test taken out of the middle, and one changed to another name.
        r.commit({"tests/engine/suite.json": self.SUITE.replace('    "start",\n', "")}, branch="b3")
        self.assertTrue(r.problems(), "a name removed from the middle")
        r.commit({"tests/engine/suite.json": self.SUITE.replace('    "start",', '    "stop",')}, branch="b4")
        self.assertTrue(r.problems(), "a name changed to another")
        # A comma changed on one line while another name is taken out in the same hunk: only the taken name counts.
        r.commit({"tests/engine/suite.json": self.SUITE.replace('    "boot",\n    "start",\n    "end"\n',
                                                                '    "boot"\n    "end",\n    "z"\n')}, branch="b5")
        lines = r.problems()
        self.assertTrue(lines and "removed line" in lines[0] and '"start"' in " ".join(lines), lines)

    def test_the_guard_guards_itself(self):
        r = self.r
        r.commit({"tools/gate/strictness.tsv": "pattern\tmode\twhere\thides\n"})
        self.assertTrue(r.problems(), "a line taken out of the table")
        r.commit({"tools/gate/strictness.py": "# the check, weaker\n"}, branch="b2")
        self.assertTrue(r.problems(), "any change of the check")

    def test_the_real_table(self):
        rows = strictness.read_table()
        self.assertGreater(len(rows), 20)
        table = scope.read_table()
        for row in rows:
            pattern = row["pattern"]
            if not any(c in pattern for c in "*?"):
                path = TREE / pattern
                self.assertTrue(path.is_file(), "%s is a file of the tree" % pattern)
                groups = scope.plan_for([pattern], table).groups
                self.assertIn("repo", groups, "%s is in a scope row that runs the repository checks" % pattern)
            if row["where"].startswith("block:"):
                names = row["where"][len("block:"):].split(",")
                found = strictness.block_lines((TREE / pattern).read_text(encoding="utf-8"), names)
                self.assertTrue(found, "%s names a block that exists: %s" % (pattern, row["where"]))
                for name in names:
                    self.assertTrue(strictness.block_lines((TREE / pattern).read_text(encoding="utf-8"), [name]), "%s: %s is in the file" % (pattern, name))
        log = (TREE / strictness.LOG).read_text(encoding="utf-8").splitlines()
        self.assertEqual(log[0].split("\t"), list(strictness.LOG_COLUMNS))
        for line in log[1:]:
            self.assertEqual([p for _, p in strictness.parse_log_lines([line]) if p], [], line)

    def test_a_table_with_a_wrong_row_is_refused(self):
        path = pathlib.Path(self.r.dir) / "table.tsv"
        path.write_text("pattern\tmode\twhere\thides\na/b\tsometimes\t*\tx\nc/d\tany\tsomewhere\ty\ne\tany\n", encoding="utf-8")
        with self.assertRaises(strictness.StrictnessError) as caught:
            strictness.read_table(path)
        text = str(caught.exception)
        self.assertIn("the mode is one of", text)
        self.assertIn("where is `*` or `block:NAME`", text)
        self.assertIn("four cells", text)

    def test_check_branches_judges_each_branch_by_its_tip(self):
        r = self.r
        r.commit({"tools/gate/tests/foo_test.py": "def test():\n    assert value == 61\n"}, branch="mend/M-001")
        r.commit({"tools/other.py": "x = 3\n"}, branch="native/S0.1.1")
        lines = strictness.check_branches(r.root, "origin/main", ["native/S0.1.1", "mend/M-001", "not-a-branch"])
        self.assertTrue(lines and lines[0].startswith("mend/M-001: ") and "foo_test.py" in lines[0], lines)
        self.assertEqual(strictness.check_branches(r.root, "origin/main", ["native/S0.1.1"]), [], "the other branch is clean")
        # no name that resolves (a by-hand run's text): HEAD is judged, which is the branch the folder is on
        r.git("checkout", "-q", "mend/M-001")
        self.assertTrue(strictness.check_branches(r.root, "origin/main", ["main 1234abcd, started by its move"]))


def test_names(args):
    """The test names asked for on the command line; the harness's own option --work FOLDER is not one."""
    out, skip = [], False
    for a in args:
        if skip:
            skip = False
        elif a == "--work":
            skip = True
        elif not a.startswith("--work="):
            out.append(a)
    return out


def main():
    result = unittest.main(argv=[sys.argv[0]] + test_names(sys.argv[1:]), exit=False, verbosity=1).result
    print("strictness_test: %s (%d tests)" % ("pass" if result.wasSuccessful() else "FAIL", result.testsRun))
    return 0 if result.wasSuccessful() else 1


if __name__ == "__main__":
    sys.exit(main())
