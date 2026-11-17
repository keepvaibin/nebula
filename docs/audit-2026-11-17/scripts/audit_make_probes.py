"""audit_make_probes.py: small mods that each make one call on the title screen (no stage, no player), to show by a run
whether the call ends the game. Built from the first-mod example's folder. Out: D:/NebulaWork/build/audit-probes/<name>."""
import json
import pathlib
import shutil

SRC = pathlib.Path("D:/NebulaWork/build/audit-examples/first-mod")
OUT = pathlib.Path("D:/NebulaWork/build/audit-probes")
PROBES = {
    "p0-control": ("", "log \"probe: no call\""),
    "p1-raw-validate-sensor": ("", "MarioAccess::validateSensor()"),
    "p2-kit-item-coins": ("use kit.item\n", "item.give(item.Gives.Coins(1))"),
    "p3-kit-item-health": ("use kit.item\n", "item.give(item.Gives.Health(1))"),
    "p4-kit-camera-now": ("use kit.camera\n", "let e = camera.now()"),
    "p5-kit-pointer-world": ("use kit.pointer\n", "let w = pointer.world()"),
}
if OUT.exists():
    shutil.rmtree(OUT)
for name, (uses, call), at in [(n + "-" + w, v, a) for n, v in PROBES.items() for w, a in (("boot", 1), ("title", 90))]:
    d = OUT / name
    shutil.copytree(SRC, d, ignore=shutil.ignore_patterns("code", "tests", "source"))
    m = json.loads((d / "luma.json").read_text(encoding="utf8"))
    m["id"] = "audit-" + name
    m["name"] = name
    m.pop("text", None)
    m.pop("options", None)
    m["code"] = {"platforms": ["windows-x64"]}
    (d / "luma.json").write_text(json.dumps(m, indent=2) + "\n", encoding="utf8")
    (d / "source").mkdir()
    (d / "source" / "main.lum").write_text(
        uses + "\nvar n: i64 = 0\n\non frame {\n    n += 1\n    if n == " + str(at) + " {\n        log \"probe: calling\"\n        "
        + call + "\n        log \"probe: survived\"\n    }\n}\n", encoding="utf8", newline="\n")
    t = d / "tests" / "title"
    t.mkdir(parents=True)
    (t / "test.json").write_text(json.dumps({
        "harness": 1, "name": name, "about": "One call on the title screen, with no stage and no player",
        "mods": ["../.."], "steps": [{"at": 240, "end": True}], "expect": [{"log": "probe: survived"}]}, indent=2) + "\n",
        encoding="utf8")
print("probes:", ", ".join(PROBES))
