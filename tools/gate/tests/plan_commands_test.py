"""plan_commands_test.py: the agent's commands and the page's note line (brief T10 part B), without the game or a build.

    python -X utf8 -I tools/gate/tests/plan_commands_test.py [--work FOLDER]

plan.py runs in the temporary git tree of plan_fixture (a bare origin, a plan tree, a work root, a live folder of its own); the game
data lookup is a fake. Nothing is built or started, nothing of D:/NebulaWork/plan is touched.

The commands (AGENT_COMMANDS in tools/plan/plan.py):
  the_table_is_five_commands          build, unit, engine, tour, record; each place in a command is one of the five the table names
  start_and_commands_print_one_block  the block `start` prints after its own text is the text `commands` prints
  agent_two_is_filled_in              --agent 2: its folder Nebula-native-2 and its build folder, none of agent 1's
  every_path_exists                   each file a command names under the work folder is in the tree, and each option of a command is
                                      one its program or script offers (agent_tour.py, luma_test.py, step_hash.py, the .ps1 parameters)
  the_runners_lock_file_sits_in_a_folder_the_build_made   the engine command's --lock is in the build folder itself (a lock inside
                                      runs/ stopped the first real run: the runner writes it before it makes its work folder)
  no_game_data_says_how_to_make_it    the line that tells how to make the game data stands in place of the pak path
  the_tour_command_runs_the_gate_calls  agent_tour.py writes the quick tour with the hash on and starts luma_test.py run with the pak, the
                                      hidden-window runner, the renderer and the HEAD commit; `recorded` plays the 20 stops of the references
The note:
  no_file_prints_nothing              no note.txt (or an empty one): no `Now:` line on the page, in STATUS.md or in `plan.py status`
  a_note_is_printed_under_the_count   note "TEXT": `Now: TEXT` under the count in STATUS.md (live folder and plan.py status) and in the page's data
  clear_empties_it                    note --clear: the file is empty and the line is gone
  markup_is_text                      text with < > & " and a closing script tag: the page's data holds it as text, the page has no closing
                                      script tag more than without a note, and the page sets it with textContent
  the_first_line_only                 a file of several lines: the first line with words; more than 200 characters: cut at 200; a longer text
                                      given to `note` is refused and the file stays
Each test was seen to fail with the thing it guards broken (a command naming a missing file, the block not printed, the note not read).
"""
import contextlib
import io
import json
import pathlib
import re
import sys
import unittest
from unittest import mock

HERE = pathlib.Path(__file__).resolve().parent
TREE = HERE.parents[2]
sys.path.insert(0, str(HERE))
sys.path.insert(0, str(TREE / "tools" / "plan"))
sys.path.insert(0, str(TREE / "tools" / "gate"))
import agent_tour  # noqa: E402
import page  # noqa: E402
import plan  # noqa: E402
from plan_fixture import Tree, setup_module, teardown_module  # noqa: E402

PAK = "D:/NebulaWork/build/game-data/RMGE01-37-aaaaaaaaaaaa/game.pak"
BLOCK = "=== YOUR COMMANDS"


def setUpModule():
    setup_module()


def tearDownModule():
    teardown_module()


def block_of(text):
    """The commands block of an output: from its heading to the end."""
    return text[text.index(BLOCK):]


class Base(unittest.TestCase):
    def setUp(self):
        self.t = Tree().__enter__()
        self.addCleanup(self.t.__exit__)
        patch = mock.patch.object(plan, "PAK_OF", lambda tree: PAK)
        patch.start()
        self.addCleanup(patch.stop)


