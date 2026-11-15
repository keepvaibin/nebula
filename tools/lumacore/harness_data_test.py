"""harness_data_test.py: what a test continuing from another keeps of Nebula's data folder, without a game.

  1. the first run's game writes a mod's file where the fs service keeps it (LOCALAPPDATA/Nebula/mod-data/<id>,
     luma_fs.cpp data_root, LOCALAPPDATA being the run's appdata) and a file in the Exchange folder; the second run
     (continue_from) reads both, byte for byte, and the first run's folder is left as it was;
  2. a mod removed in the second run ("remove": true) keeps its data folder for when it returns (services-mods 8), so
     a third run continuing from the second still has it, and the mod put back there keeps its one mods.ini section;
  3. the shader cache and diagnostics are not carried; a run with nothing in mod-data or Exchange carries nothing.
With a game, harness.md R14 names the suite whose runs write through the fs service and read in the next.
Exit 0 only when every check holds.
"""
import json
import os
import pathlib
import shutil
import sys
import tempfile

HERE = pathlib.Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
import luma_test  # noqa: E402


def game_data_folder(run_folder):
    """Where the game of a run keeps its data: platform_paths.h nebula_data_directory on Windows, LOCALAPPDATA/Nebula,
    with the LOCALAPPDATA run_one gives the game."""
    return pathlib.Path(os.path.join(str(run_folder / "appdata"), "Nebula"))


def new_run(root, name):
    run = root / name
    for sub in ("bin", "pictures", "saves", "settings", "appdata"):
        (run / sub).mkdir(parents=True)
    (run / "settings" / "mods").mkdir()
    return run


def main():
    failed = []

    def check(ok, what):
        if not ok:
            failed.append(what)

    with tempfile.TemporaryDirectory() as tmp:
        root = pathlib.Path(tmp)
        check(luma_test.run_data_folder(root / "r") == game_data_folder(root / "r"),
              "the harness's data folder is the game's: %s" % luma_test.run_data_folder(root / "r"))
        mod = root / "m"
        mod.mkdir()
        (mod / "luma.json").write_text(json.dumps({"format": "1.0", "id": "kept-mod", "name": "m", "version": "1.0.0",
                                                   "authors": ["a"], "game": {"id": "RMG"}}), encoding="utf-8")

        # 1: the first run writes, the second reads.
        first = new_run(root, "first")
        data = game_data_folder(first)
        (data / "mod-data" / "kept-mod" / "runs").mkdir(parents=True)
        (data / "mod-data" / "kept-mod" / "runs" / "best.bin").write_bytes(b"\x00\x01time 61.25\xff")
        (data / "mod-data" / "kept-mod" / "empty.bin").write_bytes(b"")
        (data / "Exchange" / "pass").mkdir(parents=True)
        (data / "Exchange" / "pass" / "note.txt").write_bytes(b"for b")
        (data / "shadercache" / "RMGE01").mkdir(parents=True)
        (data / "shadercache" / "RMGE01" / "d3d12.bin").write_bytes(b"cache")
        (data / "diagnostics").mkdir()
        (data / "diagnostics" / "ring.txt").write_bytes(b"d")
        luma_test.install(first / "settings", mod, True, 1)
        before = sorted(p.relative_to(first).as_posix() for p in first.rglob("*"))

        second = new_run(root, "second")
        luma_test.carry_data(first, second)
        got = game_data_folder(second)
        check((got / "mod-data" / "kept-mod" / "runs" / "best.bin").read_bytes() == b"\x00\x01time 61.25\xff",
              "the second run reads the mod's file")
        check((got / "mod-data" / "kept-mod" / "empty.bin").is_file() and
              (got / "mod-data" / "kept-mod" / "empty.bin").stat().st_size == 0, "an empty file comes along")
        check((got / "Exchange" / "pass" / "note.txt").read_bytes() == b"for b", "the second run reads the Exchange file")
        check(not (got / "shadercache").exists(), "the shader cache is not carried")
        check(not (got / "diagnostics").exists(), "diagnostics are not carried")
        check(sorted(p.relative_to(first).as_posix() for p in first.rglob("*")) == before,
              "the first run's folder is left as it was")

        # 2: removed in the second run, its data stays for a third.
        (second / "settings" / "mods").rmdir()
        shutil.copytree(first / "settings", second / "settings", dirs_exist_ok=True)
        check(luma_test.remove_kept(second / "settings", mod) == "kept-mod", "remove_kept gives the id")
        check((got / "mod-data" / "kept-mod" / "runs" / "best.bin").is_file(), "a removed mod keeps its data folder")
        third = new_run(root, "third")
        luma_test.carry_data(second, third)
        check((game_data_folder(third) / "mod-data" / "kept-mod" / "runs" / "best.bin").read_bytes() ==
              b"\x00\x01time 61.25\xff", "a third run still has the removed mod's data")
        check((game_data_folder(third) / "Exchange" / "pass" / "note.txt").is_file(), "and the Exchange file")
        # It comes back in the third run: its folder again, its one mods.ini section (the player's answers) kept.
        shutil.copytree(second / "settings", third / "settings", dirs_exist_ok=True)
        ini = third / "settings" / "mods.ini"
        old = ini.read_text(encoding="utf-8")
        luma_test.install(third / "settings", mod, True, 1, kept=True)
        check((third / "settings" / "mods" / "kept-mod" / "luma.json").is_file(), "the returned mod's folder is back")
        check(ini.read_text(encoding="utf-8") == old and old.count("[kept-mod]") == 1,
              "the returned mod keeps its one section: %r" % ini.read_text(encoding="utf-8"))

        # 3: nothing to carry.
        empty = new_run(root, "empty")
        after_empty = new_run(root, "after-empty")
        luma_test.carry_data(empty, after_empty)
        check(list((after_empty / "appdata").iterdir()) == [], "a run with no data carries nothing")

    if failed:
        print("harness_data_test.py: FAILED:\n  " + "\n  ".join(failed))
        sys.exit(1)
    print("harness_data_test.py: pass")


if __name__ == "__main__":
    main()
