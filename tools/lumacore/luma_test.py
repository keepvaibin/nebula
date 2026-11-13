"""luma_test.py: the proof harness's runner (contract `harness`, revision 1; plan task 0.3). Python 3, standard library.

    luma_test.py run <test folder | suite.json> --bin <folder> [--renderer r] [--work <folder>] [--who F|G]
                 [--module hooks|plain]
    luma_test.py refs <test folder> --run <run folder> --looked

`run` reads each test strictly, gives it a private run folder, installs the game half first and then the test's mods,
starts the game hidden and silent with the plan for the host half (tools/test_driver/extension/harness.cpp), waits,
checks the run's files against the test's `expect` and the checks every test has, and writes result.json. One line per
test and renderer; exit 0 only when every verdict is `pass`.

`refs` writes a run's pictures as the references a test names, after the person running it has looked at them.

The bin folder holds NebulaNative.exe (and NebulaNativeVk.exe for Vulkan), smg_game.dll (and smg_game_plain.dll, which
the game loads when no mod has code: contract modules), nebula_test_extension.dll, luma-test.dll (the game half),
nebula_import.dll (a refusal's words; without it the game gives codes) and nebula-import.exe, pak.path, dxc/, and
smg_game.luma-placement.json (the placement facts a stage patch's arguments by name need).
Nothing outside the work folder is written but the run's own files.
On Linux (plan 8.1, the container suite) the same with the platform's names: nebula (Vulkan only: give --renderer
vulkan), libsmg_game.so (and libsmg_game_plain.so), libnebula_test_extension.so, luma-test.so, libnebula_import.so and
nebula-import; the mods' code is code/linux-x64/mod.so, and the game runs on whatever X display DISPLAY names (a
virtual one in the container).
"""
import argparse
import datetime
import hashlib
import json
import math
import os
import pathlib
import platform
import re
import shutil
import struct
import subprocess
import sys
import time
import zlib

RUNNER_VERSION = "1.0"
HARNESS = 1
TREE = pathlib.Path(__file__).resolve().parents[2]
CAPABILITIES = TREE / "tools" / "lumacore" / "capabilities.json"
GAME_HALF_ID = "luma-test"
# The platform the runner drives: its game by renderer, its game module, test extension, importer (library and program)
# and game half by file name, and where a mod's code lies (luma-format 6: code/<platform>/mod.<ending>).
HOSTS = {
    "Windows": {"platform": "windows-x64", "exes": {"d3d12": "NebulaNative.exe", "vulkan": "NebulaNativeVk.exe"},
                "game": "smg_game.dll", "plain": "smg_game_plain.dll", "extension": "nebula_test_extension.dll", "importer": "nebula_import.dll",
                "importer_exe": "nebula-import.exe", "half": "luma-test.dll", "ending": ".dll"},
    "Linux": {"platform": "linux-x64", "exes": {"vulkan": "nebula"}, "game": "libsmg_game.so", "plain": "libsmg_game_plain.so",
              "extension": "libnebula_test_extension.so", "importer": "libnebula_import.so",
              "importer_exe": "nebula-import", "half": "luma-test.so", "ending": ".so"},
}
HOST = HOSTS.get(platform.system(), HOSTS["Windows"])
HOST["code"] = "code/%s/mod%s" % (HOST["platform"], HOST["ending"])
EXES = HOST["exes"]
BUTTONS = {"A", "B", "Z", "C", "PLUS", "MINUS", "ONE", "TWO", "HOME", "UP", "DOWN", "LEFT", "RIGHT"}
KEYS = {"up", "down", "left", "right", "enter", "esc", "tab", "space", "backspace"}
LANGUAGES = {
    "UsEnglish": "english", "EuEnglish": "english", "UsFrench": "french", "EuFrench": "french",
    "UsSpanish": "spanish", "EuSpanish": "spanish", "EuGerman": "german", "EuItalian": "italian",
    "EuDutch": "dutch", "JpJapanese": "japanese",
    # The game's other four (from the language packs, Nebula's NebulaText.tsv): no setting value yet.
    "UsPortuguese": None, "KrKorean": None, "CnSimpChinese": None, "TwTradChinese": None,
}
FORMS = {"normal", "rainbow", "fire", "ice", "bee", "spring", "boo", "flying", "tornado"}
# The parameter types the game half passes (tools/test_driver/luma_test/luma_test.c do_call).
CALL_TYPES = {"f32", "float", "f64", "double", "s32", "int", "u32", "u16", "s16", "u8", "s8", "bool", "const char *",
              "const Vec3f &", "const TVec3f &", "Vec3f *", "const Vec3f *", "Vec3f", "TVec3f", "LiveActor *",
              "const LiveActor *", "NameObj *", "const NameObj *", "MarioActor *"}
# The result types it takes (luma_test.c kResults); any other pointer is taken as "is there one". Any other type (an
# enum, s64, a struct by value) is refused: it could be wider than the game half's result or read as the wrong bytes.
RESULT_TYPES = {"void", "f32", "float", "f64", "double", "s32", "int", "u32", "u16", "s16", "u8", "s8", "bool",
                "const char *", "Vec3f *", "const Vec3f *", "TVec3f *", "const TVec3f *", "Vec3f", "TVec3f"}
# H-5: the longest line the game half takes, in bytes (its end not counted), and the most words of a line; a call's
# arguments are its tab-separated words (luma_test.c LINE_BYTES, LINE_WORDS).
LINE_BYTES = 1023
LINE_WORDS = 16
VEC_TYPES = {"const Vec3f &", "const TVec3f &", "Vec3f *", "const Vec3f *", "Vec3f", "TVec3f"}
OBJECT_TYPES = {"LiveActor *", "const LiveActor *", "NameObj *", "const NameObj *", "MarioActor *"}
READ_TYPES = {"f32", "s32", "u32", "u16", "s16", "u8", "bool", "TVec3f", "Vec3f", "const char *"}
FACT_OPS = {"is", "is_not", "less", "at_most", "more", "at_least", "near", "exists", "absent", "has", "count"}
# Revision 6 (R6-3, G-H6-3): the host's Settings pages by name, lower case, as the Settings screen's own list keeps
# them (runtime/src/ui/native_settings.cpp kNativeSettingsTabNames), and the number the host opens each by.
SETTINGS_PAGES = {"display": 0, "graphics": 1, "controls": 2, "audio": 3, "general": 4, "mods": 5, "system": 6,
                  "advanced": 7}
# Revision 6 (R6-1, G-H6-1): a mod's own fact is mod.<id>.<key>; the id is luma-format's (3 to 64 of a-z 0-9 - .,
# no "..", no trailing "."), the key [a-z0-9_]+, split at the last dot.
MOD_ID = re.compile(r"[a-z0-9.-]{3,64}")
MOD_KEY = re.compile(r"[a-z0-9_]+")
# Revision 6 (R6-5): an object by name or by class, the n-th live one.
OBJECT_REF = re.compile(r"(object:[^\s#]+|class:[^\s#]+)#\d+")


class HarnessError(Exception):
    """The runner's own problem or a malformed test: verdict `harness-error`."""


# --- Reading a test strictly ------------------------------------------------------------------------------------------

def where(path, key):
    return "%s.%s" % (path, key) if path else key


def no_duplicates(pairs):
    seen = {}
    for key, value in pairs:
        if key in seen:
            raise HarnessError("the key %s appears twice" % key)
        seen[key] = value
    return seen


def load_json(path):
    try:
        return json.loads(pathlib.Path(path).read_text(encoding="utf-8"), object_pairs_hook=no_duplicates)
    except HarnessError:
        raise
    except (OSError, ValueError) as error:
        raise HarnessError("%s cannot be read as JSON: %s" % (path, error))


def keys_known(obj, path, known):
    if not isinstance(obj, dict):
        raise HarnessError("%s is not an object" % (path or "the test"))
    for key in obj:
        if key not in known:
            raise HarnessError("unknown key %s" % where(path, key))


def typed(obj, key, path, kinds, what):
    value = obj[key]
    ok = any(isinstance(value, k) and not (k in (int, float) and isinstance(value, bool)) for k in kinds)
    if not ok:
        raise HarnessError("%s must be %s, not %s" % (where(path, key), what, json.dumps(value)))
    return value


def number(obj, key, path):
    return typed(obj, key, path, (int, float), "a number")


def whole(obj, key, path, low=None):
    value = typed(obj, key, path, (int,), "a whole number")
    if low is not None and value < low:
        raise HarnessError("%s must be at least %d" % (where(path, key), low))
    return value


def text(obj, key, path):
    return typed(obj, key, path, (str,), "a string")


def boolean(obj, key, path):
    return typed(obj, key, path, (bool,), "true or false")


def vec3(obj, key, path):
    value = obj[key]
    if not (isinstance(value, list) and len(value) == 3 and all(isinstance(v, (int, float)) and not isinstance(v, bool) for v in value)):
        raise HarnessError("%s must be three numbers" % where(path, key))
    return value


def plain(value, path):
    """A word that goes into a plan line as is."""
    if not isinstance(value, str) or not value or any(c in value for c in " \t\r\n"):
        raise HarnessError("%s must be a word with no spaces" % path)
    return value


def fact_key(value, path):
    """A fact key a test names: plain, and a mod's own (mod.<id>.<key>) split at its last dot (G-H6-1)."""
    key = plain(value, path)
    if key.startswith("mod."):
        mod_id, dot, own = key[4:].rpartition(".")
        if not dot or not MOD_ID.fullmatch(mod_id) or ".." in mod_id or mod_id.endswith(".") or not MOD_KEY.fullmatch(own):
            raise HarnessError("%s: %s is not mod.<id>.<key> (an id of luma-format, a key of a-z 0-9 _)" % (path, key))
    return key


def class_names(types_path):
    """The class names class_of takes (a record or an alias) from a types file (luma_types.json)."""
    data = load_json(pathlib.Path(types_path))
    names = set()
    for row in data.get("types", []) + data.get("aliases", []):
        if isinstance(row, dict) and isinstance(row.get("name"), str):
            names.add(row["name"])
    return names


# The classes class:<Class> may name (run_tests sets it from the bin folder's luma_types.json; read_test's `types`).
CLASSES = None


