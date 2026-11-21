"""page.py: the native plan's progress page (package T13, item 6).

make_data() turns the plan's files into one small dictionary (progress-data.json); render() puts the page around it.
The page (progress.html) is one self-contained file: no outside script, font or style, no address of any kind. It loads
`progress-data.json` from beside it with fetch; when that fails (a page opened from disk cannot fetch) it draws the same
data, which plan.py page embedded in a script tag of type application/json. Light and dark by prefers-color-scheme,
colours as tokens on :root, system fonts, readable at phone width with no sideways scroll.

The embedded copy means the HTML changes with each `plan.py page`; plan.py writes it only when its text differs.
"""
import json

TITLE = "Nebula Native Progress"
LAST_LANDINGS = 10


def make_data(steps, landings, state, claims, stale, faults, flakes, now):
    """`steps`: plan.read_steps (state, commit, agent). `landings`: live.read_landings. `state`: live.read_state.
    `claims`: live.read_claims. `stale`: set of stale package names. `faults`: live.fault_groups. `flakes`: a count.
    `now`: the time of writing (text)."""
    titles = {s["step"]: s["title"] for s in steps}
    packages, order = {}, []
    for s in steps:
        p = packages.get(s["package"])
        if p is None:
            p = packages[s["package"]] = {"package": s["package"], "total": 0, "landed": 0, "started": 0, "blocked": 0}
            order.append(s["package"])
        p["total"] += 1
        if s["state"] in ("landed", "started", "blocked"):
            p[s["state"]] += 1
    started = [{"step": s["step"], "title": s["title"], "agent": s.get("agent") or "1", "branch": "native/" + s["step"]}
               for s in steps if s["state"] == "started"]
    blocked = [{"step": s["step"], "title": s["title"], "agent": s.get("agent") or "1",
                "reason": (state.get(s["step"]) or {}).get("reason", "") or s.get("reason", "")}
               for s in steps if s["state"] == "blocked"]
    last = [{"step": r["step"], "title": titles.get(r["step"], ""), "time": r["finished"], "commit": r["commit"]}
            for r in landings[-LAST_LANDINGS:]][::-1]
    return {
        "title": TITLE,
        "generated": now,
        "total": len(steps),
        "landed": sum(1 for s in steps if s["state"] == "landed"),
        "packages": [packages[p] for p in order],
        "steps": [{"id": s["step"], "package": s["package"], "title": s["title"], "state": s["state"],
                   "commit": s["commit"] if s["state"] == "landed" else "",
                   "agent": (s.get("agent") or "1") if s["state"] in ("started", "blocked") else ""} for s in steps],
        "landings": last,
        "started": started,
        "blocked": blocked,
        "claims": [{"package": c["package"], "agent": c["agent"], "time": c["time"], "branch": c["branch"],
                    "stale": c["package"] in stale} for c in claims],
        "tool_faults": [{k: g[k] for k in ("signature", "count", "first_seen", "last_seen", "step", "agent", "kind", "check",
                                            "test", "first_error", "could_not", "run_folder")} for g in faults],
        "flakes": flakes,
    }


def data_json(data):
    return json.dumps(data, sort_keys=True, separators=(",", ":")) + "\n"


def embed(data):
    """The data for a script tag: `</` and `<!--` cannot end the tag or open a comment."""
    return json.dumps(data, sort_keys=True).replace("</", "<\\/").replace("<!--", "<\\u0021--")


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
<script type="application/json" id="embedded-data">__DATA__</script>
<script>
(function () {
  var app = document.getElementById("app");
  function el(tag, cls, text) {
    var e = document.createElement(tag);
    if (cls) e.className = cls;
    if (text !== undefined && text !== null) e.textContent = String(text);
    return e;
  }
  function section(title) { var h = el("h2", null, title); app.appendChild(h); }
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

  function draw(d) {
    app.textContent = "";
    document.getElementById("when").textContent = "Written " + d.generated;
    var top = el("div", "card");
    top.appendChild(el("div", "big", d.landed + " of " + d.total));
    top.appendChild(el("div", "muted", "steps landed"));
    var started = d.steps.filter(function (s) { return s.state === "started"; }).length;
    var blocked = d.steps.filter(function (s) { return s.state === "blocked"; }).length;
    top.appendChild(bar([["landed", d.landed], ["started", started], ["blocked", blocked]], d.total));
    app.appendChild(top);

    section("Packages");
    var pk = el("div", "card");
    d.packages.forEach(function (p) {
      var r = el("div", "pk");
      r.appendChild(el("span", null, p.package));
      r.appendChild(bar([["landed", p.landed], ["started", p.started], ["blocked", p.blocked]], p.total));
      r.appendChild(el("span", "n", p.landed + "/" + p.total));
      pk.appendChild(r);
    });
    app.appendChild(pk);

    section("All steps");
    var gridCard = el("div", "card");
    var g = el("div", "grid");
    d.steps.forEach(function (s) {
      var c = el("div", "cell " + s.state);
      c.title = s.id + " " + s.title + " (" + s.state + ")";
      g.appendChild(c);
    });
    gridCard.appendChild(g);
    var lg = el("div", "legend muted");
    [["landed", "landed"], ["started", "started"], ["blocked", "blocked"], ["open", "open"]].forEach(function (p) {
      var item = el("span");
      var i = el("i");
      i.style.background = "var(--" + p[0] + ")";
      item.appendChild(i);
      item.appendChild(document.createTextNode(p[1]));
      lg.appendChild(item);
    });
    gridCard.appendChild(lg);
    app.appendChild(gridCard);

    section("Last landings");
    if (!d.landings.length) none("None yet.");
    d.landings.forEach(function (l) {
      var c = el("div", "card");
      var r = el("div", "row");
      r.appendChild(el("b", null, l.step));
      r.appendChild(el("span", null, l.title));
      c.appendChild(r);
      c.appendChild(el("div", "muted", l.time + (l.commit ? "  commit " + l.commit : "")));
      app.appendChild(c);
    });

    section("Started");
    if (!d.started.length) none("Nothing started.");
    d.started.forEach(function (s) {
      var c = el("div", "card");
      var r = el("div", "row");
      r.appendChild(el("b", null, s.step));
      r.appendChild(el("span", null, s.title));
      r.appendChild(el("span", "tag", "agent " + s.agent));
      c.appendChild(r);
      c.appendChild(el("div", "muted", s.branch));
      app.appendChild(c);
    });

    section("Claims");
    if (!d.claims.length) none("No package is claimed.");
    d.claims.forEach(function (k) {
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
    if (!d.blocked.length) none("Nothing is blocked.");
    d.blocked.forEach(function (b) {
      var c = el("div", "card");
      var r = el("div", "row");
      r.appendChild(el("b", null, b.step));
      r.appendChild(el("span", null, b.title));
      c.appendChild(r);
      c.appendChild(el("div", null, b.reason || "(no reason written)"));
      app.appendChild(c);
    });

    section("Tool faults (" + d.tool_faults.length + " kinds, most frequent first)");
    if (!d.tool_faults.length) none("No tool fault recorded.");
    d.tool_faults.forEach(function (f) {
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
  }

  function embedded() {
    try { return JSON.parse(document.getElementById("embedded-data").textContent); } catch (e) { return null; }
  }
  function fallback() {
    var d = embedded();
    if (d) {
      var n = document.getElementById("note");
      n.hidden = false;
      n.textContent = "Shown from the data written into this page; the data file beside it could not be read.";
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


def render(data):
    return PAGE.replace("__DATA__", embed(data))
