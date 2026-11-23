"""plan_page_test.py: the progress page and the status text (package T13 item 6, T4b). In the live folder: progress.html (the
complete page, the data inside it, works from disk), STATUS.md (the same data as short plain text, any C:/Users/<name>/ written
~/), and in `publish` the pair for the web (index.html: static, rewritten only when the tool or the step list changes; and
progress-data.json: only what changes). The old names are removed. Every command that changes the state makes them again at
its end, and a failure there is one warning line that changes nothing of the command's result. The progress branch (STATUS.md
as the one file of one commit on `progress`) is pushed to a fake origin, a bare repository. The data matches steps.tsv, the live
state, BLOCKED.md, tool-faults.tsv and the gate's flakes.tsv and set-aside.tsv (in the live folder); no file names an outside
address; the page's script draws the data and says what to do when it cannot be loaded (run by node when node is installed). A
temporary live folder each: no game, no build, no network, nothing of D:/NebulaWork.

    python -X utf8 -I tools/gate/tests/plan_page_test.py [--work <folder>]
"""
import datetime
import json
import os
import pathlib
import re
import shutil
import subprocess
import sys
import tempfile
import unittest
from unittest import mock

HERE = pathlib.Path(__file__).resolve().parent
TREE = HERE.parents[2]
sys.path.insert(0, str(HERE))
sys.path.insert(0, str(TREE / "tools" / "plan"))
import live as lv  # noqa: E402
import page  # noqa: E402
import plan  # noqa: E402
from plan_fixture import Tree, ledger_row, plan_of, run, setup_module, step, teardown_module, write_steps  # noqa: E402

NOW = datetime.datetime(2026, 11, 20, 12, 0, 0)


def setUpModule():
    setup_module()


def tearDownModule():
    teardown_module()


def embedded(html, tag):
    m = re.search(r'<script type="application/json" id="%s">(.*?)</script>' % tag, html, re.S)
    return json.loads(m.group(1)) if m else None