def object_ref(value, path, classes):
    """`object:<name>#<n>` or `class:<Class>#<n>` (R6-5); a class the types file lacks is a harness-error (G-H6-2)."""
    if not (isinstance(value, str) and OBJECT_REF.fullmatch(value)):
        raise HarnessError("%s must be \"player\", \"object:<name>#<n>\" or \"class:<Class>#<n>\"" % path)
    if value.startswith("class:"):
        name = value[len("class:"):].rsplit("#", 1)[0]
        if classes is None:
            raise HarnessError("%s: class:%s needs the game's types (luma_types.json in the bin folder)" % (path, name))
        if name not in classes:
            raise HarnessError("%s: the game has no class %s (its types file)" % (path, name))
    return value


def hidden_path(value, path):
    """A hide_game_files path (R6-4, G-H6-4): relative to the game's folder, no drive, no leading slash, no `..`."""
    if not isinstance(value, str) or not value or any(c in value for c in "\t\r\n"):
        raise HarnessError("%s must be a path" % path)
    if (value[0] in "/\\" or re.match(r"[A-Za-z]:", value) or pathlib.PureWindowsPath(value).is_absolute()
            or ".." in re.split(r"[/\\]", value)):
        raise HarnessError("%s: %s must be relative to the game's folder, with no drive and no .." % (path, value))
    return value.replace("\\", "/")


def read_test(folder, types=None):
    folder = pathlib.Path(folder)
    classes = class_names(types) if types is not None else CLASSES
    t = load_json(folder / "test.json")
    keys_known(t, "", {"harness", "name", "about", "proves", "mods", "code", "language", "seed", "continue_from",
                       "renderers", "region", "limit_seconds", "bare", "save", "stage", "player", "steps", "expect",
                       "faults", "hide_game_files", "safe_start", "device"})
    for key in ("harness", "name", "about", "proves", "steps", "expect"):
        if key not in t:
            raise HarnessError("the test has no %s" % key)
    if whole(t, "harness", "") > HARNESS:
        raise HarnessError("harness %d: this runner reads %d" % (t["harness"], HARNESS))
    if not re.fullmatch(r"[A-Za-z0-9_.-]+", text(t, "name", "")):
        raise HarnessError("name may hold only letters, digits, -, _ and .")
    text(t, "about", "")
    if not isinstance(t["proves"], list) or not t["proves"] or not all(isinstance(p, str) for p in t["proves"]):
        raise HarnessError("proves must be a list of capability ids")
    known = set(load_json(CAPABILITIES).get("capabilities", {}))
    for capability in t["proves"]:
        if capability not in known:
            raise HarnessError("proves names %s, which is not in tools/lumacore/capabilities.json" % capability)
    bare = boolean(t, "bare", "") if "bare" in t else False
    # Player 1's device: the keyboard (the default) or a controller whose motor the harness reads (fact input.motor).
    device = text(t, "device", "") if "device" in t else "keyboard"
    if device not in ("keyboard", "pad"):
        raise HarnessError("device is keyboard or pad")
    code = text(t, "code", "") if "code" in t else "allow"
    if code not in ("allow", "deny", "ask"):
        raise HarnessError("code must be allow, deny or ask")
    mods = []
    if "mods" in t:
        if not isinstance(t["mods"], list):
            raise HarnessError("mods must be a list")
        for i, entry in enumerate(t["mods"]):
            path = "mods[%d]" % i
            if isinstance(entry, str):
                entry = {"path": entry}
            keys_known(entry, path, {"path", "options", "code", "permissions", "install", "loads"})
            if "path" not in entry:
                raise HarnessError("%s has no path" % path)
            mod = {"path": (folder / text(entry, "path", path)).resolve(), "code": code, "loads": True,
                   "install": "placed", "permissions": "allow", "options": {}}
            if "code" in entry:
                mod["code"] = text(entry, "code", path)
                # "ask" (J, consent tests): nothing answered, so the game asks (the consent question shows).
                if mod["code"] not in ("allow", "deny", "ask"):
                    raise HarnessError("%s.code must be allow, deny or ask" % path)
            if "loads" in entry:
                mod["loads"] = boolean(entry, "loads", path)
            if "install" in entry:
                mod["install"] = text(entry, "install", path)
                if mod["install"] not in ("placed", "import"):
                    raise HarnessError("%s.install must be placed or import" % path)
            if "permissions" in entry:
                mod["permissions"] = entry["permissions"]
                if not (entry["permissions"] in ("allow", "deny") or isinstance(entry["permissions"], dict)):
                    raise HarnessError("%s.permissions must be allow, deny or an object" % path)
            if "options" in entry:
                if not isinstance(entry["options"], dict):
                    raise HarnessError("%s.options must be an object" % path)
                mod["options"] = entry["options"]
            mods.append(mod)
    if bare and mods:
        raise HarnessError("a bare test has no mods")
    language = None
    if "language" in t:
        language = text(t, "language", "")
        if language not in LANGUAGES:
            raise HarnessError("language %s is not one of the game's 14" % language)
    seed = whole(t, "seed", "", 0) if "seed" in t else 0
    renderers = ["d3d12"]
    if "renderers" in t:
        if not isinstance(t["renderers"], list) or not t["renderers"]:
            raise HarnessError("renderers must be a list")
        for r in t["renderers"]:
            if r not in ("d3d12", "vulkan", "metal", "gl"):
                raise HarnessError("renderers: %s is not d3d12, vulkan, metal or gl" % json.dumps(r))
        renderers = t["renderers"]
    if "region" in t and text(t, "region", "") not in ("RMGE01", "RMGP01", "RMGJ01"):
        raise HarnessError("region must be RMGE01, RMGP01 or RMGJ01")
    limit = number(t, "limit_seconds", "") if "limit_seconds" in t else 180
    if limit <= 0:
        raise HarnessError("limit_seconds must be more than 0")
    continue_from = text(t, "continue_from", "") if "continue_from" in t else None
    faults = []
    if "faults" in t:
        if not isinstance(t["faults"], list):
            raise HarnessError("faults must be a list")
        for i, f in enumerate(t["faults"]):
            keys_known(f, "faults[%d]" % i, {"mod"})
            faults.append(text(f, "mod", "faults[%d]" % i))
    # A safe start (task 6.6): "asked" starts with --safe-start 1; "crash" leaves mods/.safe_start empty, as a crash
    # with mods on does, before the start.
    safe_start = t.get("safe_start")
    if safe_start not in (None, "asked", "crash"):
        raise HarnessError("safe_start must be \"asked\" or \"crash\"")
    hidden = []
    if "hide_game_files" in t:
        if not isinstance(t["hide_game_files"], list):
            raise HarnessError("hide_game_files must be a list")
        hidden = [hidden_path(h, "hide_game_files[%d]" % i) for i, h in enumerate(t["hide_game_files"])]
    setup = read_setup(t, bare, seed, language, classes)
    steps = read_steps(t, bare, classes)
    expect = read_expect(t, bare, steps)
    return {"folder": folder, "raw": t, "name": t["name"], "proves": t["proves"], "bare": bare, "mods": mods,
            "language": language, "seed": seed, "renderers": renderers, "region": t.get("region"), "limit": limit,
            "continue_from": continue_from, "faults": faults, "setup": setup, "steps": steps, "expect": expect,
            "has_stage": "stage" in t, "hidden": hidden, "safe_start": safe_start, "device": device}


def read_setup(t, bare, seed, language, classes):
    """The game half's setup lines, in the game's own order: the save, then the stage, then the player."""
    lines = []
    if bare:
        for key in ("save", "stage", "player", "seed", "language", "continue_from"):
            if key in t:
                raise HarnessError("a bare test has no %s" % key)
        return lines
    # User file 1 first (contract harness 3.2): created through the game's new-file path, or, continuing from an
    # earlier test, loaded from the saves that test left, as the file select does.
    lines.append("setup file 1")
    if "save" in t:
        s = t["save"]
        keys_known(s, "save", {"character", "spin", "stars", "grand_stars", "flags", "values", "from"})
        if "from" in s:
            text(s, "from", "save")
            raise HarnessError("save.from: not built yet (copying a save file in needs the save folder's layout, task 1.2)")
        if "character" in s:
            c = text(s, "character", "save")
            if c not in ("mario", "luigi"):
                raise HarnessError("save.character must be mario or luigi")
            lines.append("setup save.character %s" % c)
        if "spin" in s:
            lines.append("setup save.spin %d" % (1 if boolean(s, "spin", "save") else 0))
        if "stars" in s:
            lines.append("setup save.stars %d" % whole(s, "stars", "save", 0))
        if "grand_stars" in s:
            g = whole(s, "grand_stars", "save", 0)
            if g > 7:
                raise HarnessError("save.grand_stars is 0 to 7")
            lines.append("setup save.grand_stars %d" % g)
        if "flags" in s:
            if not isinstance(s["flags"], list):
                raise HarnessError("save.flags must be a list")
            for i, f in enumerate(s["flags"]):
                lines.append("setup save.flag %s" % plain(f, "save.flags[%d]" % i))
        if "values" in s:
            if not isinstance(s["values"], dict):
                raise HarnessError("save.values must be an object")
            for name in s["values"]:
                plain(name, "save.values key")
                lines.append("setup save.value %s %d" % (name, whole(s["values"], name, "save.values", 0)))
    lines.append("setup seed %d" % seed)
    if language is not None:
        lines.append("setup language %s" % language)
    if "stage" in t:
        st = t["stage"]
        keys_known(st, "stage", {"name", "scenario", "zone"})
        if "name" not in st or "scenario" not in st:
            raise HarnessError("stage needs name and scenario")
        if "zone" in st:
            text(st, "zone", "stage")
            raise HarnessError("stage.zone: not built yet (entering a zone, task 1.2)")
        lines.append("setup stage %s %d" % (plain(st["name"], "stage.name"), whole(st, "scenario", "stage", 0)))
    if "player" in t:
        if "stage" not in t:
            raise HarnessError("player needs stage")
        p = t["player"]
        keys_known(p, "player", {"position", "at", "form", "life"})
        if "position" in p:
            x, y, z = vec3(p, "position", "player")
            lines.append("setup player.position %r %r %r" % (float(x), float(y), float(z)))
        if "at" in p:
            at = text(p, "at", "player")
            if at.startswith("class:"):
                object_ref(at, "player.at", classes)
            elif not re.fullmatch(r"[^\s#]+#\d+", at):
                raise HarnessError("player.at is <object name>#<n> or class:<Class>#<n>")
            name, number = at.rsplit("#", 1)
            lines.append("setup player.at %s %s" % (name, number))
        if "form" in p:
            form = text(p, "form", "player")
            if form not in FORMS:
                raise HarnessError("player.form %s is not a form the game has" % form)
            lines.append("setup player.form %s" % form)
        if "life" in p:
            lines.append("setup player.life %d" % whole(p, "life", "player", 1))
    lines.append("setup done")
    return lines


