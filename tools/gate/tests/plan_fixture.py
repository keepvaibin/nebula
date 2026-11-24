"""plan_fixture.py: what the plan tool's tests share (not a test: the gate runs only *_test.py).

A small plan in a temporary git tree (a bare origin and a clone, a ledger with CRLF line ends, a source file), made once
per test module and copied for every test, with the live folder (plan.LIVE) pointed at a temporary folder: no test
touches D:/NebulaWork/plan/native.
"""
import contextlib
import io
import os
import pathlib
import shutil
import subprocess
import sys
import tempfile
import textwrap
from unittest import mock

HERE = pathlib.Path(__file__).resolve().parent
TREE = HERE.parents[2]
sys.path.insert(0, str(TREE / "tools" / "plan"))
sys.path.insert(0, str(TREE / "tools" / "verify"))
import plan  # noqa: E402


def step(step_id, package, needs="", state="open", order=0, title="t", agent=""):
    return {"order": str(order), "step": step_id, "package": package, "title": title, "package_needs": needs,
            "card_file": "03-X.md", "state": state, "commit": "", "agent": agent, "reason": ""}


def plan_of(*rows):
    return [dict(r, order=str(i + 1)) for i, r in enumerate(rows)]


def run(args, tree, work_root, store, expect=None, live=None):
    """plan.py's main with its output caught: (exit code, text)."""
    out = io.StringIO()
    extra = ["--live", str(live)] if live else []
    with contextlib.redirect_stdout(out):
        code = plan.main(["--tree", str(tree), "--work-root", str(work_root), "--failures", str(store), *extra, *args])
    if expect is not None:
        assert code == expect, "exit %r, wanted %r:\n%s" % (code, expect, out.getvalue())
    return code, out.getvalue()


# ---- the temporary tree ------------------------------------------------------------------------------------

CARDS = textwrap.dedent("""\
    # Cards

    ## A Package A

    ### A.1 First step

    - **Where.** `runtime/src/render/a.cpp`, `docs/plan/ledger.tsv`, `rows/branch`
    - **Rows.** P-001: whole. P-002 (part): the half.
    - **Proof.** a test.

    ### A.2 Second step

    - **Rows.** P-003 whole.

    ### Rows of this package

    | Row | Today |
    | --- | --- |
    | P-001 | one |

    ---

    ## B Package B

    ### B.1 Other step

    - **Rows.** none of this package's rows.

    ### C.1 Free step

    - **Rows.** P-004 whole.

    ### Rows of this package

    | Row | Today |
    | --- | --- |
    | P-004 | four |
    """)

LEDGER_HEAD = "id\tagent\tpackage\tstate\tproof\ttitle\tnote"


def ledger_text(rows):
    lines = [LEDGER_HEAD] + ["\t".join(r) for r in rows]
    return "\r\n".join(lines) + "\r\n"


def ledger_row(rid, state="open", proof="", note=""):
    return (rid, "1", "E0", state, proof, "title of " + rid, note)


def rmtree_force(path):
    """shutil.rmtree that also removes git's read-only object files (Windows)."""
    def unlock(func, p, _exc):
        os.chmod(p, 0o700)
        func(p)
    shutil.rmtree(path, onerror=unlock)


TEMPLATE = {}


def git_env(config):
    return {"GIT_CONFIG_GLOBAL": str(config), "GIT_CONFIG_NOSYSTEM": "1", "GIT_AUTHOR_NAME": "t", "GIT_AUTHOR_EMAIL": "t@example.com",
            "GIT_COMMITTER_NAME": "t", "GIT_COMMITTER_EMAIL": "t@example.com"}


def git_run(*args, cwd=None):
    exe = plan.GIT if pathlib.Path(plan.GIT).exists() else "git"
    return subprocess.run([exe, *args], cwd=cwd, check=True, capture_output=True, text=True)


