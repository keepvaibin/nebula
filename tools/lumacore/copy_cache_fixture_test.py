"""copy_cache_fixture_test.py --commands COMPILE_COMMANDS [--out DIR]

instrument.py's cache of the copies (NEBULA_LUMA_CACHE, P-035), on a copy of the places fixture
(tools/lumacore/tests/places_fixture) with a game unit's flags (Game/System/LumaLoader.cpp's command):
  1. with no cache named, no cache folder is written;
  2. the first run with the cache parses (miss), the second takes the outputs (hit); both write the bytes of a run with
     no cache, the depfile included;
  3. a header the unit includes changed, or the source changed: the next run parses again and writes what a run with
     no cache writes, and the one after it takes that;
  4. an entry that does not read (half written) is a miss, and the outputs are right;
  5. --prune-cache removes what no run used in DAYS days and keeps what a run used.
Exit 0 only when all hold.
"""
import json
import os
import pathlib
import shutil
import subprocess
import sys
import time

HERE = pathlib.Path(__file__).resolve().parent
FIXTURE = HERE / "tests" / "places_fixture"
UNIT = "places_fixture.cpp"


def option(flag, default=None):
    return sys.argv[sys.argv.index(flag) + 1] if flag in sys.argv else default


def main():
    commands = option("--commands") or sys.exit(__doc__)
    out = pathlib.Path(option("--out", str(HERE.parents[1] / "build" / "copy-cache-fixture"))).resolve()
    if out.exists():
        shutil.rmtree(out)
    tree, defined, cache, log = out / "tree", out / "defined", out / "cache", out / "log.txt"
    shutil.copytree(FIXTURE, tree)
    copy = defined / UNIT
    entries = json.loads(pathlib.Path(commands).read_text(encoding="utf-8"))
    base = next(e for e in entries if e["file"].replace("\\", "/").endswith("Game/System/LumaLoader.cpp"))
    (out / "commands.json").write_text(json.dumps([dict(base, file=str(copy))]), encoding="utf-8")
    failed = []

    def run(cached):
        if defined.exists():
            shutil.rmtree(defined)
        defined.mkdir(parents=True)
        env = {k: v for k, v in os.environ.items() if not k.startswith("NEBULA_LUMA_CACHE")}
        if cached:
            env.update(NEBULA_LUMA_CACHE=str(cache), NEBULA_LUMA_CACHE_LOG=str(log))
        before = log.read_text(encoding="utf-8").splitlines() if log.exists() else []
        r = subprocess.run([sys.executable, "-X", "utf8", "-B", str(HERE / "instrument.py"), str(tree / UNIT), str(copy),
                            "--commands", str(out / "commands.json"), "--root", str(tree), "--depfile", str(copy) + ".d"],
                           capture_output=True, text=True, encoding="utf-8", errors="replace", env=env)
        if r.returncode != 0:
            sys.exit("copy_cache_fixture_test.py: instrument.py failed:\n" + (r.stdout + r.stderr)[-3000:])
        after = log.read_text(encoding="utf-8").splitlines() if log.exists() else []
        said = [line.split(" ", 1)[0] for line in after[len(before):]]
        return {p.name: p.read_bytes() for p in sorted(defined.iterdir())}, said

    def expect(step, cached_said, want):
        got, said = run(True)
        if said != [cached_said]:
            failed.append("%s: the cache said %s, not %s" % (step, said, [cached_said]))
        if got != want:
            differ = sorted(n for n in set(got) | set(want) if got.get(n) != want.get(n))
            failed.append("%s: outputs differ from a run with no cache: %s" % (step, differ))

    # 1. no cache named
    want, said = run(False)
    if cache.exists() or said:
        failed.append("1: a run with no cache named wrote a cache (%s)" % said)
    if not any(n.endswith(".d") for n in want) or len(want) < 3:
        failed.append("1: the run wrote %s, not the copy, its parts and the depfile" % sorted(want))
    # 2. miss, then hit
    expect("2 first", "miss", want)
    expect("2 second", "hit", want)
    # 3. a header changed; then the source
    header = tree / "places_fixture.hpp"
    header.write_bytes(header.read_bytes().replace(b"    void initArea(", b"    virtual void initArea("))
    want_header, _ = run(False)
    if want_header == want:
        failed.append("3: the header's change changed no output (the test proves nothing)")
    expect("3 header changed", "miss", want_header)
    expect("3 header again", "hit", want_header)
    source = tree / UNIT
    source.write_bytes(source.read_bytes() + b"\nint pfCacheProbe() { return 1; }\n")
    want_source, _ = run(False)
    expect("3 source changed", "miss", want_source)
    expect("3 source again", "hit", want_source)
    # 4. an entry that does not read
    kept = list(cache.glob("m/*/*.json"))
    if not kept:
        failed.append("4: the cache has no entry")
    for entry in kept:
        entry.write_text('{"read": [', encoding="utf-8")
    expect("4 broken entry", "miss", want_source)
    expect("4 after", "hit", want_source)
    # 5. prune
    r = subprocess.run([sys.executable, "-X", "utf8", "-B", str(HERE / "instrument.py"), "--prune-cache", str(cache), "14"])
    if r.returncode != 0:
        failed.append("5: --prune-cache exit %d" % r.returncode)
    expect("5 kept after a prune", "hit", want_source)
    old = time.time() - 30 * 86400
    for path in cache.rglob("*"):
        os.utime(path, (old, old))
    subprocess.run([sys.executable, "-X", "utf8", "-B", str(HERE / "instrument.py"), "--prune-cache", str(cache), "14"])
    left = [str(p.relative_to(cache)) for p in cache.rglob("*") if p.is_file()]
    if left:
        failed.append("5: a prune of entries 30 days old left %s" % left[:5])
    expect("5 after the prune", "miss", want_source)

    for line in failed:
        print("FAIL", line)
    print("copy cache: %d failed" % len(failed))
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
