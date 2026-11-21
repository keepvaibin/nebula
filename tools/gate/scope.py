"""scope.py: which checks a change needs (package T4), without the game.

A landing runs the checks of the changed files, not all of them. tools/gate/scope.tsv maps path patterns to groups of
checks; the groups are named here, once, with the check names each one stands for. Every row a changed file matches adds
its groups: the run is the union over all changed files. A file that no row names makes the run `full`: everything the
main gate runs today on the native game.

    python -X utf8 -I tools/gate/scope.py [--table scope.tsv] [--base REF] [PATH ...]   print the groups and checks

The table's first lines may hold `mods = off | short | full` (a line of its own, `off` now): the mod suites and LumaLink's
own check run in no group and in no landing or nightly run while it says `off`; `short` and `full` add them back (the quick
mod set, or every mod suite).
"""
import fnmatch
import pathlib
import re
import subprocess
import sys

HERE = pathlib.Path(__file__).resolve().parent
TABLE = HERE / "scope.tsv"
MODS_VALUES = ("off", "short", "full")

# The groups, in the order a plan lists them: name, what it is, the shell patterns of the check names it stands for.
GROUPS = {
    "repo": ("the repository checks",
             ["repo/*"]),
    "tools": ("the self-tests of the test tools and of the game's description tools",
              ["harness/self", "description/self"]),
    "build": ("the Windows build",
              ["build/windows"]),
    "unit": ("the unit tests of the engine and of the game",
             ["unit/windows", "unit/game"]),
    "start": ("a start of each of the three renderers",
              ["start/*"]),
    "engine1": ("the engine tests on Direct3D 12, with their pictures",
                ["engine/d3d12", "pictures/engine/d3d12"]),
    "engine3": ("the engine tests on the three renderers, with their pictures",
                ["engine/*", "pictures/engine/*"]),
    "tour1": ("the 12-stop tour on Direct3D 12, with its pictures and its replays",
              ["tour/d3d12", "pictures/tour/d3d12", "replays/d3d12", "pictures/declared"]),
    "tour3": ("the 12-stop tour on the three renderers, with pictures and replays (the other two renderers' replays "
              "compared with Direct3D 12's)",
              ["tour/d3d12", "tour/vulkan", "tour/d3d11", "pictures/tour/*", "replays/d3d12", "replays/vulkan",
               "replays/d3d11", "pictures/declared"]),
    "import": ("the importer's tests, the collision proof on the US disc, the US game data of the tree's importer (found "
               "by its key, else made again), and the 12-stop tour on Direct3D 12 with that data",
               ["rust", "import/collision/RMGE01", "tour/d3d12", "pictures/tour/d3d12", "replays/d3d12",
                "pictures/declared"]),
    "android": ("the Android build and its unit tests",
                ["build/android"]),
    "tablet12": ("the tablet's 12 stops with pictures and replays (owed when the tablet does not answer)",
                 ["tablet/prepare", "android/tablet", "tour/tablet", "tablet/remove", "tour/tablet-pc", "replays/tablet",
                  "pictures/tablet"]),
}
GROUP_NAMES = tuple(GROUPS)
# A pseudo-group a row may name: it makes the run full, as a file no row names does.
FULL = "full"
# Checks that are taken with a group whose pictures they read; the checks they name as needs are not pulled in by them.
SOFT = ("pictures/declared",)


class ScopeError(Exception):
    """The table cannot be read: each problem is one line."""

    def __init__(self, problems):
        Exception.__init__(self, "\n".join(problems))
        self.problems = problems


class Table:
    """The rows (pattern, groups, line number) and the mods switch of a scope table."""

    def __init__(self, rows, mods="off"):
        self.rows, self.mods = list(rows), mods


class Plan:
    """What a set of changed files runs: `groups` (in the order of GROUP_NAMES), `full`, the files no row named, and
    why[path] the groups each file brought."""

    def __init__(self, groups, full, unmatched, why):
        self.groups, self.full, self.unmatched, self.why = list(groups), full, list(unmatched), why

    def text(self):
        if self.full:
            return "full" + (" (no row names: %s)" % ", ".join(self.unmatched[:5]) if self.unmatched else "")
        return ",".join(self.groups) if self.groups else "none"


