"""audit_cite.py FILE PREFIX FIRST LAST: for each finding PREFIX-nnn (FIRST..LAST) in a findings file, print its statement
line and the source lines its first citations name, so a reviewer can judge the claim against the code. Reads only."""
import pathlib
import re
import sys

ROOTS = [pathlib.Path("D:/NebulaWork/MarioGalaxyRecomp/Nebula-Clean"),
         pathlib.Path("D:/NebulaWork/lumalink-work/lumalink-sweep"),
         pathlib.Path("D:/NebulaWork/lumalink-work/lumalink-sweep/crates/lumascript/src"),
         pathlib.Path("D:/NebulaWork/lumalink-work/lumalink-sweep/crates/lumascript"),
         pathlib.Path("D:/NebulaWork/lumalink-work/lumalink-sweep/crates/lumalink-core/src"),
         pathlib.Path("D:/NebulaWork/MarioGalaxyRecomp/Nebula-Clean/crates/nebula-import/src"),
         pathlib.Path("D:/NebulaWork/MarioGalaxyRecomp/Nebula-Clean/crates/luma-validate/src")]
INDEX = {}


def index():
    for root in ROOTS[:2]:
        for p in root.rglob("*"):
            if p.is_file() and p.suffix in (".cpp", ".h", ".hpp", ".mm", ".rs", ".py", ".java", ".kt", ".lum", ".md", ".c",
                                            ".inl", ".sh", ".ps1", ".yml", ".txt", ".json", ".kts", ".pro", ".tsv"):
                s = str(p).replace("\\", "/")
                if "/target/" in s or "/third_party/" in s or "/build/" in s or "/.git/" in s:
                    continue
                INDEX.setdefault(p.name, []).append(p)


def find(path):
    for root in ROOTS:
        p = root / path
        if p.is_file():
            return p
    name = path.split("/")[-1]
    hits = INDEX.get(name, [])
    want = path.replace("\\", "/")
    for h in hits:
        if str(h).replace("\\", "/").endswith(want):
            return h
    return hits[0] if len(hits) == 1 else None


def main():
    text = pathlib.Path(sys.argv[1]).read_text(encoding="utf8", errors="replace").splitlines()
    prefix, first, last = sys.argv[2], int(sys.argv[3]), int(sys.argv[4])
    index()
    cite = re.compile(r"([A-Za-z0-9_./\-]+\.(?:cpp|hpp|h|mm|rs|py|java|kt|lum|md|c|inl|sh|ps1|yml|kts|pro|tsv)):(\d+)(?:-(\d+))?")
    for n in range(first, last + 1):
        tag = "%s-%03d" % (prefix, n)
        start = None
        for i, line in enumerate(text):
            if re.match(r"^(#+ |- |\*\*|\d+\. \*\*)?\**%s\b" % tag, line.strip()) or line.strip().startswith("- " + tag) \
                    or line.strip().startswith("### " + tag):
                start = i
                break
        if start is None:
            for i, line in enumerate(text):
                if tag in line:
                    start = i
                    break
        if start is None:
            print("==== %s: not found" % tag)
            continue
        block = text[start:start + 14]
        print("==== %s: %s" % (tag, text[start].strip()[:330]))
        seen = 0
        for line in block:
            for m in cite.finditer(line):
                if seen >= 1:
                    break
                p = find(m.group(1))
                a = int(m.group(2))
                b = int(m.group(3) or a)
                if p is None:
                    print("   [%s:%d not located]" % (m.group(1), a))
                    seen += 1
                    continue
                src = p.read_text(encoding="utf8", errors="replace").splitlines()
                lo, hi = max(1, a - 1), min(len(src), min(b, a + 2) + 1)
                print("   -- %s:%d-%d" % (str(p).replace("\\", "/").split("Nebula-Clean/")[-1].split("lumalink-sweep/")[-1], lo, hi))
                for k in range(lo, hi + 1):
                    print("   %5d  %s" % (k, src[k - 1][:150]))
                seen += 1


main()
