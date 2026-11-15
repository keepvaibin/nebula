"""nerves_list.py <Nebula tree> [<folder>]: docs/modding/reference/nerves.md and its pages by letter (reference_pages.py)

Every nerve of the game, from its source (NEW_NERVE and NEW_NERVE_ONEND in native/petari): its name, the class whose
code runs in it, and that code's functions by their full names, the names a mod's code hooks (luma.h), by class.
"""
import os
import pathlib
import re
import sys
from collections import defaultdict

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import reference_pages  # noqa: E402

NERVE = re.compile(r"^\s*NEW_NERVE(_ONEND)?\(\s*(\w+)\s*,\s*([\w:<>]+)\s*,\s*(\w+)\s*(?:,\s*(\w+)\s*)?\)", re.M)


def main():
    tree = pathlib.Path(sys.argv[1])
    out = pathlib.Path(sys.argv[2]) if len(sys.argv) > 2 else tree / "docs/modding/reference"
    root = tree / "native/petari"
    seen = set()
    by_class = defaultdict(list)
    files = defaultdict(set)
    for path in sorted(list((root / "src").rglob("*.cpp")) + list((root / "include").rglob("*.hpp")) +
                       list((root / "include").rglob("*.h"))):
        text = path.read_text(encoding="utf-8", errors="replace")
        if "NEW_NERVE" not in text:
            continue
        for m in NERVE.finditer(text):
            onend, name, cls, exe, end = m.group(1), m.group(2), m.group(3), m.group(4), m.group(5)
            if name == "name":  # the macros themselves
                continue
            if (cls, name) in seen:  # a header's nerve, counted once
                continue
            seen.add((cls, name))
            by_class[cls].append((name, f"{cls}::exe{exe}", f"{cls}::exe{end}" if onend else ""))
            files[cls].add(path.relative_to(root).as_posix())
    count = sum(len(items) for items in by_class.values())
    lines = [
        "The game's objects, screens and the states objects hand their work to run one nerve at a time: Kuribo walks in",
        "`KuriboNrvWalk`, and that nerve's code (`Kuribo::exeWalk`) runs every frame until another nerve is set.",
        "",
        "A mod hooks a nerve's code by the name in the Code column, as any function of the game. In LumaScript:",
        "`hook before Kuribo::exeWalk() { ... }` runs first and may skip the game's frame of it, `hook after` runs last,",
        "`hook wrap` runs around it (see [Hooks](lumascript/hooks.md)). In C the same kinds are `LUMA_BEFORE`,",
        "`LUMA_AFTER` and `LUMA_WRAP` (luma.h).",
        "The On leaving column is the code that runs as an object leaves the nerve. Nerves of a state (a class that does",
        "an object's work for a while, such as `WalkerStateWander`) name the state's class.",
        "",
    ]
    sections = []
    for cls in sorted(by_class, key=str.lower):
        where = ", ".join(sorted(files[cls]))
        section = [f"## {cls}", "", f"{where}", "", "| Nerve | Code | On leaving |", "|---|---|---|"]
        for name, exe, end in by_class[cls]:
            section.append(f"| `{name}` | `{exe}` | {f'`{end}`' if end else ''} |")
        sections.append("\n".join(section) + "\n\n")
    reference_pages.write_nerves("\n".join(lines), sections, out)
    print(f"{count} nerves, {len(by_class)} classes -> {out}")


if __name__ == "__main__":
    main()