def read_check(c, path, keys):
    """A fact check's operator and value (3.7), for `after` and `expect` alike."""
    ops = [k for k in c if k in FACT_OPS]
    if len(ops) != 1:
        raise HarnessError("%s needs exactly one of %s" % (path, ", ".join(sorted(FACT_OPS))))
    op = ops[0]
    value = c[op]
    within = None
    if op in ("exists", "absent") and value is not True:
        raise HarnessError("%s.%s takes true" % (path, op))
    # Revision 7 (J's R-6): `has` an object whose every key the fact's value holds with that value (an object inside
    # is checked the same way, any other value with `is`); `count` the number of times the key was written.
    if op == "has" and not isinstance(value, dict):
        raise HarnessError("%s.has takes an object" % path)
    if op == "count":
        if "count" not in keys:
            raise HarnessError("%s: count is a check of the whole run (expect), not of one fact" % path)
        if not isinstance(value, int) or isinstance(value, bool) or value < 0:
            raise HarnessError("%s.count must be a whole number of at least 0" % path)
        if "when" in c:
            raise HarnessError("%s: count counts every write; when does not go with it" % path)
    if op in ("less", "at_most", "more", "at_least"):
        number(c, op, path)
    if op == "near":
        if "within" not in c:
            raise HarnessError("%s: near needs within" % path)
        within = number(c, "within", path)
        if not (isinstance(value, (int, float)) and not isinstance(value, bool)) and not (
                isinstance(value, list) and all(isinstance(v, (int, float)) and not isinstance(v, bool) for v in value)):
            raise HarnessError("%s.near takes a number or a list of numbers" % path)
    elif "within" in c and "within" in keys:
        raise HarnessError("%s: within belongs with near" % path)
    return op, value, within


def read_steps(t, bare, classes):
    steps = t["steps"]
    if not isinstance(steps, list) or not steps:
        raise HarnessError("steps must be a list with an end step")
    out = []
    ends = 0
    pictures = set()
    actions = {"hold", "stick", "shake", "pointer", "key", "text", "settings", "picture", "call", "read", "stage", "wait",
               "end", "take_out", "copy_file", "load_file", "erase_file", "new_file"}
    for i, s in enumerate(steps):
        path = "steps[%d]" % i
        if not isinstance(s, dict):
            raise HarnessError("%s is not an object" % path)
        keys_known(s, path, actions | {"at", "after", "timeout", "frames", "signature", "args", "fact", "of", "type"}
                   | ({"wait"} if "take_out" in s else set()))
        # take_out's "wait" (true or false) is its option, not the wait action.
        kinds = [k for k in s if k in actions and not (k == "wait" and "take_out" in s)]
        if len(kinds) != 1:
            raise HarnessError("%s needs exactly one action (%s)" % (path, ", ".join(sorted(actions))))
        action = kinds[0]
        if ("at" in s) == ("after" in s):
            raise HarnessError("%s needs at or after, not both" % path)
        step = {"action": action, "at": None, "after": None, "timeout": 600}
        if "at" in s:
            step["at"] = whole(s, "at", path, 0)
            if "timeout" in s:
                raise HarnessError("%s: timeout belongs with after" % path)
        else:
            a = s["after"]
            keys_known(a, path + ".after", {"fact", "log", "is", "is_not", "less", "at_most", "more", "at_least",
                                            "near", "within", "exists", "absent", "has", "when"})
            if ("fact" in a) == ("log" in a):
                raise HarnessError("%s.after needs fact or log" % path)
            if "log" in a:
                pattern = text(a, "log", path + ".after")
                try:
                    re.compile(pattern)
                except re.error:
                    raise HarnessError("%s.after.log is not a regular expression" % path)
                if any(k in a for k in FACT_OPS) or "when" in a:
                    raise HarnessError("%s.after: a log condition takes no check" % path)
                step["after"] = ("log", pattern)
            else:
                key = fact_key(a["fact"], path + ".after.fact")
                op, value, within = read_check(a, path + ".after", {"within"})
                # Revision 8: "when": "next": the check holds on a write made after the wait began (a fact already
                # written, as the mod's "done" of an earlier stage run, does not meet it).
                if "when" in a and a["when"] != "next":
                    raise HarnessError("%s.after.when takes \"next\" (the check is made on a write after the wait began)" % path)
                step["after"] = ("fact", key, op, value, within, "when" in a)
            if "timeout" in s:
                step["timeout"] = whole(s, "timeout", path, 1)
        needs = {"hold": {"frames"}, "stick": {"frames"}, "pointer": {"frames"}, "call": {"signature"},
                 "read": {"of", "type", "fact"}}.get(action, set())
        allowed = needs | {"call": {"args", "fact"}}.get(action, set())
        for key in ("frames", "signature", "args", "fact", "of", "type"):
            if key in s and key not in allowed:
                raise HarnessError("%s: %s does not belong with %s" % (path, key, action))
        for key in needs:
            if key not in s:
                raise HarnessError("%s: %s needs %s" % (path, action, key))
        if bare and action not in ("wait", "picture", "key", "text", "settings", "end"):
            raise HarnessError("%s: a bare test has only wait, picture, key, text, settings and end steps" % path)
        v = s[action]
        if action == "hold":
            if not isinstance(v, list) or not v or not all(isinstance(b, str) and b in BUTTONS for b in v):
                raise HarnessError("%s.hold must list buttons of %s" % (path, " ".join(sorted(BUTTONS))))
            step["line"] = "hold %s %d" % ("+".join(v), whole(s, "frames", path, 1))
        elif action == "stick" or action == "pointer":
            if not (isinstance(v, list) and len(v) == 2 and all(isinstance(n, (int, float)) and not isinstance(n, bool) for n in v)):
                raise HarnessError("%s.%s must be two numbers" % (path, action))
            low = -1 if action == "stick" else 0
            if not all(low <= n <= 1 for n in v):
                raise HarnessError("%s.%s: each is %d to 1" % (path, action, low))
            step["line"] = "%s %r %r %d" % (action, float(v[0]), float(v[1]), whole(s, "frames", path, 1))
        elif action == "shake":
            step["line"] = "shake %d" % whole(s, "shake", path, 1)
        elif action == "key":
            if v not in KEYS:
                raise HarnessError("%s.key must be one of %s" % (path, " ".join(sorted(KEYS))))
            step["line"] = "key %s" % v
        elif action == "text":
            # Typed into the open text request (input.text_request) as the keyboard types; the game half reads hex.
            if not isinstance(v, str) or not v or len(v) > 256 or any(ord(c) < 0x20 or 0x7f <= ord(c) < 0xa0 for c in v):
                raise HarnessError("%s.text is 1 to 256 characters, none a control character" % path)
            step["line"] = "text %s" % v.encode("utf-8").hex()
        elif action == "settings":
            if not isinstance(v, str) or v not in SETTINGS_PAGES:
                raise HarnessError("%s.settings must name a page: %s" % (path, " ".join(sorted(SETTINGS_PAGES))))
            step["line"] = "settings %d" % SETTINGS_PAGES[v]
        elif action == "picture":
            name = plain(text(s, "picture", path), path + ".picture")
            if not re.fullmatch(r"[A-Za-z0-9_.-]+", name):
                raise HarnessError("%s.picture may hold only letters, digits, -, _ and ." % path)
            if name in pictures:
                raise HarnessError("%s: the picture %s is named twice" % (path, name))
            pictures.add(name)
            step["line"] = "picture %s" % name
            step["picture"] = name
        elif action in ("wait", "end"):
            if v is not True:
                raise HarnessError("%s.%s takes true" % (path, action))
            step["line"] = action
            ends += action == "end"
        elif action == "take_out":
            # Revision 7 (reload.md 2, G-RL4): a running mod taken out; its next step waits for takeout.<id> (the mod's
            # library closed) up to 600 frames, else setup-failed, unless "wait" is false.
            mod_id = text(s, "take_out", path)
            if not re.fullmatch(r"[a-z0-9][a-z0-9._-]*", mod_id) or len(mod_id.encode("utf-8")) > 63:
                raise HarnessError("%s.take_out must be a mod id ([a-z0-9][a-z0-9._-]*, at most 63 bytes)" % path)
            wait = s.get("wait", True)
            if not isinstance(wait, bool):
                raise HarnessError("%s.wait takes true or false" % path)
            step["line"] = "take_out %s %d" % (mod_id, 1 if wait else 0)
        elif action in ("copy_file", "load_file", "erase_file", "new_file"):
            # Revision 8 (services-save 5, kit-progress): the file select's own operations on the run's own saves, on
            # the game's save path (GameSequenceFunction); the step is answered when the save sequence has ended and
            # writes file.copied / file.loaded / file.erased / file.created.
            if action == "copy_file":
                if not (isinstance(v, list) and len(v) == 2 and all(isinstance(n, int) and not isinstance(n, bool) and 1 <= n <= 3 for n in v)
                        and v[0] != v[1]):
                    raise HarnessError("%s.copy_file is [from, to]: two different user files, each 1 to 3" % path)
                step["line"] = "game file copy %d %d" % (v[0], v[1])
            else:
                n = whole(s, action, path, 1)
                if n > 3:
                    raise HarnessError("%s.%s is a user file, 1 to 3" % (path, action))
                step["line"] = "game file %s %d" % (action[:-len("_file")], n)
        elif action == "call":
            name = plain(text(s, "call", path), path + ".call")
            sig = text(s, "signature", path)
            if "\t" in sig or "\n" in sig:
                raise HarnessError("%s.signature has a tab or line end" % path)
            params = signature_params(sig, path)
            args = s.get("args", [])
            if not isinstance(args, list) or len(args) != len(params):
                raise HarnessError("%s: args must list %d argument(s) for %s" % (path, len(params), sig))
            words = [call_arg(a, p, "%s.args[%d]" % (path, j), classes) for j, (a, p) in enumerate(zip(args, params))]
            fact = plain(text(s, "fact", path), path + ".fact") if "fact" in s else "-"
            rest = "\t".join([name, sig] + words)
            if len(rest.encode("utf-8")) > LINE_BYTES or len(params) + 2 > LINE_WORDS:
                raise HarnessError("%s: the call is longer than the game half takes (%d bytes, %d arguments)" % (path, LINE_BYTES, LINE_WORDS - 2))
            step["line"] = "game call %s %s" % (fact, rest)
        elif action == "read":
            field = text(s, "read", path)
            if not re.fullmatch(r"[A-Za-z_][A-Za-z0-9_]*\.[A-Za-z_][A-Za-z0-9_]*", field):
                raise HarnessError("%s.read is Class.field" % path)
            of = text(s, "of", path)
            if of != "player":
                object_ref(of, path + ".of", classes)
            ty = text(s, "type", path)
            if ty not in READ_TYPES:
                raise HarnessError("%s.type %s: the harness reads %s" % (path, ty, ", ".join(sorted(READ_TYPES))))
            step["line"] = "game read %s %s %s %s" % (plain(s["fact"], path + ".fact"), of, field, ty)
        elif action == "stage":
            st = v
            keys_known(st, path + ".stage", {"name", "scenario"})
            if "name" not in st or "scenario" not in st:
                raise HarnessError("%s.stage needs name and scenario" % path)
            step["line"] = "game stage %s %d" % (plain(st["name"], path + ".stage.name"), whole(st, "scenario", path + ".stage", 0))
        out.append(step)
    if ends != 1:
        raise HarnessError("the test has %d end steps; it needs exactly one" % ends)
    return out


