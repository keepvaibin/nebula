"""tables_dump.py <game.pak> <Nebula tree> <out.md> [out.tsv]: docs/modding/TABLES.md

Every cooked table (NTBL) of an asset store, for modders (M3): its path, rows, and columns with their kinds. A
column is stored by its hash (cook/table.rs column_hash: h = h * 31 + byte); its name comes from the game's own source,
every string literal in native/petari (the game reads each column by name), then the names below that no literal spells
whole (the game builds them, or never reads them; each one matched to its hash), then the store's own folder and file
names (a scenario table's columns are its zones), else it shows as #<hash>. Tables of the
same name under each stage (StageData/<stage>/...) are listed once as StageData/*/..., with how many there are and
the union of their columns.
"""
import pathlib
import re
import struct
import sys
from collections import defaultdict

KINDS = {0: "int", 1: "field", 2: "float", 3: "text"}


def column_hash(name):
    h = 0
    for b in name.encode("utf-8"):
        h = (h * 31 + b) & 0xFFFFFFFF
    return h


def dictionary(tree):
    names = {}
    literal = re.compile(r'"([A-Za-z_][A-Za-z0-9_ .\-]{0,63})"')
    for folder in ("native/petari/src", "native/petari/include"):
        for path in (tree / folder).rglob("*.[ch]pp"):
            for m in literal.finditer(path.read_text(encoding="utf-8", errors="replace")):
                names.setdefault(column_hash(m.group(1)), m.group(1))
    for name in built_names():
        names.setdefault(column_hash(name), name)
    return names


def built_names():
    """Columns no literal of the source spells whole, each matched to its table's hash: the lights' colours, positions and
    switches, the animation files and their first frames, the camera's vectors, placements' and fixed points' axes, the
    fish boss's levels, the message attributes, group links."""
    for kind in ("Player", "Strong", "Weak", "Planet"):
        for light in ("Light0", "Light1"):
            yield from (f"{kind}{light}Color{c}" for c in "RGBA")
            yield from (f"{kind}{light}Pos{c}" for c in "XYZ")
            yield f"{kind}{light}FollowCamera"
        yield from (f"{kind}Ambient{c}" for c in "RGBA")
        yield f"{kind}Alpha2"
    for file in ("Bck", "Btk", "Brk", "Bpk", "Btp", "Bva"):
        yield file + "Name"
        yield file + "StartFrame"
    for vector in ("up", "wpoint", "woffset", "axis", "vpanaxis"):
        yield from (f"{vector}.{c}" for c in "XYZ")
    for axis in "xyz":
        yield from ("r" + axis, "s" + axis, "t" + axis)
    for name in ("Size", "DropOffset", "Trans", "Rotate"):
        yield from (name + c for c in "XYZ")
    for name in ("GuardAppearNumLv", "EnergyLv", "RailIDLv", "GuardOffsetLv", "SpeedLv"):
        yield from (f"{name}{level}" for level in range(3))
    for level in range(4):
        for guard in range(8):
            yield f"GuardWaitLv{level}Id{guard}"
            yield from (f"GuardPosLv{level}Id{guard}{c}" for c in "XYZ")
    yield from (f"ForceLowScenarioName{i}" for i in range(8))
    yield from ("UpperFlag0", "UpperFlag1", "OffsetY0", "OffsetY1")
    yield from ("mCameraType", "mCameraID", "mBalloon", "mSound", "mReadID", "mMainMessage", "mAreaID")
    yield from ("parent", "child", "brother", "comment", "Fix", "No", "Scale", "SceneNo", "SceneList", "StageType",
                "ScenarioName", "InterpolateIn", "InterpolateOut")


def store_files(path):
    with open(path, "rb") as f:
        header = f.read(64)
        assert header[:8] == b"NBSTORE\0", "not an asset store"
        version, count, nodes_at, _hashes_at, names_at, names_len = struct.unpack("<IIQQQQ", header[8:48])
        f.seek(nodes_at)
        table = f.read(count * 40)
        f.seek(names_at)
        names = f.read(names_len)
        paths = [""] * count
        for i in range(count):
            start, size, name_at, parent, flags, _first, _children, _ = struct.unpack("<QQIIIIII", table[i * 40:(i + 1) * 40])
            name = names[name_at:names.index(b"\0", name_at)].decode("utf-8", "replace")
            if i:
                paths[i] = name if not paths[parent] else paths[parent] + "/" + name
            if flags & 1 or size < 32:
                continue
            f.seek(start)
            head = f.read(32)
            if head[:4] != b"NTBL":
                continue
            rows, columns = struct.unpack("<II", head[4:12])
            f.seek(start + 32)
            cols = []
            for c in range(columns):
                h, _mask, kind, _width = struct.unpack("<IIBB", f.read(12)[:10])
                cols.append((h, kind))
            yield version, paths[i], rows, cols


