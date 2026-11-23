"""page.py: the native plan's progress page and status text (package T13 item 6, T4b).

What a person opens (D:/NebulaWork/plan/native):

  progress.html          the complete page, the data inside it: it works from disk (a page opened from disk cannot fetch
                         a file). Made again each time progress changes.
  STATUS.md              a short plain-text view of the same data (make_status).

What is published on the web (the `publish` folder beside them):

  publish/index.html           the page with no data in it: layout, script and what never changes with progress (the
                               step ids, titles, packages and their order, from steps.tsv). Rewritten only when this tool
                               or the plan's step list changes, never because progress changed. It fetches
                               `progress-data.json` from beside it; when that cannot be loaded, within three seconds it
                               says so in place of "Loading...".
  publish/progress-data.json   only what changes: per step its state, agent, code commit, main's commit and landing time (keyed by
                               step id; a step still open is not in it), the blocked list with reasons, the claims, the
                               tool-fault table, what is owed to a person's check, the flake count, a generated-at time.
                               Compact JSON.

One self-contained file each: no outside script, font or style, no address of any kind. Light and dark by
prefers-color-scheme, colours as tokens on :root, system fonts, readable at phone width with no sideways scroll.
"""
import json
import re

TITLE = "Nebula Native Progress"
LAST_LANDINGS = 10
LAST_FAULTS = 20
# What the published page says in place of "Loading..." when its data cannot be loaded, and how long it waits at most.
NO_DATA = "The progress data could not be loaded. On the PC, open progress.html in the folder above this one."
WAIT_SECONDS = 3
OWED_FIELDS = ("time", "agent", "step", "text")
DECISION_FIELDS = ("date", "step", "what", "because", "doc")
STRICTNESS_FIELDS = ("date", "file", "what changed", "why", "allowed by", "id", "removed by")
OWNER = "owner:"          # a step blocked with a reason that begins so waits for the owner: the page lists it apart
OLD_PAGE = "If this time is more than three hours old, the work has stopped: open the session and send the word continue."


def mend_owner_text(m):
    """What the page says of a mend that failed three times (there is no planner during the run: it waits for the owner)."""
    return ("A test tool is at fault and three attempts did not mend it (%s: %s). The steps that need it wait; the other steps go on."
            % (m["id"], m["title"]))


def is_owner_question(reason):
    return (reason or "").strip().startswith(OWNER)


def reason_of(step, state):
    return (state.get(step["step"]) or {}).get("reason", "") or step.get("reason", "")
REPAIR_FIELDS = ("id", "opened", "source", "kind", "check", "renderer", "step", "commit", "evidence", "row", "state", "closed",
                 "seen", "note")


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


