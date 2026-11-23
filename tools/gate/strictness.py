"""strictness.py: a check may not be made to see less without a line in the log (docs/plan/native/TOOL-MEND.md section 6).

    python -X utf8 -I tools/gate/strictness.py [--base origin/main] [--head HEAD] [--branch NAME] [--tree FOLDER]

tools/gate/strictness.tsv lists the places whose change can make a check see less: pattern, mode, where, what a change could
hide. A change to a listed place needs, in the same landing, one line of docs/plan/strictness-log.tsv: date, file, what changed,
why, allowed by (the document and section), id (the step or the mend), removed by (the step that takes an exception away, else -).
The line names the file in its second column.

  pattern   a path pattern as tools/gate/scope.tsv has them (`**` any folders, `*` stays in a name)
  mode      added: an added line counts; removed: a removed or changed line counts; any: both
  where     `*` the whole file, or `block:NAME[,NAME]`: only the top-level list or number NAME (a Python or PowerShell
            assignment, up to its closing bracket) in the old or the new text of the file
  hides     one sentence on what a change to it could hide

Pure additions to a file in mode `removed` are free. The log only grows: a removed or changed line of it is refused. A branch named
`mend/*` may not change files of the game, the engine, the importer or the platform code (the four folders of GAME_FOLDERS); the build
files in them (CMake, scripts, gradle, toml) are free. Blank lines and white space are not changes. Exit 0 when nothing is refused, 1
with the lines. Python standard library only; the git calls are the tool's own, nothing is written.
"""
import argparse
import fnmatch
import pathlib
import re
import subprocess
import sys

HERE = pathlib.Path(__file__).resolve().parent
TREE = HERE.parent.parent
sys.path.insert(0, str(HERE))
import scope  # noqa: E402

GIT = "C:/Program Files/Git/cmd/git.exe"
TABLE = HERE / "strictness.tsv"
LOG = "docs/plan/strictness-log.tsv"
LOG_COLUMNS = ("date", "file", "what changed", "why", "allowed by", "id", "removed by")
TABLE_COLUMNS = ("pattern", "mode", "where", "hides")
MODES = ("added", "removed", "any")
MEND_BRANCH = "mend/"
# The four folders of the game, the engine, the importer and the platform code. A mend changes the tools, never these; the build files
# in them are free (a mend of the build scripts is a tool mend).
GAME_FOLDERS = ("native/", "runtime/", "crates/", "platform/")
BUILD_FILES = ("CMakeLists.txt", "*.cmake", "*.ps1", "*.sh", "*.bat", "*.gradle", "*.kts", "gradle.properties", "gradlew", "*.toml",
               "Cargo.lock", ".gitignore", ".gitattributes", ".clang-tidy", "*.in")


class StrictnessError(Exception):
    pass


def read_table(path=TABLE):
    """[{pattern, mode, where, hides}]; StrictnessError with every problem when the file is wrong."""
    text = pathlib.Path(path).read_text(encoding="utf-8")
    rows, problems = [], []
    seen_head = False
    for n, line in enumerate(text.splitlines(), 1):
        if not line.strip() or line.startswith("#"):
            continue
        cells = line.split("\t")
        if not seen_head:
            seen_head = True
            if tuple(cells) != TABLE_COLUMNS:
                problems.append("%s:%d: the first line is the header: %s" % (pathlib.Path(path).name, n, "<TAB>".join(TABLE_COLUMNS)))
            continue
        if len(cells) != 4 or not all(c.strip() for c in cells):
            problems.append("%s:%d: a row has four cells (%s), none empty" % (pathlib.Path(path).name, n, ", ".join(TABLE_COLUMNS)))
            continue
        row = dict(zip(TABLE_COLUMNS, (c.strip() for c in cells)))
        if row["mode"] not in MODES:
            problems.append("%s:%d: the mode is one of %s" % (pathlib.Path(path).name, n, ", ".join(MODES)))
        if row["where"] != "*" and not row["where"].startswith("block:"):
            problems.append("%s:%d: where is `*` or `block:NAME`" % (pathlib.Path(path).name, n))
        rows.append(row)
    if problems:
        raise StrictnessError("\n".join(problems))
    return rows


# ---- the diff --------------------------------------------------------------------------------------------------------

class Changes:
    """One file's changed lines: `removed` [(old line number, text)] and `added` [(new line number, text)]."""

    def __init__(self, path):
        self.path = path
        self.removed, self.added = [], []
        self.new_file = self.deleted = False


HUNK = re.compile(r"^@@ -(\d+)(?:,(\d+))? \+(\d+)(?:,(\d+))? @@")


def run_git(tree, *args, git=GIT):
    exe = git if pathlib.Path(git).exists() else "git"
    r = subprocess.run([exe, "-C", str(tree), *args], capture_output=True, text=True, encoding="utf-8", errors="replace")
    return r


