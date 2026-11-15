"""The names of the game's table columns, for the self-description (self_description.py, names.columns) and
tables_dump.py. A table keeps each column by its hash (crates/nebula-import cook/table.rs column_hash: h = h * 31 +
byte); the names are every string literal in native/petari (the game reads each column by name), then the names below
that no literal spells whole (the game builds them, or never reads them; each one matched to its hash). A hash two
names share keeps the first."""
import re


def column_hash(name):
    h = 0
    for b in name.encode("utf-8"):
        h = (h * 31 + b) & 0xFFFFFFFF
    return h


def dictionary(tree):
    """{hash: name} for the source tree `tree`."""
    names = {}
    literal = re.compile(r'"([A-Za-z_][A-Za-z0-9_ .\-]{0,63})"')
    for folder in ("native/petari/src", "native/petari/include"):
        for path in (tree / folder).rglob("*.[ch]pp"):
            for m in literal.finditer(path.read_text(encoding="utf-8", errors="replace")):
                names.setdefault(column_hash(m.group(1)), m.group(1))
    for name in built_names():
        names.setdefault(column_hash(name), name)
    return names


def built_names():
    """Columns no literal of the source spells whole, each matched to its table's hash: the lights' colours, positions and
    switches, the animation files and their first frames, the camera's vectors, placements' and fixed points' axes, the
    fish boss's levels, the message attributes, group links."""
    for kind in ("Player", "Strong", "Weak", "Planet"):
        for light in ("Light0", "Light1"):
            yield from (f"{kind}{light}Color{c}" for c in "RGBA")
            yield from (f"{kind}{light}Pos{c}" for c in "XYZ")
            yield f"{kind}{light}FollowCamera"
        yield from (f"{kind}Ambient{c}" for c in "RGBA")
        yield f"{kind}Alpha2"
    for file in ("Bck", "Btk", "Brk", "Bpk", "Btp", "Bva"):
        yield file + "Name"
        yield file + "StartFrame"
    for vector in ("up", "wpoint", "woffset", "axis", "vpanaxis"):
        yield from (f"{vector}.{c}" for c in "XYZ")
    for axis in "xyz":
        yield from ("r" + axis, "s" + axis, "t" + axis)
    for name in ("Size", "DropOffset", "Trans", "Rotate"):
        yield from (name + c for c in "XYZ")
    for name in ("GuardAppearNumLv", "EnergyLv", "RailIDLv", "GuardOffsetLv", "SpeedLv"):
        yield from (f"{name}{level}" for level in range(3))
    for level in range(4):
        for guard in range(8):
            yield f"GuardWaitLv{level}Id{guard}"
            yield from (f"GuardPosLv{level}Id{guard}{c}" for c in "XYZ")
    yield from (f"ForceLowScenarioName{i}" for i in range(8))
    yield from ("UpperFlag0", "UpperFlag1", "OffsetY0", "OffsetY1")
    yield from ("mCameraType", "mCameraID", "mBalloon", "mSound", "mReadID", "mMainMessage", "mAreaID")
    yield from ("parent", "child", "brother", "comment", "Fix", "No", "Scale", "SceneNo", "SceneList", "StageType",
                "ScenarioName", "InterpolateIn", "InterpolateOut")
