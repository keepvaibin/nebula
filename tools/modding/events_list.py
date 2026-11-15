"""events_list.py <Nebula tree> [<folder>]: docs/modding/reference/events.md (reference_pages.py adds its frontmatter)

The numbers two of the game's events carry, from the game's source: NEBULA_MOD_EVENT_ACTOR_MESSAGE's message (the ACTMES
enum in Game/Util/ActorSensorUtil.hpp) and NEBULA_MOD_EVENT_PLAYER_ACTION's action (MarioStatus in
Game/Player/MarioState.hpp), with the events themselves from nebula_mod_api.h.
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


def main():
    tree = pathlib.Path(sys.argv[1])
    out = pathlib.Path(sys.argv[2]) if len(sys.argv) > 2 else tree / "docs/modding/reference"
    src = tree / "native/petari/include/Game"
    messages = enum_values((src / "Util/ActorSensorUtil.hpp").read_text(encoding="utf-8"), "ACTMES_PLAYER_ATTACK_START", "ACTMES_")
    actions = enum_values((src / "Player/MarioState.hpp").read_text(encoding="utf-8"), "enum MarioStatus", "MarioStatus_")
    lines = [
        "An achievement's `when` (luma.json `pre_standard.achievements`) names one of these events by its lower-case name",
        "(`star_get`), with the `name` and `value` it must carry. A mod's code hooks the game function that raises it",
        "(LumaScript `hook before` or `hook after`; see [Hooks](lumascript/hooks.md); in C, `luma.h`). An event nothing",
        "waits for costs the game one load and one branch.",
        "",
        "| Event | When, and what it carries |",
        "|---|---|",
    ]
    for name, words in events(tree / "native/include/nebula_mod_api.h"):
        lines.append(f"| `{name}` | {words.replace('|', '/')} |")
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