def diff_changes(tree, base, head, git=GIT):
    """{path: Changes} between the merge base of `base` and `head`, and `head`: no renames, no white space, no blank lines. Raises
    StrictnessError when git cannot say."""
    mb = run_git(tree, "merge-base", base, head, git=git)
    if mb.returncode != 0:
        raise StrictnessError("no merge base of %s and %s: %s" % (base, head, (mb.stderr or mb.stdout).strip()[-200:]))
    start = mb.stdout.strip()
    r = run_git(tree, "diff", "-U0", "--no-renames", "--ignore-all-space", "--ignore-blank-lines", "--no-color", "--no-ext-diff", start, head,
                git=git)
    if r.returncode != 0:
        raise StrictnessError("git diff failed: %s" % (r.stderr or r.stdout).strip()[-200:])
    out, cur, old_no, new_no = {}, None, 0, 0
    old_name = None
    for line in r.stdout.splitlines():
        if line.startswith("diff --git "):
            cur, old_name = None, None
        elif line.startswith("--- "):
            old_name = None if line[4:] == "/dev/null" else line[6:] if line.startswith("--- a/") else line[4:]
        elif line.startswith("+++ "):
            new_name = None if line[4:] == "/dev/null" else line[6:] if line.startswith("+++ b/") else line[4:]
            path = new_name or old_name
            cur = out.setdefault(path, Changes(path))
            cur.new_file, cur.deleted = old_name is None, new_name is None
        elif line.startswith("@@") and cur is not None:
            m = HUNK.match(line)
            if m:
                old_no, new_no = int(m.group(1)), int(m.group(3))
        elif cur is not None and line.startswith("-") and not line.startswith("---"):
            cur.removed.append((old_no, line[1:]))
            old_no += 1
        elif cur is not None and line.startswith("+") and not line.startswith("+++"):
            cur.added.append((new_no, line[1:]))
            new_no += 1
    return out


def file_text(tree, ref, path, git=GIT):
    r = run_git(tree, "show", "%s:%s" % (ref, path), git=git)
    return r.stdout if r.returncode == 0 else ""


# ---- blocks ----------------------------------------------------------------------------------------------------------

STRING = re.compile(r'"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\'')


def block_lines(text, names):
    """The line numbers (1-based) of the top-level assignments of `names` in a Python or PowerShell text: from the line `NAME =` (or
    `$NAME =`, or `NAME: type =`) to the line where its brackets close."""
    lines = text.splitlines()
    wanted = {n.lstrip("$") for n in names}
    out, i = set(), 0
    start = re.compile(r"^\$?(\w+)\s*(?::[^=]*)?=(?!=)")
    while i < len(lines):
        m = start.match(lines[i])
        if not (m and m.group(1) in wanted):
            i += 1
            continue
        depth, j = 0, i
        while j < len(lines):
            clean = STRING.sub('""', lines[j])
            clean = re.sub(r"\s#.*$", "", clean) if j > i else re.sub(r"\s#.*$", "", clean)
            depth += sum(clean.count(c) for c in "([{") - sum(clean.count(c) for c in ")]}")
            out.add(j + 1)
            if depth <= 0 and not clean.rstrip().endswith(("\\", ",", "(", "[", "{")):
                break
            j += 1
        i = j + 1
    return out


# ---- the rules -------------------------------------------------------------------------------------------------------

def matching_rows(rows, path):
    return [r for r in rows if scope.path_matches(path, r["pattern"])]


def counted(rows, ch, tree, base_start, head, git=GIT):
    """The lines of one file's changes that a matching row counts: [(row, side, number, text)]."""
    hits = []
    old_text = new_text = None
    for row in matching_rows(rows, ch.path):
        removed = ch.removed if row["mode"] in ("removed", "any") else []
        added = ch.added if row["mode"] in ("added", "any") else []
        if row["where"] != "*":
            names = row["where"][len("block:"):].split(",")
            if removed:
                old_text = file_text(tree, base_start, ch.path, git) if old_text is None else old_text
                inside = block_lines(old_text, names)
                removed = [(n, t) for n, t in removed if n in inside]
            if added:
                new_text = file_text(tree, head, ch.path, git) if new_text is None else new_text
                inside = block_lines(new_text, names)
                added = [(n, t) for n, t in added if n in inside]
        hits += [(row, "removed", n, t) for n, t in removed] + [(row, "added", n, t) for n, t in added]
    return hits


