"""page.py: the native plan's progress page (package T13, item 6).

Three files, so that each update is cheap for the agent that publishes it after every landing:

  progress.html        static: the layout, the script and what never changes with progress (the step ids, titles,
                       packages and their order, embedded once from steps.tsv). Rewritten only when this tool or the
                       plan's step list changes, never because progress changed.
  progress-data.json   only what changes: per step its state, agent, commit and landing time (keyed by step id; a step
                       still open is not in it), the blocked list with reasons, the claims, the tool-fault table, the
                       flake count, a generated-at time. Compact JSON.
  progress-local.html  the same page with the data embedded, for opening from disk (a page opened from disk cannot fetch
                       the data file). Not published.

The published pair is progress.html plus progress-data.json. The page fetches `progress-data.json` from beside it; when
that fails it draws the data embedded in the page, if there is any (progress-local.html), else it says so. One
self-contained file each: no outside script, font or style, no address of any kind. Light and dark by
prefers-color-scheme, colours as tokens on :root, system fonts, readable at phone width with no sideways scroll.
"""
import json

TITLE = "Nebula Native Progress"
LAST_LANDINGS = 10


def make_static(steps):
    """What never changes with progress: the step ids, titles, packages and order."""
    order, totals = [], {}
    for s in steps:
        if s["package"] not in totals:
            order.append(s["package"])
            totals[s["package"]] = 0
        totals[s["package"]] += 1
    return {"title": TITLE, "total": len(steps), "packages": [{"package": p, "total": totals[p]} for p in order],
            "steps": [{"id": s["step"], "package": s["package"], "title": s["title"]} for s in steps]}


def make_data(steps, landings, state, claims, stale, faults, flakes, now, set_aside=()):
    """What changes. `steps`: plan.read_steps (state, commit, agent). `landings`: live.read_landings. `state`:
    live.read_state. `claims`: live.read_claims. `stale`: set of stale package names. `faults`: live.fault_groups.
    `flakes`: a count. `now`: the time of writing (text). `set_aside`: [{test, renderer, since, why}] of the tests the gate
    has set aside (its flakes.tsv and set-aside.tsv are in the live folder).

    "steps" is keyed by step id and has no entry for a step still open; an entry has `state`, and `agent` (a started or
    blocked step), `commit` and `time` (a landed step; the time is the landing's, when the tool recorded it)."""
    when = {}
    for r in landings:
        when[r["step"]] = r["finished"]
    by_step = {}
    for s in steps:
        if s["state"] == "open":
            continue
        e = {"state": s["state"]}
        if s["state"] in ("started", "blocked"):
            e["agent"] = s.get("agent") or "1"
        if s["state"] == "landed":
            if s["commit"]:
                e["commit"] = s["commit"]
            if when.get(s["step"]):
                e["time"] = when[s["step"]]
        by_step[s["step"]] = e
    return {
        "generated": now,
        "steps": by_step,
        "blocked": [{"step": s["step"], "agent": s.get("agent") or "1",
                     "reason": (state.get(s["step"]) or {}).get("reason", "") or s.get("reason", "")}
                    for s in steps if s["state"] == "blocked"],
        "claims": [{"package": c["package"], "agent": c["agent"], "time": c["time"], "branch": c["branch"],
                    "stale": c["package"] in stale} for c in claims],
        "tool_faults": [{k: g[k] for k in ("signature", "count", "first_seen", "last_seen", "step", "agent", "kind", "check",
                                            "test", "first_error", "could_not", "run_folder")} for g in faults],
        "flakes": flakes,
        "set_aside": [dict(a) for a in set_aside],
    }


def compact(data):
    return json.dumps(data, sort_keys=True, separators=(",", ":"))


def data_json(data):
    return compact(data) + "\n"


def embed(data):
    """The data for a script tag: `</` and `<!--` cannot end the tag or open a comment."""
    return compact(data).replace("</", "<\\/").replace("<!--", "<\\u0021--")


