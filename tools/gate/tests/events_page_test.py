"""events_page_test.py: the game's events as the docs promise them (M-111).

python -X utf8 -I tools/gate/tests/events_page_test.py [--work <folder>]

- docs/modding/reference/events.md is what tools/modding/events_list.py writes from the tree (not stale);
- each event's row names the game function that raises it (a mod hooks that function to act at the moment);
- nebula_mod_api.h's events (from `typedef enum NebulaModEvent` to the callback type) promise nothing from a return
  value: the events are told after the fact and no caller reads a result (no "return", "cancel", "instead",
  "is used", "about to").
Exit 0 when every case holds.
"""
import pathlib
import re
import subprocess
import sys
import tempfile

TREE = pathlib.Path(__file__).resolve().parents[3]
PAGE = TREE / "docs" / "modding" / "reference" / "events.md"
API = TREE / "native" / "include" / "nebula_mod_api.h"
TOOL = TREE / "tools" / "modding" / "events_list.py"
PROMISES = re.compile(r"\breturn|\bcancel|\binstead\b|\bis used\b|\babout to\b", re.IGNORECASE)


def text(path):
    return path.read_bytes().decode("utf-8").replace("\r\n", "\n")


def main():
    failed = []
    work = pathlib.Path(sys.argv[sys.argv.index("--work") + 1]) if "--work" in sys.argv else None
    if work is not None:
        work.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(dir=work) as out:
        r = subprocess.run([sys.executable, "-X", "utf8", "-I", str(TOOL), str(TREE), out], capture_output=True,
                           text=True, encoding="utf-8")
        made = pathlib.Path(out) / "events.md"
        if r.returncode != 0 or not made.exists():
            failed.append("events_list.py failed: " + (r.stdout + r.stderr).strip()[-400:])
        elif text(made) != text(PAGE):
            failed.append("docs/modding/reference/events.md is stale: run python -X utf8 tools/modding/events_list.py .")

    rows = [l for l in text(PAGE).splitlines() if l.startswith("| `NEBULA_MOD_EVENT_")]
    if len(rows) != 29:
        failed.append("events.md lists %d events, not 29" % len(rows))
    for row in rows:
        cells = [c.strip() for c in row.strip().strip("|").split("|")]
        if len(cells) < 3 or "`" not in cells[-1] or cells[-1] == cells[1]:
            failed.append("no raising function: " + row[:80])

    api = text(API)
    start = api.index("typedef enum NebulaModEvent")
    end = api.index("NebulaModCallback", start)
    for n, line in enumerate(api[start:end].splitlines(), api[:start].count("\n") + 1):
        if PROMISES.search(line):
            failed.append("nebula_mod_api.h:%d promises a result: %s" % (n, line.strip()))

    for f in failed:
        print("FAIL", f)
    print("events page: " + ("all hold" if not failed else "%d faults" % len(failed)))
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
