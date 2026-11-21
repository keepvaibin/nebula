"""plan_page_test.py: the progress page (package T13, item 6), three files: progress.html (static: layout, script and the
step list, rewritten only when the tool or the step list changes), progress-data.json (only what changes) and
progress-local.html (the data embedded, for opening from disk). The data matches steps.tsv, the live state, BLOCKED.md,
tool-faults.tsv and tests/flakes.tsv; no file names an outside address; the page's script draws the data (run by node when
node is installed). A temporary live folder each: no game, no build, no network, nothing of D:/NebulaWork.

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
        landings = [{"step": "A.1", "commit": "abc123", "finished": "2026-11-20T10:00:00", "seconds": "5", "agent": "1"}]
        d = self.data(steps, landings)
        self.assertEqual(d["steps"], {"A.1": {"state": "landed", "commit": "abc123", "time": "2026-11-20T10:00:00"},
                                      "A.2": {"state": "started", "agent": "2"}, "B.1": {"state": "blocked", "agent": "1"},
                                      "C.1": {"state": "landed"}})
        self.assertNotIn("B.2", d["steps"])
        self.assertEqual(sorted(d), ["blocked", "claims", "flakes", "generated", "steps", "tool_faults"])

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

    def files(self):
        return [self.t.live / n for n in ("progress.html", "progress-data.json", "progress-local.html")]

    def page(self):
        code, text = self.t.run(["page"], expect=0)
        html, data, local = self.files()
        return json.loads(data.read_text(encoding="utf-8")), html.read_text(encoding="utf-8"), local.read_text(encoding="utf-8"), text

    def test_page_writes_three_files_prints_their_paths_and_says_which_two_to_publish(self):
        data, html, local, text = self.page()
        self.assertEqual(sorted(p.name for p in self.t.live.iterdir()), ["progress-data.json", "progress-local.html", "progress.html"])
        for p in self.files():
            self.assertIn(p.as_posix(), text)
            self.assertIn("(%d bytes)" % p.stat().st_size, text)
        self.assertRegex(text, r"progress\.html: .*publish\n")
        self.assertRegex(text, r"progress-data\.json: .*publish\n")
        self.assertRegex(text, r"progress-local\.html: .*do not publish")
        self.assertIn("Publish progress.html with progress-data.json as its supporting file", text)

    def test_the_static_file_does_not_change_when_a_step_lands_and_the_data_does(self):
        write_steps(self.t, [("A.1", "A", "", "open"), ("A.2", "A", "", "open"), ("B.1", "B", "A", "open"), ("C.1", "C", "", "open"),
                             ("D.1", "D", "everything", "open")])
        self.page()
        html_path, data_path, local_path = self.files()
        html_bytes, html_time = html_path.read_bytes(), html_path.stat().st_mtime_ns
        data_bytes, local_bytes = data_path.read_bytes(), local_path.read_bytes()
        write_steps(self.t, [("A.1", "A", "", "landed"), ("A.2", "A", "", "open"), ("B.1", "B", "A", "open"), ("C.1", "C", "", "open"),
                             ("D.1", "D", "everything", "open")])
        lv.add_landing(self.t.live, "A.1", "abc", 60, 1, NOW)
        self.page()
        self.assertEqual(html_path.read_bytes(), html_bytes)
        self.assertEqual(html_path.stat().st_mtime_ns, html_time, "not even rewritten")
        self.assertNotEqual(data_path.read_bytes(), data_bytes)
        self.assertNotEqual(local_path.read_bytes(), local_bytes)
        self.assertEqual(json.loads(data_path.read_text(encoding="utf-8"))["steps"]["A.1"],
                         {"state": "landed", "commit": "abc", "time": "2026-11-20T12:00:00"})

    def test_the_static_file_changes_when_the_step_list_changes(self):
        self.page()
        before = self.files()[0].read_bytes()
        path = plan.native(self.t.tree) / "steps.tsv"
        path.write_text(path.read_text(encoding="utf-8").replace("Second step", "Second step, renamed"), encoding="utf-8")
        self.page()
        after = self.files()[0].read_bytes()
        self.assertNotEqual(after, before)
        self.assertIn(b"Second step, renamed", after)

    def test_the_static_file_does_not_change_with_blocked_claims_faults_or_flakes(self):
        self.page()
        before = self.files()[0].read_bytes()
        self.t.run(["start", "A.1"], expect=0)
        self.t.run(["blocked", "A.1", "stuck"], expect=0)
        self.t.run(["tool", "the runner broke"], expect=0)
        (self.t.tree / "tests").mkdir(exist_ok=True)
        (self.t.tree / "tests" / "flakes.tsv").write_text("date\ttest\n2026-11-19\tt1\n", encoding="utf-8")
        self.page()
        self.assertEqual(self.files()[0].read_bytes(), before)

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
        (self.t.tree / "tests").mkdir(exist_ok=True)
        (self.t.tree / "tests" / "flakes.tsv").write_text("date\ttest\trenderer\tcommit\tmessage\n2026-11-19\tt1\td3d11\tabc\tx\n"
                                                           "2026-11-20\tt2\tvk\tabc\ty\n", encoding="utf-8")
        data, html, local, _ = self.page()
        base = plan.read_base(self.t.tree)
        static = embedded(html, "plan-static")
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
        self.assertEqual(data["generated"], "2026-11-20T12:00:00")
        self.assertEqual(embedded(local, "embedded-data"), data, "the local page embeds the same data")
        self.assertEqual(embedded(local, "plan-static"), static)
        self.assertIsNone(embedded(html, "embedded-data"), "the published page has no data in it")

    def test_a_blocked_reason_falls_back_to_blocked_md(self):
        lv.set_state_entry(self.t.live, "C.1", "blocked", 1, "")
        plan.append_under(plan.native(self.t.tree) / "BLOCKED.md", "steps", "- 2026-11-20 C.1: the reason in the file | first failure line: x")
        data = self.page()[0]
        self.assertEqual(data["blocked"][0]["reason"], "the reason in the file | first failure line: x")

    def test_the_numbers_move_with_the_state_after_each_command(self):
        read = lambda: json.loads((self.t.live / "progress-data.json").read_text(encoding="utf-8"))
        self.t.run(["start", "A.1"], expect=0)
        self.assertEqual(read()["steps"], {"A.1": {"state": "started", "agent": "1"}})
        self.t.run(["blocked", "A.1", "stuck here"], expect=0)
        d = read()
        self.assertEqual((d["steps"]["A.1"]["state"], d["blocked"][0]["reason"]), ("blocked", "stuck here"))
        self.t.run(["tool", "the runner broke"], expect=0)
        self.assertEqual(read()["tool_faults"][0]["first_error"], "the runner broke")
        self.t.run(["toolfault", "--last", "--could-not", "run the tour"], expect=0)
        self.assertEqual(read()["tool_faults"][0]["could_not"], "run the tour")

    def test_the_data_and_local_files_are_made_again_each_time_and_the_data_is_the_same_when_nothing_changed(self):
        self.page()
        data_before = self.files()[1].read_bytes()
        self.page()
        self.assertEqual(self.files()[1].read_bytes(), data_before)

    def test_a_live_folder_that_is_not_there_is_made(self):
        shutil.rmtree(self.t.live, ignore_errors=True)
        self.page()
        self.assertTrue(all(p.is_file() for p in self.files()))


class HtmlTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        steps = plan_of(step("A.1", "A"))
        cls.static = page.make_static(steps)
        cls.data = page.make_data(steps, [], {}, [], set(), [], 0, "2026-11-20T12:00:00")
        cls.html = page.render(cls.static)
        cls.local = page.render(cls.static, cls.data)

    def test_the_published_page_holds_the_layout_the_script_and_the_static_list_only(self):
        self.assertEqual(re.findall(r"<script[^>]*>", self.html), ['<script type="application/json" id="plan-static">', "<script>"])
        self.assertNotIn("embedded-data", self.html.replace('getElementById("embedded-data")', ""))
        self.assertEqual(embedded(self.html, "plan-static"), self.static)
        self.assertNotIn("2026-11-20T12:00:00", self.html, "nothing of the current progress")

    def test_the_local_page_is_the_same_page_with_the_data_embedded(self):
        self.assertEqual(re.findall(r"<script[^>]*>", self.local),
                         ['<script type="application/json" id="plan-static">', '<script type="application/json" id="embedded-data">', "<script>"])
        self.assertEqual(embedded(self.local, "embedded-data"), self.data)
        self.assertEqual(self.local.replace('\n<script type="application/json" id="embedded-data">%s</script>' % page.embed(self.data), ""),
                         self.html)

    def test_no_file_names_an_outside_address_script_font_or_style(self):
        for html in (self.html, self.local):
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
    published pair (progress.html with the data fetched) and the local page (the data embedded) draw the same numbers."""

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
const ids = { app: new Node('div'), when: new Node('div'), note: new Node('div'), 'plan-static': { textContent: stat } };
if (embm) ids['embedded-data'] = { textContent: mode === 'bad' ? 'not json' : embm[1] };
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
            "\t".join(plan.STEP_COLUMNS) + "\n" + "".join("\t".join([str(i + 1), s[0], s[1], "Title " + s[0], s[2], "03-X.md", s[3], "c%d" % i if s[3] == "landed" else ""]) + "\n"
                                                           for i, s in enumerate(steps)), encoding="utf-8")
        self.tree = tree
        for i in range(40):
            lv.add_landing(self.live, "S%d.%d" % (i // 6, i % 6), "c%d" % i, 5, 1, NOW + datetime.timedelta(minutes=i))
        lv.append_fault(self.live, 1, "S0.1", "TOOL", "harness/self", "result.json is missing", could_not="land S0.1", now=NOW)
        lv.set_state_entry(self.live, "S6.4", "started", 2, now=NOW)
        lv.set_state_entry(self.live, "S7.0", "blocked", 1, "the reason", NOW)
        with mock.patch.object(plan, "now", lambda: NOW):
            self.html, self.json, self.local, self.data = plan.write_page(self.tree, self.live)
        (self.dir / "driver.js").write_text(self.DRIVER, encoding="utf-8")

    def node(self, html, mode):
        r = subprocess.run(["node", str(self.dir / "driver.js"), str(html), str(self.json), mode], capture_output=True, text=True,
                           encoding="utf-8", timeout=60)
        self.assertEqual(r.returncode, 0, r.stdout + r.stderr)
        return json.loads(r.stdout.strip().splitlines()[-1])

    def test_the_real_plan_data_has_138_steps_and_40_landed(self):
        static = embedded(self.html.read_text(encoding="utf-8"), "plan-static")
        self.assertEqual(static["total"], 138)
        self.assertEqual(sum(1 for e in self.data["steps"].values() if e["state"] == "landed"), 40)

    def test_the_script_parses(self):
        script = re.search(r"<script>(.*?)</script>", self.html.read_text(encoding="utf-8"), re.S).group(1)
        (self.dir / "page.js").write_text(script, encoding="utf-8")
        r = subprocess.run(["node", "--check", str(self.dir / "page.js")], capture_output=True, text=True, timeout=60)
        self.assertEqual(r.returncode, 0, r.stderr)

    def test_the_static_page_with_the_fetched_data_draws_the_numbers(self):
        out = self.node(self.html, "fetch")
        self.assertEqual(out["when"], "Written 2026-11-20T12:00:00")
        self.assertEqual(out["note"], "", "fetch worked: no note about the embedded copy")
        for words in ("40 of 138", "steps landed", "6/6", "4/6", "result.json is missing", "land S0.1", "agent 2", "S6.4", "the reason",
                      "Tool faults (1 kinds", "Title S6.4", "commit c39"):
            self.assertIn(words, out["app"])
        self.assertGreaterEqual(out["sections"], 10)
        landings = out["app"].split("Last landings")[1].split("Started")[0]
        self.assertLess(landings.index("S6.3"), landings.index("S6.2"), "newest landing first")
        self.assertEqual(landings.count("commit c"), 10, "the last ten")

    def test_the_local_page_draws_the_same_numbers_from_the_embedded_data(self):
        fetched = self.node(self.html, "fetch")
        for mode in ("rejects", "throws"):
            local = self.node(self.local, mode)
            self.assertFalse(local["noteHidden"], mode)
            self.assertIn("Shown from the data written into this page", local["note"], mode)
            self.assertEqual(local["app"], fetched["app"], mode)
            self.assertEqual(local["when"], fetched["when"])

    def test_the_static_page_with_no_data_says_so_and_does_not_throw(self):
        out = self.node(self.html, "rejects")
        self.assertIn("No data", out["when"])
        out = self.node(self.local, "bad")
        self.assertIn("No data", out["when"])


def main():
    result = unittest.main(argv=[sys.argv[0]], exit=False, verbosity=1).result
    print("plan_page_test: %s (%d tests)" % ("pass" if result.wasSuccessful() else "FAIL", result.testsRun))
    return 0 if result.wasSuccessful() else 1


if __name__ == "__main__":
    sys.exit(main())