class Commands(Base):
    def test_the_table_is_five_commands(self):
        self.assertEqual([c[0] for c in plan.AGENT_COMMANDS], ["build", "unit", "engine", "tour", "record"])
        for key, what, command, note in plan.AGENT_COMMANDS:
            places = set(re.findall(r"\{(\w+)\}", what + command + note))
            self.assertLessEqual(places, {"folder", "build", "pak"}, key)
            self.assertNotIn("{", command.format(folder="F", build="B", pak="P"), key)

    def test_start_and_commands_print_one_block(self):
        code, started = self.t.run(["start", "A.1"], expect=0)
        self.assertIn(BLOCK, started)
        self.assertTrue(started.index("started A.1") < started.index(BLOCK), "after its own text")
        code, again = self.t.run(["commands"], expect=0)
        self.assertEqual(block_of(started).strip(), again.strip())
        self.assertIn(PAK, again)
        self.assertEqual(again.count("\n") + 1 if not again.endswith("\n") else again.count("\n"),
                         2 + 2 * 5 + 1, "heading, rule line, five commands of two lines, the tour's extra line")

    def test_agent_two_is_filled_in(self):
        folder = plan.work_folder(self.t.work, 2).as_posix()
        code, text = self.t.run(["commands", "--agent", "2"], expect=0)
        self.assertIn("agent 2; folder %s; build folder %s/Nebula-native-2" % (folder, plan.BUILD_ROOT), text)
        self.assertIn("-Tree %s " % folder, text)
        self.assertIn("-Root %s/Nebula-native-2 " % plan.BUILD_ROOT, text)
        self.assertIn("--work %s/Nebula-native-2/runs/tour" % plan.BUILD_ROOT, text)
        self.assertIn("record %s/Nebula-native-2/runs/tour --out %s/tests/replays" % (plan.BUILD_ROOT, folder), text)
        self.assertNotIn("%s/Nebula-native/" % plan.BUILD_ROOT, text)
        self.assertNotIn(plan.work_folder(self.t.work, 1).as_posix() + "/", text)
        one = self.t.run(["commands"], expect=0)[1]
        self.assertIn("agent 1; folder %s" % plan.work_folder(self.t.work, 1).as_posix(), one)

    def test_every_path_exists(self):
        folder, build = "F", "B"
        for key, _, command, _ in plan.AGENT_COMMANDS:
            line = command.format(folder=folder, build=build, pak=PAK)
            for path in re.findall(r"\bF/([\w./-]+)", line):
                self.assertTrue((TREE / path).exists(), "%s names %s, which is not in the tree" % (key, path))
        # the options each program offers
        wanted = {
            "luma_test.py": (TREE / "tools" / "lumacore" / "luma_test.py", r'add_argument\("(--[a-z-]+)"'),
            "agent_tour.py": (TREE / "tools" / "gate" / "agent_tour.py", r'add_argument\("(--[a-z-]+)"'),
            "step_hash.py": (TREE / "tools" / "verify" / "step_hash.py", r'add_argument\("(--[a-z-]+)"'),
            "build_windows.ps1": (TREE / "tools" / "gate" / "build_windows.ps1", r"\[\w+\]\$(\w+)"),
            "ctest.ps1": (TREE / "tools" / "gate" / "ctest.ps1", r"\[\w+\]\$(\w+)"),
        }
        for key, _, command, _ in plan.AGENT_COMMANDS:
            for program, (source, pattern) in wanted.items():
                if program not in command:
                    continue
                offered = {o.lower() for o in re.findall(pattern, source.read_text(encoding="utf-8"))}
                given = re.findall(r"\s(--?[A-Za-z][\w-]*)", command.split(program, 1)[1])
                self.assertTrue(given, program)
                for option in given:
                    self.assertIn(option.lstrip("-").lower(), {o.lstrip("-") for o in offered}, "%s: %s of %s" % (key, option, program))

    def test_the_runners_lock_file_sits_in_a_folder_the_build_made(self):
        # The runner writes its lock file before it makes its work folder: a lock file inside runs/ (a folder nothing has made yet in a
        # fresh build folder) stopped the first engine test of a real run with FileNotFoundError.
        engine = next(c for c in plan.AGENT_COMMANDS if c[0] == "engine")[2]
        lock = re.search(r"--lock (\S+)", engine).group(1)
        self.assertEqual(lock.rsplit("/", 1)[0], "{build}")

    def test_no_game_data_says_how_to_make_it(self):
        with mock.patch.object(plan, "PAK_OF", lambda tree: None):
            text = self.t.run(["commands"], expect=0)[1]
        self.assertIn("<the US game.pak is not made: python -X utf8 -I %s/tools/gate/gamedata.py make RMGE01 --importer %s/Nebula-native/bins/"
                      "nebula-import.exe>" % (plan.work_folder(self.t.work, 1).as_posix(), plan.BUILD_ROOT), text)

    def test_the_tour_command_runs_the_gate_calls(self):
        quick = agent_tour.stops_for("quick")
        self.assertEqual(len(quick), 12)
        recorded = agent_tour.stops_for("recorded")
        self.assertEqual(len(recorded), 20)
        self.assertEqual(recorded[:12], quick, "the quick stops are the first twelve of the recorded ones")
        import tempfile
        import shutil
        work = pathlib.Path(tempfile.mkdtemp())
        self.addCleanup(shutil.rmtree, work, True)
        calls = []
        with mock.patch.object(agent_tour.subprocess, "run", lambda cmd, **k: calls.append(cmd) or mock.Mock(returncode=0, stdout="abc123def\n")):
            with contextlib.redirect_stdout(io.StringIO()):
                code = agent_tour.main(["--bin", "B/bins", "--pak", PAK, "--work", str(work), "--renderer", "vulkan"])
        self.assertEqual(code, 0)
        cmd = calls[-1]
        self.assertEqual(cmd[1:4], ["-X", "utf8", "-I"])
        self.assertTrue(cmd[4].endswith("tools/lumacore/luma_test.py") or cmd[4].endswith("tools\\lumacore\\luma_test.py"))
        self.assertEqual(cmd[5], "run")
        self.assertEqual(cmd[cmd.index("--pak") + 1], PAK)
        self.assertEqual(cmd[cmd.index("--renderer") + 1], "vulkan")
        self.assertEqual(cmd[cmd.index("--commit") + 1], "abc123def")
        suite = json.loads((work / "tour" / "suite.json").read_text(encoding="utf-8"))
        self.assertEqual(len(suite["tests"]), 12)
        first = json.loads((work / "tour" / suite["tests"][0] / "test.json").read_text(encoding="utf-8"))
        self.assertIn({"at": 0, "hash": "on"}, first["steps"], "the step hash is on from the first step")
        with mock.patch.object(agent_tour.subprocess, "run", lambda cmd, **k: mock.Mock(returncode=1, stdout="")):
            with contextlib.redirect_stdout(io.StringIO()):
                self.assertEqual(agent_tour.main(["--bin", "B/bins", "--pak", PAK, "--work", str(work / "two"), "--stops", "recorded"]), 1)
        self.assertEqual(len(json.loads((work / "two" / "tour" / "suite.json").read_text(encoding="utf-8"))["tests"]), 20)


