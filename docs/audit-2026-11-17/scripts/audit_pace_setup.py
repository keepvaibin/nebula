"""audit_pace_setup.py mod MOD_FOLDER STAGE SCENARIO | run RUN_FOLDER
mod: writes the small mod that takes the game from the title into a stage in real time (a new file at frame 800, the
     stage at frame 1100), from the audit's probe template. It is then built with lumalink.
run: writes the run folder's mods.ini (the mod on, its code allowed, as the test runner writes it) and adapter.ini."""
import json
import pathlib
import shutil
import sys

TREE = pathlib.Path("D:/NebulaWork/MarioGalaxyRecomp/Nebula-Clean")
TEMPLATE = pathlib.Path("D:/NebulaWork/build/audit-probes/p0-control-title")
ADAPTER = pathlib.Path("D:/NebulaWork/build/audit-probes-work/p0-control-title/lumalink-test-1794901027/runs/"
                       "lumalink-test-1794901027/p0-control-title/d3d12/settings/adapter.ini")


def mod(folder, stage, scenario, file_frame=800, stage_frame=1100):
    folder = pathlib.Path(folder)
    if folder.exists():
        shutil.rmtree(folder)
    shutil.copytree(TEMPLATE, folder, ignore=shutil.ignore_patterns("code", "tests", "source"))
    m = json.loads((folder / "luma.json").read_text(encoding="utf8"))
    m["id"] = "audit-pace"
    m["name"] = "audit-pace"
    m["summary"] = "Takes the game from the title into a stage, for timing runs."
    m.pop("needs", None)
    m.pop("text", None)
    m.pop("options", None)
    m["code"] = {"platforms": ["windows-x64"]}
    (folder / "luma.json").write_text(json.dumps(m, indent=2) + "\n", encoding="utf8")
    (folder / "source").mkdir()
    (folder / "source" / "main.lum").write_text(
        "var n: i64 = 0\n\non frame {\n    n += 1\n    if n == %d {\n        log \"pace: new file\"\n"
        "        GameSequenceFunction::startCreateUserFileSequence(1)\n    }\n    if n == %d {\n"
        "        log \"pace: to the stage\"\n        MR::requestChangeStageInGameMoving(\"%s\", %d)\n    }\n}\n"
        % (int(file_frame), int(stage_frame), stage, int(scenario)),
        encoding="utf8", newline="\n")


def run(folder):
    sys.path.insert(0, str(TREE / "tools" / "lumacore"))
    import luma_test
    settings = pathlib.Path(folder) / "settings"
    library = settings / "mods" / "audit-pace" / "code" / "windows-x64" / "mod.dll"
    (settings / "mods.ini").write_text(
        "; Nebula mods: their place in the list (order 1 is the top and wins), on/off and choices.\n"
        "[audit-pace]\nenabled=1\norder=1\ncode_ok=%s\n" % luma_test.code_token(library), encoding="utf8", newline="\n")
    shutil.copyfile(ADAPTER, settings / "adapter.ini")


if sys.argv[1] == "mod":
    mod(*sys.argv[2:7])
else:
    run(sys.argv[2])