def make_data(steps, landings, state, claims, stale, faults, flakes, now, set_aside=(), repairs=(), owed=(), note="", mends=(),
              decisions=(), strictness=(), tablet=None, locks=(), alive="", signin=""):
    """What changes. `steps`: plan.read_steps (state, commit, agent). `landings`: live.read_landings. `state`:
    live.read_state. `claims`: live.read_claims. `stale`: set of stale package names. `faults`: live.fault_groups.
    `flakes`: a count. `now`: the time of writing (text). `set_aside`: [{test, renderer, since, why}] of the tests the gate
    has set aside (its flakes.tsv and set-aside.tsv are in the live folder). `repairs`: [dict] of repairs.tsv, in the order
    to show (live.repair_order: open ones of kind fail first). `owed`: [dict] of owed.tsv (what only a person or other
    hardware can check), oldest first. `note`: the line the planner leaves for the page (plan.read_note), under the count; no key
    "note" in the data when there is none. `mends`: [{id, step, title, state, minutes, commit, note}] (plan.py mend); `decisions`:
    [{date, step, what, because, doc}] (docs/plan/native/decisions.tsv); `strictness`: the lines of docs/plan/strictness-log.tsv (the
    checks changed during the run); `tablet`: None, or {since, owed} when the tablet's checks are owed; `locks`: [text] of a lock held by
    hand for more than 30 minutes; `alive`: working, ended or stale (plan.py alive).

    "steps" is keyed by step id and has no entry for a step still open; an entry has `state`, and `agent` (a started or
    blocked step), `commit` (the code's commit on the step's branch), `main` (the commit main moved to, when the tool knows it) and
    `time` (a landed step; the time is the landing's, when the tool recorded it)."""
    when, on_main, figures = {}, {}, {}
    for r in landings:
        when[r["step"]] = r["finished"]
        if r.get("figures") not in (None, "", "-"):
            figures[r["step"]] = r["figures"]
        if r.get("main") not in (None, "", "-"):
            on_main[r["step"]] = r["main"]
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
            if figures.get(s["step"]):
                e["figures"] = figures[s["step"]]
            if on_main.get(s["step"]):
                e["main"] = on_main[s["step"]]
            if when.get(s["step"]):
                e["time"] = when[s["step"]]
        by_step[s["step"]] = e
    data = {
        "generated": now,
        "steps": by_step,
        "blocked": [{"step": s["step"], "agent": s.get("agent") or "1", "reason": reason_of(s, state)}
                    for s in steps if s["state"] == "blocked" and not is_owner_question(reason_of(s, state))],
        "owner": [{"step": s["step"], "title": s["title"], "question": reason_of(s, state)[len(OWNER):].strip()}
                  for s in steps if s["state"] == "blocked" and is_owner_question(reason_of(s, state))]
                 + [{"step": "", "title": m["title"], "question": mend_owner_text(m)} for m in mends if m.get("state") == "failed"],
        "claims": [{"package": c["package"], "agent": c["agent"], "time": c["time"], "branch": c["branch"],
                    "stale": c["package"] in stale} for c in claims],
        "tool_faults": [{k: g[k] for k in ("signature", "count", "first_seen", "last_seen", "step", "agent", "kind", "check",
                                            "test", "first_error", "could_not", "run_folder")} for g in faults],
        "flakes": flakes,
        "set_aside": [dict(a) for a in set_aside],
        "repairs": [{k: r.get(k, "") for k in REPAIR_FIELDS} for r in repairs],
        "owed": [{k: r.get(k, "") for k in OWED_FIELDS} for r in owed],
        "mends": [dict(m) for m in mends],
        "decisions": [{k: r.get(k, "") for k in DECISION_FIELDS} for r in decisions],
        "strictness": [{k: r.get(k, "") for k in STRICTNESS_FIELDS} for r in strictness],
        "locks": list(locks),
    }
    if tablet:
        data["tablet"] = dict(tablet)
    if alive:
        data["alive"] = alive
    if signin:
        data["signin"] = signin
    if note:
        data["note"] = note
    return data


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
.now { margin: 8px 0 2px; font-weight: 600; }
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
.tag.bad { color: var(--blocked); border-color: var(--blocked); }
.kv { display: grid; grid-template-columns: 7.5rem 1fr; gap: 2px 10px; font-size: 0.9rem; margin-top: 6px; }
.kv dt { color: var(--muted); margin: 0; } .kv dd { margin: 0; }
.err { font-family: ui-monospace, SFMono-Regular, Consolas, Menlo, monospace; font-size: 0.82rem; }
</style>
</head>
<body>
<main>
<h1>Nebula Native Progress</h1>
<div class="muted" id="when">Loading...</div>
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
    document.getElementById("when").textContent = "Written " + d.generated + ". __OLDPAGE__";
    var st = d.steps || {};
    function stateOf(id) { return st[id] ? st[id].state : "open"; }
    var n = { landed: 0, started: 0, blocked: 0 };
    S.steps.forEach(function (s) { var x = stateOf(s.id); if (n[x] !== undefined) n[x] += 1; });
    var top = el("div", "card");
    top.appendChild(el("div", "big", n.landed + " of " + S.total));
    top.appendChild(el("div", "muted", "steps landed"));
    if (d.note) top.appendChild(el("div", "now", "Now: " + d.note));
    top.appendChild(bar([["landed", n.landed], ["started", n.started], ["blocked", n.blocked]], S.total));
    app.appendChild(top);
    if (d.signin) {
      var gh = el("div", "card");
      gh.appendChild(el("b", "tag bad", "GitHub sign-in needed since " + d.signin + ": nothing can be landed until someone signs in"));
      app.appendChild(gh);
    }
    if (d.tablet) {
      var tb = el("div", "card");
      tb.appendChild(el("b", "tag stale", "Tablet away since " + d.tablet.since + ": " + d.tablet.owed + " checks owed"));
      app.appendChild(tb);
    }
    (d.locks || []).forEach(function (text) {
      var lk = el("div", "card");
      lk.appendChild(el("b", "tag stale", text));
      app.appendChild(lk);
    });

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
      c.appendChild(el("div", "muted", st[s.id].time + (st[s.id].commit ? "  commit " + st[s.id].commit : "") +
                       (st[s.id].main ? "  main " + st[s.id].main : "")));
      if (st[s.id].figures) c.appendChild(el("div", "muted", "Speed: " + st[s.id].figures));
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

    var repairs = d.repairs || [];
    section("Repairs (" + repairs.filter(function (r) { return r.state === "open" && r.kind === "fail"; }).length +
            " open of kind fail, " + repairs.length + " in all)");
    if (!repairs.length) none("No repair recorded.");
    function dash(v) { return v === "-" ? "" : v; }
    repairs.forEach(function (r) {
      var c = el("div", "card");
      var row = el("div", "row");
      row.appendChild(el("b", null, r.id));
      row.appendChild(el("span", "tag" + (r.state === "open" && r.kind === "fail" ? " bad" : ""), r.state));
      row.appendChild(el("span", "tag", r.kind));
      row.appendChild(el("span", null, r.check + (dash(r.renderer) ? " on " + r.renderer : "")));
      c.appendChild(row);
      kv(c, [["Traced to", dash(r.step) || dash(r.commit) ? [dash(r.step) ? "step " + r.step : "", dash(r.commit) ? "commit " + r.commit : ""].filter(Boolean).join(", ") : "not traced to a landing"],
             ["Seen", r.seen + (r.seen === "1" ? " time" : " times")], ["Source", r.source], ["Opened", r.opened],
             ["Ledger row", dash(r.row)], ["Evidence", dash(r.evidence), "err"], ["Closed by", dash(r.closed)],
             ["Note", dash(r.note)]]);
      app.appendChild(c);
    });

    var owner = d.owner || [];
    section("Waiting for the owner (" + owner.length + ")");
    if (!owner.length) none("Nothing waits for the owner.");
    owner.forEach(function (q) {
      var c = el("div", "card");
      head(c, q.step, [el("span", "tag stale", "owner")]);
      c.appendChild(el("div", null, q.question));
      app.appendChild(c);
    });

    var mends = d.mends || [];
    section("Tool mends (" + mends.length + ")");
    if (!mends.length) none("No tool was mended.");
    mends.forEach(function (m) {
      var c = el("div", "card");
      var r = el("div", "row");
      r.appendChild(el("b", null, m.id));
      r.appendChild(el("span", "tag" + (m.state === "failed" ? " bad" : (m.state === "open" ? " stale" : "")), m.state));
      r.appendChild(el("span", null, m.title));
      c.appendChild(r);
      kv(c, [["Step", m.step === "-" ? "" : m.step], ["Attempts", m.attempts === "0" || !m.attempts ? "" : m.attempts + " of 3"], ["Minutes", m.minutes], ["Commit", m.commit === "-" ? "" : m.commit, "err"],
             ["Note", m.note === "-" ? "" : m.note]]);
      app.appendChild(c);
    });

    var decisions = d.decisions || [];
    section("Decisions (" + decisions.length + ")");
    if (!decisions.length) none("The agent has decided nothing yet.");
    decisions.forEach(function (x) {
      var c = el("div", "card");
      head(c, x.step, [el("span", "muted", x.date)]);
      c.appendChild(el("div", null, x.what));
      kv(c, [["Because", x.because], ["Document", x.doc === "-" ? "" : x.doc]]);
      app.appendChild(c);
    });

    var strict = d.strictness || [];
    section("Checks changed during the run (" + strict.length + ")");
    if (!strict.length) none("No check was changed.");
    strict.forEach(function (x) {
      var c = el("div", "card");
      var r = el("div", "row");
      r.appendChild(el("b", null, x.id));
      r.appendChild(el("span", "err", x.file));
      r.appendChild(el("span", "muted", x.date));
      c.appendChild(r);
      c.appendChild(el("div", null, x["what changed"]));
      kv(c, [["Why", x.why], ["Allowed by", x["allowed by"]], ["Removed by", x["removed by"] === "-" ? "" : x["removed by"]]]);
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

    var owed = d.owed || [];
    section("For the owner to check by hand (" + owed.length + ")");
    if (!owed.length) none("Nothing is owed.");
    owed.forEach(function (o) {
      var c = el("div", "card");
      head(c, o.step, [el("span", "tag", "agent " + o.agent)]);
      c.appendChild(el("div", null, o.text));
      c.appendChild(el("div", "muted", o.time));
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

  /* The complete page (progress.html) has its data inside and draws it at once. The published page (index.html) fetches
     progress-data.json from beside it; when that fails, or has not answered in __WAIT__ seconds, it says so in place of
     "Loading...". Data that comes late is still drawn. */
  var drawn = false;
  function show(d) { drawn = true; draw(d); }
  function failed() {
    if (!drawn) document.getElementById("when").textContent = "__NODATA__";
  }
  function embedded() {
    var e = document.getElementById("embedded-data");
    if (!e) return null;
    try { return JSON.parse(e.textContent); } catch (x) { return null; }
  }
  var inside = embedded();
  if (inside) {
    show(inside);
  } else {
    setTimeout(failed, __WAIT__ * 1000);
    try {
      fetch("progress-data.json", { cache: "no-store" })
        .then(function (r) { if (!r.ok) throw new Error("status " + r.status); return r.json(); })
        .then(show)
        .catch(failed);
    } catch (e) {
      failed();
    }
  }
})();
</script>
</body>
</html>
"""


def render(static, data=None):
    """The page. With `data` the complete page (progress.html: the data inside, so it works from disk); without, the one
    for publishing (index.html: it fetches progress-data.json from beside it)."""
    local = ""
    if data is not None:
        local = '\n<script type="application/json" id="embedded-data">%s</script>' % embed(data)
    head, _, rest = PAGE.partition("__STATIC__")      # one pass: no title or reason can be taken for a marker
    mid, _, tail = rest.partition("__LOCAL__")
    tail = tail.replace("__WAIT__", str(WAIT_SECONDS)).replace("__NODATA__", NO_DATA).replace("__OLDPAGE__", OLD_PAGE)
    return head + embed(static) + mid + local + tail


# STATUS.md ----------------------------------------------------------------------------------------------------------

# A path that starts with the home folder of a person on this PC: C:/Users/<name>/ (either slash direction, any case).
HOME = re.compile(r"C:[\\/]+Users[\\/]+[^\\/\s|]+[\\/]", re.I)


def home_short(text):
    """`text` with each C:/Users/<name>/ at the start of a path written as ~/ ."""
    return HOME.sub("~/", text)


def cell(value):
    """One table cell: one line, no pipe."""
    return " ".join(str(value if value is not None else "").replace("|", "/").split())


def minute(stamp):
    """'2026-11-20T10:05:33' as '2026-11-20 10:05'; a text that is not a time stays as it is."""
    m = re.match(r"^(\d{4}-\d\d-\d\d)[T ](\d\d:\d\d)", stamp or "")
    return "%s %s" % m.groups() if m else cell(stamp)


def table(head, rows):
    lines = ["| " + " | ".join(head) + " |", "|" + "|".join(" --- " for _ in head) + "|"]
    lines += ["| " + " | ".join(cell(c) for c in row) + " |" for row in rows]
    return lines


def make_status(static, data, landings, faults, now):
    """STATUS.md: the same facts as the page, as short plain text. `static`, `data`: make_static, make_data; `landings`:
    live.read_landings (every one); `faults`: live.read_faults (every event, oldest first); `now`: the time (text).
    Returns (text, landed, total). A path that starts with C:/Users/<name>/ is written with ~/ ."""
    total = static["total"]
    by = data["steps"]
    state = lambda sid: by[sid]["state"] if sid in by else "open"
    title = {s["id"]: s["title"] for s in static["steps"]}
    landed = sum(1 for s in static["steps"] if state(s["id"]) == "landed")
    started = [s for s in static["steps"] if state(s["id"]) == "started"]
    out = ["# Nebula native plan: status", "", "Written %s. %s" % (minute(now), OLD_PAGE), "",
           "Steps landed: %d of %d (%d%%)." % (landed, total, round(100.0 * landed / total) if total else 0), ""]
    if data.get("signin"):
        out += ["GitHub sign-in needed since %s: nothing can be landed until someone signs in." % minute(data["signin"]), ""]
    if data.get("tablet"):
        out += ["Tablet away since %s: %s checks owed." % (minute(data["tablet"]["since"]), data["tablet"]["owed"]), ""]
    out += ["%s." % l for l in data.get("locks", [])] + [""] if data.get("locks") else []
    if data.get("note"):
        out += ["Now: " + data["note"], ""]
    if started:
        out += ["In work now:"] + ["- %s %s (agent %s)" % (s["id"], s["title"], by[s["id"]].get("agent", "1")) for s in started]
    else:
        out.append("In work now: Nothing started.")
    out += ["", "## Packages", ""]
    rows = []
    for p in static["packages"]:
        done = sum(1 for s in static["steps"] if s["package"] == p["package"] and state(s["id"]) == "landed")
        rows.append((p["package"], done, p["total"]))
    out += table(["Package", "Landed", "Of"], rows)
    last = list(landings)[-LAST_LANDINGS:][::-1]
    out += ["", "## Last %d landings" % LAST_LANDINGS, ""]
    out += table(["When", "Step", "Title", "Code commit", "Main commit"],
                 [(minute(r["finished"]), r["step"], title.get(r["step"], ""), r["commit"], r.get("main") or "-")
                  for r in last]) if last else ["None yet."]
    repairs = data.get("repairs", [])
    out += ["", "## Repairs", ""]
    out += ["%d open of kind fail, %d in all." % (sum(1 for r in repairs if r["state"] == "open" and r["kind"] == "fail"),
                                                  len(repairs))] if repairs else ["No repair recorded."]
    if repairs:
        out += [""] + table(["Id", "State", "Kind", "Check", "Renderer", "Traced to", "Seen"],
                            [(r["id"], r["state"], r["kind"], r["check"], r["renderer"],
                              " ".join(x for x in ("step " + r["step"] if r["step"] not in ("", "-") else "",
                                                   "commit " + r["commit"] if r["commit"] not in ("", "-") else "") if x) or "not traced",
                              r["seen"]) for r in repairs])
    owed = data.get("owed", [])
    out += ["", "## For the owner to check by hand", ""]
    out += ["- %s %s: %s (agent %s, %s)" % (o["step"], title.get(o["step"], ""), " ".join(o["text"].split()), o["agent"], minute(o["time"]))
            for o in owed] or ["Nothing is owed."]
    out += ["", "## Waiting for the owner", ""]
    out += ["- %s %s: %s" % (q["step"], q["title"], cell(q["question"])) for q in data.get("owner", [])] or ["Nothing waits for the owner."]
    out += ["", "## Blocked", ""]
    out += ["- %s %s: %s" % (b["step"], title.get(b["step"], ""), " ".join((b["reason"] or "(no reason written)").split()))
            for b in data["blocked"]] or ["Nothing is blocked."]
    mends = data.get("mends", [])
    out += ["", "## Tool mends", ""]
    out += table(["Id", "State", "Step", "Title", "Attempts", "Minutes", "Commit"],
                 [(m["id"], m["state"], m["step"], m["title"], m.get("attempts", "0"), m["minutes"], m["commit"]) for m in mends]) if mends else ["No tool was mended."]
    decisions = data.get("decisions", [])
    out += ["", "## Decisions (%d)" % len(decisions), ""]
    out += ["- %s %s: %s (because %s)" % (d["date"], d["step"], cell(d["what"]), cell(d["because"])) for d in decisions] or ["None."]
    strict = data.get("strictness", [])
    out += ["", "## Checks changed during the run (%d)" % len(strict), ""]
    out += ["- %s %s %s: %s (allowed by %s)" % (s["date"], s["id"], s["file"], cell(s["what changed"]), cell(s["allowed by"]))
            for s in strict] or ["None."]
    out += ["", "## Tool faults", "", "%d tool fault%s recorded." % (len(faults), "" if len(faults) == 1 else "s")]
    if faults:
        out += ["", "The last %d:" % LAST_FAULTS, ""]
        out += table(["When", "Step", "Test", "What happened", "What the agent could not do"],
                     [(minute(f["time"]), f["step"] or "-", f["test"] or f["check"], f["first_error"], f["could_not"] or "-")
                      for f in list(faults)[-LAST_FAULTS:][::-1]])
    return home_short("\n".join(out) + "\n"), landed, total
