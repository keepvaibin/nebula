"""The game's compiled program set. build_programs.py CONTENT WORK [--write]

Asks the importer for every program text (`nebula-import programs`), compiles each once in its base form and groups them by
compiled instructions and stage signature: each group is one program. --write puts one text per group in
runtime/src/render/programs/ and table.tsv there, which maps every text's hash to its program. Fails if two programs in that folder compile the same, so the set can't grow a duplicate."""
import hashlib
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


# programs.rs shared_taps: TAP_DEFINE, the taps' declarations and each TAP(name, sample).
TAP_DEFINE = "#if NATIVE_TRACED || NATIVE_PREPASS\n#define TAP(name, sample) (sample)\n#else\n#define TAP(name, sample) name\n#endif\n"
TAP_DECLARATIONS = re.compile(r"#if !\(NATIVE_TRACED \|\| NATIVE_PREPASS\)\n(?:    const float4 tap\d+ = [^\n]*\n)+#endif\n")
TAP_USE = re.compile(r"TAP\(tap\d+, (TEX\((\d+)\)\.Sample\(SAMP\(\2\), i\.uv\d+(?: / float2\([0-9., ]*\))?\))\)")


def untapped(text):
    """A text with shared taps as the generator wrote it before (each stage's own sample)."""
    return TAP_USE.sub(r"\1", TAP_DECLARATIONS.sub("", text.replace(TAP_DEFINE, "")))


def key_of(path):
    """Stage signatures and instructions; not the reflection, the hashes or the metadata numbers."""
    text = open(path, encoding="utf-8", errors="replace").read()
    signature = []
    for name in ("; Input signature:", "; Output signature:"):
        at = text.find(name)
        if at >= 0:
            end = text.find("\n;\n;", at)
            signature.append(text[at:end if end >= 0 else at + 2000])
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
    # A text with shared taps is grouped as the same text without them would be (its forms other than the raster ones
    # compile that text), and named by that text for the choice of a group's program (the first name): a group keeps
    # the program it had, with taps or without, and so its ray-tracing forms.
    tapped = {}
    named = {}
    for n in names:
        with open(os.path.join(texts, n + ".hlsl"), encoding="utf-8", errors="replace", newline="") as f:
            text = f.read().replace("\r\n", "\n")
        if "#define TAP(" in text:
            plain = os.path.join(ll, n + ".untapped.hlsl")
            old = untapped(text)
            if not os.path.exists(plain) or open(plain, encoding="utf-8", newline="").read() != old:
                with open(plain, "w", encoding="utf-8", newline="") as f:
                    f.write(old)
            tapped[n] = (plain, os.path.join(ll, n + ".untapped.ll"), n[0])
            # programs.rs text_hash: the first 64 bits of the SHA-1, little-endian.
            digest = hashlib.sha1(old.encode()).digest()
            named[n] = n[0] + "%016x" % int.from_bytes(digest[:8], "little")
    with ThreadPoolExecutor(int(os.environ.get("JOBS", "4"))) as pool:
        failed = [r for r in pool.map(compile_one, tapped.values()) if r]
    if failed:
        print("FAILED", failed[0])
        return 1
    groups = {}
    for n in names:
        # A text whose base form leaves out what other forms keep (programs.rs stage_lines: an alpha test only ray
        # tracing's and the per-sample forms keep) stays its own program, so those forms stay its own.
        with open(os.path.join(texts, n + ".hlsl"), encoding="utf-8", errors="replace") as f:
            text = f.read()
        own = n if "#if NATIVE_TRACED || NATIVE_PREPASS || NATIVE_SAMPLE" in text else None
        listing = tapped[n][1] if n in tapped else os.path.join(ll, n + ".ll")
        groups.setdefault((n[0], key_of(listing), own), []).append(n)
    vertex = sum(1 for k in groups if k[0] == "v")
    pixel = sum(1 for k in groups if k[0] == "p")
    group_of = {}
    def program(members):
        return min(members, key=lambda m: named.get(m, m))

    for (stage, _, _), members in groups.items():
        for m in members:
            group_of[m] = program(members)
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
        keep = sorted({program(m) for m in groups.values()})
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
