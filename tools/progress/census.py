"""census.py <repo> <game build dir> <snapshot>: appends this batch's console API census to census.tsv.

Two measurements (M), both tracked to zero:
- call sites of console SDK functions in the game source (src/Game, src/JSystem, src/nw4r, src/RVLFaceLib), by family:
  sites and distinct functions;
- the game code's undefined symbols (objects under CMakeFiles/smg_game.dir/petari) by owner, from
  native/tools/boundary.py."""
import collections
import csv
import os
import re
import subprocess
import sys
import tempfile

repo, build, snap = sys.argv[1], sys.argv[2], sys.argv[3]
here = os.path.dirname(os.path.abspath(__file__))
FAMILIES = ["PSMTX", "PSVEC", "PSQUAT", "C_MTX", "C_VEC", "NWC24", "WPAD", "KPAD", "NAND", "WENC", "DVD", "ARQ", "ARC",
            "DSP", "TPL", "THP", "HBM", "RSO", "RFL", "MTX", "VEC", "MEM", "PPC", "GX", "GD", "OS", "VI", "AI", "AR", "DC",
            "IC", "LC", "SC", "VF"]
call = re.compile(r"\b(" + "|".join(FAMILIES) + r")([A-Z][A-Za-z0-9_]*)\s*\(")
sites, names = collections.Counter(), collections.defaultdict(set)
for sub in ("src/Game", "src/JSystem", "src/nw4r", "src/RVLFaceLib"):
    for base, _, files in os.walk(os.path.join(repo, "native", "petari", sub)):
        for f in files:
            if not f.endswith((".c", ".cpp", ".h", ".hpp", ".inc")):
                continue
            text = open(os.path.join(base, f), encoding="utf-8", errors="replace").read()
            text = re.sub(r"/\*.*?\*/", " ", text, flags=re.S)
            text = re.sub(r"//[^\n]*", "", text)
            for m in call.finditer(text):
                fam = m.group(1)
                if fam in ("C_MTX", "C_VEC") or fam + m.group(2) not in ("ARRAY_SIZE", "ARRAY_SIZEU"):
                    sites[fam] += 1
                    names[fam].add(m.group(0).rstrip("( \t"))

rows = []
for fam in sorted(sites, key=lambda f: -sites[f]):
    rows.append({"snapshot": snap, "kind": "call sites", "metric": fam, "value": sites[fam],
                 "detail": f"{len(names[fam])} functions", "evidence": "M census.py"})
rows.append({"snapshot": snap, "kind": "call sites", "metric": "total", "value": sum(sites.values()),
             "detail": f"{sum(len(v) for v in names.values())} functions", "evidence": "M census.py"})

objdir = os.path.join(build, "CMakeFiles", "smg_game.dir", "native", "petari")
if not os.path.isdir(objdir):
    objdir = os.path.join(build, "CMakeFiles", "smg_game.dir", "petari")
# The game code's objects only (the decompiled SDK under src/RVL_SDK is the platform side): hard links in a folder
# next to the build.
with tempfile.TemporaryDirectory(dir=build) as tmp:
    objs = os.path.join(tmp, "objs")
    for base, _, files in os.walk(objdir):
        if "RVL_SDK" in base.replace("\\", "/").split("/"):
            continue
        for f in files:
            # objects of sources that no longer exist (deleted since the build folder last saw them) are not the game's
            rel = os.path.relpath(os.path.join(base, f), objdir)
            if f.endswith((".o", ".obj")) and not os.path.exists(
                    os.path.join(repo, "native", "petari", os.path.splitext(rel)[0])):
                continue
            if f.endswith((".o", ".obj")):
                dst = os.path.join(objs, os.path.relpath(base, objdir))
                os.makedirs(dst, exist_ok=True)
                os.link(os.path.join(base, f), os.path.join(dst, f))
    r = subprocess.run([sys.executable, "-I", os.path.join(repo, "native", "tools", "boundary.py"), objs, tmp],
                       capture_output=True, text=True, check=True)
    owners = collections.Counter()
    with open(os.path.join(tmp, "boundary.tsv"), encoding="utf-8") as f:
        for b in csv.DictReader(f, delimiter="\t"):
            owners[b["owner"]] += 1
for o, n in owners.most_common():
    rows.append({"snapshot": snap, "kind": "undefined symbols", "metric": o, "value": n, "detail": "",
                 "evidence": "M boundary.py"})
rows.append({"snapshot": snap, "kind": "undefined symbols", "metric": "total", "value": sum(owners.values()),
             "detail": r.stdout.splitlines()[0], "evidence": "M boundary.py"})

path = os.path.join(here, "census.tsv")
old = []
if os.path.exists(path):
    with open(path, encoding="utf-8") as f:
        old = [x for x in csv.DictReader(f, delimiter="\t") if x["snapshot"] != snap]
with open(path, "w", encoding="utf-8", newline="\n") as f:
    w = csv.DictWriter(f, ["snapshot", "kind", "metric", "value", "detail", "evidence"], delimiter="\t")
    w.writeheader()
    w.writerows(old + rows)
for x in rows:
    if x["metric"] == "total":
        print(x["kind"], x["value"], x["detail"])
