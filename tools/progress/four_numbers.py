"""four_numbers.py <inventory.tsv> <census.tsv> <snapshot>: the program's four numbers for one batch.

console call sites (census.py, game source), undefined console symbols (census.py: the game objects' undefined
symbols, all owners), inventory signal matches (every signal column summed), lines in REPLACE-disposition files."""
import csv
import sys

inv_path, census_path, snap = sys.argv[1], sys.argv[2], sys.argv[3]
with open(inv_path, encoding="utf-8") as f:
    rows = list(csv.DictReader(f, delimiter="\t"))
meta = {"path", "area", "disposition", "requirements", "lines", "action"}
signals = sum(int(v) for r in rows for k, v in r.items() if k not in meta)
replace_lines = sum(int(r["lines"]) for r in rows if r["disposition"] == "REPLACE")
calls = undefined = None
with open(census_path, encoding="utf-8") as f:
    for r in csv.DictReader(f, delimiter="\t"):
        if r["snapshot"] == snap and r["metric"] == "total":
            if r["kind"] == "call sites":
                calls = int(float(r["value"]))
            else:
                undefined = int(float(r["value"]))
print(f"{snap}: call sites {calls}, undefined symbols {undefined}, signal matches {signals}, REPLACE lines {replace_lines}")
