"""audit_ledger_build.py RAW.tsv OUT_DIR: the plan's ledger: every finding of the audit with the agent and the work
package that closes it. Writes OUT_DIR/03-LEDGER.md and OUT_DIR/ledger.tsv. Fails if any finding has no package or two."""
import pathlib
import re
import sys

# Agent 1 (engine and port). Package -> finding numbers.
P = {
    "E0 gate": "23 24 43 44 68 124 89 90 91 159 169 14",
    "E1 data safety": "113 114 79 81 82 119 94 93 18 92 67 45 80 85 95 96",
    "E2 crashes and wrong behaviour": "5 8 12 62 122 164 170 171 177 178 183 153 172 179 158 123 161 51 47 182",
    "E3 render data path": "151 154 155 156 157 149 150 142 143 173 181 144 145 46",
    "E4 the frame": "6 7 21 31 55 25 27 53 63",
    "E5 hitches and GPU waits": "2 10 28 30 48 49 50 52 54 56 57 65 66 70 59 26 29 58 60 61 64 11 117",
    "E6 pacing and in-between pictures": "1 3 33 41 42 69 78 163 174 4 34 147",
    "E7 game step speed": "13 38 39 40 127 128 129 130 131 132 133 134 135 136 137 138 139 140 141 146 152 165 88",
    "E8 console limits and gameplay rules": "148 166 167 168 180 118",
    "E9 upscalers, HDR and option parity": "175 176 32 9 87",
    "E10 audio, input, menus, text": "37 71 72 73 74 75 76 77 83 84 86 160 162 106",
    "E11 Android app": "19 20 97 98 99 100 101 102",
    "E12 Windows, Linux, Mac": "15 16 17 35 36 103 104 105 107 108 109 110 111 112 115 116 120 121 125 126",
    "E13 tidy": "22",
}
I = {"E6 pacing and in-between pictures": " ".join(str(n) for n in range(1, 17))}
# Agent 2 (mods).
M = {
    "M0 mod tests into the gate": "26 35 36 100 117 174 19 164",
    "M1 kernel": "1 2 3 5 7 8 9 10 13 18 20 27 28 110 129 112",
    "M2 call safety": "14 15 111 113 38 45 49 173 76 77",
    "M3 services": "11 29 82 30 55 72 73 75 78 79 81 83 84 85 86 88 90 91 92 93 94 95 96 97 98 99 109",
    "M4 ownership": "21 33 37 40 41 42 43 44 46 47 48 50 51 52 53 54 59 65 70 71 80 87 89 135 136 138",
    "M5 after a crash": "31 32",
    "M6 files from strangers": "143 144 145 146 150 151 152 156 157 158 159 160 161 162 166 167 168 169",
    "M7 compiler": "114 116 118 119 121 122 123 124 125 126 127 128 130 131 132 133 134 137 139 140",
    "M8 LumaLink": "23 141 142 147 148 149 153 163 165",
    "M9 mod manager": "101 102 103 104 105 106 107 108 154 155",
    "M10 camera, look and first person": "56 57 58 60 61 62 63 64 66 67 68 69 175",
    "M11 documentation": "4 16 17 22 24 25 34 39 115 120 170 171 172 176 177",
    "M12 platforms for mods": "6 12 74",
}
NOTE = {
    "P-166": "Owner: keep the port's behaviour.",
    "P-167": "Owner: restore the console's rule.",
    "P-168": "Owner: a small difference is fine where it is faster. Keep unless a faster form is measured; replays must still match.",
    "P-148": "Owner: let it grow, like every other console limit.",
    "P-180": "Owner: leave as it is.",
    "P-118": "Owner: leave as it is.",
    "P-104": "Owner: out of scope until the game builds there; mods stop building for it (M-006).",
    "P-035": "Owner: must compile and be complete in code; the owner tests on Apple silicon later.",
    "P-042": "Owner did not answer; the recommendation stands: a 120 Hz display starts at 120 once the measures hold.",
    "P-082": "Withdrawn by the reader. Confirm by test and close.",
    "P-019": "Corrected: the hint exists; its fault is P-069.",
    "P-055": "Same finding as P-006.",
    "P-122": "The owner checks it in play later; do not wait: find the cause by run and fix it.",
    "P-032": "Owner keeps Direct3D 11. Where HDR cannot be offered, the option is not shown.",
    "P-009": "Owner keeps Direct3D 11: bring it to parity or hide what it cannot do.",
    "M-100": "Owner: the notice goes on the Mods list.",
    "M-102": "Withdrawn by the reader. Confirm by test and close.",
    "M-166": "Withdrawn by the reader. Confirm by test and close.",
    "M-006": "Owner: stop building mods for iOS and Windows on ARM until the game builds there.",
    "M-024": "Owner: mods must be able to do anything. Correct the wording about permissions; do not narrow what a mod can do.",
    "M-014": "Run by the coordinator: the crash is at boot frames, not on the title.",
    "M-106": "Corrected: the message says OLDER VERSION; the replacement still happens.",
}