def many(n, landed=0):
    """138-step plans like the real one: 6 steps to a package."""
    return plan_of(*[step("S%d.%d" % (i // 6, i % 6), "S%d" % (i // 6), title="A step of the plan, number %d" % i,
                          state="landed" if i < landed else "open") for i in range(n)])


class DataTest(unittest.TestCase):
    def data(self, steps, landings=(), state=None, claims=(), stale=(), faults=(), flakes=0):
        return page.make_data(steps, list(landings), state or {}, list(claims), set(stale), list(faults), flakes, "2026-11-20T12:00:00")

    def test_the_static_part_is_the_step_list_and_nothing_that_changes(self):
        steps = plan_of(step("A.1", "A", state="landed"), step("A.2", "A", state="started", agent="2"), step("B.1", "B", title="Other"))
        static = page.make_static(steps)
        self.assertEqual(static, {"title": "Nebula Native Progress", "total": 3,
                                  "packages": [{"package": "A", "total": 2}, {"package": "B", "total": 1}],
                                  "steps": [{"id": "A.1", "package": "A", "title": "t"}, {"id": "A.2", "package": "A", "title": "t"},
                                            {"id": "B.1", "package": "B", "title": "Other"}]})
        steps[1]["state"], steps[0]["state"] = "landed", "open"
        self.assertEqual(page.make_static(steps), static, "a step's state is not in it")

    def test_the_data_is_keyed_by_step_and_has_no_entry_for_an_open_step(self):
        steps = plan_of(step("A.1", "A", state="landed"), step("A.2", "A", state="started", agent="2"), step("B.1", "B", state="blocked"),
                        step("B.2", "B"), step("C.1", "C", state="landed"))
        steps[0]["commit"] = "abc123"
        landings = [{"step": "A.1", "commit": "abc123", "finished": "2026-11-20T10:00:00", "seconds": "5", "agent": "1",
                     "main": "0123456789"}]
        d = self.data(steps, landings)
        self.assertEqual(d["steps"], {"A.1": {"state": "landed", "commit": "abc123", "main": "0123456789", "time": "2026-11-20T10:00:00"},
                                      "A.2": {"state": "started", "agent": "2"}, "B.1": {"state": "blocked", "agent": "1"},
                                      "C.1": {"state": "landed"}})
        self.assertNotIn("B.2", d["steps"])
        self.assertEqual(sorted(d), ["blocked", "claims", "decisions", "flakes", "generated", "locks", "mends", "owed", "owner", "repairs", "set_aside", "steps", "strictness",
                                    "tool_faults"])

    def test_the_repairs_are_in_the_data_in_the_order_given_with_every_field(self):
        rows = [{"id": "R-002", "opened": "2026-11-21T01:00:00", "source": "nightly", "kind": "fail", "check": "tour/d3d11",
                 "renderer": "d3d11", "step": "S0.2.5", "commit": "abc1234", "evidence": "D:/ev/1", "row": "P-242", "state": "open",
                 "closed": "-", "seen": "2", "note": "stop 025 differs"},
                {"id": "R-001", "opened": "2026-11-20T01:00:00", "source": "stress", "kind": "stress", "check": "soak",
                 "renderer": "-", "step": "-", "commit": "-", "evidence": "D:/ev/2", "row": "-", "state": "open", "closed": "-",
                 "seen": "1", "note": "-"}]
        d = page.make_data(plan_of(step("A.1", "A")), [], {}, [], set(), [], 0, "2026-11-20T12:00:00", repairs=rows)
        self.assertEqual([r["id"] for r in d["repairs"]], ["R-002", "R-001"])
        self.assertEqual(sorted(d["repairs"][0]), sorted(page.REPAIR_FIELDS))
        self.assertEqual(d["repairs"][0], rows[0])
        self.assertEqual(self.data(plan_of(step("A.1", "A")))["repairs"], [], "none recorded: an empty list")

    def test_blocked_steps_carry_their_reason_and_agent(self):
        steps = plan_of(step("A.1", "A", state="blocked", agent="2"))
        d = self.data(steps, state={"A.1": {"reason": "the card is wrong"}})
        self.assertEqual(d["blocked"], [{"step": "A.1", "agent": "2", "reason": "the card is wrong"}])

    def test_claims_with_the_stale_mark_and_the_flake_count(self):
        d = self.data(plan_of(step("A.1", "A")), claims=[{"package": "G6", "agent": "2", "time": "t", "branch": "b"},
                                                         {"package": "G7", "agent": "1", "time": "t", "branch": ""}],
                      stale=["G7"], flakes=4)
        self.assertEqual([(c["package"], c["stale"]) for c in d["claims"]], [("G6", False), ("G7", True)])
        self.assertEqual(d["flakes"], 4)

    def test_nothing_landed_the_data_is_under_4_kilobytes_and_compact(self):
        text = page.data_json(self.data(many(138)))
        self.assertLess(len(text.encode("utf-8")), 4096)
        self.assertNotIn("\n ", text)
        self.assertNotIn(", ", text)
        self.assertNotIn(": ", text)
        self.assertEqual(text.count("\n"), 1)
        self.assertEqual(json.loads(text)["steps"], {})

    def test_with_sixty_landed_the_data_grows_by_the_steps_that_landed_only(self):
        steps = many(138, landed=60)
        for i, s in enumerate(steps[:60]):
            s["commit"] = "%09x" % (i * 7919)
        landings = [{"step": s["step"], "commit": s["commit"], "finished": "2026-11-20T10:%02d:00" % i, "seconds": "5", "agent": "1"}
                    for i, s in enumerate(steps[:60])]
        size = len(page.data_json(self.data(steps, landings)).encode("utf-8"))
        self.assertGreater(size, 4096)
        self.assertLess(size, 9000)

    def test_the_embedded_text_cannot_end_the_script_tag(self):
        d = self.data(plan_of(step("A.1", "A", state="blocked")), state={"A.1": {"reason": "</script><b>x</b><!-- y"}})
        static = page.make_static(plan_of(step("A.1", "A", title="</script> __LOCAL__ __STATIC__")))
        html = page.render(static, d)
        self.assertEqual(html.count("</script>"), 3, "the two data scripts and the page's own, no more")
        self.assertEqual(embedded(html, "embedded-data")["blocked"][0]["reason"], "</script><b>x</b><!-- y")
        self.assertEqual(embedded(html, "plan-static")["steps"][0]["title"], "</script> __LOCAL__ __STATIC__")

    def test_the_data_file_round_trips(self):
        d = self.data(many(10, landed=3))
        self.assertEqual(json.loads(page.data_json(d)), d)


class PageFilesTest(unittest.TestCase):
    def setUp(self):
        self.t = Tree().__enter__()
        self.addCleanup(self.t.__exit__)
        self.fixed = mock.patch.object(plan, "now", lambda: NOW)
        self.fixed.start()
        self.addCleanup(self.fixed.stop)
        self.paths = plan.page_paths(self.t.live)

    def outputs(self):
        p = self.paths
        return [p.html, p.status, p.index, p.data]

    def page(self):
        code, text = self.t.run(["page"], expect=0)
        p = self.paths
        return (json.loads(p.data.read_text(encoding="utf-8")), p.html.read_text(encoding="utf-8"),
                p.index.read_text(encoding="utf-8"), text)

    def test_page_writes_the_complete_page_the_status_and_the_published_pair(self):
        data, html, index, text = self.page()
        self.assertEqual(sorted(p.name for p in self.t.live.iterdir()), ["STATUS.md", "heartbeat.txt", "progress.html", "publish"])
        self.assertEqual(sorted(p.name for p in self.paths.publish.iterdir()), ["index.html", "progress-data.json"])
        for p in self.outputs():
            self.assertIn(p.as_posix(), text)
            self.assertIn("(%d bytes)" % p.stat().st_size, text)
        self.assertIn("the page to open, the data inside, works from disk", text)
        self.assertIn("progress branch: not pushed", text)

    def test_the_old_names_are_removed_and_nothing_else_is(self):
        self.t.live.mkdir(parents=True)
        for name in ("progress-local.html", "progress-data.json"):
            (self.t.live / name).write_text("old", encoding="utf-8")
        (self.t.live / "progress-url.txt").write_text("https://example.invalid/p\n", encoding="utf-8")
        (self.t.live / "flakes.tsv").write_text("", encoding="utf-8")
        self.page()
        self.assertEqual(sorted(p.name for p in self.t.live.iterdir()),
                         ["STATUS.md", "flakes.tsv", "heartbeat.txt", "progress-url.txt", "progress.html", "publish"])
        self.assertTrue(self.paths.data.is_file(), "the new progress-data.json is in publish")

    def test_progress_html_holds_the_data_and_the_published_index_does_not(self):
        data, html, index, _ = self.page()
        self.assertEqual(embedded(html, "embedded-data"), data)
        self.assertEqual(embedded(html, "plan-static"), embedded(index, "plan-static"))
        self.assertIsNone(embedded(index, "embedded-data"))
        self.assertNotIn("2026-11-20T12:00:00", index, "nothing of the current progress")

    def test_the_status_file_is_the_same_data_as_text(self):
        write_steps(self.t, [("A.1", "A", "", "landed"), ("A.2", "A", "", "open"), ("B.1", "B", "A", "open"), ("C.1", "C", "", "open"),
                             ("D.1", "D", "everything", "open")])
        plan.set_state(self.t.tree, "A.2", "started", agent=2)
        lv.add_landing(self.t.live, "A.1", "abc", 60, 1, NOW)
        self.page()
        text = self.paths.status.read_text(encoding="utf-8")
        self.assertIn("Steps landed: 1 of 5 (20%).", text)
        self.assertIn("- A.2 Title of A.2 (agent 2)", text)
        self.assertIn("| 2026-11-20 12:00 | A.1 | Title of A.1 | abc |", text)

    def test_the_index_does_not_change_when_a_step_lands_and_the_data_does(self):
        write_steps(self.t, [("A.1", "A", "", "open"), ("A.2", "A", "", "open"), ("B.1", "B", "A", "open"), ("C.1", "C", "", "open"),
                             ("D.1", "D", "everything", "open")])
        self.page()
        index_bytes, index_time = self.paths.index.read_bytes(), self.paths.index.stat().st_mtime_ns
        before = {p.name: p.read_bytes() for p in (self.paths.data, self.paths.html, self.paths.status)}
        write_steps(self.t, [("A.1", "A", "", "landed"), ("A.2", "A", "", "open"), ("B.1", "B", "A", "open"), ("C.1", "C", "", "open"),
                             ("D.1", "D", "everything", "open")])
        lv.add_landing(self.t.live, "A.1", "abc", 60, 1, NOW)
        self.page()
        self.assertEqual(self.paths.index.read_bytes(), index_bytes)
        self.assertEqual(self.paths.index.stat().st_mtime_ns, index_time, "not even rewritten")
        for p in (self.paths.data, self.paths.html, self.paths.status):
            self.assertNotEqual(p.read_bytes(), before[p.name], p.name)
        self.assertEqual(json.loads(self.paths.data.read_text(encoding="utf-8"))["steps"]["A.1"],
                         {"state": "landed", "commit": "abc", "time": "2026-11-20T12:00:00"})

    def test_the_index_changes_when_the_step_list_changes(self):
        self.page()
        before = self.paths.index.read_bytes()
        path = plan.native(self.t.tree) / "steps.tsv"
        path.write_text(path.read_text(encoding="utf-8").replace("Second step", "Second step, renamed"), encoding="utf-8")
        self.page()
        after = self.paths.index.read_bytes()
        self.assertNotEqual(after, before)
        self.assertIn(b"Second step, renamed", after)

    def test_the_index_does_not_change_with_blocked_claims_faults_or_flakes(self):
        self.page()
        before = self.paths.index.read_bytes()
        self.t.run(["start", "A.1"], expect=0)
        self.t.run(["blocked", "A.1", "stuck"], expect=0)
        self.t.run(["tool", "the runner broke"], expect=0)
        self.t.live.mkdir(parents=True, exist_ok=True)
        (self.t.live / "flakes.tsv").write_text("date\ttest\n2026-11-19\tt1\n", encoding="utf-8")
        self.page()
        self.assertEqual(self.paths.index.read_bytes(), before)

    def test_the_flake_count_and_the_set_aside_tests_are_the_gates_files_in_the_live_folder(self):
        """T2 writes flakes.tsv (no header) and set-aside.tsv in D:/NebulaWork/plan/native, the live folder."""
        self.t.live.mkdir(parents=True, exist_ok=True)
        (self.t.live / "flakes.tsv").write_text("2026-11-20T10:00:00\td3d11_depth\td3d11\tabc\tfirst message\n"
                                                "2026-11-20T11:00:00\td3d11_depth\td3d11\tdef\tsecond\n", encoding="utf-8")
        (self.t.live / "set-aside.tsv").write_text("2026-11-20T12:00:00\td3d11_depth\td3d11\t3 flakes in 7 days\n", encoding="utf-8")
        data, html, index, _ = self.page()
        self.assertEqual(data["flakes"], 2)
        self.assertEqual(data["set_aside"], [{"since": "2026-11-20T12:00:00", "test": "d3d11_depth", "renderer": "d3d11",
                                              "why": "3 flakes in 7 days"}])
        self.assertIn("Set aside", html, "the page's script draws the set-aside tests")

    def test_the_data_matches_the_plan_files(self):
        write_steps(self.t, [("A.1", "A", "", "landed"), ("A.2", "A", "", "open"), ("B.1", "B", "A", "open"), ("C.1", "C", "", "open"),
                             ("D.1", "D", "everything", "open")])
        plan.set_state(self.t.tree, "A.2", "started", agent=1)
        plan.set_state(self.t.tree, "C.1", "blocked", agent=1, reason="the owner's choice is needed")
        lv.add_landing(self.t.live, "A.1", "abc", 90, 1, NOW - datetime.timedelta(hours=2))
        lv.put_claim(self.t.live, "A", 1, "native/A.2", NOW)
        for _ in range(2):
            lv.append_fault(self.t.live, 1, "A.2", "TOOL", "harness/self", "result.json is missing", now=NOW)
        lv.append_fault(self.t.live, 2, "G6.1", "ENV", "start/vk", "no device", run_folder="D:/runs/9", now=NOW)
        self.t.live.mkdir(parents=True, exist_ok=True)
        (self.t.live / "flakes.tsv").write_text("date\ttest\trenderer\tcommit\tmessage\n2026-11-19\tt1\td3d11\tabc\tx\n"
                                                           "2026-11-20\tt2\tvk\tabc\ty\n", encoding="utf-8")
        data, html, index, _ = self.page()
        base = plan.read_base(self.t.tree)
        static = embedded(index, "plan-static")
        self.assertEqual((static["total"], [s["id"] for s in static["steps"]]), (len(base), [s["step"] for s in base]), "steps.tsv's order")
        self.assertEqual([(s["id"], s["package"], s["title"]) for s in static["steps"]], [(s["step"], s["package"], s["title"]) for s in base])
        self.assertEqual([(p["package"], p["total"]) for p in static["packages"]], [("A", 2), ("B", 1), ("C", 1), ("D", 1)])
        self.assertEqual(sum(1 for e in data["steps"].values() if e["state"] == "landed"), sum(1 for s in base if s["state"] == "landed"))
        self.assertEqual(data["steps"], {"A.1": {"state": "landed", "commit": "abc", "time": "2026-11-20T10:00:00"},
                                         "A.2": {"state": "started", "agent": "1"}, "C.1": {"state": "blocked", "agent": "1"}})
        self.assertEqual(data["blocked"], [{"step": "C.1", "agent": "1", "reason": "the owner's choice is needed"}])
        self.assertEqual([(c["package"], c["agent"]) for c in data["claims"]], [("A", "1")])
        self.assertEqual([(f["count"], f["check"], f["agent"]) for f in data["tool_faults"]], [(2, "harness/self", "1"), (1, "start/vk", "2")])
        self.assertEqual(data["tool_faults"][1]["run_folder"], "D:/runs/9")
        self.assertEqual(data["flakes"], 2)
        self.assertEqual(data["set_aside"], [], "no test is set aside")
        self.assertEqual(data["generated"], "2026-11-20T12:00:00")
        self.assertEqual(embedded(html, "embedded-data"), data, "the complete page embeds the same data")
        self.assertEqual(embedded(html, "plan-static"), static)
        self.assertIsNone(embedded(index, "embedded-data"), "the published page has no data in it")

    def test_a_blocked_reason_falls_back_to_blocked_md(self):
        lv.set_state_entry(self.t.live, "C.1", "blocked", 1, "")
        plan.append_under(plan.blocked_path(self.t.live), "steps", "- 2026-11-20 C.1: the reason in the file | first failure line: x")
        data = self.page()[0]
        self.assertEqual(data["blocked"][0]["reason"], "the reason in the file | first failure line: x")

    def test_the_numbers_move_with_the_state_after_each_command(self):
        read = lambda: json.loads(self.paths.data.read_text(encoding="utf-8"))
        self.t.run(["start", "A.1"], expect=0)
        self.assertEqual(read()["steps"], {"A.1": {"state": "started", "agent": "1"}})
        self.t.run(["blocked", "A.1", "stuck here"], expect=0)
        d = read()
        self.assertEqual((d["steps"]["A.1"]["state"], d["blocked"][0]["reason"]), ("blocked", "stuck here"))
        self.t.run(["tool", "the runner broke"], expect=0)
        self.assertEqual(read()["tool_faults"][0]["first_error"], "the runner broke")
        self.t.run(["toolfault", "--last", "--could-not", "run the tour"], expect=0)
        self.assertEqual(read()["tool_faults"][0]["could_not"], "run the tour")

    def test_the_files_are_the_same_when_nothing_changed(self):
        self.page()
        before = [p.read_bytes() for p in self.outputs()]
        self.page()
        self.assertEqual([p.read_bytes() for p in self.outputs()], before)

    def test_a_live_folder_that_is_not_there_is_made(self):
        shutil.rmtree(self.t.live, ignore_errors=True)
        self.page()
        self.assertTrue(all(p.is_file() for p in self.outputs()))


class RepairsPageTest(unittest.TestCase):
    """The repairs of the live folder reach the page's data, STATUS.md and `plan.py status`."""

    def test_repairs_reach_the_data_file_the_page_and_the_status_file_open_fail_first(self):
        with Tree() as t:
            lv.add_repair(t.live, "stress", "stress", "soak", "-", "-", "-", "D:/ev/soak", "-", "a stress finding")
            lv.add_repair(t.live, "nightly", "fail", "tour/d3d11", "d3d11", "A.1", "abc123456", "D:/ev/tour", "P-001", "stop differs")
            lv.add_repair(t.live, "measure", "worse", "measure/route", "-", "-", "-", "D:/ev/m", "-", "slower")
            lv.add_repair(t.live, "nightly", "fail", "unit/game", "-", "-", "-", "D:/ev/u", "-", "a unit fails")
            lv.set_repair(t.live, "R-004", "landed", "0123456789")
            written = plan.write_page(t.tree, t.live)
            ids = [r["id"] for r in written.data["repairs"]]
            self.assertEqual(ids, ["R-002", "R-001", "R-003", "R-004"], "open fail first, then the other open ones, landed last")
            on_disk = json.loads(written.paths.data.read_text(encoding="utf-8"))
            self.assertEqual(on_disk["repairs"], written.data["repairs"])
            self.assertEqual(embedded(written.paths.html.read_text(encoding="utf-8"), "embedded-data")["repairs"], on_disk["repairs"])
            status = written.paths.status.read_text(encoding="utf-8")
            self.assertIn("| R-002 | open | fail | tour/d3d11 | d3d11 | step A.1 commit abc123456 | 1 |", status)
            self.assertIn("| R-004 | landed | fail | unit/game | - | not traced | 1 |", status)
            text = t.run(["status"], expect=0)[1]
            self.assertIn("## Repairs (1 open of kind fail, 4 in all)", text)
            self.assertLess(text.index("- R-002 open fail: tour/d3d11 on d3d11, traced to step A.1 commit abc123456, seen 1x"),
                            text.index("- R-001 open stress: soak, not traced to a landing, seen 1x"))
            self.assertIn("- R-004 landed fail: unit/game, not traced to a landing, seen 1x", text)

    def test_the_html_has_the_repairs_section_and_no_outside_address(self):
        html = page.render(page.make_static(plan_of(step("A.1", "A"))))
        self.assertIn('section("Repairs (', html)
        self.assertIn("Traced to", html)
        self.assertNotRegex(html, r"https?://")


class RegenerateTest(unittest.TestCase):
    """Every command that changes the state makes the progress files again at its end, and a failure there is one warning
    line that changes nothing of the command's result."""

    def setUp(self):
        self.t = Tree().__enter__()
        self.addCleanup(self.t.__exit__)
        self.paths = plan.page_paths(self.t.live)
        self.outputs = [self.paths.html, self.paths.status, self.paths.index, self.paths.data]

    def clear(self):
        """The progress files are gone (the state in the live folder stays)."""
        for p in self.outputs:
            p.unlink(missing_ok=True)

    def made(self):
        return [p.is_file() for p in self.outputs]

    def land_ready(self):
        """A.1 started, its ledger ready and committed, the gate faked as one that passes."""
        self.t.run(["start", "A.1"], expect=0)
        self.t.write_ledger([ledger_row("P-001", "fixed", "tests/render/a_test 1-9 fails before"),
                             ledger_row("P-002", note="advanced 11-20: half done"), ledger_row("P-003"), ledger_row("P-004")],
                            tree=self.t.folder("A.1"))
        self.t.commit("A.1", "A.1: the code", {"runtime/src/render/a.cpp": "a\nb\nc\nd\n"})
        patcher = mock.patch.object(plan, "LAND", lambda *a: plan.Verdict(True, [("repo/code", "PASS", "ok")]))
        patcher.start()
        self.addCleanup(patcher.stop)

    def test_each_command_that_changes_the_state_makes_every_file_again(self):
        self.t.run(["start", "A.1"], expect=0)
        self.assertEqual(self.made(), [True] * 4, "start")
        for name, args in (("claim", ["claim", "C"]), ("blocked", ["blocked", "A.1", "stuck"]), ("tool", ["tool", "the runner broke"]),
                           ("toolfault", ["toolfault", "--last", "--could-not", "run the tour"])):
            self.clear()
            self.t.run(args, expect=0)
            self.assertEqual(self.made(), [True] * 4, name)
        self.assertIn("run the tour", self.paths.status.read_text(encoding="utf-8"), "the last command's state is in STATUS.md")

    def test_a_landing_makes_every_file_again(self):
        self.land_ready()
        self.clear()
        code, text = self.t.run(["land", "A.1"], expect=0)
        self.assertEqual(self.made(), [True] * 4)
        self.assertIn("Steps landed: 1 of 5", self.paths.status.read_text(encoding="utf-8"))

    def test_a_command_that_changes_nothing_makes_nothing(self):
        self.t.run(["next"], expect=0)
        self.assertFalse(self.paths.html.exists())

    def test_a_failure_while_regenerating_is_one_warning_line_and_the_result_is_the_same(self):
        for args in (["claim", "C"], ["tool", "the runner broke"], ["start", "A.1"]):
            with self.subTest(args=args):
                with Tree() as ok:
                    code, same = ok.run(args)
                    same = same.replace(ok.dir.as_posix(), "T")
                with Tree() as t:
                    with mock.patch.object(plan, "write_page", side_effect=OSError("the disk is full")):
                        code_failed, text = t.run(args)
                    text = text.replace(t.dir.as_posix(), "T")
                self.assertEqual(code_failed, code, "the command's exit code does not change")
                warnings = [l for l in text.splitlines() if "warning" in l]
                self.assertEqual(len(warnings), 1, text)
                self.assertIn("the progress files were not made again", warnings[0])
                self.assertIn("the disk is full", warnings[0])
                self.assertEqual([l for l in text.splitlines() if "warning" not in l], same.splitlines(),
                                 "the command says what it said")

    def test_any_kind_of_failure_is_only_a_warning(self):
        for error in (OSError("x"), ValueError("y"), KeyError("z"), RuntimeError("w"), lv.Busy("busy")):
            with Tree() as t:
                with mock.patch.object(plan, "write_page", side_effect=error):
                    code, text = t.run(["tool", "the runner broke"])
                self.assertEqual(code, 0, repr(error))
                self.assertEqual(len([l for l in text.splitlines() if "warning" in l]), 1, repr(error))
                self.assertEqual(lv.read_faults(t.live)[0]["first_error"], "the runner broke", "the command's own work is done")

    def test_a_failing_landing_keeps_its_exit_code_when_the_files_fail_too(self):
        self.land_ready()
        failed = plan.Verdict(False, [], [("tour/d3d12", "FAIL", "replay step 0")])
        failed.code, failed.gate_recorded = 1, True
        with mock.patch.object(plan, "LAND", lambda *a: failed), mock.patch.object(plan, "write_page", side_effect=OSError("x")):
            code, text = self.t.run(["land", "A.1"])
        self.assertEqual(code, 1)
        self.assertEqual(len([l for l in text.splitlines() if "warning" in l]), 1)

    def test_a_failing_push_is_one_warning_line_and_the_result_is_the_same(self):
        with mock.patch.dict(os.environ):
            os.environ.pop(plan.PUSH_OFF, None)
            with mock.patch.object(plan, "publish_progress", side_effect=RuntimeError("git push: refused")):
                code, text = self.t.run(["tool", "the runner broke"])
        self.assertEqual(code, 0)
        warnings = [l for l in text.splitlines() if "warning" in l]
        self.assertEqual(len(warnings), 1, text)
        self.assertIn("the progress branch was not pushed", warnings[0])
        self.assertTrue(all(self.made()), "the files are made all the same")


class StatusTextTest(unittest.TestCase):
    """STATUS.md from a fixed state, without a tree: page.make_status."""

    def state(self, started=0, landed=3, landings=12, faults=25, blocked=True):
        steps = plan_of(*[step("S%d.%d" % (i // 2, i % 2), "S%d" % (i // 2), title="Step number %d" % i,
                               state="landed" if i < landed else "open") for i in range(8)])
        for i in range(landed, landed + started):
            steps[i]["state"], steps[i]["agent"] = "started", "2" if i % 2 else "1"
        if blocked:
            steps[7]["state"] = "blocked"
        rows = [{"step": "S%d.%d" % (i // 2, i % 2), "commit": "c%02d" % i, "finished": "2026-11-20T10:%02d:09" % i, "seconds": "5", "agent": "1"}
                for i in range(landings)]
        events = [{"time": "2026-11-20T11:%02d:30" % i, "agent": "1", "step": "S0.%d" % (i % 2), "kind": "TOOL", "check": "harness/self",
                   "test": "t%d" % i if i % 3 else "", "first_error": "error %d" % i, "run_folder": "", "could_not": "run it" if i % 2 else ""}
                  for i in range(faults)]
        data = page.make_data(steps, rows, {"S3.1": {"reason": "needs the owner | now"}}, [], set(), [], 0, "2026-11-20T12:00:00")
        return page.make_static(steps), data, rows, events

    def text(self, **kw):
        static, data, rows, events = self.state(**kw)
        return page.make_status(static, data, rows, events, "2026-11-20T12:00:00")

    def test_every_line_the_plan_asks_for(self):
        text, landed, total = self.text(started=2)
        self.assertEqual((landed, total), (3, 8))
        lines = text.splitlines()
        self.assertEqual(lines[0], "# Nebula native plan: status")
        self.assertIn("Written 2026-11-20 12:00. " + page.OLD_PAGE, lines)
        self.assertIn("Steps landed: 3 of 8 (38%).", lines)
        self.assertIn("In work now:", lines)
        self.assertIn("- S1.1 Step number 3 (agent 2)", lines)
        self.assertIn("- S2.0 Step number 4 (agent 1)", lines)
        self.assertIn("| Package | Landed | Of |", lines)
        self.assertIn("| S0 | 2 | 2 |", lines)
        self.assertIn("| S1 | 1 | 2 |", lines)
        self.assertIn("| S3 | 0 | 2 |", lines)
        self.assertIn("- S3.1 Step number 7: needs the owner | now", lines, "blocked, with why")
        self.assertIn("25 tool faults recorded.", lines)

    def test_nothing_started(self):
        text, _, _ = self.text(started=0)
        self.assertIn("In work now: Nothing started.", text.splitlines())
        text, _, _ = self.text(started=0, blocked=False, faults=0, landings=0)
        self.assertIn("Nothing is blocked.", text)
        self.assertIn("0 tool faults recorded.", text)
        self.assertIn("None yet.", text)
        self.assertNotIn("The last 20", text)

    def test_the_last_ten_landings_newest_first(self):
        text, _, _ = self.text(landings=12)
        rows = [l for l in text.splitlines() if re.match(r"^\| 2026-11-20 10:", l)]
        self.assertEqual(len(rows), 10)
        self.assertTrue(rows[0].startswith("| 2026-11-20 10:11 |"), rows[0])
        self.assertTrue(rows[-1].startswith("| 2026-11-20 10:02 |"), rows[-1])
        self.assertIn("| 2026-11-20 10:05 | S2.1 | Step number 5 | c05 | - |", rows)
        self.assertEqual(text.splitlines()[text.splitlines().index("| When | Step | Title | Code commit | Main commit |") - 2], "## Last 10 landings")

    def test_the_last_twenty_tool_faults_newest_first_with_all_five_columns(self):
        text, _, _ = self.text(faults=25)
        rows = [l for l in text.splitlines() if re.match(r"^\| 2026-11-20 11:", l)]
        self.assertEqual(len(rows), 20)
        self.assertTrue(rows[0].startswith("| 2026-11-20 11:24 |"))
        self.assertTrue(rows[-1].startswith("| 2026-11-20 11:05 |"))
        self.assertIn("| 2026-11-20 11:23 | S0.1 | t23 | error 23 | run it |", rows, "step, test, what happened, what it could not do")
        self.assertIn("| 2026-11-20 11:24 | S0.0 | harness/self | error 24 | - |", rows, "nothing to say it could not do: a dash")
        self.assertIn("| When | Step | Test | What happened | What the agent could not do |", text.splitlines())
        self.assertIn("| 2026-11-20 11:09 | S0.1 | harness/self | error 9 | run it |", rows, "no test named: the check is shown")

    def test_a_golden_text_for_a_small_state(self):
        text, _, _ = self.text(started=1, landed=2, landings=2, faults=2)
        self.assertEqual(text, "\n".join([
            "# Nebula native plan: status",
            "",
            "Written 2026-11-20 12:00. " + page.OLD_PAGE,
            "",
            "Steps landed: 2 of 8 (25%).",
            "",
            "In work now:",
            "- S1.0 Step number 2 (agent 1)",
            "",
            "## Packages",
            "",
            "| Package | Landed | Of |",
            "| --- | --- | --- |",
            "| S0 | 2 | 2 |",
            "| S1 | 0 | 2 |",
            "| S2 | 0 | 2 |",
            "| S3 | 0 | 2 |",
            "",
            "## Last 10 landings",
            "",
            "| When | Step | Title | Code commit | Main commit |",
            "| --- | --- | --- | --- | --- |",
            "| 2026-11-20 10:01 | S0.1 | Step number 1 | c01 | - |",
            "| 2026-11-20 10:00 | S0.0 | Step number 0 | c00 | - |",
            "",
            "## Repairs",
            "",
            "No repair recorded.",
            "",
            "## For the owner to check by hand",
            "",
            "Nothing is owed.",
            "",
            "## Waiting for the owner",
            "",
            "Nothing waits for the owner.",
            "",
            "## Blocked",
            "",
            "- S3.1 Step number 7: needs the owner | now",
            "",
            "## Tool mends",
            "",
            "No tool was mended.",
            "",
            "## Decisions (0)",
            "",
            "None.",
            "",
            "## Checks changed during the run (0)",
            "",
            "None.",
            "",
            "## Tool faults",
            "",
            "2 tool faults recorded.",
            "",
            "The last 20:",
            "",
            "| When | Step | Test | What happened | What the agent could not do |",
            "| --- | --- | --- | --- | --- |",
            "| 2026-11-20 11:01 | S0.1 | t1 | error 1 | run it |",
            "| 2026-11-20 11:00 | S0.0 | harness/self | error 0 | - |",
            ""]))

    def test_the_repairs_section_names_check_renderer_traced_landing_seen_count_and_state(self):
        static, data, rows, events = self.state(faults=0)
        data["repairs"] = [
            {"id": "R-003", "opened": "2026-11-21T01:00:00", "source": "nightly", "kind": "fail", "check": "tour/d3d11",
             "renderer": "d3d11", "step": "S0.2.5", "commit": "abc1234", "evidence": "D:/ev/1", "row": "P-242", "state": "open",
             "closed": "-", "seen": "2", "note": "x"},
            {"id": "R-001", "opened": "2026-11-20T01:00:00", "source": "stress", "kind": "stress", "check": "soak", "renderer": "-",
             "step": "-", "commit": "-", "evidence": "D:/ev/2", "row": "-", "state": "started", "closed": "-", "seen": "1", "note": "-"}]
        text = page.make_status(static, data, rows, events, "2026-11-20T12:00:00")[0].splitlines()
        at = text.index("## Repairs")
        self.assertEqual(text[at + 2], "1 open of kind fail, 2 in all.")
        self.assertEqual(text[at + 4:at + 8], [
            "| Id | State | Kind | Check | Renderer | Traced to | Seen |", "| --- | --- | --- | --- | --- | --- | --- |",
            "| R-003 | open | fail | tour/d3d11 | d3d11 | step S0.2.5 commit abc1234 | 2 |",
            "| R-001 | started | stress | soak | - | not traced | 1 |"], "in the order given: open fail repairs first")

    def test_a_path_that_starts_with_the_home_folder_is_written_with_a_tilde(self):
        for old, new in (("C:/Users/vaibh/Documents/Nebula/a.txt", "~/Documents/Nebula/a.txt"),
                         ("C:\\Users\\vaibh\\AppData\\Local\\Temp\\x", "~/AppData\\Local\\Temp\\x"),
                         ("C:\\Users/vaibh\\Documents/y", "~/Documents/y"),
                         ("c:/USERS/vaibh/z", "~/z"),
                         ("D:/NebulaWork/plan/native/x", "D:/NebulaWork/plan/native/x"),
                         ("C:/Users/vaibh", "C:/Users/vaibh"),
                         ("C:/Windows/Users/vaibh/x", "C:/Windows/Users/vaibh/x")):
            self.assertEqual(page.home_short(old), new, old)
        self.assertEqual(page.home_short("see C:/Users/vaibh/a and C:\\Users\\vaibh\\b"), "see ~/a and ~/b")

    def test_the_rule_is_applied_to_the_whole_file(self):
        static, data, rows, events = self.state(faults=2)
        events[0]["first_error"] = "cannot open C:/Users/vaibh/Documents/MarioGalaxyRecomp/x.py"
        events[1]["could_not"] = "read C:\\Users\\vaibh\\AppData\\Local\\Temp\\log.txt"
        data["blocked"][0]["reason"] = "see C:/Users/vaibh/notes.txt"
        text, _, _ = page.make_status(static, data, rows, events, "2026-11-20T12:00:00")
        self.assertNotIn("Users", text)
        self.assertIn("cannot open ~/Documents/MarioGalaxyRecomp/x.py", text)
        self.assertIn("read ~/AppData\\Local\\Temp\\log.txt", text)
        self.assertIn("see ~/notes.txt", text)

    def test_the_numbers(self):
        self.assertEqual(self.text(landed=0, blocked=False)[0].splitlines()[4], "Steps landed: 0 of 8 (0%).")
        steps = plan_of(*[step("A.%d" % i, "A", state="landed") for i in range(138)])
        text, landed, total = page.make_status(page.make_static(steps), page.make_data(steps, [], {}, [], set(), [], 0, "2026-11-20T12:00:00"),
                                               [], [], "2026-11-20T12:00:00")
        self.assertIn("Steps landed: 138 of 138 (100%).", text)
        self.assertEqual((landed, total), (138, 138))


def git(repo, *args, env=None):
    exe = plan.GIT if pathlib.Path(plan.GIT).exists() else "git"
    r = subprocess.run([exe, "-C", str(repo), *args], capture_output=True, text=True, encoding="utf-8", env=env)
    return r.returncode, (r.stdout + r.stderr).strip()


class PushTest(unittest.TestCase):
    """The progress branch, with a bare repository as the fake origin (the fixture's own): nothing here reaches GitHub."""

    def setUp(self):
        self.t = Tree().__enter__()
        self.addCleanup(self.t.__exit__)
        self.fixed = mock.patch.object(plan, "now", lambda: NOW)
        self.fixed.start()
        self.addCleanup(self.fixed.stop)
        env = mock.patch.dict(os.environ)
        env.start()
        self.addCleanup(env.stop)
        os.environ.pop(plan.PUSH_OFF, None)               # the push is on in these tests, towards the fake origin
        self.main_sha = self.t.git("rev-parse", "main", cwd=self.t.origin)

    def tip(self):
        code, out = git(self.t.origin, "rev-parse", "--verify", "--quiet", "refs/heads/progress")
        return out if code == 0 else None

    def refs(self):
        return sorted(l.split()[1] for l in git(self.t.origin, "for-each-ref", "--format=%(objectname) %(refname)")[1].splitlines())

    def parents(self, sha):
        return git(self.t.origin, "rev-list", "--parents", "-n", "1", sha)[1].split()[1:]

    def test_the_first_push_makes_the_branch_with_one_commit_of_one_file_and_no_parent(self):
        code, text = self.t.run(["page"], expect=0)
        tip = self.tip()
        self.assertIsNotNone(tip, text)
        self.assertIn("pushed the first commit %s on refs/heads/progress (no parent)" % tip[:9], text)
        self.assertEqual(self.parents(tip), [], "no parent: it shares no history with main")
        self.assertEqual(git(self.t.origin, "ls-tree", "-r", "--name-only", tip)[1], "STATUS.md")
        self.assertEqual(git(self.t.origin, "log", "-1", "--format=%s", tip)[1], "Progress: 0 of 5 steps landed")
        self.assertEqual(git(self.t.origin, "show", "%s:STATUS.md" % tip)[1].replace("\r\n", "\n") + "\n",
                         self.t.live.joinpath("STATUS.md").read_bytes().decode("utf-8").replace("\r\n", "\n"))
        self.assertEqual(git(self.t.origin, "merge-base", tip, "main")[0], 1, "no history in common with main")
        self.assertEqual(self.refs(), ["refs/heads/main", "refs/heads/progress"], "nothing but the one branch")
        self.assertEqual(self.t.git("rev-parse", "main", cwd=self.t.origin), self.main_sha, "main is not touched")

    def test_a_second_push_adds_one_commit_on_top(self):
        self.t.run(["page"], expect=0)
        first = self.tip()
        self.t.run(["tool", "the runner broke"], expect=0)           # a command that changes the state pushes by itself
        second = self.tip()
        self.assertNotEqual(first, second)
        self.assertEqual(self.parents(second), [first], "the previous tip is the parent")
        self.assertEqual(git(self.t.origin, "rev-list", "--count", second)[1], "2")
        self.assertEqual(git(self.t.origin, "ls-tree", "-r", "--name-only", second)[1], "STATUS.md")
        self.assertIn("the runner broke", git(self.t.origin, "show", "%s:STATUS.md" % second)[1])
        self.assertEqual(git(self.t.origin, "diff", "--name-only", first, second)[1], "STATUS.md")

    def test_the_message_counts_the_landed_steps(self):
        write_steps(self.t, [("A.1", "A", "", "landed"), ("A.2", "A", "", "landed"), ("B.1", "B", "A", "open"), ("C.1", "C", "", "open"),
                             ("D.1", "D", "everything", "open")])
        self.t.run(["page"], expect=0)
        self.assertEqual(git(self.t.origin, "log", "-1", "--format=%s", self.tip())[1], "Progress: 2 of 5 steps landed")

    def test_the_same_content_pushes_nothing(self):
        self.t.run(["page"], expect=0)
        first = self.tip()
        code, text = self.t.run(["page"], expect=0)
        self.assertEqual(self.tip(), first)
        self.assertIn("nothing pushed", text)
        self.assertEqual(git(self.t.origin, "rev-list", "--count", first)[1], "1")
        self.t.run(["tool", "the runner broke"], expect=0)
        second = self.tip()
        self.t.run(["page"], expect=0)
        self.assertEqual(self.tip(), second, "unchanged again")

    def test_a_push_that_fails_only_warns(self):
        before = {p: p.read_bytes() for p in (self.t.live / "STATUS.md",) if p.exists()}
        self.t.git("remote", "set-url", "origin", str(self.t.dir / "no-such-origin.git"))
        for args in (["page"], ["tool", "the runner broke"]):
            code, text = self.t.run(args)
            self.assertEqual(code, 0, args)
            warnings = [l for l in text.splitlines() if "warning" in l]
            self.assertEqual(len(warnings), 1, text)
            self.assertIn("the progress branch was not pushed", warnings[0])
        self.assertTrue((self.t.live / "STATUS.md").is_file(), "STATUS.md stays local")
        self.assertEqual(lv.read_faults(self.t.live)[0]["first_error"], "the runner broke")

    def test_a_push_the_origin_refuses_only_warns(self):
        hook = self.t.origin / "hooks" / "pre-receive"
        hook.parent.mkdir(exist_ok=True)
        hook.write_bytes(b"#!/bin/sh\necho refused here >&2\nexit 1\n")
        code, text = self.t.run(["tool", "the runner broke"])
        self.assertEqual(code, 0)
        self.assertEqual(len([l for l in text.splitlines() if "warning" in l]), 1, text)
        self.assertIsNone(self.tip())

    def test_the_switch_and_the_environment_turn_the_push_off(self):
        code, text = run(["--no-push", "tool", "x"], self.t.tree, self.t.work, self.t.store, 0, self.t.live)
        self.assertIsNone(self.tip())
        with mock.patch.dict(os.environ, {plan.PUSH_OFF: "1"}):
            self.t.run(["tool", "y"], expect=0)
        self.assertIsNone(self.tip())
        with mock.patch.dict(os.environ, {plan.PUSH_OFF: "0"}):
            self.t.run(["tool", "z"], expect=0)
        self.assertIsNotNone(self.tip(), "0 does not turn it off")

    def test_the_work_folder_is_not_touched(self):
        """Plumbing commands only: no branch, no tag, no change of the index or of any file of the tree."""
        refs_before = git(self.t.tree, "for-each-ref")[1]       # the push itself adds refs/remotes/origin/progress, as any push does
        status_before = git(self.t.tree, "status", "--porcelain")[1]
        head_before = git(self.t.tree, "rev-parse", "HEAD")[1]
        self.t.run(["page"], expect=0)
        self.assertIsNotNone(self.tip())
        after = [l for l in git(self.t.tree, "for-each-ref")[1].splitlines() if not l.endswith("refs/remotes/origin/progress")]
        self.assertEqual("\n".join(after), refs_before, "no branch, no tag, no other ref")
        self.assertEqual(git(self.t.tree, "status", "--porcelain")[1], status_before)
        self.assertEqual(git(self.t.tree, "rev-parse", "HEAD")[1], head_before)
        self.assertEqual(git(self.t.tree, "rev-parse", "--abbrev-ref", "HEAD")[1], "main")

    def test_the_main_repository_of_a_work_folder_is_its_common_one(self):
        folder = self.t.dir / "a-work-folder"
        self.t.git("worktree", "add", "-q", "-b", "side", str(folder))
        self.assertEqual(plan.main_repo(folder), self.t.tree)
        self.assertEqual(plan.main_repo(self.t.tree), self.t.tree)
        self.assertIsNone(plan.main_repo(self.t.dir / "not-a-tree"))

    def test_a_push_from_a_work_folders_plan_goes_through_the_main_repository(self):
        folder = self.t.dir / "a-work-folder"
        self.t.git("worktree", "add", "-q", "-b", "side", str(folder))
        code, text = run(["page"], folder, self.t.work, self.t.store, 0, self.t.live)
        self.assertIsNotNone(self.tip(), text)
        self.assertEqual(git(folder, "status", "--porcelain")[1], "")


class HtmlTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        steps = plan_of(step("A.1", "A"))
        cls.static = page.make_static(steps)
        cls.data = page.make_data(steps, [], {}, [], set(), [], 0, "2026-11-20T12:00:00")
        cls.html = page.render(cls.static, cls.data)          # progress.html: the complete page
        cls.index = page.render(cls.static)                    # publish/index.html

    def test_the_published_page_holds_the_layout_the_script_and_the_static_list_only(self):
        self.assertEqual(re.findall(r"<script[^>]*>", self.index), ['<script type="application/json" id="plan-static">', "<script>"])
        self.assertNotIn("embedded-data", self.index.replace('getElementById("embedded-data")', ""))
        self.assertEqual(embedded(self.index, "plan-static"), self.static)
        self.assertNotIn("2026-11-20T12:00:00", self.index, "nothing of the current progress")

    def test_the_complete_page_is_the_same_page_with_the_data_embedded(self):
        self.assertEqual(re.findall(r"<script[^>]*>", self.html),
                         ['<script type="application/json" id="plan-static">', '<script type="application/json" id="embedded-data">', "<script>"])
        self.assertEqual(embedded(self.html, "embedded-data"), self.data)
        self.assertEqual(self.html.replace('\n<script type="application/json" id="embedded-data">%s</script>' % page.embed(self.data), ""),
                         self.index)

    def test_the_published_page_says_what_to_do_when_its_data_cannot_be_loaded(self):
        """The text and the three seconds are in the page, and `Loading...` is replaced by that path (the script itself is run by
        ScriptTest when node is installed)."""
        for html in (self.html, self.index):
            self.assertEqual(page.NO_DATA, "The progress data could not be loaded. On the PC, open progress.html in the folder above this one.")
            self.assertIn('textContent = "%s"' % page.NO_DATA, html)
            self.assertEqual(page.WAIT_SECONDS, 3)
            self.assertIn("setTimeout(failed, 3 * 1000)", html)
            self.assertNotIn("__WAIT__", html)
            self.assertNotIn("__NODATA__", html)
            self.assertEqual(html.count('id="when">Loading...<'), 1, "Loading... is only the starting text")
            self.assertEqual(re.findall(r'getElementById\("when"\)\.textContent = ([^;]*);', html),
                             ['"Written " + d.generated + ". %s"' % page.OLD_PAGE, '"%s"' % page.NO_DATA], "the two ways Loading... is replaced")
            self.assertIn(".catch(failed)", html)
            self.assertIn("failed();", html, "a fetch that throws at once is the same message")
            self.assertIn("if (!drawn)", html, "data that came is never overwritten by the message")

    def test_no_file_names_an_outside_address_script_font_or_style(self):
        for html in (self.html, self.index):
            for bad in ("http:", "https:", "ftp:", "//", "<link", "@import", "url(", "@font-face", "src=", "href=", "<img", "<iframe",
                        "XMLHttpRequest", "import("):
                self.assertNotIn(bad, html, bad)
            self.assertEqual(re.findall(r"<style[^>]*>", html), ["<style>"])
            self.assertEqual(re.findall(r"fetch\(([^,)]*)", html), ['"progress-data.json"'], "the one thing it loads is the data beside it")
        self.assertNotIn("//", page.data_json(self.data))

    def test_the_title_the_viewport_and_the_tokens(self):
        html = self.html
        self.assertIn("<title>Nebula Native Progress</title>", html)
        self.assertIn('<meta name="viewport" content="width=device-width, initial-scale=1">', html)
        self.assertIn(":root {", html)
        self.assertIn("@media (prefers-color-scheme: dark)", html)
        self.assertIn(':root:not([data-theme="light"])', html)
        self.assertIn(':root[data-theme="dark"]', html)
        for token in ("--bg", "--fg", "--landed", "--started", "--blocked", "--open"):
            self.assertIn(token + ":", html)

    def test_it_uses_system_fonts_and_has_a_background_and_no_sideways_scroll(self):
        html = self.html
        self.assertIn("system-ui", html)
        self.assertRegex(html, r"body \{[^}]*background: var\(--bg\)")
        self.assertIn("overflow-wrap: anywhere", html)
        self.assertNotRegex(html, r"min-width: *[0-9]{3,}px")
        self.assertNotIn("white-space: nowrap", html)
        self.assertIn("padding: 20px 16px", html, "a 16px gutter")

    def test_colours_are_only_in_the_two_token_blocks(self):
        css = re.search(r"<style>(.*?)</style>", self.html, re.S).group(1)
        outside = re.sub(r":root[^{]*\{[^}]*\}", "", css)
        self.assertNotRegex(outside, r"#[0-9a-fA-F]{3,8}\b", "no colour literal outside :root")
        self.assertNotRegex(outside, r"\brgba?\(")

    def test_every_section_the_plan_names_is_there(self):
        for words in ("steps landed", "Packages", "All steps", "Last landings", "Started", "Blocked", "Tool faults", "Flakes", "Claims",
                      "First error", "Could not", "Run folder", "Last seen", "First seen"):
            self.assertIn(words, self.html)


@unittest.skipUnless(shutil.which("node"), "node is not installed")
class ScriptTest(unittest.TestCase):
    """The page's own script, run by node with a stand-in for the browser's document, over data from a 138-step plan: the
    complete page (progress.html, the data inside), the published page with its data fetched, and the published page when
    the data cannot be loaded (at once, or not within three seconds)."""

    DRIVER = r"""
const fs = require('fs');
const [htmlPath, jsonPath, mode] = process.argv.slice(2);
const html = fs.readFileSync(htmlPath, 'utf8');
const stat = html.match(/<script type="application\/json" id="plan-static">([\s\S]*?)<\/script>/)[1];
const embm = html.match(/<script type="application\/json" id="embedded-data">([\s\S]*?)<\/script>/);
const code = html.match(/<script>([\s\S]*?)<\/script>/)[1];
function Node(tag) { this.tag = tag; this.children = []; this.style = {}; this.className = ''; this._text = ''; this.hidden = false; this.title = ''; }
Node.prototype.appendChild = function (c) { this.children.push(c); return c; };
Object.defineProperty(Node.prototype, 'textContent', {
  set: function (v) { this._text = String(v); if (v === '') this.children = []; },
  get: function () { return this._text + this.children.map(function (c) { return c.textContent; }).join(' '); }
});
const ids = { app: new Node('div'), when: new Node('div'), 'plan-static': { textContent: stat } };
ids.when.textContent = 'Loading...';
if (embm) ids['embedded-data'] = { textContent: embm[1] };
global.document = { getElementById: function (id) { return ids[id]; }, createElement: function (t) { return new Node(t); },
                    createTextNode: function (t) { return { textContent: t }; } };
const data = function () { return JSON.parse(fs.readFileSync(jsonPath, 'utf8')); };
let fetched = 0;
if (mode === 'fetch') {
  global.fetch = function () { fetched++; return Promise.resolve({ ok: true, json: function () { return Promise.resolve(data()); } }); };
} else if (mode === 'throws') {
  global.fetch = function () { fetched++; throw new TypeError('no fetch from a file'); };
} else if (mode === 'missing') {
  global.fetch = function () { fetched++; return Promise.resolve({ ok: false, status: 404, json: function () { return Promise.reject(new Error('no')); } }); };
} else if (mode === 'never') {
  global.fetch = function () { fetched++; return new Promise(function () {}); };
} else if (mode === 'late') {
  global.fetch = function () { fetched++; return new Promise(function (resolve) { setTimeout(function () {
    resolve({ ok: true, json: function () { return Promise.resolve(data()); } }); }, 3300); }); };
} else {
  global.fetch = function () { fetched++; return Promise.reject(new TypeError('Failed to fetch')); };
}
try { (0, eval)(code); } catch (e) { console.log('SCRIPT ERROR ' + e); process.exit(1); }
const snap = function () { return { when: ids.when.textContent, app: ids.app.textContent, sections: ids.app.children.length, fetched: fetched }; };
let early = null;
setTimeout(function () { early = snap(); }, 2500);
setTimeout(function () { console.log(JSON.stringify({ early: early, late: snap() })); }, mode === 'late' ? 3900 : (mode === 'never' ? 3500 : 2600));
"""

    def setUp(self):
        self.dir = pathlib.Path(tempfile.mkdtemp(prefix="plan-page-js-"))
        self.addCleanup(shutil.rmtree, self.dir, True)
        self.live = self.dir / "live"
        steps = [("S%d.%d" % (i // 6, i % 6), "S%d" % (i // 6), "", "landed" if i < 40 else "open") for i in range(138)]
        tree = self.dir / "tree"
        (tree / "docs" / "plan" / "native").mkdir(parents=True)
        (tree / "docs" / "plan" / "native" / "steps.tsv").write_text(
            "\t".join(plan.STEP_COLUMNS) + "\n" + "".join("\t".join([str(i + 1), s[0], s[1], "Title " + s[0], s[2], "03-X.md", s[3], "c%d" % i if s[3] == "landed" else ""]) + "\n"
                                                           for i, s in enumerate(steps)), encoding="utf-8")
        self.tree = tree
        for i in range(40):
            lv.add_landing(self.live, "S%d.%d" % (i // 6, i % 6), "c%d" % i, 5, 1, NOW + datetime.timedelta(minutes=i))
        lv.append_fault(self.live, 1, "S0.1", "TOOL", "harness/self", "result.json is missing", could_not="land S0.1", now=NOW)
        lv.set_state_entry(self.live, "S6.4", "started", 2, now=NOW)
        lv.set_state_entry(self.live, "S7.0", "blocked", 1, "the reason", NOW)
        with mock.patch.object(plan, "now", lambda: NOW):
            self.written = plan.write_page(self.tree, self.live)
        self.html, self.index, self.json, self.data = (self.written.paths.html, self.written.paths.index, self.written.paths.data,
                                                       self.written.data)
        (self.dir / "driver.js").write_text(self.DRIVER, encoding="utf-8")

    def node(self, html, mode):
        r = subprocess.run(["node", str(self.dir / "driver.js"), str(html), str(self.json), mode], capture_output=True, text=True,
                           encoding="utf-8", timeout=60)
        self.assertEqual(r.returncode, 0, r.stdout + r.stderr)
        return json.loads(r.stdout.strip().splitlines()[-1])

    def test_the_repairs_are_drawn_open_fail_first_with_check_renderer_traced_landing_seen_count_and_state(self):
        lv.add_repair(self.live, "nightly", "fail", "tour/d3d11", "d3d11", "S6.3", "abc1234", "D:/NebulaWork/build/gate-runs/n1", "P-242",
                      "stop 025 differs", now=NOW)
        lv.add_repair(self.live, "nightly", "fail", "tour/d3d11", "d3d11", "S6.3", "abc1234", "D:/ev", "P-242", "again", now=NOW)
        lv.add_repair(self.live, "stress", "stress", "soak", "-", "-", "-", "D:/ev/soak", "-", "slow leak", now=NOW)
        with mock.patch.object(plan, "now", lambda: NOW):
            self.written = plan.write_page(self.tree, self.live)
        self.json = self.written.paths.data
        for html in (self.written.paths.html, self.written.paths.index):
            app = self.node(html, "fetch")["late"]["app"]
            self.assertIn("Repairs (1 open of kind fail, 2 in all)", app)
            self.assertIn("tour/d3d11 on d3d11", app)
            self.assertIn("step S6.3, commit abc1234", app)
            self.assertIn("2 times", app)
            self.assertIn("not traced to a landing", app)
            self.assertIn("D:/NebulaWork/build/gate-runs/n1", app)
            self.assertLess(app.index("tour/d3d11 on d3d11"), app.index("soak"), "open fail first")

    def test_the_real_plan_data_has_138_steps_and_40_landed(self):
        static = embedded(self.html.read_text(encoding="utf-8"), "plan-static")
        self.assertEqual(static["total"], 138)
        self.assertEqual(sum(1 for e in self.data["steps"].values() if e["state"] == "landed"), 40)

    def test_the_script_parses(self):
        script = re.search(r"<script>(.*?)</script>", self.html.read_text(encoding="utf-8"), re.S).group(1)
        (self.dir / "page.js").write_text(script, encoding="utf-8")
        r = subprocess.run(["node", "--check", str(self.dir / "page.js")], capture_output=True, text=True, timeout=60)
        self.assertEqual(r.returncode, 0, r.stderr)

    def test_the_published_page_with_the_fetched_data_draws_the_numbers(self):
        out = self.node(self.index, "fetch")["late"]
        self.assertEqual(out["when"], "Written 2026-11-20T12:00:00. " + page.OLD_PAGE)
        for words in ("40 of 138", "steps landed", "6/6", "4/6", "result.json is missing", "land S0.1", "agent 2", "S6.4", "the reason",
                      "Tool faults (1 kinds", "Title S6.4", "commit c39"):
            self.assertIn(words, out["app"])
        self.assertGreaterEqual(out["sections"], 10)
        landings = out["app"].split("Last landings")[1].split("Started")[0]
        self.assertLess(landings.index("S6.3"), landings.index("S6.2"), "newest landing first")
        self.assertEqual(landings.count("commit c"), 10, "the last ten")

    def test_the_complete_page_draws_the_same_numbers_from_the_data_inside_and_never_fetches(self):
        fetched = self.node(self.index, "fetch")["late"]
        for mode in ("fetch", "rejects", "throws", "never"):
            out = self.node(self.html, mode)
            for snap in (out["early"], out["late"]):
                self.assertEqual(snap["fetched"], 0, mode)
                self.assertEqual(snap["app"], fetched["app"], mode)
                self.assertEqual(snap["when"], fetched["when"], mode)

    def test_the_published_page_says_it_at_once_when_the_data_cannot_be_read(self):
        for mode in ("rejects", "throws", "missing"):
            out = self.node(self.index, mode)
            self.assertEqual(out["early"]["when"], page.NO_DATA, mode)
            self.assertEqual(out["late"]["when"], page.NO_DATA, mode)
            self.assertNotIn("Loading", out["late"]["when"])
            self.assertEqual(out["late"]["app"], "", "nothing drawn")

    def test_the_published_page_says_it_when_the_data_has_not_come_in_three_seconds(self):
        out = self.node(self.index, "never")
        self.assertEqual(out["early"]["when"], "Loading...", "at 2.5 seconds it still waits")
        self.assertEqual(out["late"]["when"], page.NO_DATA, "after 3 seconds it says so in place of Loading...")

    def test_data_that_comes_after_the_message_is_drawn_all_the_same(self):
        out = self.node(self.index, "late")
        self.assertEqual(out["early"]["when"], "Loading...")
        self.assertEqual(out["late"]["when"], "Written 2026-11-20T12:00:00. " + page.OLD_PAGE)
        self.assertIn("40 of 138", out["late"]["app"])


def main():
    result = unittest.main(argv=[sys.argv[0]], exit=False, verbosity=1).result
    print("plan_page_test: %s (%d tests)" % ("pass" if result.wasSuccessful() else "FAIL", result.testsRun))
    return 0 if result.wasSuccessful() else 1


if __name__ == "__main__":
    sys.exit(main())
