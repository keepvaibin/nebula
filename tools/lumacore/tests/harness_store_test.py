"""harness_store_test.py: which store version a run's game data must have (luma_test.py run_one), without a game.

python -X utf8 -I tools/lumacore/tests/harness_store_test.py

The speed pair (tools/measure/measure.py pair) runs both sides through this runner, which is the candidate's tree's. A side plays with
its own build's programs and its own game.pak, so the version its game data must have is its own build's, not the runner's tree's:
  1. a pak of another version than the runner's tree, with a build whose own importer says the pak's version: not refused for it
     (the run goes on to start the game);
  2. a pak of another version than the build's own importer says: refused, the words give both versions and say whose the
     wanted one is (the build's own importer's);
  3. a build whose importer cannot be asked (not a library, no such function): the tree's version is the wanted one, as before;
  4. asked_store_version: a library's exported function is asked in a program of its own and its number returned; a file that is
     not a library, a missing function and a missing file give None (Windows: kernel32.dll's GetCurrentProcessId stands in for the
     importer's function, which no test may build).
Exit 0 when every case holds.
"""
import argparse
import errno
import pathlib
import platform
import subprocess
import sys
import tempfile

HERE = pathlib.Path(__file__).resolve().parent
sys.path.insert(0, str(HERE.parent))
import luma_test  # noqa: E402

failures = []


def check(ok, what):
    if not ok:
        failures.append(what)


def write_pak(path, version):
    path.write_bytes(b"NBSTORE\0" + version.to_bytes(4, "little") + bytes(200))
    return path


def fake_bins(folder):
    folder.mkdir(parents=True)
    for name in (luma_test.HOST["exes"]["d3d12"], luma_test.HOST["game"], luma_test.HOST["importer"], luma_test.HOST["extension"]):
        (folder / name).write_bytes(b"x")
    return folder


def run_with(tmp, name, pak_version, built_version):
    """run_one for a build (fake files) whose own importer says `built_version` (None: it cannot be asked) and a pak of `pak_version`.
    Returns the words of the refusal for the version, or "started" when the run went on to start the game (which no test starts)."""
    bins = fake_bins(tmp / (name + "-bin"))
    pak = write_pak(tmp / (name + ".pak"), pak_version)
    test = luma_test.read_test(tmp / "suite" / "fine")
    test["bare"] = True
    args = argparse.Namespace(bin=str(bins), pak=str(pak), pak_for=[], extension=None, half=None, warm=None, diagnostics=None,
                              module="hooks", gpu=None, commit="x", who="t")
    real_popen, real_asked = subprocess.Popen, getattr(luma_test, "asked_store_version", None)

    def no_start(command, *a, **k):
        if pathlib.Path(str(command[0])).name != luma_test.HOST["exes"]["d3d12"]:
            return real_popen(command, *a, **k)
        raise FileNotFoundError(errno.ENOENT, "the program is not there")

    subprocess.Popen = no_start
    luma_test.asked_store_version = lambda library, symbol="nebula_import_store_version": built_version
    try:
        luma_test.run_one(test, "d3d12", args, tmp / (name + "-run"), None)
    except luma_test.HarnessError as error:
        return "started" if "the game could not be started" in str(error) else str(error)
    finally:
        subprocess.Popen = real_popen
        if real_asked is not None:
            luma_test.asked_store_version = real_asked
        else:
            del luma_test.asked_store_version
    return "returned"


with tempfile.TemporaryDirectory(ignore_cleanup_errors=True) as tmp:
    tmp = pathlib.Path(tmp)
    (tmp / "suite" / "fine").mkdir(parents=True)
    (tmp / "suite" / "fine" / "test.json").write_text(
        '{"harness": 1, "name": "fine", "about": "a", "proves": ["services.net.local"], "steps": [{"at": 1, "end": true}], '
        '"expect": [{"fact": "f", "is": true}]}', encoding="utf-8")
    tree = int((luma_test.TREE / "runtime" / "include" / "galaxy" / "store_version.inc").read_text(encoding="utf-8").strip())
    older = tree - 3

    # 1. An older build with its own older data, played by this tree's runner.
    said = run_with(tmp, "own", older, older)
    check(said == "started", "a pak of version %d, the build's own importer saying %d, the tree's %d: not refused (%s)" % (older, older, tree, said))

    # 2. Data that is not the build's own.
    said = run_with(tmp, "other", older - 1, older)
    check("store version %d" % (older - 1) in said and "%d" % older in said and "its own importer" in said,
          "a pak of version %d, the build's own importer saying %d: refused, naming both and whose is wanted: %s" % (older - 1, older, said))
    said = run_with(tmp, "tree-data", tree, older)
    check("store version %d" % tree in said and "its own importer" in said,
          "the tree's data given to an older build is refused too: %s" % said)

    # 3. A build that cannot be asked: the tree's version, as before.
    said = run_with(tmp, "unasked-right", tree, None)
    check(said == "started", "importer not askable, pak of the tree's version: not refused (%s)" % said)
    said = run_with(tmp, "unasked-wrong", older, None)
    check("store version %d" % older in said and "this tree's" in said,
          "importer not askable, pak of another version: refused with the tree's version as the wanted one: %s" % said)

    # 4. The question to a library.
    asked = getattr(luma_test, "asked_store_version", None)
    if asked is None:
        failures.append("luma_test has no asked_store_version")
    else:
        if platform.system() == "Windows":
            number = asked("kernel32.dll", "GetCurrentProcessId")
            check(isinstance(number, int) and number > 0, "a library's function is asked: %r" % (number,))
            check(asked("kernel32.dll", "NoSuchFunctionHere") is None, "a missing function gives None")
        junk = tmp / "junk.dll"
        junk.write_bytes(b"not a library")
        check(asked(junk) is None, "a file that is not a library gives None")
        check(asked(tmp / "absent.dll") is None, "a missing file gives None")

if failures:
    print("harness_store_test.py: FAILED:\n  " + "\n  ".join(failures))
    sys.exit(1)
print("harness_store_test.py: pass")
