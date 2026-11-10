"""test_plugin.py SETTINGS_FOLDER LIBRARY: the test library's game half as a mod of that settings folder.

The test library (nebula_test_extension.dll) is two halves: the host loads it with --extension (controls, pictures, sound,
menus), and the mods run it as a plugin (tours, events, teleports and walks through the mod API). This puts it in
SETTINGS_FOLDER/mods/nebula-tests with its luma.json, and turns it on in mods.ini with its code allowed, as the Mods
page's allow would (code_ok: FNV-1a 64 over the library folder's files, as mod_plugins.cpp's plugin_token makes it).
The library keeps its file name: the plugin finds the host's instance of it by that name.
"""
import json
import pathlib
import re
import shutil
import sys

MOD_ID = "nebula-tests"
OFFSET, PRIME, MASK = 14695981039346656037, 1099511628211, 0xFFFFFFFFFFFFFFFF


def fnv(h, data):
    for b in data:
        h = ((h ^ b) * PRIME) & MASK
    return h


def code_hash(library):
    h = OFFSET
    for path in sorted(p for p in library.parent.iterdir() if p.is_file()):
        h = fnv(h, path.name.encode("utf-8") + b"\0")
        h = fnv(h, path.read_bytes() + b"\0")
    return "%016x" % h


def main():
    settings = pathlib.Path(sys.argv[1])
    library = pathlib.Path(sys.argv[2])
    mod = settings / "mods" / MOD_ID
    if mod.exists():
        shutil.rmtree(mod)
    (mod / "plugins").mkdir(parents=True)
    shutil.copyfile(library, mod / "plugins" / library.name)
    rel = "plugins/" + library.name
    (mod / "luma.json").write_text(json.dumps({
        "format": 1,
        "id": MOD_ID,
        "name": "Nebula tests",
        "version": "1.0.0",
        "game": "RMG",
        "authors": ["Nebula tests"],
        "description": "The test runs' tours and events (tools/test_driver/extension/test_plugin.cpp).",
        "layers": [],
        "plugins": [{"api": 2, "binaries": {"windows-x64": rel}}],
    }, indent=2), encoding="utf-8")
    token = "%016x" % fnv(OFFSET, code_hash(mod / rel).encode())
    ini = settings / "mods.ini"
    text = ini.read_text(encoding="utf-8") if ini.exists() else ""
    # Its own section replaced; it runs after the run's other mods.
    text = re.sub(r"(?ms)^\[" + re.escape(MOD_ID) + r"\]\n.*?(?=^\[|\Z)", "", text)
    orders = [int(n) for n in re.findall(r"(?m)^order=(\d+)", text)]
    if text and not text.endswith("\n"):
        text += "\n"
    text += "[%s]\nenabled=1\norder=%d\ncode_ok=%s\n" % (MOD_ID, max(orders, default=0) + 1, token)
    ini.write_text(text, encoding="utf-8")


if __name__ == "__main__":
    main()
