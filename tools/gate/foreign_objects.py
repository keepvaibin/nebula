"""foreign_objects.py: objects of a build folder that another source tree made (R-004, 11-23).

    python -X utf8 -I tools/gate/foreign_objects.py list FOLDER [--tree TREE] [--ninja NINJA]
    python -X utf8 -I tools/gate/foreign_objects.py drop FOLDER [--tree TREE] [--ninja NINJA]

A ninja build folder keeps, for each object, the files its compile read (`ninja -t deps`). An object whose compile read the files of
ANOTHER work tree (Nebula-A1h under D:/NebulaWork/MarioGalaxyRecomp, say) is never built again when this tree's files change: ninja
looks at the other tree's files. The coverage folder of the long run held 338 such objects on 11-23, among them the one that holds the
store version, and its game could not open the data of store 40 (docs/plan/native/repairs R-004).

`list` prints one line, `FOLDER: tree <the folder's own tree>; <n> of <m> objects read another tree's files (<those trees>)`, and exits 1
when n is not 0, 0 when it is 0. The folder's own tree is --tree, else the one its CMakeCache.txt names (CMAKE_HOME_DIRECTORY). A file
outside every work tree (the SDK, third_party's downloads, the folder itself) is nobody else's. `drop` deletes those objects' files, so
that the next build makes them again from this tree, and says how many; it never touches another object, so a sound folder builds as
before. Nothing here starts a build; a folder with no .ninja_deps, or no ninja, gives n = 0 and no change.
"""
import argparse
import os
import pathlib
import re
import shutil
import subprocess
import sys

WORK_ROOT_NAME = "MarioGalaxyRecomp"     # D:/NebulaWork/MarioGalaxyRecomp/<tree>, C:/Users/<name>/Documents/MarioGalaxyRecomp/<tree>
VS_NINJA = "C:/Program Files/Microsoft Visual Studio/2022/Community/Common7/IDE/CommonExtensions/Microsoft/CMake/Ninja/ninja.exe"


def tree_of(path):
    """The work tree a path is in (the folder under MarioGalaxyRecomp, as a normalised lower-case path), or None when it is in none."""
    parts = os.path.normpath(str(path)).replace("\\", "/").split("/")
    for i, part in enumerate(parts[:-1]):
        if part.lower() == WORK_ROOT_NAME.lower() and i + 1 < len(parts):
            return "/".join(parts[:i + 2]).lower()
    return None


def own_tree(folder, tree=None):
    """The folder's tree: `tree` when given, else CMAKE_HOME_DIRECTORY of its CMakeCache.txt; None when neither."""
    if tree:
        return tree_of(tree) or os.path.normpath(str(tree)).replace("\\", "/").lower()
    cache = pathlib.Path(folder) / "CMakeCache.txt"
    if cache.is_file():
        for line in cache.read_text(encoding="utf-8", errors="replace").splitlines():
            if line.startswith("CMAKE_HOME_DIRECTORY:"):
                return tree_of(line.split("=", 1)[1].strip())
    return None


def parse_deps(text):
    """{object: [dep, ...]} from the text of `ninja -t deps`: a line that does not start with a space names an object (`obj: #deps N, ...`),
    the indented lines under it are what its compile read."""
    out, current = {}, None
    for line in text.splitlines():
        if not line.strip():
            continue
        if not line[0].isspace():
            current = line.split(": #deps", 1)[0] if ": #deps" in line else None
            if current is not None:
                out[current] = []
        elif current is not None:
            out[current].append(line.strip())
    return out


def foreign(deps, folder, tree):
    """{object: {other tree, ...}} of the objects one of whose deps is in a work tree other than `tree` (dep paths are as ninja prints
    them: relative to the folder, or absolute)."""
    found = {}
    inside = os.path.normcase(os.path.abspath(str(folder))) + os.sep
    for obj, files in deps.items():
        others = set()
        for dep in files:
            absolute = os.path.normpath(dep if os.path.isabs(dep) else os.path.join(os.path.abspath(str(folder)), dep))
            if os.path.normcase(absolute).startswith(inside):
                continue            # the folder's own files (a link step's objects are named after the sources' paths: D_/NebulaWork/...)
            owner = tree_of(absolute)
            if owner is not None and owner != tree:
                others.add(owner.rsplit("/", 1)[-1])
        if others:
            found[obj] = others
    return found


def find_ninja(given=None):
    for candidate in (given, shutil.which("ninja"), VS_NINJA):
        if candidate and pathlib.Path(candidate).is_file():
            return str(candidate)
    return None


class Doubt(Exception):
    """The detector cannot answer (no ninja, ninja failed, the answer cannot be read): nothing is judged and nothing is deleted."""


def deps_text(folder, ninja=None, run=subprocess.run):
    """`ninja -C FOLDER -t deps` (it reads the folder's logs and changes nothing); "" when the folder has no .ninja_deps (nothing built
    there yet: nothing to ask). Raises Doubt when it has one and the answer cannot be had."""
    folder = pathlib.Path(folder)
    if not (folder / ".ninja_deps").is_file():
        return ""
    program = find_ninja(ninja)
    if program is None:
        raise Doubt("no ninja program found")
    try:
        done = run([program, "-C", str(folder), "-t", "deps"], capture_output=True, text=True, encoding="utf-8", errors="replace")
    except (OSError, subprocess.SubprocessError) as error:
        raise Doubt("ninja could not be run: %s" % " ".join(str(error).split())[:100])
    if done.returncode != 0:
        raise Doubt("ninja -t deps ended with %s" % done.returncode)
    return done.stdout


def check(folder, tree=None, ninja=None, run=subprocess.run):
    """(own tree, number of objects, {object: other trees}) of a folder."""
    owner = own_tree(folder, tree)
    deps = parse_deps(deps_text(folder, ninja, run))
    if owner is None:
        return None, len(deps), {}
    return owner, len(deps), foreign(deps, folder, owner)


def describe(folder, owner, total, bad):
    trees = sorted({t for v in bad.values() for t in v})
    return "%s: tree %s; %d of %d objects read another tree's files%s" % (
        folder, owner.rsplit("/", 1)[-1] if owner else "unknown", len(bad), total, " (%s)" % ", ".join(trees) if trees else "")


def drop(folder, bad):
    """Deletes the objects' files; returns how many were there."""
    gone = 0
    for obj in bad:
        path = pathlib.Path(folder) / obj
        try:
            if path.is_file():
                path.unlink()
                gone += 1
        except OSError:
            pass            # a file that cannot be deleted stays; the build then finds it as it found it before
    return gone


def main(argv=None):
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("command", choices=("list", "drop"))
    p.add_argument("folder")
    p.add_argument("--tree", default="")
    p.add_argument("--ninja", default="")
    a = p.parse_args(argv)
    try:
        owner, total, bad = check(a.folder, a.tree or None, a.ninja or None)
    except Exception as error:  # noqa: BLE001 - a detector that cannot answer leaves the build as it is: one line, nothing deleted
        print("foreign_objects: %s: not checked (%s: %s); nothing deleted, the build goes on" % (
            a.folder, type(error).__name__, " ".join(str(error).split())[:120]))
        return 0 if a.command == "drop" else 2
    print(describe(a.folder, owner, total, bad))
    if a.command == "drop" and bad:
        print("foreign_objects: %d object file(s) deleted; the next build makes them again from this tree" % drop(a.folder, bad))
        return 0
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