def build(prefix, table):
    out = {}
    for package, numbers in table.items():
        for n in numbers.split():
            tag = "%s-%03d" % (prefix, int(n))
            assert tag not in out, "%s is in two packages" % tag
            out[tag] = package
    return out


raw = [line.rstrip("\n").split("\t") for line in pathlib.Path(sys.argv[1]).read_text(encoding="utf8").splitlines()]
where = {}
where.update(build("P", P))
where.update(build("M", M))
where.update(build("I", I))
missing = [r[0] for r in raw if r[0] not in where]
assert not missing, "no package: %s" % " ".join(missing)
extra = [k for k in where if k not in {r[0] for r in raw}]
assert not extra, "not a finding: %s" % " ".join(extra)

FILES = {"M": "A-findings-mods.md", "P": "B-findings-port.md", "I": "E-pacing-interpolation-as-built.md"}
rows = []
for tag, _heading, title in raw:
    title = title.strip()
    broken = (not title or title[0] in ",." or re.match(r"^(Missing feature|Maintainability|Docs wrong|is in Area|M-\d\d\d gains|\(same finding)", title)
              or re.match(r"^, ", title))
    if broken:
        title = "see the finding's own text"
    title = title.replace("|", "/")
    package = where[tag]
    agent = "1" if package.startswith("E") else "2"
    rows.append((tag, agent, package, title[:150], NOTE.get(tag, "")))

out = pathlib.Path(sys.argv[2])
out.mkdir(parents=True, exist_ok=True)
(out / "ledger.tsv").write_text("id\tagent\tpackage\tstate\tproof\ttitle\tnote\n" + "\n".join(
    "%s\t%s\t%s\topen\t\t%s\t%s" % (r[0], r[1], r[2], r[3], r[4]) for r in rows) + "\n", encoding="utf8")

text = ["# 03. The ledger: every finding, who closes it, and in which package", "",
        "376 findings from the audit: M-001 to M-177 (file A), P-001 to P-183 (file B), I-001 to I-016 (file E). "
        "The gaps and measurements of file F (G1 to G9, M1 to M4, T1 to T7) are in package E6 and are listed in the Agent 1 plan.", "",
        "**How it is used.** `ledger.tsv` beside this file goes into the repository on day one (`docs/plan/ledger.tsv`). "
        "Each row has a `state` and a `proof`. A row is closed only as one of:", "",
        "| State | Meaning | What `proof` must name |", "|---|---|---|",
        "| `fixed` | The fault is gone | The commit, and the test in the gate that fails without the fix |",
        "| `not-a-fault` | Checked again and the finding was wrong | The test or the lines that show it |",
        "| `decided` | The owner chose to leave it | The owner's words (the note column, or file 00) |",
        "| `out-of-scope` | Named as outside this plan in file 00 | The line of file 00 |", "",
        "The work is finished only when no row is `open`. A row may not be closed as `fixed` without a test.", "",
        "The title is the first line of the finding. The finding's full text, with file and line and the suggested fix, is in the audit file.", ""]
for agent, name in (("1", "Agent 1: engine and port"), ("2", "Agent 2: mods")):
    text += ["## %s" % name, ""]
    packages = []
    for r in rows:
        if r[1] == agent and r[2] not in packages:
            packages.append(r[2])
    packages.sort(key=lambda p: int(re.match(r"[EM](\d+)", p).group(1)))
    for package in packages:
        mine = [r for r in rows if r[2] == package]
        text += ["### %s (%d)" % (package, len(mine)), "", "| Finding | First line | Owner's decision or note |", "|---|---|---|"]
        text += ["| %s | %s | %s |" % (r[0], r[3], r[4]) for r in mine]
        text.append("")
(out / "03-LEDGER.md").write_text("\n".join(text), encoding="utf8")
count = {}
for r in rows:
    count[r[2]] = count.get(r[2], 0) + 1
print(len(rows), "findings;", sum(1 for r in rows if r[1] == "1"), "to agent 1;", sum(1 for r in rows if r[1] == "2"), "to agent 2")
print("; ".join("%s %d" % kv for kv in sorted(count.items(), key=lambda kv: (kv[0][0], int(re.match(r"[EM](\d+)", kv[0]).group(1))))))