# The table ---------------------------------------------------------------------------------------------------------


def parse(text, name="scope.tsv"):
    """The Table of the table's text. A comment starts with '#'; `mods = off|short|full` is a line; the first other line is
    the header `pattern<TAB>groups`; each row after it is a pattern (shell: `**` any folders, `*` within a name; with no
    `/` it names a file anywhere) and its comma-separated groups."""
    problems, rows, mods, header = [], [], None, False
    for n, line in enumerate(text.splitlines(), 1):
        if not line.strip() or line.lstrip().startswith("#"):
            continue
        m = re.match(r"^mods\s*=\s*(\S+)\s*$", line)
        if m:
            if m.group(1) not in MODS_VALUES:
                problems.append("%s:%d: mods is one of %s, not %r" % (name, n, ", ".join(MODS_VALUES), m.group(1)))
            mods = m.group(1)
            continue
        cells = [c.strip() for c in line.split("\t")]
        if not header:
            if cells != ["pattern", "groups"]:
                problems.append("%s:%d: the first line after the mods line is the header: pattern<TAB>groups" % (name, n))
            header = True
            continue
        if len(cells) != 2 or not all(cells):
            problems.append("%s:%d: a row is a pattern and its groups, with a tab between" % (name, n))
            continue
        groups = [g.strip() for g in cells[1].split(",") if g.strip()]
        for g in groups:
            if g not in GROUPS and g != FULL:
                problems.append("%s:%d: %r is not a group (%s)" % (name, n, g, ", ".join(GROUP_NAMES)))
        rows.append((cells[0], tuple(groups), n))
    if mods is None:
        problems.append("%s: no `mods = off` line" % name)
    if not rows and not problems:
        problems.append("%s: no row" % name)
    if problems:
        raise ScopeError(problems)
    return Table(rows, mods)


def read_table(path=TABLE):
    path = pathlib.Path(path)
    try:
        text = path.read_text(encoding="utf-8")
    except OSError as error:
        raise ScopeError(["%s: cannot be read: %s" % (path, error)])
    return parse(text, path.name)


# Matching ------------------------------------------------------------------------------------------------------------


def glob_regex(pattern):
    out, i = [], 0
    while i < len(pattern):
        if pattern.startswith("**", i):
            out.append(".*")
            i += 2
        elif pattern[i] == "*":
            out.append("[^/]*")
            i += 1
        elif pattern[i] == "?":
            out.append("[^/]")
            i += 1
        else:
            out.append(re.escape(pattern[i]))
            i += 1
    return re.compile("^" + "".join(out) + "$")


def path_matches(path, pattern):
    """Whether `path` (with forward slashes) matches the shell pattern: a pattern with no folder (`*.md`) matches the name
    of a file anywhere."""
    pattern = pattern.strip()
    if not pattern:
        return False
    if "/" not in pattern:
        return fnmatch.fnmatchcase(path.rsplit("/", 1)[-1], pattern)
    return bool(glob_regex(pattern).match(path))


def plan_for(paths, table):
    """The Plan for the changed paths: the union of the groups of every row each path matches; a path no row names makes the
    run full. No path at all is full too (nothing says what changed)."""
    paths = [p.replace("\\", "/") for p in paths if p and p.strip()]
    wanted, why, unmatched, full = set(), {}, [], not paths
    for path in paths:
        brought = []
        for pattern, groups, _ in table.rows:
            if path_matches(path, pattern):
                brought += [g for g in groups if g not in brought]
        if not brought:
            unmatched.append(path)
            full = True
        if FULL in brought:
            full = True
        wanted.update(g for g in brought if g != FULL)
        why[path] = brought
    ordered = [g for g in GROUP_NAMES if g in wanted]
    return Plan(ordered, full, unmatched, why)


