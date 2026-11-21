"""harness_self.py: the harness's own tests (tests/mods/harness). Each has, beside its test.json, an expected.json
that says how the RUNNER must answer it: the verdict (a test that must fail or be refused), what the reason names,
the facts the run left, the run folder's contents. `judge` gives the ways a run's answer differs from that; none means
the runner answered as its contract requires.

expected.json keys:
    verdict, verdict_one_of        the verdict, or the verdicts allowed
    reason_has, reason_has_any     texts the reason names (all of them; at least one)
    reason_has_pattern             a regular expression the reason matches
    check_actual                   the first failing check's actual value
    facts, facts_lack              facts with their last value; facts that must be absent
    fact_mentions                  {key: [texts]}: each text in some value of that key
    import_matches_check           {fact, file}: the game's lines for an imported file against `lumalink check --json`
                                   of the same file, finding for finding (LUMALINK and LUMA_PAK name the programs)
    run_folder_lacks               path parts no file in the run folder may have
    twice_same_facts               run twice, the same facts both times but the keys of its "except" (mod_tests.py)
    note, also, references, d3d12, vulkan, on_difference, facts_file: words for the person reading, not checked
"""
import json
import os
import pathlib
import re
import subprocess

# Every result.json has these (docs/modding/lumalink/test-harness.md, section 5).
RESULT_KEYS = ("harness", "test", "proves", "renderer", "platform", "os", "gpu", "commit", "module", "verdict",
               "kind", "reason", "checks", "setup", "frames", "seconds", "pictures")


def fact_rows(folder: pathlib.Path):
    path = folder / "facts.jsonl"
    if not path.exists():
        return []
    return [json.loads(line) for line in path.read_text(encoding="utf-8").splitlines() if line.strip()]


def import_matches_check(want, test_folder: pathlib.Path, folder: pathlib.Path, platform: str):
    """The differences between the game's import lines and lumalink check's findings for the same file."""
    lumalink, pak = os.environ.get("LUMALINK"), os.environ.get("LUMA_PAK")
    if not lumalink or not pak:
        return ["import_matches_check needs LUMALINK (the lumalink program) and LUMA_PAK (the run's game.pak)"]
    source = (test_folder / want["file"]).resolve()
    r = subprocess.run([lumalink, "check", str(source), "--no-installed", "--game", pak, "--platform", platform, "--json"],
                       capture_output=True, text=True, encoding="utf-8", errors="replace")
    try:
        findings = json.loads(r.stdout)["findings"]
    except (ValueError, KeyError):
        return ["lumalink check gave no report: %r" % (r.stdout + r.stderr)[-300:]]
    lines = sorted((row["value"] for row in fact_rows(folder) if row["key"] == want["fact"]),
                   key=lambda v: v.get("line", 0) if isinstance(v, dict) else 0)
    if not lines or not isinstance(lines[0], dict):
        return ["no fact %s" % want["fact"]]
    problems = []
    first = lines[0]
    if first.get("text") != "NOT IMPORTED" or first.get("refused") is not True or first.get("checked") is not True:
        problems.append("line 1 is %r, wanted NOT IMPORTED, checked and refused" % (first,))
    game = [v for v in lines[1:] if "code" in v]
    if len(game) != len(findings):
        problems.append("%d finding lines in the game, %d from lumalink check" % (len(game), len(findings)))
    for g, f in zip(game, findings):
        place = "%s:%s" % (f["line"], f["column"]) if f.get("line") and f.get("column") else (str(f["line"]) if f.get("line") else None)
        theirs = {"code": f["code"], "file": f["file"], "at": place, "error": f["level"] == "error", "text": f["message"]}
        ours = {"code": "L%d" % g["code"], "file": g["file"], "at": g.get("at"), "error": g["error"], "text": g["text"]}
        if ours != theirs:
            problems.append("line %s: game %r, lumalink check %r" % (g.get("line"), ours, theirs))
    return problems


def judge(expected: dict, result: dict, run_folder: pathlib.Path, test_folder: pathlib.Path):
    """The ways the runner's answer (result.json and its run folder) differs from expected.json; empty when it agrees."""
    problems = []
    verdict = result.get("verdict")
    reason = str(result.get("reason", ""))
    wanted = expected.get("verdict_one_of") or [expected["verdict"]]
    if verdict not in wanted:
        problems.append("verdict %r, the contract wants %s" % (verdict, wanted))
    for text in expected.get("reason_has", []):
        if text not in reason:
            problems.append("the reason does not name %r: %r" % (text, reason))
    if expected.get("reason_has_any") and not any(t in reason for t in expected["reason_has_any"]):
        problems.append("the reason names none of %s: %r" % (expected["reason_has_any"], reason))
    if "reason_has_pattern" in expected and not re.search(expected["reason_has_pattern"], reason):
        problems.append("the reason does not match %r: %r" % (expected["reason_has_pattern"], reason))
    if "check_actual" in expected:
        failing = [c for c in result.get("checks", []) if c.get("result") != "pass"]
        if not failing or failing[0].get("actual") != expected["check_actual"]:
            problems.append("the first failing check's actual is not %r: %s" % (expected["check_actual"], failing[:1]))
    rows = fact_rows(run_folder)
    facts = {row["key"]: row["value"] for row in rows}
    for key, value in expected.get("facts", {}).items():
        if facts.get(key, "<absent>") != value:
            problems.append("fact %s is %r, expected %r" % (key, facts.get(key, "<absent>"), value))
    for key in expected.get("facts_lack", []):
        if key in facts:
            problems.append("fact %s is present (%r), expected absent" % (key, facts[key]))
    for key, texts in expected.get("fact_mentions", {}).items():
        values = [json.dumps(r["value"], ensure_ascii=False) for r in rows if r["key"] == key]
        for text in texts:
            if not any(text in v for v in values):
                problems.append("no value of fact %s names %r: %s" % (key, text, values))
    if "import_matches_check" in expected:
        problems += import_matches_check(expected["import_matches_check"], test_folder, run_folder,
                                         result.get("platform", "windows-x64"))
    for part in expected.get("run_folder_lacks", []):
        hits = [p for p in run_folder.rglob("*") if part in p.as_posix()]
        if hits:
            problems.append("the run folder holds %r: %s" % (part, hits[0]))
    for key in RESULT_KEYS:
        if key not in result:
            problems.append("result.json lacks %r" % key)
    return problems