def parse_log_lines(lines):
    """[(cells, problem)] for each added line of the log: seven cells, none empty."""
    out = []
    for text in lines:
        cells = text.split("\t")
        if len(cells) != len(LOG_COLUMNS) or any(not c.strip() for c in cells):
            out.append((cells, "a line of %s has %d cells (%s), none empty" % (LOG, len(LOG_COLUMNS), ", ".join(LOG_COLUMNS))))
        elif not re.match(r"^\d{4}-\d\d-\d\d$", cells[0]):
            out.append((cells, "a line of %s starts with the date as YYYY-MM-DD: %r" % (LOG, cells[0][:20])))
        else:
            out.append((cells, ""))
    return out


def is_game_source(path):
    if not path.startswith(GAME_FOLDERS):
        return False
    name = path.rsplit("/", 1)[-1]
    return not any(fnmatch.fnmatchcase(name, p) for p in BUILD_FILES)


def problems_for(tree, base, head, branch="", rows=None, git=GIT):
    """[line] of what is refused between the merge base of `base` and `head` (a branch's tip). Empty when nothing is."""
    rows = read_table() if rows is None else rows
    changes = diff_changes(tree, base, head, git)
    start = run_git(tree, "merge-base", base, head, git=git).stdout.strip()
    out = []
    log = changes.get(LOG)
    if log is not None and log.removed:
        out.append("%s: %d line%s removed or changed. The log only grows: %s" % (
            LOG, len(log.removed), "" if len(log.removed) == 1 else "s", "; ".join(t[:80] for _, t in log.removed[:3])))
    log_lines = parse_log_lines([t for _, t in (log.added if log else []) if not t.startswith("date\t")])
    bad_lines = [p for _, p in log_lines if p]
    out += sorted(set(bad_lines))
    named = {cells[1] for cells, p in log_lines if not p}
    for path in sorted(changes):
        if path == LOG:
            continue
        hits = counted(rows, changes[path], tree, start, head, git)
        if not hits or path in named:
            continue
        hides = "; ".join(sorted({h[0]["hides"] for h in hits}))
        out.append("%s: %d added and %d removed line%s are in a place that can make a check see less (%s)" % (
            path, sum(1 for h in hits if h[1] == "added"), sum(1 for h in hits if h[1] == "removed"),
            "" if len(hits) == 1 else "s", hides))
        for row, side, n, text in hits[:3]:
            out.append("    %s %d: %s" % ("-" if side == "removed" else "+", n, text.strip()[:100]))
        out.append("    Add one line to %s in this landing: %s, with %s in its second column (the file), the reason, the document and "
                   "section that allows it, and the step or mend id." % (LOG, ", ".join(LOG_COLUMNS), path))
    if branch.startswith(MEND_BRANCH):
        game = sorted(p for p in changes if is_game_source(p))
        if game:
            out.append("%s changes files of the game, the engine, the importer or the platform code: %s. A mend changes the tools; a fault "
                       "that needs these is reported NOT MENDED, needs game code (docs/plan/native/TOOL-MEND.md section 4)." % (
                           branch, ", ".join(game[:6]) + (" and %d more" % (len(game) - 6) if len(game) > 6 else "")))
    return out


def resolve(tree, name, git=GIT):
    for ref in ("refs/heads/" + name, "refs/remotes/origin/" + name):
        r = run_git(tree, "rev-parse", "--verify", "-q", ref + "^{commit}", git=git)
        if r.returncode == 0 and r.stdout.strip():
            return r.stdout.strip()
    return ""


def check_branches(tree, base, names, git=GIT):
    """The gate's check: each covered branch against `base` on its own; with no name that resolves, HEAD. [line]."""
    rows = read_table(pathlib.Path(tree) / "tools" / "gate" / "strictness.tsv") if (pathlib.Path(tree) / "tools" / "gate" / "strictness.tsv").is_file() else read_table()
    tips = [(n, resolve(tree, n, git)) for n in names]
    tips = [(n, sha) for n, sha in tips if sha]
    if not tips:
        return problems_for(tree, base, "HEAD", "", rows, git)
    out = []
    for name, sha in tips:
        lines = problems_for(tree, base, sha, name, rows, git)
        out += ["%s: %s" % (name, lines[0])] + lines[1:] if lines else []
    return out


def main(argv=None):
    p = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    p.add_argument("--tree", default=str(TREE))
    p.add_argument("--base", default="origin/main")
    p.add_argument("--head", default="HEAD")
    p.add_argument("--branch", action="append", default=[], help="a branch name to judge by its name too (mend/*); the tip is --head")
    a = p.parse_args(argv)
    try:
        lines = problems_for(a.tree, a.base, a.head, a.branch[0] if a.branch else "")
    except StrictnessError as error:
        print("strictness.py: %s" % error)
        return 2
    if lines:
        print("strictness.py: refused:")
        for l in lines:
            print("  " + l)
        return 1
    print("strictness.py: nothing in a listed place is changed without a line in the log")
    return 0


if __name__ == "__main__":
    sys.exit(main())