def setUpModule():
    """The temporary tree is made once (a bare origin and a clone with a small plan, a ledger and a source file, one
    commit pushed) and copied for every test: git is slow to start here."""
    root = pathlib.Path(tempfile.mkdtemp(prefix="plan-test-template-")).resolve()
    TEMPLATE["root"] = root
    config = root / "gitconfig"
    config.write_text("", encoding="utf-8")
    TEMPLATE["config"] = config
    with mock.patch.dict(os.environ, git_env(config)):
        git_run("init", "--bare", "-b", "main", str(root / "origin.git"))
        git_run("clone", str(root / "origin.git"), str(root / "tree"))
        tree = root / "tree"
        git_run("checkout", "-b", "main", cwd=str(tree))
        n = plan.native(tree)
        n.mkdir(parents=True)
        steps = [("1", "A.1", "A", "First step", "", "03-X.md", "open", ""), ("2", "A.2", "A", "Second step", "", "03-X.md", "open", ""),
                 ("3", "B.1", "B", "Other step", "A", "03-X.md", "open", ""), ("4", "C.1", "C", "Free step", "", "03-X.md", "open", ""),
                 ("5", "D.1", "D", "Last step", "everything", "03-X.md", "open", "")]
        (n / "steps.tsv").write_text("\t".join(plan.STEP_COLUMNS) + "\n" + "".join("\t".join(r) + "\n" for r in steps), encoding="utf-8")
        (n / "03-X.md").write_text(CARDS + "\n### D.1 Last step\n\n- **Rows.** None.\n", encoding="utf-8")
        (tree / "docs" / "plan").mkdir(parents=True, exist_ok=True)
        (tree / plan.LEDGER).write_bytes(ledger_text([ledger_row("P-001"), ledger_row("P-002", note="old 11-01"), ledger_row("P-003"),
                                                       ledger_row("P-004")]).encode("utf-8"))
        (tree / "runtime" / "src" / "render").mkdir(parents=True)
        (tree / "runtime" / "src" / "render" / "a.cpp").write_text("a\nb\nc\n", encoding="utf-8")
        (tree / "tools" / "verify").mkdir(parents=True)
        (tree / "tools" / "verify" / "doc_refs_allow.txt").write_text("", encoding="utf-8")
        git_run("add", "-A", cwd=str(tree))
        git_run("commit", "-m", "start", cwd=str(tree))
        git_run("push", "origin", "main", cwd=str(tree))


def tearDownModule():
    rmtree_force(TEMPLATE["root"])


class Tree:
    """A copy of the template: a bare origin, a plan tree (a clone of it, on main) and a work root, in a temporary folder."""

    def __init__(self):
        self.dir = pathlib.Path(tempfile.mkdtemp(prefix="plan-test-")).resolve()
        self.origin = self.dir / "origin.git"
        self.tree = self.dir / "tree"
        self.work = self.dir / "work"
        self.store = self.dir / "failures.tsv"
        self.live = self.dir / "live"
        # plan.py pushes the progress branch after a change of state: the tests push nothing unless a test turns it on.
        self.env = mock.patch.dict(os.environ, dict(git_env(TEMPLATE["config"]), **{plan.PUSH_OFF: "1"}))
        self.live_patch = mock.patch.object(plan, "LIVE", str(self.live))
        # A step's landing needs its hand-off sheet (mend M-007): plan_stack_test proves that rule; the other tests land without one.
        self.handoff_patch = mock.patch.object(plan, "HANDOFF_REQUIRED", False)

    def __enter__(self):
        self.env.start()
        self.live_patch.start()
        self.handoff_patch.start()
        shutil.copytree(TEMPLATE["root"] / "origin.git", self.origin)
        shutil.copytree(TEMPLATE["root"] / "tree", self.tree)
        self.work.mkdir()
        git_run("remote", "set-url", "origin", str(self.origin), cwd=str(self.tree))
        self.git = lambda *a, cwd=None: git_run(*a, cwd=str(cwd or self.tree)).stdout.strip()
        return self

    def __exit__(self, *exc):
        self.handoff_patch.stop()
        self.live_patch.stop()
        self.env.stop()
        rmtree_force(self.dir)

    def write_ledger(self, rows, tree=None):
        (pathlib.Path(tree or self.tree) / plan.LEDGER).write_bytes(ledger_text(rows).encode("utf-8"))

    def run(self, args, expect=None):
        return run(args, self.tree, self.work, self.store, expect, self.live)

    def folder(self, step_id=None, agent=1):
        """The agent's work folder: the same one for every step (the id is only there to read well)."""
        return plan.work_folder(self.work, agent)

    def push_plan_files(self, message="plan"):
        """Commits what changed in the plan tree and pushes it to origin's main: what a landing that passed leaves there."""
        self.git("add", "-A")
        if self.git("status", "--porcelain"):
            self.git("commit", "-q", "-m", message)
        self.git("push", "-q", "origin", "main")

    def landed(self, *ids, commit="abc123def"):
        """These steps are landed: in the tree's steps.tsv, off the live state, and on origin's main (a step is landed only when
        origin/main says so)."""
        for step_id in ids:
            plan.set_state(self.tree, step_id, "landed", commit)
        self.push_plan_files("Plan: %s landed" % ", ".join(ids))

    def commit(self, step_id, message="work", files=None):
        """A commit in the step's work folder (an edit of runtime/src/render/a.cpp unless `files` says what to write)."""
        folder = self.folder(step_id)
        for name, text in (files or {"runtime/src/render/a.cpp": "a\nb\nc\nd\n"}).items():
            (folder / name).parent.mkdir(parents=True, exist_ok=True)
            (folder / name).write_text(text, encoding="utf-8")
        self.git("add", "-A", cwd=folder)
        self.git("commit", "-m", message, cwd=folder)
        return self.git("rev-parse", "--short=9", "HEAD", cwd=folder)




