"""scope.py: which checks a change needs (package T4), without the game.

A landing runs the checks of the changed files, not all of them. tools/gate/scope.tsv maps path patterns to groups of
checks; the groups are named here, once, with the check names each one stands for. Every row a changed file matches adds
its groups: the run is the union over all changed files, and always the group `repo` (the cheap repository checks), whatever
the table says. A file that no row names makes the run `full`: everything the
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
    "pictures": ("every picture the main level compares: the engine tests and every stop of the tour on the three renderers, the "
                 "two other discs' tours on Direct3D 12, the tablet's 12 stops (the groups engine3, tour3 and tablet12 come with it), "
                 "and the pictures/declared check; the landing is of the main level, with these checks only. Every stop its "
                 "PICTURES-CHANGE.txt names is also played on the other discs' Direct3D 11 and Vulkan, which the long run compares",
                 ["pictures/*", "engine/*", "tour/*", "replays/d3d12", "replays/vulkan", "replays/d3d11",
                  "pictures/declared"]),
    "validate": ("the 12-stop tour on the three renderers with the graphics API's debug layer on (a layer that is not "
                 "installed on this PC is left out with one line)",
                 ["validate/*"]),
    "import": ("the importer's tests, the collision proof on the US disc, the US game data of the tree's importer (found "
               "by its key, else made again), and the 12-stop tour on Direct3D 12 with that data",
               ["rust", "import/collision/RMGE01", "data/RMGE01", "tour/d3d12", "pictures/tour/d3d12", "replays/d3d12",
                "pictures/declared"]),
    "android": ("the Android build and its unit tests",
                ["build/android"]),
    "tablet12": ("the tablet's 12 stops with pictures and replays (owed when the tablet does not answer)",
                 ["tablet/prepare", "android/tablet", "tour/tablet", "tablet/remove", "tour/tablet-pc", "replays/tablet",
                  "pictures/tablet"]),
    # No local check: they switch GitHub's part of a landing on (package T8a, tools/gate/github_rule.py). A landing whose
    # plan has either pushes the candidate branch (GitHub runs its jobs, tools/gate/github_jobs.tsv). With `github` alone
    # main moves when this machine's checks pass and the jobs are read after the move (a red one opens a repair); with
    # `github-first` too (or a full run) the move waits for every `must` job. One without both pushes nothing, starts no
    # watcher and reads nothing.
    "github": ("GitHub's jobs on the candidate, read after the move (a red one opens a repair)",
               []),
    "github-first": ("GitHub's jobs on the candidate, waited for before the move: files this machine cannot build",
                     []),
    # Not rows of the table: plan_for adds them by measure_groups (a landing that plays the game is measured).
    "measure_pc": ("the speed measure of the play route on Direct3D 12, last, with the machine alone (a figure slower than its "
                   "band twice in a row stops the landing)",
                   ["measure/step/pc"]),
    "measure_tablet": ("the speed measure of the play route on the tablet, in the Android lane after its build (owed when the "
                       "tablet does not answer)",
                       ["measure/step/tablet"]),
    # Not a row of the table either: the step tool asks for it (`gate.py land --section`) at the last step of a package of the
    # native plan, or at the fourth step landed since the last one. No check of its own: it brings the main level's whole set.
    "section": ("the section's full check: every picture the main level compares, the debug layers, the Android build and both "
                "speed measures, whatever the changed files are",
                []),
}
GROUP_NAMES = tuple(GROUPS)
# The speed measure's groups (package B8, tools/gate/gate.py measure/step/pc and measure/step/tablet): a plan that holds an engine or a
# tour group gets measure_pc; one that holds the Android build besides gets measure_tablet. No row of the table names them.
GAME_GROUPS = ("engine1", "engine3", "tour1", "tour3")
# A group that brings others with it: `pictures` is the main level's picture checks, whose tablet part is the group tablet12.
BRINGS = {"pictures": ("engine3", "tour3", "tablet12"),
          "section": ("pictures", "engine3", "tour3", "tablet12", "validate", "android", "measure_pc", "measure_tablet")}
# The section's full check (mend M-006, docs/plan/04-GATE-AND-MAIN.md "Two tiers"): a landing without it is a step's, which plays
# the 12 quick stops on the three renderers and on the tablet and leaves these groups to the section's check.
SECTION = "section"
STEP_LEAVES = ("validate", "measure_pc", "measure_tablet")
# The group of a change that can move a picture of the game: the landing takes every picture the main level compares.
PICTURES = "pictures"
# A pseudo-group a row may name: it makes the run full, as a file no row names does.
FULL = "full"
# A row may also name `-full`: for the files that row matches, a `full` that another row brought is taken away (a narrow row for
# one file of a folder whose other rows make the run full).
NOT_FULL = "-full"
# The groups that switch GitHub's part on, and the one that makes the move wait for it.
GITHUB, GITHUB_FIRST = "github", "github-first"
# The groups every run has, whatever the table says: the cheap repository checks (the audit, the code checks, the scripts).
ALWAYS = ("repo",)
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

    @property
    def takes_pictures(self):
        """Whether the landing of this plan compares pictures of the game (the group `pictures`, or a full run): every picture the
        main level compares when it is also the section's check (`is_section`), else the 12 quick stops on each place."""
        return self.full or PICTURES in self.groups

    @property
    def is_section(self):
        """Whether the landing of this plan is a section's full check (the group `section`, or a full run)."""
        return self.full or SECTION in self.groups

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
            if g not in GROUPS and g not in (FULL, NOT_FULL):
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
    if pattern.startswith("/"):      # anchored at the top folder: `/CMakeLists.txt` is the root's file only
        return bool(glob_regex(pattern[1:]).match(path))
    if "/" not in pattern:
        return fnmatch.fnmatchcase(path.rsplit("/", 1)[-1], pattern)
    return bool(glob_regex(pattern).match(path))


