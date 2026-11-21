"""doc_refs_test.py: tools/verify/doc_refs.py on a small tree made here (package T13).

A `path/to/file.ext` or `path/to/file.ext:123-145` in backticks is reported when the file is not in the tree or the
line is past its end; the allow-list lets known outside references through. No game, no build, no network.

    python -X utf8 -I tools/gate/tests/doc_refs_test.py [--work <folder>]
"""
import pathlib
import shutil
import subprocess
import sys
import tempfile
import unittest

HERE = pathlib.Path(__file__).resolve().parent
sys.path.insert(0, str(HERE.parents[1] / "verify"))
import doc_refs  # noqa: E402


class DocRefsTest(unittest.TestCase):
    def setUp(self):
        self.root = pathlib.Path(tempfile.mkdtemp(prefix="doc-refs-"))
        self.addCleanup(shutil.rmtree, self.root, True)
        (self.root / "runtime" / "src").mkdir(parents=True)
        (self.root / "runtime" / "src" / "a.cpp").write_text("one\ntwo\nthree\n", encoding="utf-8")
        (self.root / "runtime" / "src" / "nonewline.cpp").write_text("one\ntwo", encoding="utf-8")
        (self.root / "runtime" / "src" / "empty.cpp").write_text("", encoding="utf-8")
        self.none = (set(), {})

    def bad(self, text, allow=None, name="doc.md", message=False):
        return doc_refs.check_text(name, text, self.root, allow or self.none, message)

    def test_a_file_that_exists_is_fine(self):
        self.assertEqual(self.bad("See `runtime/src/a.cpp`."), [])

    def test_a_file_that_is_not_there_is_reported_with_its_line(self):
        out = self.bad("first line\nSee `runtime/src/invented.cpp` here.\n")
        self.assertEqual(out, ["doc.md:2: runtime/src/invented.cpp: no such file in the tree"])

    def test_a_folder_that_is_not_there_is_reported(self):
        self.assertEqual(len(self.bad("`nothing/here/x.h`")), 1)

    def test_lines_inside_the_file_are_fine_and_past_the_end_are_reported(self):
        self.assertEqual(self.bad("`runtime/src/a.cpp:3` `runtime/src/a.cpp:1-3`"), [])
        out = self.bad("`runtime/src/a.cpp:4`")
        self.assertEqual(len(out), 1)
        self.assertIn("line 4 is past the end (the file has 3 lines)", out[0])
        out = self.bad("`runtime/src/a.cpp:2-9`")
        self.assertIn("line 9 is past the end", out[0])

    def test_a_last_line_with_no_newline_counts(self):
        self.assertEqual(self.bad("`runtime/src/nonewline.cpp:2`"), [])
        self.assertEqual(len(self.bad("`runtime/src/nonewline.cpp:3`")), 1)

    def test_line_zero_and_a_backwards_range_are_reported(self):
        self.assertEqual(len(self.bad("`runtime/src/a.cpp:0`")), 1)
        self.assertIn("backwards", self.bad("`runtime/src/a.cpp:3-1`")[0])

    def test_an_empty_file_has_no_line_one(self):
        self.assertEqual(len(self.bad("`runtime/src/empty.cpp:1`")), 1)
        self.assertEqual(self.bad("`runtime/src/empty.cpp`"), [])

    def test_text_that_is_not_a_reference_is_left_alone(self):
        for text in ("`renderer_common.cpp`",                 # no folder: which file is not known
                     "`C:/Users/x/file.txt`", "`D:/NebulaWork/a/b.md`", "`/usr/lib/x.so`", "`~/x/y.md`",
                     "`https://example.com/a/b.html`",
                     "`runtime/src/*.cpp`", "`runtime/<name>/x.cpp`", "`runtime/{a,b}/x.cpp`",
                     "`rows/p022c`",                         # a branch: no extension
                     "`src/a.cpp::function`", "`runtime/src/a.cpp line 3`", "`1.5/2.5`",
                     "plain runtime/src/missing.cpp outside backticks"):
            self.assertEqual(self.bad(text), [], text)

    def test_two_references_on_one_line_are_both_read(self):
        out = self.bad("`runtime/src/a.cpp` and `runtime/src/b.cpp` and `runtime/src/c.cpp`")
        self.assertEqual([o.split(": ")[1] for o in out], ["runtime/src/b.cpp", "runtime/src/c.cpp"])

    def test_a_fenced_block_is_not_read(self):
        text = "```\n`runtime/src/invented.cpp`\n```\n`runtime/src/also-invented.cpp`\n"
        out = self.bad(text)
        self.assertEqual(len(out), 1)
        self.assertIn(":4:", out[0])

    def test_the_allow_list_lets_a_reference_through_everywhere(self):
        allow = ({"runtime/src/branch_only.cpp"}, {})
        self.assertEqual(self.bad("`runtime/src/branch_only.cpp`", allow), [])
        self.assertEqual(self.bad("`runtime/src/branch_only.cpp:120`", allow), [], "one entry allows every line")
        self.assertEqual(len(self.bad("`runtime/src/other.cpp`", allow)), 1)

    def test_an_allowed_line_still_has_to_be_what_was_written(self):
        allow = ({"runtime/src/a.cpp:9"}, {})
        self.assertEqual(self.bad("`runtime/src/a.cpp:9`", allow), [])
        self.assertEqual(len(self.bad("`runtime/src/a.cpp:8`", allow)), 1)

    def test_an_allow_entry_for_one_document_is_for_that_document(self):
        allow = (set(), {"docs/a.md": {"x/y.cpp"}})
        self.assertEqual(self.bad("`x/y.cpp`", allow, name="docs/a.md"), [])
        self.assertEqual(len(self.bad("`x/y.cpp`", allow, name="docs/b.md")), 1)

    def test_an_allow_pattern_with_a_star_covers_every_reference_of_those_documents(self):
        allow = (set(), {"docs/plan/native/*.md": {"*"}})
        self.assertEqual(self.bad("`x/y.cpp` `z/w.h:9`", allow, name="docs/plan/native/03.md"), [])
        self.assertEqual(len(self.bad("`x/y.cpp`", allow, name="docs/plan/other.md")), 1)

    def test_the_allow_file_is_read(self):
        path = self.root / "allow.txt"
        path.write_text("# comment\n\nx/y.cpp  # why\ndocs/*.md :: p/q.h\ndocs/n.md :: *\n", encoding="utf-8")
        everywhere, only_in = doc_refs.read_allow(path)
        self.assertEqual(everywhere, {"x/y.cpp"})
        self.assertEqual(only_in, {"docs/*.md": {"p/q.h"}, "docs/n.md": {"*"}})
        self.assertEqual(doc_refs.read_allow(self.root / "absent.txt"), (set(), {}))

    def test_a_commit_message_is_read_without_backticks(self):
        text = "P-1: fix runtime/src/a.cpp:3\n\nSee runtime/src/nope.cpp and (runtime/src/a.cpp:7).\n"
        out = self.bad(text, name="the commit messages", message=True)
        self.assertEqual([o.split(": ")[1] for o in out], ["runtime/src/nope.cpp", "runtime/src/a.cpp:7"])

    def test_a_commit_message_keeps_a_sentence_end_and_a_version_out_of_it(self):
        text = "Fixes the thing, see runtime/src/a.cpp. Version 1.2.3 and 4/5 are fine; so is a/b.c.\n"
        out = self.bad(text, message=True)
        self.assertEqual([o.split(": ")[1] for o in out], ["a/b.c"])

    def run_cli(self, *args):
        return subprocess.run([sys.executable, "-X", "utf8", "-I", str(HERE.parents[1] / "verify" / "doc_refs.py"), *args],
                              capture_output=True, text=True, encoding="utf-8")

    def test_the_command_exits_one_with_a_line_per_bad_reference(self):
        doc = self.root / "d.md"
        doc.write_text("`runtime/src/a.cpp` `runtime/src/gone.cpp:5`\n", encoding="utf-8")
        r = self.run_cli("--root", str(self.root), "--allow", str(self.root / "none.txt"), str(doc))
        self.assertEqual(r.returncode, 1)
        self.assertIn("d.md:1: runtime/src/gone.cpp:5: no such file in the tree", r.stdout)
        self.assertIn("doc_refs: 1 bad reference", r.stdout)

    def test_the_command_exits_zero_when_all_is_there(self):
        doc = self.root / "d.md"
        doc.write_text("`runtime/src/a.cpp:2`\n", encoding="utf-8")
        r = self.run_cli("--root", str(self.root), "--allow", str(self.root / "none.txt"), str(doc))
        self.assertEqual((r.returncode, r.stdout.strip()), (0, "doc_refs: pass"))

    def test_the_command_reads_a_message_file(self):
        msg = self.root / "m.txt"
        msg.write_text("closes runtime/src/gone.cpp\n", encoding="utf-8")
        r = self.run_cli("--root", str(self.root), "--allow", str(self.root / "none.txt"), "--message", str(msg))
        self.assertEqual(r.returncode, 1)

    def test_a_file_that_cannot_be_read_is_a_failure(self):
        r = self.run_cli("--root", str(self.root), str(self.root / "absent.md"))
        self.assertEqual(r.returncode, 1)
        self.assertIn("cannot be read", r.stdout)

    def test_the_default_allow_list_covers_the_native_plan(self):
        everywhere, only_in = doc_refs.read_allow(doc_refs.DEFAULT_ALLOW)
        self.assertTrue(doc_refs.allowed("Game/Render/RenderPath.hpp", "docs/plan/native/03-NATIVE-STAGE-0.md", (everywhere, only_in)))
        self.assertFalse(doc_refs.allowed("Game/Render/RenderPath.hpp", "docs/engine/FRAME-MODEL.md", (everywhere, only_in)))


def main():
    result = unittest.main(argv=[sys.argv[0]], exit=False, verbosity=1).result
    print("doc_refs_test: %s (%d tests)" % ("pass" if result.wasSuccessful() else "FAIL", result.testsRun))
    return 0 if result.wasSuccessful() else 1


if __name__ == "__main__":
    sys.exit(main())