def signature_params(sig, path):
    m = re.fullmatch(r"\s*(.+?)\s*\((.*)\)\s*(const)?\s*", sig)
    if not m:
        raise HarnessError("%s.signature is not a signature as the game writes it" % path)
    result = m.group(1).strip()
    if result not in RESULT_TYPES and not result.endswith("*"):
        raise HarnessError("%s.signature: the harness cannot take a result of type %s" % (path, result))
    inner = m.group(2).strip()
    params = [p.strip() for p in inner.split(",")] if inner else []
    if len(params) > LINE_WORDS:
        raise HarnessError("%s.signature: more than %d parameters" % (path, LINE_WORDS))
    for p in params:
        if p not in CALL_TYPES:
            raise HarnessError("%s.signature: the harness cannot pass a parameter of type %s" % (path, p))
    return params


def call_arg(value, param, path, classes):
    if param in ("f32", "float"):
        if isinstance(value, bool) or not isinstance(value, (int, float)):
            raise HarnessError("%s must be a number for %s" % (path, param))
        return repr(float(value))
    if param in ("f64", "double"):
        if isinstance(value, bool) or not isinstance(value, (int, float)):
            raise HarnessError("%s must be a number for %s" % (path, param))
        return repr(float(value))
    if param in ("s32", "int", "u32", "u16", "s16", "u8", "s8"):
        lo, hi = {"s32": (-2**31, 2**31 - 1), "int": (-2**31, 2**31 - 1), "u32": (0, 2**32 - 1), "u16": (0, 65535),
                  "s16": (-32768, 32767), "u8": (0, 255), "s8": (-128, 127)}[param]
        if isinstance(value, bool) or not isinstance(value, int) or not lo <= value <= hi:
            raise HarnessError("%s must be a whole number from %d to %d for %s" % (path, lo, hi, param))
        return str(value)
    if param == "bool":
        if not isinstance(value, bool):
            raise HarnessError("%s must be true or false" % path)
        return "true" if value else "false"
    if param == "const char *":
        if not isinstance(value, str) or any(c in value for c in "\t\r\n"):
            raise HarnessError("%s must be a string with no tab or line end" % path)
        return value
    if param in VEC_TYPES:
        # Revision 8: a live actor's place, `object:<name>#<n>` or `class:<Class>#<n>` (its position as the game holds it).
        if isinstance(value, str) and value != "player":
            return object_ref(value, path, classes)
        if not (isinstance(value, list) and len(value) == 3 and all(isinstance(v, (int, float)) and not isinstance(v, bool) for v in value)):
            raise HarnessError("%s must be [x, y, z]" % path)
        return " ".join(repr(float(v)) for v in value)
    if param in OBJECT_TYPES:
        return value if value == "player" else object_ref(value, path, classes)
    raise HarnessError("%s: the harness cannot pass a parameter of type %s" % (path, param))


def read_expect(t, bare, steps):
    expect = t["expect"]
    if not isinstance(expect, list):
        raise HarnessError("expect must be a list")
    pictures = {s["picture"] for s in steps if "picture" in s}
    out = []
    for i, c in enumerate(expect):
        path = "expect[%d]" % i
        if not isinstance(c, dict):
            raise HarnessError("%s is not an object" % path)
        if "fact" in c:
            keys_known(c, path, {"fact", "when", "frames", "within", "matching", "field"} | FACT_OPS)
            if bare:
                raise HarnessError("%s: a bare test checks only the log, pictures and sound" % path)
            op, value, within = read_check(c, path, {"within", "count"})
            # `matching` (J): only the writes whose value has it (as `has` reads) are checked, so `count`, `absent`
            # and the rest speak of one mod's or one word's writes of a key many write ("modlog", "fx.*").
            matching = None
            if "matching" in c:
                matching = c["matching"]
                if not isinstance(matching, dict) or not matching:
                    raise HarnessError("%s.matching takes an object with at least one key" % path)
            # `field` (J): the check reads one member of each write's object value, a dotted path ("particles",
            # "at.x"); a write without it reads as no value, which no comparison takes.
            field = None
            if "field" in c:
                field = c["field"]
                if not isinstance(field, str) or not re.fullmatch(r"[A-Za-z_][A-Za-z0-9_]*(\.[A-Za-z_][A-Za-z0-9_]*)*", field):
                    raise HarnessError("%s.field is a dotted path of names" % path)
                if op in ("absent", "exists", "count"):
                    raise HarnessError("%s.field goes with a comparison, not %s" % (path, op))
            when = text(c, "when", path) if "when" in c else "last"
            if when not in ("last", "first", "any", "all"):
                raise HarnessError("%s.when must be last, first, any or all" % path)
            frames = None
            if "frames" in c:
                frames = c["frames"]
                if not (isinstance(frames, list) and len(frames) == 2 and all(isinstance(f, int) and not isinstance(f, bool) for f in frames)):
                    raise HarnessError("%s.frames must be [first, last]" % path)
            out.append({"kind": "fact", "key": fact_key(c["fact"], path + ".fact"), "op": op, "value": value,
                        "within": within, "when": when, "frames": frames, "matching": matching, "field": field,
                        "source": c})
        elif "log" in c or "no_log" in c:
            keys_known(c, path, {"log", "no_log", "times"})
            if ("log" in c) == ("no_log" in c):
                raise HarnessError("%s: log and no_log are separate checks" % path)
            key = "log" if "log" in c else "no_log"
            pattern = text(c, key, path)
            times = c.get("times")
            if times is not None and (key != "log" or not isinstance(times, int) or isinstance(times, bool) or times < 0):
                raise HarnessError("%s.times must be a whole number of at least 0, on a log check" % path)
            try:
                re.compile(pattern)
            except re.error:
                raise HarnessError("%s.%s is not a regular expression" % (path, key))
            out.append({"kind": key, "pattern": pattern, "times": times, "source": c})
        elif "picture" in c:
            keys_known(c, path, {"picture", "like", "unlike", "within", "beyond", "box", "mean_rgb"})
            name = text(c, "picture", path)
            if name not in pictures:
                raise HarnessError("%s: no step takes a picture named %s" % (path, name))
            box = None
            if "box" in c:
                box = c["box"]
                if not (isinstance(box, list) and len(box) == 4 and all(isinstance(v, int) and not isinstance(v, bool) and v >= 0 for v in box)):
                    raise HarnessError("%s.box must be [x, y, w, h] in pixels" % path)
            if "mean_rgb" in c:
                rgb = c["mean_rgb"]
                if box is None or "within" not in c or any(k in c for k in ("like", "unlike", "beyond")):
                    raise HarnessError("%s: mean_rgb takes box and within, nothing else" % path)
                if not (isinstance(rgb, list) and len(rgb) == 3 and all(isinstance(v, (int, float)) and not isinstance(v, bool) for v in rgb)):
                    raise HarnessError("%s.mean_rgb must be three numbers" % path)
                out.append({"kind": "mean_rgb", "picture": name, "box": box, "rgb": rgb, "within": number(c, "within", path), "source": c})
                continue
            if ("like" in c) == ("unlike" in c):
                raise HarnessError("%s needs like or unlike" % path)
            mode = "like" if "like" in c else "unlike"
            limit_key = "within" if mode == "like" else "beyond"
            if limit_key not in c or not isinstance(c[limit_key], dict):
                raise HarnessError("%s: %s needs %s: {\"rmse\": r}" % (path, mode, limit_key))
            keys_known(c[limit_key], path + "." + limit_key, {"rmse"})
            if "rmse" not in c[limit_key]:
                raise HarnessError("%s.%s needs rmse" % (path, limit_key))
            out.append({"kind": mode, "picture": name, "ref": text(c, mode, path), "rmse": number(c[limit_key], "rmse", path + "." + limit_key),
                        "box": box, "source": c})
        elif "sound" in c:
            keys_known(c, path, {"sound", "rms"})
            s, r = c["sound"], c.get("rms")
            if not (isinstance(s, list) and len(s) == 2 and all(isinstance(v, int) and not isinstance(v, bool) for v in s)):
                raise HarnessError("%s.sound must be [first_frame, last_frame]" % path)
            if not (isinstance(r, list) and len(r) == 2 and all(isinstance(v, (int, float)) and not isinstance(v, bool) for v in r)):
                raise HarnessError("%s.rms must be [low, high] in dBFS" % path)
            out.append({"kind": "sound", "frames": s, "rms": r, "source": c})
        else:
            raise HarnessError("%s is not a check this harness knows (fact, log, no_log, picture, sound)" % path)
    return out


# --- Pictures (PPM in, PNG out and in) ---------------------------------------------------------------------------------

