"""Every mod function a service provider runs goes to the guard as the mod's own: no function of the provider's
between the guard and the mod's (M-028). The guard resumes a fault only when no game frame lies between its boundary
and the mod's code; a provider's trampoline is one unless the compiler happens to make its call a tail jump.

The check: the function a provider passes to its kernel's `call` (the event function) is never one defined in the game
module's or the runtime's own sources. Exits 1 and names each place otherwise.

    python -I tools/lumacore/guard_frames_fixture_test.py [--tree DIR]

It runs in the gate's description/self check with the fixture tests (their --commands and --out are taken and not
needed).
"""
import argparse
import pathlib
import re
import sys

CALL = re.compile(r"->call\(\s*[^,]+,\s*[^,]+,\s*([A-Za-z_][A-Za-z_0-9:]*)\s*,")
DEFINED = r"(?:^|\n)[ \t]*(?:static[ \t]+)?(?:void|int32_t|s32|u32)[ \t]+%s[ \t]*\("


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--tree", default=str(pathlib.Path(__file__).resolve().parents[2]))
    parser.add_argument("--commands")
    parser.add_argument("--out")
    tree = pathlib.Path(parser.parse_args().tree)
    roots = [tree / "native" / "petari" / "src" / "Game" / "System", tree / "runtime" / "src"]
    found = []
    calls = 0
    for root in roots:
        for path in sorted(root.rglob("*.c*")):
            text = path.read_text(encoding="utf-8", errors="replace")
            for match in CALL.finditer(text):
                calls += 1
                name = match.group(1).split("::")[-1]
                if re.search(DEFINED % re.escape(name), text):
                    line = text.count("\n", 0, match.start()) + 1
                    found.append("%s:%d: %s is the provider's own function" % (path.relative_to(tree).as_posix(), line, name))
    for line in found:
        print(line)
    print("guard_frames: %d calls, %d through a provider's function" % (calls, len(found)))
    return 1 if found or calls == 0 else 0


if __name__ == "__main__":
    sys.exit(main())