PAGE = r"""<!doctype html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>Nebula Native Progress</title>
<style>
:root {
  --bg: #f6f7f9; --card: #ffffff; --fg: #15181d; --muted: #5a6370; --border: #d9dde3;
  --landed: #2a8f4e; --started: #2b6fd6; --blocked: #cc3a3a; --open: #c9ced6; --track: #e4e7ec; --warn: #b36b00;
}
@media (prefers-color-scheme: dark) {
  :root:not([data-theme="light"]) {
    --bg: #101317; --card: #181c22; --fg: #e8ebef; --muted: #9aa4b2; --border: #2c323b;
    --landed: #43b56a; --started: #5b9bff; --blocked: #ef6a6a; --open: #3a414c; --track: #262b33; --warn: #e0a040;
  }
}
:root[data-theme="dark"] {
  --bg: #101317; --card: #181c22; --fg: #e8ebef; --muted: #9aa4b2; --border: #2c323b;
  --landed: #43b56a; --started: #5b9bff; --blocked: #ef6a6a; --open: #3a414c; --track: #262b33; --warn: #e0a040;
}
* { box-sizing: border-box; }
html { -webkit-text-size-adjust: 100%; }
body {
  margin: 0; background: var(--bg); color: var(--fg);
  font: 16px/1.45 system-ui, -apple-system, "Segoe UI", Roboto, "Helvetica Neue", Arial, sans-serif;
  overflow-wrap: anywhere;
}
main { max-width: 960px; margin: 0 auto; padding: 20px 16px 48px; }
h1 { font-size: 1.5rem; margin: 0 0 4px; }
h2 { font-size: 1.05rem; margin: 28px 0 10px; }
.muted { color: var(--muted); font-size: 0.875rem; }
.card { background: var(--card); border: 1px solid var(--border); border-radius: 10px; padding: 12px 14px; margin: 0 0 10px; }
.big { font-size: 2rem; font-weight: 650; line-height: 1.1; }
.bar { height: 12px; background: var(--track); border-radius: 6px; overflow: hidden; display: flex; }
.bar > span { display: block; height: 100%; }
.bar .landed { background: var(--landed); }
.bar .started { background: var(--started); }
.bar .blocked { background: var(--blocked); }
.pk { display: grid; grid-template-columns: 4.5rem 1fr 4.5rem; gap: 8px; align-items: center; margin: 6px 0; font-size: 0.9rem; }
.pk .n { text-align: right; color: var(--muted); }
.grid { display: grid; grid-template-columns: repeat(auto-fill, minmax(22px, 1fr)); gap: 4px; }
.cell { aspect-ratio: 1; border-radius: 4px; background: var(--open); }
.cell.landed { background: var(--landed); } .cell.started { background: var(--started); } .cell.blocked { background: var(--blocked); }
.legend { display: flex; flex-wrap: wrap; gap: 4px 14px; margin-top: 8px; }
.legend i { display: inline-block; width: 11px; height: 11px; border-radius: 3px; margin-right: 5px; vertical-align: -1px; }
.row { display: flex; flex-wrap: wrap; gap: 2px 12px; }
.row b { font-weight: 650; }
.tag { display: inline-block; padding: 0 7px; border-radius: 9px; font-size: 0.78rem; border: 1px solid var(--border); color: var(--muted); }
.tag.stale { color: var(--warn); border-color: var(--warn); }
.kv { display: grid; grid-template-columns: 7.5rem 1fr; gap: 2px 10px; font-size: 0.9rem; margin-top: 6px; }
.kv dt { color: var(--muted); margin: 0; } .kv dd { margin: 0; }
.err { font-family: ui-monospace, SFMono-Regular, Consolas, Menlo, monospace; font-size: 0.82rem; }
.note { color: var(--warn); font-size: 0.875rem; margin: 8px 0; }
</style>
</head>
<body>
<main>
<h1>Nebula Native Progress</h1>
<div class="muted" id="when">Loading...</div>
<div class="note" id="note" hidden></div>
<div id="app"></div>
</main>
<script type="application/json" id="plan-static">__STATIC__</script>__LOCAL__
<script>
(function () {
  var app = document.getElementById("app");
  var S = JSON.parse(document.getElementById("plan-static").textContent);
  var titles = {};
  S.steps.forEach(function (s) { titles[s.id] = s.title; });
  function el(tag, cls, text) {
    var e = document.createElement(tag);
    if (cls) e.className = cls;
    if (text !== undefined && text !== null) e.textContent = String(text);
    return e;
  }
  function section(title) { app.appendChild(el("h2", null, title)); }
  function bar(parts, total) {
    var b = el("div", "bar");
    parts.forEach(function (p) {
      var s = el("span", p[0]);
      s.style.width = (total ? (100 * p[1] / total) : 0) + "%";
      b.appendChild(s);
    });
    return b;
  }
  function kv(card, pairs) {
    var dl = el("dl", "kv");
    pairs.forEach(function (p) {
      if (p[1] === "" || p[1] === null || p[1] === undefined) return;
      dl.appendChild(el("dt", null, p[0]));
      dl.appendChild(el("dd", p[2] || null, p[1]));
    });
    card.appendChild(dl);
  }
  function none(text) { app.appendChild(el("div", "muted", text)); }
  function head(card, id, extra) {
    var r = el("div", "row");
    r.appendChild(el("b", null, id));
    r.appendChild(el("span", null, titles[id] || ""));
    (extra || []).forEach(function (x) { r.appendChild(x); });
    card.appendChild(r);
  }

  function draw(d) {
    app.textContent = "";
    document.getElementById("when").textContent = "Written " + d.generated;
    var st = d.steps || {};
    function stateOf(id) { return st[id] ? st[id].state : "open"; }
    var n = { landed: 0, started: 0, blocked: 0 };
    S.steps.forEach(function (s) { var x = stateOf(s.id); if (n[x] !== undefined) n[x] += 1; });
    var top = el("div", "card");
    top.appendChild(el("div", "big", n.landed + " of " + S.total));
    top.appendChild(el("div", "muted", "steps landed"));
    top.appendChild(bar([["landed", n.landed], ["started", n.started], ["blocked", n.blocked]], S.total));
    app.appendChild(top);

    section("Packages");
    var pk = el("div", "card");
    S.packages.forEach(function (p) {
      var c = { landed: 0, started: 0, blocked: 0 };
      S.steps.forEach(function (s) { if (s.package === p.package) { var x = stateOf(s.id); if (c[x] !== undefined) c[x] += 1; } });
      var r = el("div", "pk");
      r.appendChild(el("span", null, p.package));
      r.appendChild(bar([["landed", c.landed], ["started", c.started], ["blocked", c.blocked]], p.total));
      r.appendChild(el("span", "n", c.landed + "/" + p.total));
      pk.appendChild(r);
    });
    app.appendChild(pk);

    section("All steps");
    var gridCard = el("div", "card");
    var g = el("div", "grid");
    S.steps.forEach(function (s) {
      var x = stateOf(s.id);
      var c = el("div", "cell " + x);
      c.title = s.id + " " + s.title + " (" + x + ")";
      g.appendChild(c);
    });
    gridCard.appendChild(g);
    var lg = el("div", "legend muted");
    ["landed", "started", "blocked", "open"].forEach(function (name) {
      var item = el("span");
      var i = el("i");
      i.style.background = "var(--" + name + ")";
      item.appendChild(i);
      item.appendChild(document.createTextNode(name));
      lg.appendChild(item);
    });
    gridCard.appendChild(lg);
    app.appendChild(gridCard);

    section("Last landings");
    var landed = S.steps.filter(function (s) { return stateOf(s.id) === "landed" && st[s.id].time; });
    landed.sort(function (a, b) { return st[a.id].time < st[b.id].time ? 1 : (st[a.id].time > st[b.id].time ? -1 : 0); });
    if (!landed.length) none("None yet.");
    landed.slice(0, 10).forEach(function (s) {
      var c = el("div", "card");
      head(c, s.id);
      c.appendChild(el("div", "muted", st[s.id].time + (st[s.id].commit ? "  commit " + st[s.id].commit : "")));
      app.appendChild(c);
    });

    section("Started");
    var started = S.steps.filter(function (s) { return stateOf(s.id) === "started"; });
    if (!started.length) none("Nothing started.");
    started.forEach(function (s) {
      var c = el("div", "card");
      head(c, s.id, [el("span", "tag", "agent " + (st[s.id].agent || "1"))]);
      c.appendChild(el("div", "muted", "native/" + s.id));
      app.appendChild(c);
    });

    section("Claims");
    var claims = d.claims || [];
    if (!claims.length) none("No package is claimed.");
    claims.forEach(function (k) {
      var c = el("div", "card");
      var r = el("div", "row");
      r.appendChild(el("b", null, k.package));
      r.appendChild(el("span", "tag", "agent " + k.agent));
      if (k.stale) r.appendChild(el("span", "tag stale", "stale"));
      c.appendChild(r);
      c.appendChild(el("div", "muted", k.time + (k.branch ? "  " + k.branch : "")));
      app.appendChild(c);
    });

    section("Blocked");
    var blocked = d.blocked || [];
    if (!blocked.length) none("Nothing is blocked.");
    blocked.forEach(function (b) {
      var c = el("div", "card");
      head(c, b.step, [el("span", "tag", "agent " + b.agent)]);
      c.appendChild(el("div", null, b.reason || "(no reason written)"));
      app.appendChild(c);
    });

    var faults = d.tool_faults || [];
    section("Tool faults (" + faults.length + " kinds, most frequent first)");
    if (!faults.length) none("No tool fault recorded.");
    faults.forEach(function (f) {
      var c = el("div", "card");
      var r = el("div", "row");
      r.appendChild(el("b", null, f.count + (f.count === 1 ? " time" : " times")));
      r.appendChild(el("span", "tag", f.kind));
      r.appendChild(el("span", null, f.check));
      c.appendChild(r);
      kv(c, [["First error", f.first_error, "err"], ["Test", f.test], ["Step", f.step], ["Agent", f.agent],
             ["First seen", f.first_seen], ["Last seen", f.last_seen], ["Could not", f.could_not],
             ["Run folder", f.run_folder, "err"]]);
      app.appendChild(c);
    });

    section("Flakes");
    app.appendChild(el("div", "card", d.flakes + (d.flakes === 1 ? " flake recorded" : " flakes recorded")));
    (d.set_aside || []).forEach(function (a) {
      var c = el("div", "card");
      kv(c, [["Set aside", a.test + " on " + a.renderer], ["Since", a.since], ["Why", a.why]]);
      app.appendChild(c);
    });
  }

  function embedded() {
    var e = document.getElementById("embedded-data");
    if (!e) return null;
    try { return JSON.parse(e.textContent); } catch (x) { return null; }
  }
  function fallback() {
    var d = embedded();
    if (d) {
      var note = document.getElementById("note");
      note.hidden = false;
      note.textContent = "Shown from the data written into this page; the data file beside it could not be read.";
      draw(d);
    } else {
      document.getElementById("when").textContent = "No data: progress-data.json could not be read and none is embedded.";
    }
  }
  try {
    fetch("progress-data.json", { cache: "no-store" })
      .then(function (r) { if (!r.ok) throw new Error("status " + r.status); return r.json(); })
      .then(draw)
      .catch(fallback);
  } catch (e) {
    fallback();
  }
})();
</script>
</body>
</html>
"""


def render(static, data=None):
    """The page. With `data` (the local copy) the data is embedded as well; without, the page is the static one."""
    local = ""
    if data is not None:
        local = '\n<script type="application/json" id="embedded-data">%s</script>' % embed(data)
    head, _, rest = PAGE.partition("__STATIC__")      # one pass: no title or reason can be taken for a marker
    mid, _, tail = rest.partition("__LOCAL__")
    return head + embed(static) + mid + local + tail
