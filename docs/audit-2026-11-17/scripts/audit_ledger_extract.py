"""audit_ledger_extract.py AUDIT_DIR OUT.tsv: every finding of the audit (M-001..M-177, P-001..P-183, I-001..I-016) as one
row: id, the heading it sits under in its file, and its own first line. Reads only."""
import pathlib
import re
import sys

D = pathlib.Path(sys.argv[1])
SETS = (("M", 177, "A-findings-mods.md"), ("P", 183, "B-findings-port.md"), ("I", 16, "E-pacing-interpolation-as-built.md"))
rows = []
for prefix, last, name in SETS:
    lines = (D / name).read_text(encoding="utf8", errors="replace").splitlines()
    heading_at = []
    current = ""
    for text in lines:
        if re.match(r"^#{2,3} ", text):
            current = re.sub(r"^#+ ", "", text).strip()
        heading_at.append(current)
    for n in range(1, last + 1):
        tag = "%s-%03d" % (prefix, n)
        found = None
        define = re.compile(r"^\s*(?:[-*] |#{2,5} |\d+\. )?\**%s\b\**" % re.escape(tag))
        for i, text in enumerate(lines):
            if define.match(text):
                found = i
                break
        if found is None:
            for i, text in enumerate(lines):
                if tag in text:
                    found = i
                    break
        if found is None:
            rows.append((tag, "", "NOT FOUND"))
            continue
        title = re.sub(r"^\s*(?:[-*] |#{2,5} |\d+\. )?", "", lines[found]).replace("**", "").strip()
        title = re.sub(r"^%s[ .:)]*" % re.escape(tag), "", title).strip()
        rows.append((tag, heading_at[found][:70], title[:230]))
pathlib.Path(sys.argv[2]).write_text("\n".join("\t".join(r) for r in rows) + "\n", encoding="utf8")
print(len(rows), "rows;", sum(1 for r in rows if r[2] == "NOT FOUND"), "not found")
