"""The game's compiled program set. build_programs.py CONTENT WORK [--write]

Asks the importer for every program text (`nebula-import programs`), compiles each once in its base form and groups them by
compiled instructions and stage signature: each group is one program. --write puts one text per group in
runtime/src/render/programs/ and table.tsv there, which maps every text's hash to its program. Fails if two programs in that folder compile the same, so the set can't grow a duplicate."""
import os
import re
import subprocess
import sys
from concurrent.futures import ThreadPoolExecutor

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
DXC = os.environ.get("DXC", "C:/Program Files (x86)/Windows Kits/10/bin/10.0.26100.0/x64/dxc.exe")
IMPORT = os.environ.get("NEBULA_IMPORT", os.path.join(ROOT, "target", "release", "nebula-import.exe"))
INCLUDE = os.path.join(ROOT, "runtime", "src", "render", "materials")
OUT = os.path.join(ROOT, "runtime", "src", "render", "programs")


def compile_one(job):
    src, out, stage = job
    if os.path.exists(out) and os.path.getsize(out) and os.path.getmtime(out) >= os.path.getmtime(src):
        return None
    profile, entry = ("vs_6_0", "program_vs") if stage == "v" else ("ps_6_0", "program_ps")
    r = subprocess.run([DXC, "-nologo", "-T", profile, "-E", entry, "-O3", "-Qstrip_debug", "-I", INCLUDE, "-Fc", out, "-Fo", out + ".o", src],
                       capture_output=True, text=True)
    if os.path.exists(out + ".o"):
        os.remove(out + ".o")
    if r.returncode:
        return src + "\n" + r.stderr[:1500]
    return None


def key_of(path):
    """Stage signatures and instructions; not the reflection, the hashes or the metadata numbers."""
    text = open(path, encoding="utf-8", errors="replace").read()
    signature = []
    for name in ("; Input signature:", "; Output signature:"):
        # d23: the whole table, to the two bare ";" lines that close it. ("\n;\n;" alone matched between the header's
        # own ";" line and "; Name": the key held no signature, and two pixel programs with the same instructions
        # and different inputs were grouped as one; r0715's refused pipelines.)
        at = text.find(name)
        end = text.find("\n;\n;\n", at) if at >= 0 else -1
        table = text[at:end] if end > at else ""
        if "; Name" not in table or "-----" not in table:
            raise SystemExit(f"build_programs: {path}: no whole '{name}' table (the grouping key needs it)")
        signature.append(table)
    begin = text.find("define void @")
    end = text.find("\n}\n", begin)
    body = text[begin:end]
    body = re.sub(r"!\w+ !\d+", "", body)
    body = re.sub(r", !\w+", "", body)
    return "\n".join(signature) + body


def main():
    content, work = sys.argv[1], sys.argv[2]
    write = "--write" in sys.argv[3:]
    texts = os.path.join(work, "texts")
    ll = os.path.join(work, "ll")
    os.makedirs(ll, exist_ok=True)
    subprocess.run([IMPORT, "programs", content, texts], check=True)
    names = sorted(n[:-5] for n in os.listdir(texts) if n.endswith(".hlsl"))
    jobs = [(os.path.join(texts, n + ".hlsl"), os.path.join(ll, n + ".ll"), n[0]) for n in names]
    with ThreadPoolExecutor(int(os.environ.get("JOBS", "4"))) as pool:
        failed = [r for r in pool.map(compile_one, jobs) if r]
    for f in failed[:10]:
        print("FAILED", f)
    if failed:
        print(len(failed), "failed of", len(jobs))
        return 1
    groups = {}
    for n in names:
        groups.setdefault((n[0], key_of(os.path.join(ll, n + ".ll"))), []).append(n)
    vertex = sum(1 for k in groups if k[0] == "v")
    pixel = sum(1 for k in groups if k[0] == "p")
    group_of = {}
    for (stage, _), members in groups.items():
        for m in members:
            group_of[m] = min(members)
    pairs = set()
    with open(os.path.join(texts, "forms.tsv"), encoding="utf-8", errors="replace") as forms:
        plain = set()
        materials = 0
        for line in forms:
            f = line.rstrip("\n").split("\t")
            materials += 1
            pair = (group_of["v" + f[5]], group_of["p" + f[6]])
            pairs.add(pair)
            if f[2] == "0" and f[3] == "0" and f[4] == "0":
                plain.add(pair)
    print(f"texts {len(names)} vertex {vertex} pixel {pixel} pairs {len(pairs)} plain pairs {len(plain)} forms {materials}")
    if write:
        os.makedirs(OUT, exist_ok=True)
        keep = sorted({min(m) for m in groups.values()})
        for n in os.listdir(OUT):
            if n.endswith(".hlsl") and n[:-5] not in keep:
                os.remove(os.path.join(OUT, n))
        for n in keep:
            with open(os.path.join(texts, n + ".hlsl"), encoding="utf-8", newline="") as src, open(os.path.join(OUT, n + ".hlsl"), "w", encoding="utf-8", newline="") as dst:
                dst.write(src.read())
        with open(os.path.join(OUT, "table.tsv"), "w", encoding="utf-8", newline="\n") as table:
            for n in sorted(names):
                table.write(f"{n[0]}\t{n[1:]}\t{group_of[n]}\n")
    return 0


if __name__ == "__main__":
    sys.exit(main())