def live_files(t):
    live = pathlib.Path(t.live)
    return live / "STATUS.md", live / "progress.html", live / "publish" / "progress-data.json"


def embedded(html):
    m = re.search(r'<script type="application/json" id="embedded-data">(.*?)</script>', html, re.S)
    return json.loads(m.group(1)) if m else None


class Note(Base):
    def status_after_page(self):
        self.t.run(["page"], expect=0)
        return live_files(self.t)[0].read_text(encoding="utf-8")

    def test_no_file_prints_nothing(self):
        text = self.status_after_page()
        self.assertNotIn("Now:", text)
        self.assertNotIn("Now:", self.t.run(["status"], expect=0)[1])
        _, html, data = live_files(self.t)
        self.assertNotIn("note", json.loads(data.read_text(encoding="utf-8")))
        self.assertNotIn("note", embedded(html.read_text(encoding="utf-8")))
        pathlib.Path(self.t.live).mkdir(parents=True, exist_ok=True)
        (pathlib.Path(self.t.live) / plan.NOTE_FILE).write_text("\n  \n", encoding="utf-8")
        self.assertNotIn("Now:", self.status_after_page(), "an empty file")

    def test_a_note_is_printed_under_the_count(self):
        self.assertEqual(self.t.run(["note", "The build is running."], expect=0)[1].strip(), "note: The build is running.")
        text = live_files(self.t)[0].read_text(encoding="utf-8")
        lines = text.splitlines()
        count = next(i for i, l in enumerate(lines) if l.startswith("Steps landed:"))
        self.assertEqual(lines[count + 2], "Now: The build is running.", "under the count: %s" % lines[count:count + 4])
        status = self.t.run(["status"], expect=0)[1].splitlines()
        count = next(i for i, l in enumerate(status) if l.startswith("Steps landed:"))
        self.assertEqual(status[count + 2], "Now: The build is running.")
        _, html, data = live_files(self.t)
        self.assertEqual(json.loads(data.read_text(encoding="utf-8"))["note"], "The build is running.")
        self.assertEqual(embedded(html.read_text(encoding="utf-8"))["note"], "The build is running.")
        self.assertEqual(plan.read_note(self.t.live), "The build is running.")

    def test_clear_empties_it(self):
        self.t.run(["note", "something"], expect=0)
        self.assertIn("Now: something", live_files(self.t)[0].read_text(encoding="utf-8"))
        self.assertEqual(self.t.run(["note", "--clear"], expect=0)[1].strip(), "note cleared")
        path = pathlib.Path(self.t.live) / plan.NOTE_FILE
        self.assertEqual((path.exists(), path.read_text(encoding="utf-8")), (True, ""))
        self.assertNotIn("Now:", live_files(self.t)[0].read_text(encoding="utf-8"))
        self.assertNotIn("note", embedded(live_files(self.t)[1].read_text(encoding="utf-8")))
        self.assertEqual(self.t.run(["note"], expect=2)[0], 2, "neither a text nor --clear")
        self.assertEqual(self.t.run(["note", "x", "--clear"], expect=2)[0], 2, "both")

    def test_markup_is_text(self):
        plain = self.t.run(["page"], expect=0) and live_files(self.t)[1].read_text(encoding="utf-8")
        text = '<b>bold</b> & "q" </script><script>alert(1)</script> <!-- x -->'
        self.t.run(["note", text], expect=0)
        html = live_files(self.t)[1].read_text(encoding="utf-8")
        self.assertEqual(embedded(html)["note"], text, "the page's data holds the text as it is")
        self.assertEqual(html.count("</script"), plain.count("</script"), "no closing script tag of the note's")
        self.assertEqual(html.count("<!--"), plain.count("<!--"), "no comment opens inside the data")
        self.assertIn('el("div", "now", "Now: " + d.note)', page.PAGE, "the page sets it as text")
        self.assertIn("e.textContent = String(text)", page.PAGE, "and el() does that")
        self.assertEqual(json.loads(live_files(self.t)[2].read_text(encoding="utf-8"))["note"], text)

    def test_the_first_line_only(self):
        live = pathlib.Path(self.t.live)
        live.mkdir(parents=True, exist_ok=True)
        (live / plan.NOTE_FILE).write_text("\n  First   line \nsecond line\n", encoding="utf-8")
        self.assertEqual(plan.read_note(self.t.live), "First line")
        (live / plan.NOTE_FILE).write_text("x" * 300 + "\nmore\n", encoding="utf-8")
        self.assertEqual(plan.read_note(self.t.live), "x" * 200, "cut at 200")
        (live / plan.NOTE_FILE).write_text("keep\n", encoding="utf-8")
        code, text = self.t.run(["note", "y" * 201])
        self.assertEqual(code, 1)
        self.assertIn("201 characters; 200 at most", text)
        self.assertEqual((live / plan.NOTE_FILE).read_text(encoding="utf-8"), "keep\n", "nothing was written")
        self.t.run(["note", "y" * 200], expect=0)
        self.assertEqual(plan.read_note(self.t.live), "y" * 200)
        self.t.run(["note", "two\nlines  here"], expect=0)
        self.assertEqual(plan.read_note(self.t.live), "two lines here", "one line")


def main():
    result = unittest.main(argv=[sys.argv[0]], exit=False, verbosity=1).result
    print("plan_commands_test: %s (%d tests)" % ("pass" if result.wasSuccessful() else "FAIL", result.testsRun))
    return 0 if result.wasSuccessful() else 1


if __name__ == "__main__":
    sys.exit(main())
