"""The self-description (plan 1.8, contract self-description revision 1): one file that says everything a mod can name
in one build of the game, made by the build beside the module from the generator's own outputs (never by hand):

    self_description.py --types luma_types.json --functions luma_functions.json --numbers luma_numbers.json
                        --module smg_game.dll --platform windows-x64 --abi msvc-x64 --luma-h native/include/luma.h
                        [--source TREE] --out smg_game.luma-self.json

The API version is luma.h's LUMA_API; built_from is the source tree's git commit (null when it is not a checkout,
said on stderr).

The types, globals and their left-out list, the functions with their marks, the game's numbers and tables with their
keys are the generator's (gen_types.py, instrument.py), passed through. What is made here: the module's header, and
`names`, the lists a mod names things by, each read from those same rows:
- nerves: every class with the base Nerve, with the one function its execute calls (its exe, read from the body by
  gen_types) and that function's class (its owner);
- messages: every enumerator whose name begins ACTMES_ (the game's actor messages), with its value;
- keys: every table whose rows begin with a string (its first field, or the row itself), by the table's name: the
  event flags (sGameEventFlagStatic), the event values, the object factory's names and the rest.
A row the inputs give twice stops the tool, naming it; an owner or exe no row has is null: nothing is guessed."""
import argparse
import hashlib
import json
import pathlib
import re
import subprocess
import sys

SELF_VERSION = 1


def fail(text):
    print("self_description: " + text, file=sys.stderr)
    sys.exit(1)


def read(path):
    try:
        return json.loads(pathlib.Path(path).read_text(encoding="utf-8"))
    except (OSError, ValueError) as error:
        fail("%s: %s" % (path, error))


def sha256(path):
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for block in iter(lambda: f.read(1 << 20), b""):
            h.update(block)
    return h.hexdigest()


def luma_api(header):
    try:
        text = pathlib.Path(header).read_text(encoding="utf-8")
    except OSError as error:
        fail("%s: %s" % (header, error))
    found = re.findall(r"^#define LUMA_API (\d+)u?\s*$", text, re.M)
    if len(found) != 1:
        fail("%s: no one #define LUMA_API" % header)
    return int(found[0])


def commit(tree):
    if tree is None:
        return None
    try:
        done = subprocess.run(["git", "-C", tree, "rev-parse", "HEAD"], capture_output=True, text=True, timeout=60)
    except (OSError, subprocess.SubprocessError) as error:
        print("self_description: built_from is null: git: %s" % error, file=sys.stderr)
        return None
    if done.returncode != 0 or not re.fullmatch(r"[0-9a-f]{40}", done.stdout.strip()):
        print("self_description: built_from is null: %s is not a git checkout" % tree, file=sys.stderr)
        return None
    return done.stdout.strip()


def nerves(types):
    """Every class with the base Nerve. Its exe: the one function its execute calls (gen_types' `calls`, read from the
    body: NEW_NERVE's `actor->exeWander()`), and its owner, that function's class. Null when execute calls none or
    several, or is not written in the class."""
    by_name = {t["name"] for t in types if t.get("name")}
    out = []
    for t in types:
        if not any(b.get("name") == "Nerve" for b in t.get("bases", [])):
            continue
        owner = exe = None
        execute = [v for v in t.get("virtuals", []) if v["name"].rpartition("::")[2] == "execute"]
        calls = execute[0].get("calls") if len(execute) == 1 else None
        if calls is not None and len(calls) == 1:
            exe = calls[0]["name"]
            scope = exe.rpartition("::")[0]
            owner = scope if scope in by_name else None
        out.append({"name": t["name"], "owner": owner, "exe": exe, "file": t.get("file"), "line": t.get("line")})
    return out


def messages(types):
    out = []
    seen = set()
    for t in types:
        if t.get("kind") != "enum":
            continue
        for v in t.get("values", []):
            if v["name"].startswith("ACTMES_"):
                if v["name"] in seen:
                    fail("the message %s is in two enums" % v["name"])
                seen.add(v["name"])
                out.append({"name": v["name"], "value": v["value"]})
    return out


def keys(tables):
    return {t["name"]: t["keys"] for t in tables if t.get("keys")}


def main():
    p = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    for name in ("types", "functions", "numbers", "module", "platform", "abi", "luma-h", "out"):
        p.add_argument("--" + name, required=True)
    p.add_argument("--source")
    p.add_argument("--game", default="RMG")
    a = p.parse_args()
    types_file = read(a.types)
    functions = read(a.functions)["functions"]
    numbers_file = read(a.numbers)
    types = types_file["types"]
    seen = set()
    for f in functions:
        key = (f["name"], f["signature"])
        if key in seen:
            fail("the function %s %s is listed twice" % key)
        seen.add(key)
    named = set()
    for t in types:
        if t.get("name") is not None:
            key = (t["name"], t["kind"])
            if key in named:
                fail("the type %s (%s) is listed twice" % key)
            named.add(key)
    module = pathlib.Path(a.module)
    if not module.is_file():
        fail("%s: no such module" % module)
    tables = numbers_file["tables"]
    out = {
        "self": SELF_VERSION,
        "module": {"id": sha256(module), "file": module.name, "game": a.game, "platform": a.platform, "abi": a.abi,
                   "luma_api": luma_api(a.luma_h), "hooks": True, "built_from": commit(a.source)},
        "functions": functions,
        "types": types,
        "globals": types_file["globals"],
        "left_out": types_file["left_out"],
        "data": {"numbers": numbers_file["numbers"], "tables": tables},
        "names": {"nerves": nerves(types), "messages": messages(types), "keys": keys(tables)},
    }
    text = json.dumps(out, ensure_ascii=False, separators=(",", ":"))
    target = pathlib.Path(a.out)
    temporary = target.with_name(target.name + ".tmp")
    temporary.write_text(text + "\n", encoding="utf-8", newline="\n")
    temporary.replace(target)
    n = out["names"]
    print("self_description: %s, %d bytes: %d functions, %d types, %d globals, %d left out, %d numbers, %d tables "
          "(%d with keys), %d nerves (%d with an owner, %d with an exe), %d messages"
          % (target.name, len(text.encode("utf-8")) + 1, len(functions), len(types), len(out["globals"]),
             len(out["left_out"]), len(out["data"]["numbers"]), len(tables), len(n["keys"]), len(n["nerves"]),
             sum(1 for x in n["nerves"] if x["owner"]), sum(1 for x in n["nerves"] if x["exe"]), len(n["messages"])))


if __name__ == "__main__":
    main()
