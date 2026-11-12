"""placement.py --types luma_types.json --defined DIR --out smg_game.luma-placement.json

The placement facts (plan 1.8, contract self-description "places"; G's 3.9 cooks a stage's placement arguments by name),
made by the build beside the module from instrument.py's .places parts (DIR: the folder of its copies), never by hand:

  factory          the object factory's rows in its order (NameObjFactory.cpp cCreateTable): {name, class, creator};
                   class: a template creator's argument, or the one class a function creator news (its body in
                   any unit); null when there is none or more than one
  placement_args   by class (the types file's name): every placement argument read its methods make, and its bases'
                   (each row's "from" names the class whose method reads it), as
                   {arg, field, type, with_init, target, local, written, function, signature, file, line, from, via}
                   - arg: 0 to 7 for Obj_argN; null for a column read by its own name (field: "RotateSpeed")
                   - target: the field the read sets as written (mShotInterval); null for a local (named in "local"),
                     an element, or a read made through a helper
                   - via: [] for a read in the class's own method; for a read inside a function the method calls with
                     its placement iterator (getDomeIdFromArg0(rIter), a free function or another class's), the chain
                     of those functions, the read's own function last; target is then null and file/line are the
                     method's call
  left             what has no class to go under: reads in free functions no method calls ({function, reason})

A unit the build instrumented without a .places part stops the tool, naming it; nothing is guessed."""
import argparse
import json
import pathlib
import sys

PLACEMENT_VERSION = 1


def fail(text):
    print("placement: " + text, file=sys.stderr)
    sys.exit(1)


def read_json(path):
    try:
        return json.loads(pathlib.Path(path).read_text(encoding="utf-8"))
    except (OSError, ValueError) as error:
        fail("%s: %s" % (path, error))


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--types", required=True)
    parser.add_argument("--defined", required=True)
    parser.add_argument("--out", required=True)
    a = parser.parse_args()

    bases = {}
    for t in read_json(a.types)["types"]:
        # A record with no name (an anonymous struct or union inside a class) is no class a placement can make, and
        # sorted(bases) cannot order None among names (the integration build, 11-12).
        if t["name"] is None:
            continue
        bases[t["name"]] = [b["name"] for b in t.get("bases", ()) if b["name"] is not None]

    folder = pathlib.Path(a.defined)
    functions = {}
    makers = {}  # a free function that news one class -> {that class}
    factory = None
    for rows in sorted(folder.rglob("*.rows")):
        part = pathlib.Path(str(rows)[:-len(".rows")] + ".places")
        if not part.is_file():
            fail("%s: instrumented, but no .places part (an instrument.py from before 1.8 places?)" % rows.name[:-5])
        data = read_json(part)
        if data.get("factory") is not None:
            if factory is not None:
                fail("%s: a second factory table" % part.name)
            factory = data["factory"]
        for f in data["functions"]:
            key = (f["name"], f["signature"])
            if key in functions:
                fail("%s %s: given by two units (%s, %s)" % (f["name"], f["signature"], functions[key]["file"], f["file"]))
            functions[key] = f
        for name, made in data.get("makers", {}).items():
            makers.setdefault(name, set()).add(made)
    if factory is None:
        fail("no unit gave the factory's table (NameObjFactory.cpp's .places)")
    # A row whose creator's body its unit could not see: the class that function news, from the unit defining it (one
    # class, from one definition; otherwise it stays null).
    factory = [dict(r, **{"class": next(iter(makers[r["creator"]]))})
               if r["class"] is None and len(makers.get(r["creator"], ())) == 1 else r for r in factory]

    def ancestors(name, seen=None):
        """A class's bases, all the way up, nearest first, each once."""
        seen = [] if seen is None else seen
        for b in bases.get(name, ()):
            if b not in seen:
                seen.append(b)
                ancestors(b, seen)
        return seen

    def through(key, chain):
        """The reads a function makes, itself and through the functions it hands its iterator to: (read, via)."""
        f = functions.get(key)
        if f is None or key in chain:
            return []
        chain = chain + [key]
        found = [(r, [k[0] for k in chain]) for r in f["reads"]]
        for h in f["helpers"]:
            found += through((h["name"], h["signature"]), chain)
        return found

    own = {}
    called = set()
    for key, f in functions.items():
        for h in f["helpers"]:
            called.add((h["name"], h["signature"]))
    left = []
    for key, f in sorted(functions.items()):
        owner = f["owner"]
        if owner is None:
            if key not in called and f["reads"]:
                left.append({"function": f["name"], "signature": f["signature"], "file": f["file"],
                             "reason": "a free function no method hands a placement iterator to"})
            continue
        rows = own.setdefault(owner, [])
        for r in f["reads"]:
            rows.append({"arg": r["arg"], "field": r["field"], "type": r["type"], "with_init": r["with_init"], "target": r["target"],
                         "local": r["local"], "written": r["written"], "function": f["name"],
                         "signature": f["signature"], "file": f["file"], "line": r["line"], "via": []})
        family = [owner] + ancestors(owner)
        for h in f["helpers"]:
            hkey = (h["name"], h["signature"])
            helper = functions.get(hkey)
            if helper is None or helper["owner"] in family:
                continue  # no reads of its own, or the class's (or a base's) own method: its rows are there already
            for r, via in through(hkey, [key]):
                rows.append({"arg": r["arg"], "field": r["field"], "type": r["type"], "with_init": r["with_init"], "target": None,
                             "local": None, "written": r["written"], "function": f["name"],
                             "signature": f["signature"], "file": f["file"], "line": h["line"], "via": via[1:]})

    placement_args = {}
    for name in sorted(bases):
        rows = []
        for cls in [name] + ancestors(name):
            for r in own.get(cls, ()):
                row = dict(r, **{"from": cls})
                if row not in rows:
                    rows.append(row)
        if rows:
            rows.sort(key=lambda r: (r["arg"] if r["arg"] is not None else 8, r["field"] or "", r["from"] != name, r["function"], r["line"]))
            placement_args[name] = rows
    unknown = sorted(c for c in own if c not in bases)
    if unknown:
        fail("owners the types file has no row for: %s" % ", ".join(unknown[:10]))
    for row in factory:
        if row["class"] is not None and row["class"] not in bases:
            fail("factory row %s: its class %s has no row in the types file" % (row["name"], row["class"]))

    out = {"placement": PLACEMENT_VERSION, "factory": factory, "placement_args": placement_args, "left": left}
    text = json.dumps(out, ensure_ascii=False, indent=1) + "\n"
    path = pathlib.Path(a.out)
    if not path.is_file() or path.read_text(encoding="utf-8") != text:
        path.write_text(text, encoding="utf-8")
    reads = sum(len(f["reads"]) for f in functions.values())
    print("placement: %d factory rows (%d without a class), %d reads in %d functions, %d classes with arguments, %d left"
          % (len(factory), sum(1 for r in factory if r["class"] is None), reads, len(functions), len(placement_args),
             len(left)))


if __name__ == "__main__":
    main()