def groups_from_text(text):
    """The groups of a plan's text ('repo,build', 'full', 'none'): (groups, full). Refuses a name that is none."""
    text = (text or "").strip()
    if text.startswith("full"):
        return list(GROUP_NAMES), True
    names = [g.strip() for g in text.split(",") if g.strip() and g.strip() != "none"]
    bad = [g for g in names if g not in GROUPS]
    if bad:
        raise ScopeError(["not a group: %s (%s)" % (", ".join(bad), ", ".join(GROUP_NAMES))])
    return [g for g in GROUP_NAMES if g in names], False


# The checks ---------------------------------------------------------------------------------------------------------


def check_patterns(groups):
    """The shell patterns of the check names the groups stand for, in the groups' order, each once."""
    out = []
    for g in groups:
        for p in GROUPS[g][1]:
            if p not in out:
                out.append(p)
    return out


def select(needs, groups):
    """The checks a landing of these groups runs, from a dry run's {check: needs}: those a group's pattern names, and what
    each needs, transitively (the build for a tour, the tour for its pictures). A soft check (SOFT) brings its own name only."""
    patterns = check_patterns(groups)
    chosen = {n for n in needs if any(fnmatch.fnmatchcase(n, p) for p in patterns)}
    grow = [n for n in chosen if n not in SOFT]
    while grow:
        for n in needs.get(grow.pop(), ()):
            if n in needs and n not in chosen:
                chosen.add(n)
                if n not in SOFT:
                    grow.append(n)
    return chosen


def describe(plan):
    """Lines for a person: each group of the plan with what it runs."""
    if plan.full:
        return ["full: everything the main gate runs today on the native game" +
                ((" (no row of tools/gate/scope.tsv names: %s)" % ", ".join(plan.unmatched[:5])) if plan.unmatched else "")]
    return ["%s: %s" % (g, GROUPS[g][0]) for g in plan.groups] or ["none: nothing in the change needs a check"]


# Git -------------------------------------------------------------------------------------------------------------------


def changed_paths(git, tree, base_ref, head="HEAD"):
    """The files changed from the merge base of `base_ref` and `head` to `head`: (paths, None), or ([], why not)."""
    base = subprocess.run([git, "-C", str(tree), "merge-base", base_ref, head], capture_output=True, text=True)
    if base.returncode != 0:
        return [], "no merge base of %s and %s: %s" % (base_ref, head, (base.stderr or base.stdout).strip()[-200:])
    diff = subprocess.run([git, "-C", str(tree), "diff", "--name-only", "--no-renames", base.stdout.strip(), head],
                          capture_output=True, text=True)
    if diff.returncode != 0:
        return [], "git diff failed: %s" % (diff.stderr or diff.stdout).strip()[-200:]
    return [l for l in diff.stdout.splitlines() if l.strip()], None


def main(argv=None):
    import argparse
    p = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    p.add_argument("--table", default=str(TABLE))
    p.add_argument("--base", default="", help="a ref: the changed files are those from its merge base with HEAD to HEAD")
    p.add_argument("--git", default="C:/Program Files/Git/cmd/git.exe")
    p.add_argument("--tree", default=str(HERE.parent.parent))
    p.add_argument("paths", nargs="*")
    a = p.parse_args(argv)
    try:
        table = read_table(a.table)
    except ScopeError as error:
        print("scope.py: the table is wrong:\n  " + "\n  ".join(error.problems), file=sys.stderr)
        return 2
    paths = list(a.paths)
    if a.base:
        found, why = changed_paths(a.git, a.tree, a.base)
        if why:
            print("scope.py: " + why, file=sys.stderr)
            return 2
        paths += found
    plan = plan_for(paths, table)
    print("mods = %s" % table.mods)
    for line in describe(plan):
        print(line)
    if not plan.full:
        print("checks: " + ", ".join(check_patterns(plan.groups)))
    return 0


if __name__ == "__main__":
    sys.exit(main())
