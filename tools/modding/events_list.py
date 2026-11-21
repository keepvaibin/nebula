"""events_list.py <Nebula tree> [<folder>]: docs/modding/reference/events.md (reference_pages.py adds its frontmatter)

The numbers two of the game's events carry, from the game's source: NEBULA_MOD_EVENT_ACTOR_MESSAGE's message (the ACTMES
enum in Game/Util/ActorSensorUtil.hpp) and NEBULA_MOD_EVENT_PLAYER_ACTION's action (MarioStatus in
Game/Player/MarioState.hpp), with the events themselves from nebula_mod_api.h and the functions that raise each, found
in the game's source (native/petari/src: the function around each NEBULA_MOD_EVENT_<name>).
"""
import os
import pathlib
import re
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import reference_pages  # noqa: E402


def enum_values(text, start, prefix):
    """(name, number) of an enum's members from the line holding `start`, counting as C does."""
    body = text[text.rfind("enum", 0, text.index(start) + len(start)):]
    body = body[body.index("{") + 1:body.index("};")]
    out, number, known = [], -1, {}
    for raw in body.split(","):
        item = re.sub(r"//.*|/\*.*?\*/", "", raw, flags=re.S).strip()
        if not item:
            continue
        name, _, value = item.partition("=")
        name, value = name.strip(), value.strip()
        if value:
            number = known[value] if value in known else int(value, 0)
        else:
            number += 1
        known[name] = number
        if name.startswith(prefix):
            out.append((name, number))
    return out


def events(header):
    text = header.read_text(encoding="utf-8")
    body = text[text.index("typedef enum NebulaModEvent {"):text.index("} NebulaModEvent;")]
    rows, comment = [], []
    for line in body.splitlines()[1:]:
        line = line.strip()
        if line.startswith("/*"):
            comment = [line]
        elif comment and not comment[-1].endswith("*/"):
            comment.append(line)
        elif line.startswith("NEBULA_MOD_EVENT_") and not line.startswith("NEBULA_MOD_EVENT_COUNT"):
            words = " ".join(comment).removeprefix("/*").removesuffix("*/").strip()
            rows.append((line.rstrip(",").split("=")[0].strip(), words))
    return rows


HEAD = re.compile(r"^(?P<indent> {0,4})(?:[\w:<>*&]+\s+)*?(?P<name>[\w:~]+)\s*\([^;{}]*\)\s*(?:const\s*)?\{\s*$")
NAMESPACE = re.compile(r"^namespace (\w+) \{")


def raisers(src):
    """Event name -> the game functions that raise it, sorted: for each NEBULA_MOD_EVENT_<name> in a .cpp (but
    the tables and checks outside any function in NebulaMods.cpp and LumaEvents.cpp), the function whose head is the nearest line above it, qualified by the
    file's namespace when it is written inside one."""
    found = {}
    for path in sorted(src.rglob("*.cpp")):
        tables = path.parent.name == "System" and path.name in ("NebulaMods.cpp", "LumaEvents.cpp")
        lines = path.read_text(encoding="utf-8", errors="replace").splitlines()
        for at, line in enumerate(lines):
            for event in re.findall(r"NEBULA_MOD_EVENT_(\w+)", line):
                if event == "COUNT":
                    continue
                name, space = None, None
                for back in range(at, -1, -1):
                    m = HEAD.match(lines[back])
                    if m and m.group("name") in ("if", "for", "while", "switch", "catch", "return"):
                        m = None  # a statement's block, not a function's
                    if m and name is None:
                        name, indent = m.group("name"), m.group("indent")
                        if indent == "" or "::" in name:
                            break
                        continue
                    n = NAMESPACE.match(lines[back])
                    if name is not None and n:
                        space = n.group(1)
                        break
                if name is None and tables:
                    continue  # the events' own tables and checks
                if name is None:
                    raise SystemExit(f"{path}:{at + 1}: NEBULA_MOD_EVENT_{event} in no function")
                found.setdefault(f"NEBULA_MOD_EVENT_{event}", set()).add(f"{space}::{name}" if space else name)
    return {k: sorted(v) for k, v in found.items()}


def main():
    # A self-test that starts this tool as a child process runs it under the live-state guard (tools/gate/selftest_guard.py).
    import pathlib as _pathlib
    import sys as _sys
    _gate = str(_pathlib.Path(__file__).resolve().parents[1] / "gate")
    _sys.path.insert(0, _gate)
    try:
        import selftest_guard
    finally:
        _sys.path.remove(_gate)
    selftest_guard.install_if_asked()
    tree = pathlib.Path(sys.argv[1])
    out = pathlib.Path(sys.argv[2]) if len(sys.argv) > 2 else tree / "docs/modding/reference"
    src = tree / "native/petari/include/Game"
    messages = enum_values((src / "Util/ActorSensorUtil.hpp").read_text(encoding="utf-8"), "ACTMES_PLAYER_ATTACK_START", "ACTMES_")
    actions = enum_values((src / "Player/MarioState.hpp").read_text(encoding="utf-8"), "enum MarioStatus", "MarioStatus_")
    raised = raisers(tree / "native/petari/src")
    lines = [
        "An achievement's `when` (luma.json `pre_standard.achievements`) names one of these events by its lower-case name",
        "(`star_get`), with the `name` and `value` it must carry.",
        "",
        "A mod's code watches them with the events service: it is told at the start of the next frame, where it may do",
        "anything (see [events](services/events.md)). To act at the moment itself (cancel a hit, change a count, play",
        "another sound), it hooks the function that raises the event, named below (LumaScript `hook before` or",
        "`hook after`; see [Hooks](lumascript/hooks.md); in C, `luma.h`). An event nothing waits for costs the game one",
        "load and one branch.",
        "",
        "| Event | When, and what it carries | Raised in |",
        "|---|---|---|",
    ]
    for name, words in events(tree / "native/include/nebula_mod_api.h"):
        where = ", ".join(f"`{f}`" for f in raised.get(name, []))
        if not where:
            raise SystemExit(f"{name}: raised by no function of the game's source")
        lines.append(f"| `{name}` | {words.replace('|', '/')} | {where} |")
    lines += [
        "",
        "## Messages (NEBULA_MOD_EVENT_ACTOR_MESSAGE)",
        "",
        "The game's objects talk through messages: the player's stomp on a Goomba is `ACTMES_PLAYER_TRAMPLE` sent to it.",
        "Ranges: the player's attacks lie between `ACTMES_PLAYER_ATTACK_START` and `ACTMES_PLAYER_ATTACK_END`, enemies'",
        "between `ACTMES_ENEMY_ATTACK_START` and `ACTMES_ENEMY_ATTACK_END`.",
        "",
        "| Message | Number |",
        "|---|---|",
    ]
    lines += [f"| `{name}` | {number} (0x{number:X}) |" for name, number in messages]
    lines += [
        "",
        "## Player actions (NEBULA_MOD_EVENT_PLAYER_ACTION)",
        "",
        "Actions the player's code runs on its own (the ordinary moves, running and jumping, are not actions here: they",
        "come as PLAYER_JUMP, PLAYER_LAND and PLAYER_SPIN). Names sharing a number are one action.",
        "",
        "| Action | Number |",
        "|---|---|",
    ]
    lines += [f"| `{name}` | {number} |" for name, number in actions]
    reference_pages.write_page("events", "\n".join(lines), out)
    print(f"{len(messages)} messages, {len(actions)} actions -> {out}")


main()