def expand(groups):
    """The groups with those each one brings (BRINGS), as a set."""
    out = set(groups)
    for name in list(out):
        out.update(BRINGS.get(name, ()))
    return out


def measure_groups(groups):
    """The speed measure's groups a plan of `groups` gets: measure_pc when it holds an engine or a tour group, and measure_tablet
    too when it holds the Android build as well. None otherwise."""
    if not any(g in groups for g in GAME_GROUPS):
        return []
    return ["measure_pc"] + (["measure_tablet"] if "android" in groups else [])


def plan_for(paths, table):
    """The Plan for the changed paths: the union of the groups of every row each path matches, and ALWAYS (the repository
    checks run in every landing, whatever a row says); a path no row names makes the run full. No path at all is full too
    (nothing says what changed)."""
    paths = [p.replace("\\", "/") for p in paths if p and p.strip()]
    wanted, why, unmatched, full = set(ALWAYS), {}, [], not paths
    for path in paths:
        brought = []
        for pattern, groups, _ in table.rows:
            if path_matches(path, pattern):
                brought += [g for g in groups if g not in brought]
        if not brought:
            unmatched.append(path)
            full = True
        if FULL in brought and NOT_FULL not in brought:
            full = True
        wanted.update(g for g in brought if g not in (FULL, NOT_FULL))
        why[path] = brought
    wanted = expand(wanted)
    wanted.update(measure_groups(wanted))
    ordered = [g for g in GROUP_NAMES if g in wanted]
    return Plan(ordered, full, unmatched, why)


def with_group(plan, name):
    """The plan with the group `name` added (a full plan has every group already)."""
    if plan.full or name in plan.groups:
        return plan
    return Plan([g for g in GROUP_NAMES if g in expand(set(plan.groups) | {name})], False, plan.unmatched, plan.why)


def step_plan(plan):
    """The plan of a step's landing (not a section's check): the plan without the debug layers and the speed measures (STEP_LEAVES),
    which the section's check runs. A full plan stays full; a section's plan stays as it is."""
    if plan.is_section:
        return plan
    return Plan([g for g in plan.groups if g not in STEP_LEAVES], False, plan.unmatched, plan.why)


def section_plan(plan):
    """The plan of a section's full check: the plan with the group `section` and all it brings."""
    return with_group(plan, SECTION)


def github_form(plan):
    """How a landing of this plan meets GitHub: None (no candidate, nothing read), `after` (the candidate is pushed and main
    moves when this machine's checks pass; the jobs are read after the move) or `first` (the move waits for every `must` job:
    the plan has `github-first`, or the run is full, which may hold any file). `plan` None is a run with no plan: `first`."""
    if plan is None or plan.full or GITHUB_FIRST in plan.groups:
        return "first"
    return "after" if GITHUB in plan.groups else None


def groups_from_text(text):
    """The groups of a plan's text ('repo,build', 'full', 'none'): (groups, full). Refuses a name that is none."""
    text = (text or "").strip()
    if text.startswith("full"):
        return list(GROUP_NAMES), True
    names = [g.strip() for g in text.split(",") if g.strip() and g.strip() != "none"]
    bad = [g for g in names if g not in GROUPS]
    if bad:
        raise ScopeError(["not a group: %s (%s)" % (", ".join(bad), ", ".join(GROUP_NAMES))])
    return [g for g in GROUP_NAMES if g in expand(names)], False


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