def read_ppm(path):
    data = pathlib.Path(path).read_bytes()
    fields, at = [], 0
    while len(fields) < 4:
        while data[at:at + 1].isspace():
            at += 1
        if data[at:at + 1] == b"#":
            while data[at:at + 1] not in (b"\n", b""):
                at += 1
            continue
        start = at
        while not data[at:at + 1].isspace():
            at += 1
        fields.append(data[start:at])
    at += 1
    if fields[0] != b"P6" or int(fields[3]) != 255:
        raise ValueError("not an 8-bit P6 picture")
    w, h = int(fields[1]), int(fields[2])
    return w, h, data[at:at + w * h * 3]


def write_png(path, w, h, rgb):
    raw = b"".join(b"\0" + rgb[y * w * 3:(y + 1) * w * 3] for y in range(h))

    def chunk(kind, body):
        return struct.pack(">I", len(body)) + kind + body + struct.pack(">I", zlib.crc32(kind + body) & 0xFFFFFFFF)
    png = b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 2, 0, 0, 0))
    png += chunk(b"IDAT", zlib.compress(raw, 6)) + chunk(b"IEND", b"")
    pathlib.Path(path).write_bytes(png)


def read_png(path):
    """An 8-bit RGB or RGBA PNG, not interlaced, as (width, height, RGB bytes)."""
    data = pathlib.Path(path).read_bytes()
    if data[:8] != b"\x89PNG\r\n\x1a\n":
        raise ValueError("not a PNG")
    at, idat, w = 8, b"", None
    while at < len(data):
        length, kind = struct.unpack(">I4s", data[at:at + 8])
        body = data[at + 8:at + 8 + length]
        at += 12 + length
        if kind == b"IHDR":
            w, h, depth, colour, _, _, interlace = struct.unpack(">IIBBBBB", body)
            if depth != 8 or colour not in (2, 6) or interlace != 0:
                raise ValueError("only 8-bit RGB or RGBA, not interlaced")
            bpp = 3 if colour == 2 else 4
        elif kind == b"IDAT":
            idat += body
        elif kind == b"IEND":
            break
    if w is None:
        raise ValueError("no IHDR")
    raw = zlib.decompress(idat)
    stride = w * bpp
    out = bytearray(h * w * 3)
    prev = bytearray(stride)
    for y in range(h):
        f = raw[y * (stride + 1)]
        line = bytearray(raw[y * (stride + 1) + 1:(y + 1) * (stride + 1)])
        for x in range(stride):
            a = line[x - bpp] if x >= bpp else 0
            b = prev[x]
            c = prev[x - bpp] if x >= bpp else 0
            if f == 1:
                line[x] = (line[x] + a) & 255
            elif f == 2:
                line[x] = (line[x] + b) & 255
            elif f == 3:
                line[x] = (line[x] + (a + b) // 2) & 255
            elif f == 4:
                p = a + b - c
                pa, pb, pc = abs(p - a), abs(p - b), abs(p - c)
                line[x] = (line[x] + (a if pa <= pb and pa <= pc else b if pb <= pc else c)) & 255
            elif f != 0:
                raise ValueError("bad filter")
        for x in range(w):
            out[(y * w + x) * 3:(y * w + x) * 3 + 3] = line[x * bpp:x * bpp + 3]
        prev = line
    return w, h, bytes(out)


def rmse(a, b, w, box):
    x0, y0, bw, bh = box if box else (0, 0, w, len(a) // (3 * w))
    total, count = 0, 0
    for y in range(y0, y0 + bh):
        row = (y * w + x0) * 3
        pa, pb = a[row:row + bw * 3], b[row:row + bw * 3]
        total += sum((p - q) * (p - q) for p, q in zip(pa, pb))
        count += len(pa)
    return math.sqrt(total / count) if count else 0.0


# --- Installing mods ------------------------------------------------------------------------------------------------

OFFSET, PRIME, MASK = 14695981039346656037, 1099511628211, 0xFFFFFFFFFFFFFFFF


def fnv(h, data):
    for b in data:
        h = ((h ^ b) * PRIME) & MASK
    return h


def code_token(library):
    """The code consent token the game keeps in mods.ini (runtime/src/mods.cpp plugin_token), for a library's folder."""
    h = OFFSET
    for path in sorted(p for p in library.parent.iterdir() if p.is_file()):
        h = fnv(h, path.name.encode("utf-8") + b"\0")
        h = fnv(h, path.read_bytes() + b"\0")
    return "%016x" % fnv(OFFSET, ("%016x" % h).encode())


def permission_digest(declared):
    """consent.md 1.2 (runtime mod_consent.cpp permission_digest): FNV-1a 64 over the name and each host, lowercased
    (A-Z), sorted in byte order, each followed by a 0 byte; `why` is not in it."""
    h = fnv(OFFSET, declared["name"].encode("utf-8") + b"\0")
    hosts = ["".join(chr(ord(c) + 32) if "A" <= c <= "Z" else c for c in host) for host in declared.get("hosts", [])]
    for host in sorted(hosts, key=lambda text: text.encode("utf-8")):
        h = fnv(h, host.encode("utf-8") + b"\0")
    # net.roots (consent 1.2): "roots" and each file as written, sorted in byte order, when there are any.
    roots = declared.get("roots", [])
    if roots:
        h = fnv(h, b"roots\0")
        for root in sorted(roots, key=lambda text: text.encode("utf-8")):
            h = fnv(h, root.encode("utf-8") + b"\0")
    return "%016x" % h


def install(settings, mod_folder, code_allowed, order, kept=False, permissions="allow", broken_ok=False):
    """A mod folder holding luma.json (the base's form, as examples/luma-hello) into the run's mods, on, in order.
    kept: the mods an earlier test left (continue_from) stay as they are, with their data; only new ids are added.
    permissions: "allow", "deny" or {name: "allow"|"deny"}: each declared permission's answer, as the consent screen
    records a player's (consent.md 1.2; a name the object leaves out is allowed).
    broken_ok: a mod the test expects not to load (loads false) may have a luma.json that is not JSON on purpose: it is
    placed as it is, under its folder's name, as the game names a mod whose manifest cannot be read (mod-log 1)."""
    try:
        manifest = load_json(mod_folder / "luma.json")
    except HarnessError:
        if not broken_ok:
            raise
        target = settings / "mods" / mod_folder.name
        if target.exists():
            if kept:
                return mod_folder.name
            raise HarnessError("two mods have the id %s" % mod_folder.name)
        shutil.copytree(mod_folder, target)
        with (settings / "mods.ini").open("a", encoding="utf-8") as out:
            out.write("[%s]\nenabled=1\norder=%d\n" % (mod_folder.name, order))
        return mod_folder.name
    mod_id = manifest.get("id")
    if not isinstance(mod_id, str) or not mod_id:
        raise HarnessError("%s/luma.json has no id" % mod_folder)
    # The previous runtime's form would install with no code and fail far from here: named at once instead.
    if "plugins" in manifest:
        raise HarnessError("%s/luma.json has \"plugins\": a mod's code is code/<platform>/mod.<ending> with "
                           "\"code\": {\"platforms\": [...]} (board/contracts/manifest-one-form.md)" % mod_folder)
    if not isinstance(manifest.get("format"), str):
        raise HarnessError("%s/luma.json: format must be \"1.0\", a string (board/contracts/manifest-one-form.md)"
                           % mod_folder)
    libraries = []
    # luma-format 6: code/<platform>/mod.<ending> by `code.platforms` alone. A mod whose code is for other platforms
    # only has no library here: the game says it has no version for this device.
    code = manifest.get("code")
    platforms = code.get("platforms") if isinstance(code, dict) else None
    if isinstance(platforms, list) and HOST["platform"] in platforms:
        rel = HOST["code"]
        if not (mod_folder / rel).is_file():
            raise HarnessError("%s: the code %s is missing (building is not the runner's job)" % (mod_folder, rel))
        libraries.append(rel)
    target = settings / "mods" / mod_id
    if target.exists():
        if kept:
            return mod_id
        raise HarnessError("two mods have the id %s" % mod_id)
    shutil.copytree(mod_folder, target)
    ini = settings / "mods.ini"
    lines = "[%s]\nenabled=1\norder=%d\n" % (mod_id, order)
    if code_allowed and libraries:
        lines += "code_ok=%s\n" % code_token(target / libraries[0])
    # The player's answers as the consent screen records them (consent.md 1.2): every declared permission answered.
    declared = [p for p in manifest.get("permissions", []) if isinstance(p, dict) and isinstance(p.get("name"), str)]
    names = {p["name"] for p in declared}
    if isinstance(permissions, dict):
        for name, answer in permissions.items():
            if name not in names:
                raise HarnessError("%s: permissions names %s, which the mod does not ask for" % (mod_folder, name))
            if answer not in ("allow", "deny"):
                raise HarnessError("%s: the answer for %s must be allow or deny" % (mod_folder, name))
    for p in declared if permissions != "ask" else []:
        wanted = permissions if isinstance(permissions, str) else permissions.get(p["name"], "allow")
        lines += "permission.%s=%s %s\n" % (p["name"], "yes" if wanted == "allow" else "no", permission_digest(p))
    with ini.open("a", encoding="utf-8") as out:
        out.write(lines)
    return mod_id


def install_game_half(settings, bin_folder):
    library = bin_folder / HOST["half"]
    if not library.is_file():
        raise HarnessError("the bin folder has no %s (the game half)" % HOST["half"])
    if (settings / "mods" / GAME_HALF_ID).exists():
        return  # kept from the test this one continues
    folder = settings / "staging-game-half"
    (folder / "code" / HOST["platform"]).mkdir(parents=True)
    shutil.copyfile(library, folder / HOST["code"])
    (folder / "luma.json").write_text(json.dumps({
        "format": "1.0", "id": GAME_HALF_ID, "name": GAME_HALF_ID, "summary": "The test runner's game half.",
        "authors": ["keepvaibin"], "version": RUNNER_VERSION + ".0", "game": {"id": "RMG"},
        "code": {"platforms": [HOST["platform"]]}}, indent=2), encoding="utf-8")
    install(settings, folder, True, 0)
    shutil.rmtree(folder)


# --- One run ----------------------------------------------------------------------------------------------------------

def sha256(path):
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for block in iter(lambda: f.read(1 << 20), b""):
            h.update(block)
    return h.hexdigest()


def plan_text(test, out):
    lines = ["harness 1", "out %s" % out, "bare %d" % (1 if test["bare"] else 0)]
    if not test["has_stage"]:
        lines.append("clock title")
    if test.get("device", "keyboard") == "pad":
        lines.append("device pad")
    lines += test["setup"]
    for s in test["steps"]:
        if s["at"] is not None:
            lines.append("step %d %s" % (s["at"], s["line"]))
        else:
            lines.append("step after %s" % s["line"])
            a = s["after"]
            if a[0] == "log":
                lines.append("cond log %s" % a[1])
            else:
                _, key, op, value, within, fresh = a
                lines.append("cond %s %s %s %s %s" % ("next" if fresh else "fact", key, op, "-" if within is None else repr(float(within)),
                                                        json.dumps(value if op not in ("exists", "absent") else True,
                                                                   ensure_ascii=False, separators=(",", ":"))))
            lines.append("timeout %d" % s["timeout"])
    for line in lines:
        game = line[len("setup "):] if line.startswith("setup ") else None
        if game is None and line.startswith("step ") and " game " in line:
            game = line.split(" game ", 1)[1]
        if game is None:
            continue
        given = line if line.startswith("setup ") else game
        if len(given.encode("utf-8")) > LINE_BYTES or (not game.startswith("call ") and len(given.split(" ")) > LINE_WORDS):
            raise HarnessError("the plan line %r is longer than the game half takes (%d bytes, %d words)" % (given[:60], LINE_BYTES, LINE_WORDS))
    return "\n".join(lines) + "\n"


def run_one(test, renderer, args, run_folder, previous):
    """Starts the game for one test and renderer; returns what the checks need."""
    if renderer not in EXES:
        raise HarnessError("no adapter for the renderer %s on this platform" % renderer)
    if platform.system() not in HOSTS:
        raise HarnessError("no adapter for %s" % platform.system())
    if test["region"] is not None:
        raise HarnessError("region: not built yet (one game per bin folder; the bin names its region in 0.8)")
    for m in test["mods"]:
        if m["options"]:
            raise HarnessError("mod options: not built yet (the Mods screen's options, task 6.3)")
        if m["path"].suffix == ".luma":
            raise HarnessError("a .luma file: not readable before the luma-format reader (task 3.1); give the mod's folder")
    if test["language"] is not None and LANGUAGES[test["language"]] is None:
        raise HarnessError("language %s: not built yet (the language packs' setting, task 3.8)" % test["language"])
    bin_folder = pathlib.Path(args.bin).resolve()
    exe = bin_folder / EXES[renderer]
    # nebula_import.dll gives refusals their words; a run without it would prove only codes (run 17), so it is required.
    for need in (exe, bin_folder / HOST["game"], bin_folder / HOST["extension"], bin_folder / "pak.path",
                 bin_folder / HOST["importer"]):
        if not need.is_file():
            raise HarnessError("the bin folder has no %s" % need.name)
    # The pak must be of this tree's store version (the host refuses another at its start: "need importing again").
    pak = pathlib.Path(bin_folder.joinpath("pak.path").read_text(encoding="utf-8").strip())
    wanted = int((TREE / "runtime" / "include" / "galaxy" / "store_version.inc").read_text(encoding="utf-8").strip())
    try:
        with open(pak, "rb") as f:
            head = f.read(12)
    except OSError as error:
        raise HarnessError("pak.path names %s, which does not open: %s" % (pak, error))
    if head[:8] != b"NBSTORE" + bytes(1) or int.from_bytes(head[8:12], "little") != wanted:
        raise HarnessError("pak.path names %s, store version %s; this tree's store_version.inc is %d: import with this "
                           "tree's nebula-import or name another pak" % (pak, int.from_bytes(head[8:12], "little") if head[:8] == b"NBSTORE" + bytes(1) else "none (not a store)", wanted))
    if run_folder.exists():
        shutil.rmtree(run_folder)
    for sub in ("bin", "pictures", "saves", "settings", "appdata"):
        (run_folder / sub).mkdir(parents=True)
    settings = run_folder / "settings"
    if previous is not None:
        for sub in ("saves", "settings"):
            shutil.rmtree(run_folder / sub)
            shutil.copytree(previous / sub, run_folder / sub)
    frozen = {}
    # The plain module when the bin folder has it (plan 1.7): without it the game loads the rows module and says so.
    plain = (HOST["plain"],) if (bin_folder / HOST["plain"]).is_file() else ()
    for name in (exe.name, HOST["game"]) + plain + (HOST["extension"],):
        shutil.copy2(bin_folder / name, run_folder / "bin" / name)  # copy2: the program keeps its mode on Linux
        frozen[name] = sha256(run_folder / "bin" / name)
    # The game's placement facts, which the build writes beside the module (native/CMakeLists.txt, placement.py): an
    # installed placement patch's arguments by name need them (crates/nebula-import cook/placement_args.rs).
    if (bin_folder / "smg_game.luma-placement.json").is_file():
        shutil.copyfile(bin_folder / "smg_game.luma-placement.json", run_folder / "bin" / "smg_game.luma-placement.json")
    if (bin_folder / "dxc").is_dir():
        shutil.copytree(bin_folder / "dxc", run_folder / "bin" / "dxc")
    if (bin_folder / HOST["importer_exe"]).is_file():
        shutil.copy2(bin_folder / HOST["importer_exe"], run_folder / "bin" / HOST["importer_exe"])
    # The game loads the importer's library from its own folder (mod_plugins.cpp validate_message): a run without it
    # gets refusals as codes, so it is frozen with the rest.
    shutil.copyfile(bin_folder / HOST["importer"], run_folder / "bin" / HOST["importer"])
    frozen[HOST["importer"]] = sha256(run_folder / "bin" / HOST["importer"])
    # R6-4: the test's hidden files are left out of the run's own copy (the bin folder itself is never touched).
    for hidden in test["hidden"]:
        if not (bin_folder / hidden).is_file():
            raise SetupFailed("hide_game_files: the game's folder has no %s" % hidden)
        copy = run_folder / "bin" / hidden
        if copy.is_file():
            copy.unlink()
        frozen.pop(hidden, None)
    mod_ids = []
    if not test["bare"]:
        frozen[HOST["half"]] = sha256(bin_folder / HOST["half"])
        install_game_half(settings, bin_folder)
    imports = []
    for i, m in enumerate(test["mods"]):
        if not (m["path"] / "luma.json").is_file():
            raise HarnessError("the mod %s has no luma.json" % m["path"])
        if m["install"] == "import":
            # Not placed: the game imports it as "Open with" starts it (--import, task 6.1), through the validator;
            # each line of its result page is the fact import.<file name> (4).
            if m["loads"]:
                raise HarnessError("the imported mod %s: an import adds it for the next start, so it takes loads false"
                                   % m["path"])
            imports += ["--import", str(m["path"].resolve())]
            continue
        mod_id = install(settings, m["path"], m["code"] == "allow", i + 1, kept=previous is not None,
                         permissions="ask" if m["code"] == "ask" else m["permissions"], broken_ok=not m["loads"])
        mod_ids.append((mod_id, m))
        for lib in sorted((settings / "mods" / mod_id).rglob("*" + HOST["ending"])):
            frozen["mods/%s/%s" % (mod_id, lib.relative_to(settings / "mods" / mod_id).as_posix())] = sha256(lib)
    if test["safe_start"] == "asked":
        imports += ["--safe-start", "1"]
    elif test["safe_start"] == "crash":
        (settings / "mods").mkdir(parents=True, exist_ok=True)
        (settings / "mods" / ".safe_start").write_bytes(b"")
    assignments = ["GALAXY_WINDOW_WIDTH=1280", "GALAXY_WINDOW_HEIGHT=720"]
    if test["language"] is not None:
        assignments.append("GALAXY_LANGUAGE=%s" % LANGUAGES[test["language"]])
    subprocess.run([sys.executable, "-I", str(TREE / "tools" / "verify" / "settings_args.py"), str(settings)] + assignments,
                   check=True, capture_output=True)
    plan = run_folder / "plan.txt"
    plan.write_text(plan_text(test, run_folder.as_posix()), encoding="utf-8", newline="\n")
    pak = (bin_folder / "pak.path").read_text(encoding="utf-8").strip()
    env = dict(os.environ)
    # The test extension's data guard (tools/test_driver/extension/data_guard.cpp) declines to start unless the run
    # names its own work folder and every private folder lies inside it, away from the player's data.
    env.update({
        "NEBULA_WORK": str(run_folder),
        "NEBULA_SETTINGS": str(settings),
        "NEBULA_SAVES": str(run_folder / "saves"),
        "NEBULA_APPDATA": str(run_folder / "appdata"),
        "GALAXY_RENDER_CONFIG_PATH": str(settings / "config.ini"),
        "GALAXY_RUNTIME_SETTINGS_PATH": str(settings / "runtime_settings.ini"),
        "LOCALAPPDATA": str(run_folder / "appdata"),
        "GALAXY_CRASH_DUMP_DIR": str(run_folder),
        "GALAXY_AUDIO_WAV_DUMP_ONLY": "1",
        "GALAXY_AUDIO_WAV_DUMP": str(run_folder / "audio.wav"),
        "GALAXY_AUDIO_WAV_DUMP_SECONDS": str(int(math.ceil(test["limit"]))),
        "LUMA_HARNESS_PLAN": str(plan),
    })
    if platform.system() == "Linux":
        # The importer's library is opened by name (dlopen "libnebula_import.so"), SDL's beside the game.
        env["LD_LIBRARY_PATH"] = os.pathsep.join([str(run_folder / "bin")] + [p for p in [os.environ.get("LD_LIBRARY_PATH")] if p])
    for name in ("GALAXY_PRESENT_SNAPSHOT_DIR", "GALAXY_INPUT_AUTOPRESS_SCRIPT", "GALAXY_TEST_DETERMINISTIC"):
        env.pop(name, None)
    command = [str(run_folder / "bin" / exe.name), "--pak", pak, "--saves", str(run_folder / "saves"),
               "--seconds", str(int(math.ceil(test["limit"]))), "--settings", str(settings),
               "--crash-dumps", str(run_folder), "--extension", str(run_folder / "bin" / HOST["extension"])] + imports
    hidden = {}
    if platform.system() == "Windows":
        startup = subprocess.STARTUPINFO()
        startup.dwFlags |= subprocess.STARTF_USESHOWWINDOW
        startup.wShowWindow = subprocess.SW_HIDE
        hidden["startupinfo"] = startup
    began = time.time()
    with (run_folder / "run.log").open("wb") as log:
        try:
            code = subprocess.call(command, env=env, stdout=log, stderr=subprocess.STDOUT, timeout=test["limit"] + 60,
                                   **hidden)
        except subprocess.TimeoutExpired:
            code = None
    seconds = time.time() - began
    return {"code": code, "seconds": seconds, "frozen": frozen, "mods": mod_ids, "exe": exe.name}


def read_facts(run_folder):
    facts = []
    path = run_folder / "facts.jsonl"
    if not path.is_file():
        return facts
    for n, line in enumerate(path.read_text(encoding="utf-8").splitlines(), 1):
        if not line.strip():
            continue
        try:
            facts.append(json.loads(line))
        except ValueError:
            facts.append({"frame": -1, "key": "harness.bad_fact", "value": "facts.jsonl line %d is not JSON" % n})
    return facts


def same_value(a, b):
    if isinstance(a, bool) or isinstance(b, bool):
        return a is b
    if isinstance(a, (int, float)) and isinstance(b, (int, float)):
        return a == b
    if isinstance(a, list) and isinstance(b, list):
        return len(a) == len(b) and all(same_value(x, y) for x, y in zip(a, b))
    return a == b


def has_value(actual, value):
    """Revision 7's `has`: every key of `value` is in `actual` with the same value (an object, recursively)."""
    if not isinstance(actual, dict):
        return False
    for key, want in value.items():
        if key not in actual:
            return False
        if isinstance(want, dict):
            if not has_value(actual[key], want):
                return False
        elif not same_value(actual[key], want):
            return False
    return True


NO_MEMBER = object()


def member(value, field):
    """The member a dotted `field` names in an object value, or NO_MEMBER."""
    for name in field.split("."):
        if not isinstance(value, dict) or name not in value:
            return NO_MEMBER
        value = value[name]
    return value


def check_value(actual, op, value, within):
    if op == "is":
        return same_value(actual, value)
    if op == "has":
        return has_value(actual, value)
    if op == "is_not":
        return not same_value(actual, value)
    if op == "near":
        if isinstance(actual, (int, float)) and not isinstance(actual, bool) and isinstance(value, (int, float)):
            return abs(actual - value) <= within
        if isinstance(actual, list) and isinstance(value, list) and len(actual) == len(value) and all(
                isinstance(v, (int, float)) and not isinstance(v, bool) for v in actual):
            return math.sqrt(sum((p - q) ** 2 for p, q in zip(actual, value))) <= within
        return False
    if isinstance(actual, bool) or not isinstance(actual, (int, float)):
        return False
    return {"less": actual < value, "at_most": actual <= value, "more": actual > value, "at_least": actual >= value}[op]


# The host's own crash line (native/host/host_main.cpp log_line("crash:", ...)): its time, then the tag. A mod or a
# fact that writes the word elsewhere is not a crash.
CRASH_LINE = re.compile(r"^\[\s*\d+\.\d{3}\] crash: ")


def evaluate(test, renderer, run_folder, ran):
    """The verdict and every check, from the run's files."""
    facts = read_facts(run_folder)
    log_text = (run_folder / "run.log").read_text(encoding="utf-8", errors="replace") if (run_folder / "run.log").exists() else ""
    log_lines = log_text.splitlines()
    by_key = {}
    for f in facts:
        by_key.setdefault(f.get("key"), []).append(f)
    checks = []

    def add(name, ok, actual):
        checks.append({"check": name, "result": "pass" if ok else "fail", "actual": actual})
        return ok

    dumps = sorted(p.name for p in run_folder.glob("*.dmp"))
    stop = by_key.get("harness.stop", [])
    end = by_key.get("harness.end", [])
    verdict, reason = None, None
    declined = [line for line in log_lines if "fatal: --extension" in line or "extension: test extension:" in line]
    # The host could not open the bin folder's game.pak (host_main.cpp open_pak: another store version, or unreadable).
    unopened = [line for line in log_lines if "import: The game files need importing again" in line]
    if unopened and not dumps:
        verdict = "harness-error"
        reason = "the bin folder's game.pak did not open (another store version than this build's store_version.inc, or unreadable): %s" % unopened[0]
    elif declined and not dumps:
        # The extension refused the run's folders: the runner's fault, not the game's.
        verdict = "harness-error"
        reason = "the test extension declined the run: %s" % " | ".join(declined)
    elif dumps or any(CRASH_LINE.match(line) for line in log_lines):
        verdict = "crashed"
        reason = "dump %s; last lines: %s" % (", ".join(dumps) or "none", " | ".join(log_lines[-5:]))
    elif stop:
        verdict = stop[-1]["value"].get("verdict", "fail")
        reason = stop[-1]["value"].get("reason", "")
    elif not end:
        verdict = "timeout"
        reason = "the run did not reach its end step within limit_seconds (%s s)" % test["limit"]
    # Always checked (3.7).
    end_step = next(s for s in test["steps"] if s["action"] == "end")
    if end:
        frame = end[-1]["value"].get("frame")
        add("the run ended at its end step", end_step["at"] is None or frame == end_step["at"], frame)
    add("no crash dump", not dumps, dumps)
    add("the game quit by itself with exit 0", ran.get("code") == 0, ran.get("code"))
    # The game says this once when it cannot give a refusal in words (mod_plugins.cpp validate_message): the importer's
    # library did not load or has no luma_validate_message. Every refusal of the run is then codes only.
    coded = [line for line in log_lines if "refusals are given as codes" in line]
    add("refusals are given in words", not coded, coded)
    for bad in by_key.get("harness.bad_fact", []):
        add("every fact is well formed (JSON, a key a mod may log)", False, bad["value"])
    if not test["bare"]:
        loaded = by_key.get("mod.%s.loaded" % GAME_HALF_ID, [])
        add("the game half loaded", bool(loaded) and loaded[-1]["value"] is True, loaded[-1]["value"] if loaded else None)
        add("every setup item verified", bool(by_key.get("setup.done")),
            [f["key"] for f in facts if f.get("key", "").startswith("setup.")])
    for mod_id, m in ran["mods"]:
        loaded = by_key.get("mod.%s.loaded" % mod_id, [])
        value = loaded[-1]["value"] if loaded else None
        if m["loads"]:
            add("the mod %s loaded" % mod_id, value is True, value)
    guards = [f for f in facts if f.get("key", "").startswith("guard.")]
    for g in guards:
        mod = g["key"][len("guard."):]
        add("guard event in %s is listed in faults" % mod, mod in test["faults"], g["value"])
    # The test's own checks.
    for c in test["expect"]:
        if c["kind"] == "fact":
            got = [f for f in by_key.get(c["key"], []) if c["frames"] is None or c["frames"][0] <= f["frame"] <= c["frames"][1]]
            name = "fact %s %s %s" % (c["key"], c["op"], json.dumps(c["value"]))
            if c["matching"] is not None:
                got = [f for f in got if has_value(f["value"], c["matching"])]
                name += " matching %s" % json.dumps(c["matching"], ensure_ascii=False)
            if c["op"] == "absent":
                add(name, not got, [f["value"] for f in got])
                continue
            if c["op"] == "exists":
                add(name, bool(got), got[-1]["value"] if got else None)
                continue
            if c["op"] == "count":
                add(name, len(got) == c["value"], len(got))
                continue
            if not got:
                add(name, False, None)
                continue
            values = [f["value"] for f in got]
            if c.get("field") is not None:
                values = [member(v, c["field"]) for v in values]
                name += " field %s" % c["field"]
            results = [v is not NO_MEMBER and check_value(v, c["op"], c["value"], c["within"]) for v in values]
            values = [None if v is NO_MEMBER else v for v in values]
            pick = {"last": results[-1:], "first": results[:1], "any": [any(results)], "all": [all(results)]}[c["when"]]
            add(name + " (" + c["when"] + ")", all(pick), values[-1] if c["when"] == "last" else values[0] if c["when"] == "first" else values)
        elif c["kind"] in ("log", "no_log"):
            pattern = re.compile(c["pattern"])
            hits = [line for line in log_lines if pattern.search(line)]
            if c["kind"] == "log" and c.get("times") is not None:
                # exactly n lines: "said once" is a count, not a presence (run 16, R16-2)
                add("log matches %s %d times" % (c["pattern"], c["times"]), len(hits) == c["times"], [len(hits)] + hits[:3])
            elif c["kind"] == "log":
                add("log matches %s" % c["pattern"], bool(hits), hits[:1])
            else:
                add("no log line matches %s" % c["pattern"], not hits, hits[:3])
        elif c["kind"] in ("like", "unlike", "mean_rgb"):
            png = run_folder / "pictures" / (c["picture"] + ".png")
            if not png.exists():
                add("picture %s was kept" % c["picture"], False, None)
                continue
            w, h, rgb = read_png(png)
            box = c["box"]
            if box and (box[0] + box[2] > w or box[1] + box[3] > h):
                raise HarnessError("the box %s is outside the %dx%d picture %s" % (box, w, h, c["picture"]))
            if c["kind"] == "mean_rgb":
                x0, y0, bw, bh = box
                sums = [0, 0, 0]
                for y in range(y0, y0 + bh):
                    for x in range(x0, x0 + bw):
                        for k in range(3):
                            sums[k] += rgb[(y * w + x) * 3 + k]
                mean = [s / (bw * bh) for s in sums]
                distance = math.sqrt(sum((m - r) ** 2 for m, r in zip(mean, c["rgb"])))
                add("picture %s box %s mean %s within %s" % (c["picture"], box, c["rgb"], c["within"]), distance <= c["within"],
                    [round(m, 2) for m in mean])
                continue
            ref = test["folder"] / c["ref"].replace("{renderer}", renderer)
            if not ref.is_file():
                raise HarnessError("the reference %s is missing (luma_test.py refs writes it after you look)" % ref)
            rw, rh, rrgb = read_png(ref)
            if (rw, rh) != (w, h):
                add("picture %s %s %s" % (c["picture"], c["kind"], c["ref"]), False,
                    "the picture is %dx%d, the reference %dx%d" % (w, h, rw, rh))
                continue
            value = rmse(rgb, rrgb, w, box)
            ok = value <= c["rmse"] if c["kind"] == "like" else value > c["rmse"]
            add("picture %s %s %s rmse %s %s" % (c["picture"], c["kind"], c["ref"], "<=" if c["kind"] == "like" else ">", c["rmse"]),
                ok, round(value, 3))
        elif c["kind"] == "sound":
            add("sound %s rms %s" % (c["frames"], c["rms"]), *sound_check(run_folder, facts, c))
    failed = [c for c in checks if c["result"] == "fail"]
    if verdict is None:
        verdict, reason = ("fail", "%s (actual: %s)" % (failed[0]["check"], json.dumps(failed[0]["actual"]))) if failed else ("pass", "")
    return verdict, reason, checks, facts


def sound_check(run_folder, facts, c):
    """RMS in dBFS of the mix over test frames [a, b], at 60 game frames a second from the game frame that is frame 0.
    Unproven until a test shows the WAV's first sample is game frame 0 (harness.clock); the check says what it used."""
    clock = [f for f in facts if f.get("key") == "harness.clock"]
    wav = run_folder / "audio.wav"
    if not clock or not wav.is_file():
        return False, "no harness.clock fact or no audio.wav"
    data = wav.read_bytes()
    if data[0:4] != b"RIFF" or data[8:12] != b"WAVE":
        return False, "audio.wav is not a RIFF WAVE file"
    fmt, samples, at = None, None, 12
    while at + 8 <= len(data):
        cid, size = data[at:at + 4], struct.unpack("<I", data[at + 4:at + 8])[0]
        body = data[at + 8:at + 8 + size]
        if cid == b"fmt " and len(body) >= 16:
            fmt = struct.unpack("<HHIIHH", body[:16])
        elif cid == b"data":
            samples = body
        at += 8 + size + (size & 1)
    if fmt is None or samples is None:
        return False, "audio.wav has no fmt or no data chunk"
    tag, channels, rate, _, _, bits = fmt
    if tag != 1 or channels != 2 or bits != 16:
        return False, "audio.wav is format %d, %d channels, %d bits; the check reads 16-bit PCM stereo" % (tag, channels, bits)
    base = clock[-1]["value"]["game_frame"]
    first = int((base + c["frames"][0]) * rate / 60) * 4
    last = int((base + c["frames"][1] + 1) * rate / 60) * 4
    chunk = samples[first:last]
    if len(chunk) < 4:
        return False, "the WAV has no samples there"
    values = struct.unpack("<%dh" % (len(chunk) // 2), chunk[:len(chunk) // 2 * 2])
    rms = math.sqrt(sum(v * v for v in values) / len(values)) / 32768.0
    db = 20 * math.log10(rms) if rms > 0 else -math.inf
    return c["rms"][0] <= db <= c["rms"][1], round(db, 2)


def finish_pictures(run_folder):
    kept = []
    for ppm in sorted((run_folder / "pictures").glob("*.ppm")):
        w, h, rgb = read_ppm(ppm)
        write_png(ppm.with_suffix(".png"), w, h, rgb)
        ppm.unlink()
        kept.append(ppm.stem)
    return kept


def gpu_line(log_lines):
    for line in log_lines:
        m = re.search(r"(?:gpu|adapter)[:=]\s*(.+)", line, re.IGNORECASE)
        if m:
            return m.group(1).strip()
    return "unknown"


def run_tests(args):
    target = pathlib.Path(args.target).resolve()
    if target.is_file() and target.name == "suite.json":
        suite = load_json(target)
        keys_known(suite, "suite", {"harness", "tests"})
        if suite.get("harness") != HARNESS or not isinstance(suite.get("tests"), list):
            raise SystemExit("suite.json: harness 1 and a list of tests")
        folders = [target.parent / t for t in suite["tests"]]
        suite_name = target.parent.name
    else:
        folders = [target]
        suite_name = target.name
    work = pathlib.Path(args.work or os.environ.get("LUMA_TEST_WORK", "")).resolve() if (args.work or os.environ.get("LUMA_TEST_WORK")) else None
    if work is None:
        raise SystemExit("give --work or LUMA_TEST_WORK")
    if not args.lock:
        raise SystemExit("give --lock or LUMA_TEST_LOCK")
    lock = pathlib.Path(args.lock)
    if lock.exists():
        raise SystemExit("run.lock held: " + lock.read_text(encoding="utf-8", errors="replace").strip())
    global CLASSES
    types = pathlib.Path(args.bin).resolve() / "luma_types.json"
    CLASSES = class_names(types) if types.is_file() else None
    lock.write_text("%s %s luma-test %s\n" % (args.who, datetime.datetime.now().strftime("%Y-%m-%d %H:%M:%S"), suite_name), encoding="utf-8")
    all_pass = True
    passed = {}
    try:
        for folder in folders:
            try:
                test = read_test(folder)
                renderers = [args.renderer] if args.renderer else test["renderers"]
            except HarnessError as error:
                all_pass = False
                report(work / "runs" / suite_name / folder.name / "-", folder.name, [], "-", "harness-error", str(error), [], args, {})
                continue
            for renderer in renderers:
                run_folder = work / "runs" / suite_name / test["name"] / renderer
                previous = None
                try:
                    if test["continue_from"] is not None:
                        prev = passed.get((test["continue_from"], renderer))
                        if prev is None:
                            raise SetupFailed("continue_from %s: that test did not pass earlier in this suite" % test["continue_from"])
                        previous = prev
                    ran = run_one(test, renderer, args, run_folder, previous)
                    pictures = finish_pictures(run_folder)
                    verdict, reason, checks, facts = evaluate(test, renderer, run_folder, ran)
                except HarnessError as error:
                    verdict, reason, checks, facts, pictures, ran = "harness-error", str(error), [], [], [], {}
                except SetupFailed as error:
                    verdict, reason, checks, facts, pictures, ran = "setup-failed", str(error), [], [], [], {}
                if verdict == "pass":
                    passed[(test["name"], renderer)] = run_folder
                else:
                    all_pass = False
                if verdict not in ("crashed",) and (run_folder / "bin").exists():
                    for p in (run_folder / "bin").iterdir():
                        if p.is_file() and (p.suffix in (".exe", ".dll", ".pdb", ".so") or p.name in EXES.values()
                                            or p.name == HOST["importer_exe"]):
                            p.unlink()
                setup = {f["key"]: f["value"] for f in facts if f.get("key", "").startswith("setup.")}
                report(run_folder, test["name"], test["proves"], renderer, verdict, reason, checks, args,
                       {"setup": setup, "frames": max([f.get("frame", -1) for f in facts] or [-1]),
                        "seconds": round(ran.get("seconds", 0.0), 2), "pictures": pictures,
                        "binaries": ran.get("frozen", {}), "exit": ran.get("code"),
                        "test_json": test["raw"]})
    finally:
        lock.unlink()
    return 0 if all_pass else 1


class SetupFailed(Exception):
    pass


def report(run_folder, name, proves, renderer, verdict, reason, checks, args, extra):
    run_folder.mkdir(parents=True, exist_ok=True)
    log_lines = (run_folder / "run.log").read_text(encoding="utf-8", errors="replace").splitlines() if (run_folder / "run.log").exists() else []
    result = {"harness": HARNESS, "runner": RUNNER_VERSION, "test": name, "proves": proves, "renderer": renderer,
              "platform": HOST["platform"], "os": platform.platform(), "gpu": gpu_line(log_lines),
              "commit": args.commit, "module": args.module, "verdict": verdict, "reason": reason, "checks": checks}
    test_json = extra.pop("test_json", None)
    result.update(extra)
    # Every result has contract 5's fields; a test refused before it ran has them empty (audit 0.3 H-15).
    for key, empty in (("setup", {}), ("frames", None), ("seconds", None), ("pictures", [])):
        result.setdefault(key, empty)
    (run_folder / "result.json").write_text(json.dumps(result, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")
    if test_json is not None:
        (run_folder / "test.json").write_text(json.dumps({"runner": RUNNER_VERSION, "test": test_json}, indent=2, ensure_ascii=False) + "\n",
                                              encoding="utf-8")
    print("%-28s %-7s %-14s %s" % (name, renderer, verdict, reason))


def write_refs(args):
    if not args.looked:
        raise SystemExit("refs writes references only after you have looked at the pictures: add --looked")
    test = read_test(args.target)
    run_folder = pathlib.Path(args.run).resolve()
    renderer = run_folder.name
    for c in test["expect"]:
        if c["kind"] in ("like", "unlike"):
            source = run_folder / "pictures" / (c["picture"] + ".png")
            if not source.is_file():
                raise SystemExit("the run kept no picture %s" % c["picture"])
            target = test["folder"] / c["ref"].replace("{renderer}", renderer)
            target.parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(source, target)
            print("%s -> %s (sha256 %s)" % (source, target, sha256(target)[:16]))
    return 0


def main():
    # Names and reasons may be any language (a Japanese object name, run 13): the console or the file the output goes
    # to takes them as UTF-8, whatever the system's code page.
    for stream in (sys.stdout, sys.stderr):
        stream.reconfigure(encoding="utf-8", errors="backslashreplace")
    parser = argparse.ArgumentParser(description="The proof harness's runner (contract harness, revision 1).")
    sub = parser.add_subparsers(dest="command", required=True)
    run = sub.add_parser("run")
    run.add_argument("target")
    run.add_argument("--bin", required=True)
    run.add_argument("--renderer")
    run.add_argument("--work")
    run.add_argument("--who", default="?")
    run.add_argument("--module", default="hooks", choices=("hooks", "plain"))
    run.add_argument("--commit", default="unknown")
    run.add_argument("--lock", default=os.environ.get("LUMA_TEST_LOCK"),
                     help="the run lock file (or LUMA_TEST_LOCK); the runner refuses to start while it exists")
    refs = sub.add_parser("refs")
    refs.add_argument("target")
    refs.add_argument("--run", required=True)
    refs.add_argument("--looked", action="store_true")
    args = parser.parse_args()
    if args.command == "run":
        return run_tests(args)
    return write_refs(args)


if __name__ == "__main__":
    sys.exit(main())
