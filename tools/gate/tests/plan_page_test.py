"""plan_page_test.py: the progress page (package T13, item 6): `plan.py page` writes progress-data.json and progress.html; the
data matches steps.tsv, the live state, BLOCKED.md, tool-faults.tsv and tests/flakes.tsv; the page names no outside address;
the embedded copy is the same data; the page's script draws it (run by node when node is installed). A temporary live
folder each: no game, no build, no network, nothing of D:/NebulaWork.

    python -X utf8 -I tools/gate/tests/plan_page_test.py [--work <folder>]
"""
import datetime
import json
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
from plan_fixture import Tree, plan_of, setup_module, step, teardown_module, write_steps  # noqa: E402

NOW = datetime.datetime(2026, 11, 20, 12, 0, 0)


def setUpModule():
    setup_module()


def tearDownModule():
    teardown_module()


def embedded(html):
    m = re.search(r'<script type="application/json" id="embedded-data">(.*?)</script>', html, re.S)
    return json.loads(m.group(1))


class DataTest(unittest.TestCase):
    def data(self, steps, landings=(), state=None, claims=(), stale=(), faults=(), flakes=0):
        return page.make_data(steps, list(landings), state or {}, list(claims), set(stale), list(faults), flakes, "2026-11-20T12:00:00")

    def test_totals_packages_and_every_step_by_state(self):
        steps = plan_of(step("A.1", "A", state="landed"), step("A.2", "A", state="started", agent="2"), step("B.1", "B", state="blocked"),
                        step("B.2", "B"), step("C.1", "C", state="landed"))
        steps[0]["commit"] = "abc123"
        d = self.data(steps)
        self.assertEqual((d["total"], d["landed"], d["title"]), (5, 2, "Nebula Native Progress"))
        self.assertEqual(d["packages"], [{"package": "A", "total": 2, "landed": 1, "started": 1, "blocked": 0},
                                         {"package": "B", "total": 2, "landed": 0, "started": 0, "blocked": 1},
                                         {"package": "C", "total": 1, "landed": 1, "started": 0, "blocked": 0}])
        self.assertEqual([(s["id"], s["state"], s["agent"], s["commit"]) for s in d["steps"]],
                         [("A.1", "landed", "", "abc123"), ("A.2", "started", "2", ""), ("B.1", "blocked", "1", ""),
                          ("B.2", "open", "", ""), ("C.1", "landed", "", "")])
        self.assertEqual(d["started"], [{"step": "A.2", "title": "t", "agent": "2", "branch": "native/A.2"}])

    def test_the_last_ten_landings_newest_first_with_the_step_titles(self):
        steps = plan_of(*[step("S%d" % i, "S", title="title %d" % i, state="landed") for i in range(15)])
        landings = [{"step": "S%d" % i, "commit": "c%d" % i, "finished": "2026-11-20T10:%02d:00" % i, "seconds": "5", "agent": "1"}
                    for i in range(15)]
        d = self.data(steps, landings)
        self.assertEqual([l["step"] for l in d["landings"]], ["S%d" % i for i in range(14, 4, -1)])
        self.assertEqual(d["landings"][0], {"step": "S14", "title": "title 14", "time": "2026-11-20T10:14:00", "commit": "c14"})

    def test_blocked_steps_carry_their_reason_and_agent(self):
        steps = plan_of(step("A.1", "A", state="blocked", agent="2"))
        d = self.data(steps, state={"A.1": {"reason": "the card is wrong"}})
        self.assertEqual(d["blocked"], [{"step": "A.1", "title": "t", "agent": "2", "reason": "the card is wrong"}])

    def test_claims_with_the_stale_mark_and_the_flake_count(self):
        d = self.data(plan_of(step("A.1", "A")), claims=[{"package": "G6", "agent": "2", "time": "t", "branch": "b"},
                                                         {"package": "G7", "agent": "1", "time": "t", "branch": ""}],
                      stale=["G7"], flakes=4)
        self.assertEqual([(c["package"], c["stale"]) for c in d["claims"]], [("G6", False), ("G7", True)])
        self.assertEqual(d["flakes"], 4)

    def test_the_embedded_text_cannot_end_the_script_tag(self):
        d = self.data(plan_of(step("A.1", "A", state="blocked")), state={"A.1": {"reason": "</script><b>x</b><!-- y"}})
        html = page.render(d)
        self.assertEqual(html.count("</script>"), 2, "the data script and the page's own, no more")
        self.assertEqual(embedded(html)["blocked"][0]["reason"], "</script><b>x</b><!-- y")

    def test_the_data_file_is_small_json_that_round_trips(self):
        d = self.data(plan_of(*[step("S%d.%d" % (i // 5, i % 5), "S%d" % (i // 5), title="A step of the plan %d" % i) for i in range(138)]))
        text = page.data_json(d)
        self.assertEqual(json.loads(text), d)
        self.assertLess(len(text.encode("utf-8")), 30000)
        self.assertTrue(text.endswith("\n"))


class PageFilesTest(unittest.TestCase):
    def setUp(self):
        self.t = Tree().__enter__()
        self.addCleanup(self.t.__exit__)
        self.fixed = mock.patch.object(plan, "now", lambda: NOW)
        self.fixed.start()
        self.addCleanup(self.fixed.stop)

    def page(self):
        code, text = self.t.run(["page"], expect=0)
        return (json.loads((self.t.live / "progress-data.json").read_text(encoding="utf-8")),
                (self.t.live / "progress.html").read_text(encoding="utf-8"), text)

    def test_page_writes_the_two_files_and_says_where_and_how_to_publish(self):
        data, html, text = self.page()
        self.assertIn("progress.html: ", text)
        self.assertIn("progress-data.json: ", text)
        self.assertIn("(%d bytes)" % (self.t.live / "progress.html").stat().st_size, text)
        self.assertIn("Publish progress.html with progress-data.json as its supporting file; keep the address in", text)
        self.assertIn("progress-url.txt", text)
        self.assertEqual(sorted(p.name for p in self.t.live.iterdir()), ["progress-data.json", "progress.html"])

    def test_the_data_matches_the_plan_files(self):
        steps_text = (plan.native(self.t.tree) / "steps.tsv").read_text(encoding="utf-8")
        write_steps(self.t, [("A.1", "A", "", "landed"), ("A.2", "A", "", "open"), ("B.1", "B", "A", "open"), ("C.1", "C", "", "open"),
                             ("D.1", "D", "everything", "open")])
        plan.set_state(self.t.tree, "A.2", "started", agent=1)
        plan.set_state(self.t.tree, "C.1", "blocked", agent=1, reason="the owner's choice is needed")
        lv.add_landing(self.t.live, "A.1", "abc", 90, 1, NOW - datetime.timedelta(hours=2))
        lv.put_claim(self.t.live, "A", 1, "native/A.2", NOW)
        for _ in range(2):
            lv.append_fault(self.t.live, 1, "A.2", "TOOL", "harness/self", "result.json is missing", now=NOW)
        lv.append_fault(self.t.live, 2, "G6.1", "ENV", "start/vk", "no device", run_folder="D:/runs/9", now=NOW)
        (self.t.tree / "tests").mkdir(exist_ok=True)
        (self.t.tree / "tests" / "flakes.tsv").write_text("date\ttest\trenderer\tcommit\tmessage\n2026-11-19\tt1\td3d11\tabc\tx\n"
                                                           "2026-11-20\tt2\tvk\tabc\ty\n", encoding="utf-8")
        data, html, _ = self.page()
        base = plan.read_base(self.t.tree)
        self.assertEqual(data["total"], len(base))
        self.assertEqual(data["landed"], sum(1 for s in base if s["state"] == "landed"))
        self.assertEqual([s["id"] for s in data["steps"]], [s["step"] for s in base], "the order of steps.tsv")
        self.assertEqual({s["id"]: s["state"] for s in data["steps"]},
                         {"A.1": "landed", "A.2": "started", "B.1": "open", "C.1": "blocked", "D.1": "open"})
        self.assertEqual([(p["package"], p["total"], p["landed"], p["started"], p["blocked"]) for p in data["packages"]],
                         [("A", 2, 1, 1, 0), ("B", 1, 0, 0, 0), ("C", 1, 0, 0, 1), ("D", 1, 0, 0, 0)])
        self.assertEqual(data["blocked"][0]["reason"], "the owner's choice is needed")
        self.assertEqual(data["landings"], [{"step": "A.1", "title": "Title of A.1", "time": "2026-11-20T10:00:00", "commit": "abc"}])
        self.assertEqual(data["started"][0]["step"], "A.2")
        self.assertEqual([(c["package"], c["agent"]) for c in data["claims"]], [("A", "1")])
        self.assertEqual([(f["count"], f["check"], f["agent"]) for f in data["tool_faults"]], [(2, "harness/self", "1"), (1, "start/vk", "2")])
        self.assertEqual(data["tool_faults"][1]["run_folder"], "D:/runs/9")
        self.assertEqual(data["flakes"], 2)
        self.assertEqual(data["generated"], "2026-11-20T12:00:00")
        self.assertEqual(embedded(html), data, "the embedded copy is the same data")
        self.assertNotEqual(steps_text, (plan.native(self.t.tree) / "steps.tsv").read_text(encoding="utf-8"))

    def test_a_blocked_reason_falls_back_to_blocked_md(self):
        lv.set_state_entry(self.t.live, "C.1", "blocked", 1, "")
        plan.append_under(plan.native(self.t.tree) / "BLOCKED.md", "steps", "- 2026-11-20 C.1: the reason in the file | first failure line: x")
        data = self.page()[0]
        self.assertEqual(data["blocked"][0]["reason"], "the reason in the file | first failure line: x")

    def test_the_numbers_move_with_the_state_after_each_command(self):
        self.t.run(["start", "A.1"], expect=0)
        data = json.loads((self.t.live / "progress-data.json").read_text(encoding="utf-8"))
        self.assertEqual([s["step"] for s in data["started"]], ["A.1"])
        self.t.run(["blocked", "A.1", "stuck here"], expect=0)
        data = json.loads((self.t.live / "progress-data.json").read_text(encoding="utf-8"))
        self.assertEqual((data["started"], data["blocked"][0]["reason"]), ([], "stuck here"))
        self.t.run(["tool", "the runner broke"], expect=0)
        data = json.loads((self.t.live / "progress-data.json").read_text(encoding="utf-8"))
        self.assertEqual(data["tool_faults"][0]["first_error"], "the runner broke")
        self.t.run(["toolfault", "--last", "--could-not", "run the tour"], expect=0)
        data = json.loads((self.t.live / "progress-data.json").read_text(encoding="utf-8"))
        self.assertEqual(data["tool_faults"][0]["could_not"], "run the tour")

    def test_the_html_is_not_rewritten_when_nothing_changed(self):
        self.page()
        path = self.t.live / "progress.html"
        before = path.stat().st_mtime_ns
        data_before = (self.t.live / "progress-data.json").read_bytes()
        self.page()
        self.assertEqual(path.stat().st_mtime_ns, before)
        self.assertEqual((self.t.live / "progress-data.json").read_bytes(), data_before)
        lv.append_fault(self.t.live, 1, "S", "TOOL", "c", "e", now=NOW)
        self.page()
        self.assertNotEqual(path.stat().st_mtime_ns, before)

    def test_a_live_folder_that_is_not_there_is_made(self):
        shutil.rmtree(self.t.live, ignore_errors=True)
        self.page()
        self.assertTrue((self.t.live / "progress.html").is_file())


class HtmlTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.html = page.render(page.make_data(plan_of(step("A.1", "A")), [], {}, [], set(), [], 0, "2026-11-20T12:00:00"))

    def test_it_names_no_outside_address_script_font_or_style(self):
        html = self.html
        for bad in ("http:", "https:", "ftp:", "//", "<link", "@import", "url(", "@font-face", "src=", "href=", "<img", "<iframe",
                    "XMLHttpRequest", "import("):
            self.assertNotIn(bad, html, bad)
        self.assertEqual(re.findall(r"<script[^>]*>", html), ['<script type="application/json" id="embedded-data">', "<script>"])
        self.assertEqual(re.findall(r"<style[^>]*>", html), ["<style>"])
        self.assertEqual(re.findall(r"fetch\(([^,)]*)", html), ['"progress-data.json"'], "the one thing it loads is the data beside it")

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
    """The page's own script, run by node with a stand-in for the browser's document, over data from a real plan."""

    DRIVER = r"""
const fs = require('fs');
const [htmlPath, jsonPath, mode] = process.argv.slice(2);
const html = fs.readFileSync(htmlPath, 'utf8');
const emb = html.match(/<script type="application\/json" id="embedded-data">([\s\S]*?)<\/script>/)[1];
const code = html.match(/<script>([\s\S]*?)<\/script>/)[1];
function Node(tag) { this.tag = tag; this.children = []; this.style = {}; this.className = ''; this._text = ''; this.hidden = false; this.title = ''; }
Node.prototype.appendChild = function (c) { this.children.push(c); return c; };
Object.defineProperty(Node.prototype, 'textContent', {
  set: function (v) { this._text = String(v); if (v === '') this.children = []; },
  get: function () { return this._text + this.children.map(function (c) { return c.textContent; }).join(' '); }
});
const ids = { app: new Node('div'), when: new Node('div'), note: new Node('div'), 'embedded-data': { textContent: mode === 'bad' ? 'not json' : emb } };
global.document = { getElementById: function (id) { return ids[id]; }, createElement: function (t) { return new Node(t); },
                    createTextNode: function (t) { return { textContent: t }; } };
if (mode === 'fetch') {
  global.fetch = function () { return Promise.resolve({ ok: true, json: function () { return Promise.resolve(JSON.parse(fs.readFileSync(jsonPath, 'utf8'))); } }); };
} else if (mode === 'throws') {
  global.fetch = function () { throw new TypeError('no fetch from a file'); };
} else {
  global.fetch = function () { return Promise.reject(new TypeError('Failed to fetch')); };
}
try { (0, eval)(code); } catch (e) { console.log('SCRIPT ERROR ' + e); process.exit(1); }
setTimeout(function () {
  console.log(JSON.stringify({ when: ids.when.textContent, note: ids.note.textContent, noteHidden: ids.note.hidden, app: ids.app.textContent,
                               sections: ids.app.children.length }));
}, 100);
"""

    def setUp(self):
        self.dir = pathlib.Path(tempfile.mkdtemp(prefix="plan-page-js-"))
        self.addCleanup(shutil.rmtree, self.dir, True)
        self.live = self.dir / "live"
        steps = [("S%d.%d" % (i // 6, i % 6), "S%d" % (i // 6), "", "landed" if i < 40 else "open") for i in range(138)]
        tree = self.dir / "tree"
        (tree / "docs" / "plan" / "native").mkdir(parents=True)
        (tree / "docs" / "plan" / "native" / "steps.tsv").write_text(
            "\t".join(plan.STEP_COLUMNS) + "\n" + "".join("\t".join([str(i + 1), s[0], s[1], "Title " + s[0], s[2], "03-X.md", s[3], ""]) + "\n"
                                                           for i, s in enumerate(steps)), encoding="utf-8")
        self.tree = tree
        lv.append_fault(self.live, 1, "S0.1", "TOOL", "harness/self", "result.json is missing", could_not="land S0.1", now=NOW)
        lv.set_state_entry(self.live, "S6.4", "started", 2, now=NOW)
        lv.set_state_entry(self.live, "S7.0", "blocked", 1, "the reason", NOW)
        with mock.patch.object(plan, "now", lambda: NOW):
            self.html, self.json, self.data = plan.write_page(self.tree, self.live)
        (self.dir / "driver.js").write_text(self.DRIVER, encoding="utf-8")

    def node(self, mode):
        r = subprocess.run(["node", str(self.dir / "driver.js"), str(self.html), str(self.json), mode], capture_output=True, text=True,
                           encoding="utf-8", timeout=60)
        self.assertEqual(r.returncode, 0, r.stdout + r.stderr)
        return json.loads(r.stdout.strip().splitlines()[-1])

    def test_the_real_plan_data_has_138_steps(self):
        self.assertEqual((self.data["total"], self.data["landed"]), (138, 40))

    def test_the_script_parses(self):
        script = re.search(r"<script>(.*?)</script>", self.html.read_text(encoding="utf-8"), re.S).group(1)
        (self.dir / "page.js").write_text(script, encoding="utf-8")
        r = subprocess.run(["node", "--check", str(self.dir / "page.js")], capture_output=True, text=True, timeout=60)
        self.assertEqual(r.returncode, 0, r.stderr)

    def test_it_draws_the_data_it_fetches(self):
        out = self.node("fetch")
        self.assertEqual(out["when"], "Written 2026-11-20T12:00:00")
        self.assertEqual(out["note"], "", "fetch worked: no note about the embedded copy")
        for words in ("40 of 138", "steps landed", "6/6", "4/6", "result.json is missing", "land S0.1", "agent 2", "S6.4", "the reason",
                      "Tool faults (1 kinds"):
            self.assertIn(words, out["app"])
        self.assertGreaterEqual(out["sections"], 10)

    def test_opened_from_disk_where_fetch_fails_it_draws_the_embedded_copy_and_says_so(self):
        for mode in ("rejects", "throws"):
            out = self.node(mode)
            self.assertIn("40 of 138", out["app"], mode)
            self.assertFalse(out["noteHidden"], mode)
            self.assertIn("Shown from the data written into this page", out["note"], mode)

    def test_with_no_data_at_all_it_says_so_and_does_not_throw(self):
        out = self.node("bad")
        self.assertIn("No data", out["when"])


def main():
    result = unittest.main(argv=[sys.argv[0]], exit=False, verbosity=1).result
    print("plan_page_test: %s (%d tests)" % ("pass" if result.wasSuccessful() else "FAIL", result.testsRun))
    return 0 if result.wasSuccessful() else 1


if __name__ == "__main__":
    sys.exit(main())
