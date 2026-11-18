"""harness_self_description_test.py: a run's game has the bin folder's self-description (luma/), shared, without the game.

python -X utf8 -I tools/lumacore/tests/harness_self_description_test.py [--work <folder>]

luma_test.py gives each run's bin/luma as a link to one copy of the bin folder's luma/ per work folder (N-2054: before,
the game in a test run had none, and the start's check ran without the game's names, types, services and renderer
features). Checks, on a made-up bin folder:
- the run sees every file of the bin folder's luma/ through its link, the same bytes;
- a second run of the same bins uses the same copy; bins changed since get a new one, and the change does not reach
  the old copy (a copy, not links to the bins' files);
- deleting a run folder (the runner's way, or shutil.rmtree as mod_tests.py and the gate do) deletes its link
  only: the shared copy and the bin folder keep every file;
- a bin folder with no luma/ gets no copy.
Exit 0 when every check holds.
"""
import pathlib
import shutil
import sys
import tempfile

HERE = pathlib.Path(__file__).resolve().parent
sys.path.insert(0, str(HERE.parent))
import luma_test as L  # noqa: E402


def option(flag, default=None):
    return sys.argv[sys.argv.index(flag) + 1] if flag in sys.argv[:-1] else default


def files_of(folder):
    return {f.relative_to(folder).as_posix(): f.read_bytes() for f in sorted(folder.rglob("*")) if f.is_file()}


def main():
    work = pathlib.Path(option("--work") or tempfile.mkdtemp(prefix="harness-self-description-")).resolve()
    if work.exists():
        shutil.rmtree(work)
    work.mkdir(parents=True)
    failed = []

    def check(ok, what):
        print(("pass  " if ok else "FAIL  ") + what)
        if not ok:
            failed.append(what)

    bins = work / "bins"
    (bins / "luma" / "Game" / "System").mkdir(parents=True)
    (bins / "luma" / "mods").mkdir()
    (bins / "luma" / "luma_types.json").write_text('{"types": []}\n', encoding="utf-8")
    (bins / "luma" / "luma_renderer.json").write_text('{"renderer": {"features": []}}\n', encoding="utf-8")
    (bins / "luma" / "Game" / "System" / "A.cpp.rows").write_text("row one\n", encoding="utf-8")
    (bins / "luma" / "mods" / "kit.luma").write_bytes(b"PK\x03\x04 a kit")
    want = files_of(bins / "luma")
    root = work / "work"

    runs = []
    for n in range(2):
        run = root / "runs" / ("t%d" % n) / "bin"
        run.mkdir(parents=True)
        shared = L.self_description(bins, root)
        L.link_folder(shared, run / "luma")
        runs.append((run.parent, shared))
        check(L.folder_link(run / "luma"), "run %d: bin/luma is a link" % n)
        check(files_of(run / "luma") == want, "run %d sees every file of the bins' luma/, the same bytes" % n)
    check(runs[0][1] == runs[1][1], "the second run uses the first run's copy")
    check(not (bins / "luma").samefile(runs[0][1]), "the copy is not the bins' own folder")

    L.remove_run_folder(runs[0][0])
    check(not runs[0][0].exists(), "the run folder is deleted")
    check(files_of(runs[0][1]) == want, "the shared copy keeps every file after a run folder is deleted")
    check(files_of(bins / "luma") == want, "the bins' luma/ keeps every file after a run folder is deleted")
    check(files_of(runs[1][0] / "bin" / "luma") == want, "the other run still sees every file")

    # Bins changed since: a new copy (a fresh process, as the next run of the tests is).
    L.SELF_MADE.clear()
    (bins / "luma" / "luma_renderer.json").write_text('{"renderer": {"features": ["mesh_shaders"]}}\n', encoding="utf-8")
    changed = L.self_description(bins, root)
    check(changed != runs[1][1], "bins changed since get a new copy")
    check(files_of(changed) == files_of(bins / "luma"), "the new copy has the changed file")

    bare = work / "bare"
    bare.mkdir()
    check(L.self_description(bare, root) is None, "a bin folder with no luma/ gets no copy")

    check(files_of(runs[1][1]) == want, "a change to the bins after a copy was made does not reach the copy")
    # mod_tests.py and the gate delete run folders with shutil.rmtree: a link is not followed.
    bins_now = files_of(bins / "luma")
    shutil.rmtree(runs[1][0])
    check(files_of(runs[1][1]) == want and files_of(bins / "luma") == bins_now,
          "shutil.rmtree of a run folder leaves the shared copy and the bins whole")
    if failed:
        print("harness_self_description_test.py: FAIL (%d)" % len(failed))
        sys.exit(1)
    print("harness_self_description_test.py: pass")


if __name__ == "__main__":
    main()
