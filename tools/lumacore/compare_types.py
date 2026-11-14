"""compare_types.py GENERATED DESCRIBED

docs/modding/language/types.md: the module's type rows (DESCRIBED, what nebula_game_luma_describe wrote: the host's
--luma-describe or native/tools/luma_describe.c) against the generator's luma_types.json (GENERATED: libclang's numbers,
and `reachable` and `left_out` by C++'s lookup). Every record, field, base, alias and global with a row, and for every
record every name its bases give it: bound with the same position, or refused for the same reason and names (TY-1,
TY2-1); and the kernel's left rows against left_out (TY-7). Any difference fails, naming the row; exit 1. Exit 0 prints
the counts compared.
"""
import json
import pathlib
import sys

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
import gen_types  # noqa: E402  (left_rows: the kernel's left rows as the generator writes them)

SHOWN = 60


def main():
    if len(sys.argv) != 3:
        sys.exit(__doc__)
    generated = json.loads(pathlib.Path(sys.argv[1]).read_text(encoding="utf-8"))
    described = json.loads(pathlib.Path(sys.argv[2]).read_text(encoding="utf-8"))
    wrong = []

    def differ(text):
        wrong.append(text)

    records = {r["name"]: r for r in generated["types"] if "fields" in r}
    rows = {r["name"]: r for r in described["types"]}
    for name in sorted(set(records) - set(rows)):
        differ(f"record {name}: in the generator's JSON, no row in the module")
    for name in sorted(set(rows) - set(records)):
        differ(f"record {name}: a row in the module, not in the generator's JSON")
    compared = {"records": 0, "fields": 0, "bases": 0, "reachable": 0, "refused": 0, "aliases": 0, "globals": 0}
    for name in sorted(set(records) & set(rows)):
        r, k = records[name], rows[name]
        compared["records"] += 1
        for key in ("size", "align", "polymorphic"):
            if r[key] != k[key]:
                differ(f"record {name}: {key} {r[key]} by libclang, {k[key]} in the module")
        if [f["name"] for f in r["fields"]] != [f["name"] for f in k["fields"]]:
            differ(f"record {name}: fields {[f['name'] for f in r['fields']]} by libclang, "
                   f"{[f['name'] for f in k['fields']]} in the module")
        else:
            for f, g in zip(r["fields"], k["fields"]):
                compared["fields"] += 1
                if f["bits"]:
                    want = (f["bits"]["offset"], f["bits"]["width"])
                    got = (g["offset"] * 8 + g["bit"], g["bits"])
                else:
                    want, got = (f["offset"] * 8, 0), (g["offset"] * 8, g["bits"])
                if want != got or f["type"] != g["type"] or f["count"] != g["count"]:
                    differ(f"field {name}.{f['name']}: bit position and width {want}, type {f['type']!r}, count "
                           f"{f['count']} by libclang; {got}, {g['type']!r}, {g['count']} in the module")
        want_bases = [(b["name"], b["offset"], b["virtual"]) for b in r["bases"]]
        got_bases = [(b["name"], b["offset"], b["virtual"]) for b in k["bases"]]
        compared["bases"] += len(want_bases)
        if want_bases != got_bases:
            differ(f"record {name}: bases {want_bases} by libclang, {got_bases} in the module")
        # Each inherited name, bound at the same place (a bit-field: its position in bits).
        want_reach = {}
        for h in r["reachable"]:
            declared = next((f for f in records.get(h["from"], {}).get("fields", []) if f["name"] == h["name"]), None)
            if declared is not None and declared["bits"]:
                want_reach[h["name"]] = (h["offset"] - declared["offset"]) * 8 + declared["bits"]["offset"]
            else:
                want_reach[h["name"]] = h["offset"]
        got_reach = {h["name"]: h["value"] for h in k["reachable"]}
        compared["reachable"] += len(want_reach)
        for n in sorted(set(want_reach) | set(got_reach)):
            if want_reach.get(n) != got_reach.get(n):
                differ(f"field {name}.{n} (inherited): {want_reach.get(n)} by the generator's lookup, "
                       f"{got_reach.get(n)} by the kernel's")
        for h in k["refused"]:
            compared["refused"] += 1
    # Every name left out as ambiguous or through a virtual base is refused by the kernel with that reason.
    want_refused = {}
    for x in generated["left_out"]:
        if x["kind"] != "field":
            continue
        if x["reason"].startswith("ambiguous: "):
            want_refused[x["name"]] = ("ambiguous", x["reason"][len("ambiguous: "):])
        elif x["reason"].startswith("through the virtual base "):
            want_refused[x["name"]] = ("virtual", x["reason"][len("through the virtual base "):])
    got_refused = {k["name"] + "." + h["name"]: (h["why"], h["names"]) for k in described["types"] for h in k["refused"]}
    for n in sorted(set(want_refused) | set(got_refused)):
        if want_refused.get(n) != got_refused.get(n):
            differ(f"field {n}: refused as {want_refused.get(n)} by the generator, {got_refused.get(n)} by the kernel")
    want_aliases = {a["name"]: a["record"] for a in generated["types"] if a.get("kind") in ("alias", "spelling") and a.get("row")}
    got_aliases = {a["name"]: a["record"] for a in described["aliases"]}
    compared["aliases"] = len(want_aliases)
    for n in sorted(set(want_aliases) | set(got_aliases)):
        if want_aliases.get(n) != got_aliases.get(n):
            differ(f"alias {n}: names {want_aliases.get(n)} in the generator's JSON, {got_aliases.get(n)} in the module")
    # The kernel's left rows are left_out's, one per group and name, with the reason's id (the validator's words).
    want_left = {(g, n): (rid, what) for g, n, rid, what in gen_types.left_rows(
        [(x["kind"], x["name"], x["file"], x["line"], x["reason"]) for x in generated["left_out"]])}
    got_left = {(x["group"], x["name"]): (x["why"], x.get("what")) for x in described["left_out"]}
    compared["left_out"] = len(want_left)
    for key in sorted(set(want_left) | set(got_left)):
        if want_left.get(key) != got_left.get(key):
            differ(f"left_out {key[1]} (group {key[0]}): {want_left.get(key)} by the generator, {got_left.get(key)} in the module")
    want_globals = {g["name"]: (g["type"], g["const"]) for g in generated["globals"] if g.get("defined")}
    got_globals = {g["name"]: (g["type"], g["const"]) for g in described["globals"]}
    compared["globals"] = len(want_globals)
    for n in sorted(set(want_globals) | set(got_globals)):
        if want_globals.get(n) != got_globals.get(n):
            differ(f"global {n}: {want_globals.get(n)} in the generator's JSON, {got_globals.get(n)} in the module")
    # objects.md: one method row (a virtual call) per virtual the generator gave a row, and no other.
    want_methods = {(v["name"], v["signature"], r["name"], v["call"]["spelling"], v["params"])
                    for r in generated["types"] for v in r.get("virtuals", ()) if v["row"]}
    got_methods = [(m["name"], m["signature"], m["record"], m["own"], m["params"]) for m in described["methods"]]
    compared["methods"] = len(want_methods)
    if len(set(got_methods)) != len(got_methods):
        differ("methods: %d rows in the module, %d names and signatures" % (len(got_methods), len(set(got_methods))))
    for m in sorted(want_methods ^ set(got_methods)):
        differ(f"method {m[0]} {m[1]} (record {m[2]}, {m[3]}{m[4]}): " + (
            "in the generator's JSON, no row in the module" if m in want_methods
            else "a row in the module, not in the generator's JSON"))
    if wrong:
        print(f"compare_types.py: {len(wrong)} differences between the module's type rows and the generator's", file=sys.stderr)
        for line in wrong[:SHOWN]:
            print("  " + line, file=sys.stderr)
        if len(wrong) > SHOWN:
            print(f"  ... and {len(wrong) - SHOWN} more", file=sys.stderr)
        sys.exit(1)
    print("compare_types.py: the module's type rows are the generator's: " +
          ", ".join(f"{v} {k}" for k, v in compared.items()))


if __name__ == "__main__":
    main()