SAFETY = {}


def setup_module():
    """For setUpModule: the template tree, and plan.LIVE aimed at a folder of its own for tests that make no Tree."""
    setUpModule()
    SAFETY["live"] = pathlib.Path(tempfile.mkdtemp(prefix="plan-live-safety-"))
    SAFETY["patch"] = mock.patch.object(plan, "LIVE", str(SAFETY["live"]))
    SAFETY["patch"].start()
    SAFETY["env"] = mock.patch.dict(os.environ, {plan.PUSH_OFF: "1"})    # a test of plan.py never pushes the real repository's branch
    SAFETY["env"].start()


def teardown_module():
    SAFETY["env"].stop()
    SAFETY["patch"].stop()
    shutil.rmtree(SAFETY["live"], ignore_errors=True)
    tearDownModule()


def ready_a1(t, step_id="A.1"):
    """The ledger the card of A.1 wants (P-001 fixed with a proof, P-002 with a new dated note), committed in the step's work
    folder. Returns the code commit."""
    t.write_ledger([ledger_row("P-001", "fixed", "tests/render/a_test 1-9 fails before"),
                    ledger_row("P-002", note="advanced 11-20: half done"), ledger_row("P-003"), ledger_row("P-004")],
                   tree=t.folder(step_id))
    return t.commit(step_id, "A.1: the code", {"runtime/src/render/a.cpp": "a\nb\nc\nd\n"})


def write_steps(t, rows):
    """Replaces the plan's steps: rows of (id, package, needs, state)."""
    n = plan.native(t.tree)
    lines = ["\t".join(plan.STEP_COLUMNS)]
    for i, (sid, pkg, needs, state) in enumerate(rows, 1):
        lines.append("\t".join([str(i), sid, pkg, "Title of " + sid, needs, "03-X.md", state, "abc" if state == "landed" else ""]))
    (n / "steps.tsv").write_text("\n".join(lines) + "\n", encoding="utf-8")
    t.push_plan_files("steps")                    # origin/main has them too: it alone says what is landed


def moving_land(verdict=None, commit="0123456789"):
    """A fake of plan.LAND that moves origin's main the way a pass of the gate does (the branch is pushed to main, a fast
    forward: the folder's branch was cut from it) and answers with `verdict` (a pass by default, with main's commit)."""
    def land(folder, branch, tablet, wait, options=(), **kw):
        git_run("push", "-q", "origin", "%s:main" % branch, cwd=str(folder))
        if verdict is not None:
            return verdict
        v = plan.Verdict(True, [("c", "PASS", "ok")])
        v.code, v.commit, v.gate_recorded = 0, commit, True
        return v
    return land