LANGUAGES = re.compile(r"(?i)\b(us|eu|jp|kr|cn|tw)(english|french|spanish|german|italian|dutch|portuguese|japanese|korean|simpchinese|tradchinese)\b")


def general_key(path):
    """A path with its own name (stage, object, demo, zone, language) as a placeholder."""
    path = LANGUAGES.sub("<language>", path)
    parts = path.split("/")
    if len(parts) > 2 and parts[0] == "StageData":
        stage = re.sub(r"(?i)(scenario|map|design|sound|light)$", "", parts[1])
        rest = re.sub(re.escape(stage), "<stage>", "/".join(parts[2:]), flags=re.I) if stage else "/".join(parts[2:])
        return "StageData/<stage>/" + rest
    # Collision attributes: one table per collision model, the same columns everywhere.
    if parts[0] in ("ObjectData", "StageData") and path.lower().endswith(".pa"):
        return parts[0] + "/<" + ("object" if parts[0] == "ObjectData" else "stage") + ">/<collision>.pa"
    if len(parts) > 3 and parts[0] == "ObjectData" and parts[1] == "DemoSheet":
        demo = parts[2]
        return "ObjectData/DemoSheet/<demo>/" + re.sub(re.escape(demo), "<demo>", "/".join(parts[3:]), flags=re.I)
    if len(parts) == 3 and parts[0] == "ObjectData" and parts[1] == "LightData":
        return "ObjectData/LightData/" + re.sub(r"^light.*\.bcsv$", "light<zone>.bcsv", parts[2])
    if len(parts) > 2 and parts[0] == "ObjectData":
        obj = parts[1]
        return "ObjectData/<object>/" + re.sub(re.escape(obj), "<object>", "/".join(parts[2:]), flags=re.I)
    return path


def main():
    pak, tree, out_md = pathlib.Path(sys.argv[1]), pathlib.Path(sys.argv[2]), pathlib.Path(sys.argv[3])
    out_tsv = pathlib.Path(sys.argv[4]) if len(sys.argv) > 4 else None
    names = dictionary(tree)
    groups = defaultdict(lambda: {"count": 0, "rows": 0, "cols": {}})
    version = 0
    found = list(store_files(pak))
    tables = len(found)
    for _, path, _, _ in found:
        for part in path.split("/"):
            names.setdefault(column_hash(part), part)
            names.setdefault(column_hash(part.split(".")[0]), part.split(".")[0])
    # A placeholder stands only where at least three tables share the general path; else each keeps its own.
    sharing = defaultdict(int)
    for _, path, _, _ in found:
        sharing[general_key(path)] += 1
    for version, path, rows, cols in found:
        general = general_key(path)
        g = groups[general if sharing[general] >= 3 else LANGUAGES.sub("<language>", path)]
        g["count"] += 1
        g["rows"] += rows
        for h, kind in cols:
            g["cols"].setdefault(h, kind)
    named = sum(1 for g in groups.values() for h in g["cols"] if h in names)
    total = sum(len(g["cols"]) for g in groups.values())
    lines = [
        "# The game's tables",
        "",
        f"Generated by tools/modding/tables_dump.py from a store of version {version}: {tables} tables in "
        f"{len(groups)} kinds, {total} columns ({named} named; #hash for the rest).",
        "",
        "A mod changes a table with a file at the table's path under a .luma layer's `merge/` folder, `<path>.json`:",
        "`remove` (rows matching a set of columns), `change` (`where` a set of columns, `set` columns), and `rows` to add.",
        "The path is as listed, or the game's own with its archive (`ObjectData/DemoSheet.arc/...`). A column is named as",
        "below; `#` and a hash is a column no game code names. `<stage>`, `<object>`, `<demo>`, `<zone>` and `<language>` stand",
        "for each one's own table of that name.",
        "",
        "| Table | Files | Rows | Columns |",
        "|---|---|---|---|",
    ]
    tsv = ["table\tfiles\trows\tcolumn\tkind"]
    for key in sorted(groups):
        g = groups[key]
        cols = sorted(g["cols"].items(), key=lambda kv: names.get(kv[0], "~%08x" % kv[0]).lower())
        shown = ", ".join(f"{names.get(h, '#%08x' % h)} ({KINDS.get(k, k)})" for h, k in cols)
        lines.append(f"| `{key}` | {g['count']} | {g['rows']} | {shown} |")
        for h, k in cols:
            tsv.append(f"{key}\t{g['count']}\t{g['rows']}\t{names.get(h, '#%08x' % h)}\t{KINDS.get(k, k)}")
    out_md.write_text("\n".join(lines) + "\n", encoding="utf-8")
    if out_tsv:
        out_tsv.write_text("\n".join(tsv) + "\n", encoding="utf-8")
    print(f"{tables} tables, {len(groups)} kinds, {named}/{total} columns named")


if __name__ == "__main__":
    main()
