"""agent_tour.py: the tour of the gate for the coding agent's work folder (brief T10 part B, brief AF), and its pictures.

    python -X utf8 -I tools/gate/agent_tour.py --bin BIN --pak PAK --work WORK
        [--renderer d3d12|vulkan|d3d11|all] [--disc RMGE01|RMGP01|RMGJ01|all] [--stops quick|recorded|all|declared]
        [--stop "Stage N" ...] [--set landing|whole] [--tablet SERIAL --apk APK --android-bin DIR] [--jobs N]
        [--changes PICTURES-CHANGE.txt] [--manifest approved.tsv] [--store DIR]

It does what the gate does for a tour and nothing else: it writes the tour of tools/verify/tours/tour_enterable.txt (gate_tour.py), puts
the step hash on every test of the US tour from its first step (step_hash.with_hash), and plays it with the runner (tools/lumacore/luma_test.py
run) on each renderer and disc asked for: the game window hidden, no sound, each disc's game data PAK. Then it compares every picture the
runs took with the approved list, the way the gate does (tools/gate/pictures.py), and says which differ:
  the same     the approved picture, bit for bit;
  new          no approved row (the landing approves it);
  declared     a line of PICTURES-CHANGE.txt names it (suite/stop, picture, place, renderer, disc), so the landing approves it;
  fail         it differs and no line names it (printed by name, with the line to write), or an approved picture of a whole tour was
               not taken, or a line names no differing picture (only when --set says the whole of what a landing takes was played).
The stops:
  quick     the 12 stops of the gate's quick tour (gate.py QUICK_STOPS), one of each kind of stage; the default;
  recorded  the 20 stops whose step hashes are kept in tests/replays (step_hash.py RECORDED_STOPS), the 12 above and 8 more: the run
            that `step_hash.py record WORK` needs;
  all       every stop of the tour list (102);
  declared  the stops PICTURES-CHANGE.txt names (any number: what a landing plays on the other discs' Direct3D 11 and Vulkan).
  --stop "Stage N" (repeatable) names stops one by one instead.
The parts of the set (a landing of the main level takes the first, the long run the second):
  --set landing  every stop on the three renderers of the US disc, both other discs on Direct3D 12, the stops PICTURES-CHANGE.txt names on
                 the other discs' Direct3D 11 and Vulkan, and (with --tablet) the tablet's 12 stops;
  --set whole    every stop on every renderer of every disc, and (with --tablet) the tablet's 12 stops.
Any other part: --renderer, --disc and --stops together (all asks for every one). The units play side by side, --jobs at a time (default 2).
The tablet: the test app is installed (luma_test_android.py prepare, --apk and --pak), the 12 stops play on Vulkan with the device lock held
(--tablet-lock, default D:/NebulaWork/plan/tablet.lock), the test app is removed. Its pictures are the tablet's own rows.

WORK holds the run. A single unit (one renderer, one disc) is WORK/tour (the tests), WORK/runs (what the runner wrote), WORK/run.lock while
it runs; with more than one, each unit is WORK/<tour-label>-<renderer>/ with the same three. The pictures' sheet is WORK/pictures-review/
review.html. Exit 0 when every stop passed and no picture fails, 1 a stop or a picture failed, 2 the declaration file or the options are
wrong, 3 the machine was not fit (the runner's code). The commit the runner writes into each result (and `record` into the references'
first line) is this tree's HEAD.
"""
import argparse
import concurrent.futures
import contextlib
import os
import pathlib
import shutil
import subprocess
import sys
import threading

HERE = pathlib.Path(__file__).resolve().parent
TREE = HERE.parent.parent
sys.path.insert(0, str(HERE))
sys.path.insert(0, str(TREE / "tools" / "verify"))
import gate_tour  # noqa: E402
import pictures  # noqa: E402
import step_hash  # noqa: E402

RUNNER = TREE / "tools" / "lumacore" / "luma_test.py"
ANDROID_RUNNER = TREE / "tools" / "lumacore" / "luma_test_android.py"
GIT = "C:/Program Files/Git/cmd/git.exe"
RENDERERS = ("d3d12", "vulkan", "d3d11")
DISCS = pictures.DISCS
US_DISC = pictures.US_DISC
OTHER_DISCS = tuple(d for d in DISCS if d != US_DISC)
TABLET_LOCK = "D:/NebulaWork/plan/tablet.lock"
SETS = ("landing", "whole")


def quick_stops():
    """The gate's twelve quick stops (gate.py QUICK_STOPS)."""
    import gate
    return list(gate.QUICK_STOPS)


def stops_for(which):
    return quick_stops() if which == "quick" else list(step_hash.RECORDED_STOPS)


def head_commit(tree=TREE):
    r = subprocess.run([GIT, "-C", str(tree), "rev-parse", "--short=9", "HEAD"], capture_output=True, text=True)
    return r.stdout.strip() if r.returncode == 0 and r.stdout.strip() else "unknown"


def command(suite, bin_dir, pak, work, renderer, commit, device=None):
    """The runner's command line, the way the gate's harness source builds it (gate.py HarnessSource.command) with the game data given;
    `device` (a serial) plays on that Android device, `bin_dir` then being the test build's folder."""
    where = ["--device", str(device), "--bin", str(bin_dir)] if device else ["--bin", str(bin_dir), "--pak", str(pak)]
    return [sys.executable, "-X", "utf8", "-I", str(RUNNER), "run", str(suite)] + where + [
        "--work", str(work), "--who", "agent", "--lock", str(pathlib.Path(work) / "run.lock"), "--commit", commit, "--renderer", renderer]


class Unit:
    """One tour on one renderer of one disc (or the tablet's): `stops` the list of "Stage N" (None: every stop), `partial` when it plays
    only some stops of a tour the approved list holds whole (its missing pictures are not a fault)."""

    def __init__(self, disc, renderer, stops, place="pc"):
        self.disc, self.renderer, self.stops, self.place = disc, renderer, stops, place
        self.label = "tour-tablet" if place == "tablet" else ("tour" if disc == US_DISC else "tour-" + disc)
        self.partial = stops is not None

    @property
    def key(self):
        return "%s-%s" % (self.label, self.renderer)

    def text(self):
        return "%s %s%s" % (self.label, self.renderer, "" if self.stops is None else " (%d stop%s)" % (
            len(self.stops), "" if len(self.stops) == 1 else "s"))


def declared_stop_names(changes):
    """The stops ("Stage N") a PICTURES-CHANGE.txt names, in the tour's order."""
    names = gate_tour.test_names()
    wanted = set(pictures.declared_stops(pictures.read_declarations(changes), names))
    return ["%s %d" % (stage, scenario) for name, (_, stage, scenario) in zip(names, gate_tour.stops()) if name in wanted]


def choose(value, every):
    return list(every) if value == "all" else [value]


def units_of(a):
    """The units a run plays: [Unit]."""
    explicit = [s for s in (a.stop or [])]
    if a.set == "landing":
        declared = declared_stop_names(a.changes)
        units = [Unit(US_DISC, r, None) for r in RENDERERS] + [Unit(d, "d3d12", None) for d in OTHER_DISCS]
        if declared:
            units += [Unit(d, r, declared) for d in OTHER_DISCS for r in RENDERERS if r != "d3d12"]
    elif a.set == "whole":
        units = [Unit(d, r, None) for d in DISCS for r in RENDERERS]
    else:
        if explicit:
            stops = explicit
        elif a.stops == "all":
            stops = None
        elif a.stops == "declared":
            stops = declared_stop_names(a.changes)
            if not stops:
                raise SystemExit("agent_tour: --stops declared: %s names no stop" % a.changes)
        else:
            stops = stops_for(a.stops)
        units = [Unit(d, r, stops) for d in choose(a.disc, DISCS) for r in choose(a.renderer, RENDERERS)]
    if a.tablet:
        units.append(Unit(US_DISC, "vulkan", quick_stops(), place="tablet"))
    return units


def disc_paks(a, units):
    """{disc: game.pak} of every disc the units play: --pak for the US disc, --disc-pak DISC=PATH, else the game data kept by key
    (tools/gate/gamedata.py path DISC)."""
    given = {}
    for text in a.disc_pak or []:
        disc, _, path = text.partition("=")
        if disc not in DISCS or not path:
            raise SystemExit("agent_tour: --disc-pak is DISC=PATH with DISC one of %s" % ", ".join(DISCS))
        given[disc] = path
    paks = {US_DISC: a.pak}
    for disc in sorted({u.disc for u in units} - {US_DISC}):
        if disc not in given:
            found = subprocess.run([sys.executable, "-X", "utf8", "-I", str(HERE / "gamedata.py"), "path", disc], capture_output=True, text=True)
            path = (found.stdout or "").strip()
            if found.returncode != 0 or not path or not pathlib.Path(path).is_file():
                raise SystemExit("agent_tour: no game data of %s is made: give --disc-pak %s=PATH, or make it (tools/gate/gamedata.py make %s)"
                                 % (disc, disc, disc))
            given[disc] = path
        paks[disc] = given[disc]
    return paks


def paks_text(paks):
    """LUMA_TEST_PAKS: the other discs' game.paks, DISC=path joined by the path separator (the gate's paks_text)."""
    return os.pathsep.join("%s=%s" % (d, p) for d, p in sorted(paks.items()) if d != US_DISC)


def unit_folder(a, unit, single):
    return pathlib.Path(a.work) if single else pathlib.Path(a.work) / unit.key


def play(a, unit, single, paks, commit):
    """Plays one unit; (the runner's exit code, the pictures' items). The tour is written, the step hash put on the US disc's, the runner run."""
    folder = unit_folder(a, unit, single)
    folder.mkdir(parents=True, exist_ok=True)
    suite = gate_tour.write(folder / "tour", unit.stops, None if unit.disc == US_DISC else unit.disc)
    if unit.disc == US_DISC:
        step_hash.with_hash(suite.parent)
    env = dict(os.environ)
    with contextlib.suppress(Exception):
        import gate
        env.update(gate.watch_env(unit.renderer, {}))
    env["LUMA_TEST_PAKS"] = paks_text(paks)
    if unit.place == "tablet":
        cmd = command(suite, a.android_bin, None, folder, unit.renderer, commit, device=a.tablet)
    else:
        cmd = command(suite, a.bin, paks[unit.disc] if unit.disc == US_DISC else paks[US_DISC], folder, unit.renderer, commit)
    code = subprocess.run(cmd, env=env).returncode
    return code, judge(a, unit, folder)


def judge(a, unit, folder):
    """The pictures of one unit's runs against the approved list: pictures.judge's items. The approved pictures a whole tour's runs did
    not take fail (--complete) only when the unit played every stop."""
    args = argparse.Namespace(manifest=pathlib.Path(a.manifest), store=a.store, changes=a.changes, runs=str(folder), renderer=unit.renderer,
                              place=unit.place, complete=not unit.partial, out=None, new_out=None)
    return pictures.judge(args)


def play_tablet_unit(a, unit, single, paks, commit, say):
    """The tablet's unit: the device lock, the test app installed with the US data, the stops played on it, the test app removed."""
    import gate
    held = gate.take_device_lock_wait(a.tablet_lock, "agent_tour", a.tablet_wait)
    if held is not None:
        say("agent_tour: the tablet's lock stayed held %g minutes: %s" % (a.tablet_wait, held))
        return 3, []
    try:
        prepared = subprocess.run([sys.executable, "-X", "utf8", "-I", str(ANDROID_RUNNER), "prepare", "--device", a.tablet, "--apk", str(a.apk),
                                   "--pak", str(paks[US_DISC]), "--who", "agent"]).returncode
        if prepared != 0:
            return (3 if prepared == 3 else 1), []
        try:
            return play(a, unit, single, paks, commit)
        finally:
            subprocess.run([sys.executable, "-X", "utf8", "-I", str(ANDROID_RUNNER), "remove", "--device", a.tablet])
    finally:
        shutil.rmtree(a.tablet_lock, ignore_errors=True)


def stale_lines(declarations, items, tablet_played):
    """The declarations no differing picture of `items` uses: [(line, text)]. A line for the tablet alone is not told stale when the
    tablet did not play."""
    used = {n for i in items for n in i.get("declaration", [])}
    out = []
    for d in declarations:
        if d[0] in used or (d[3] == "tablet" and not tablet_played):
            continue
        out.append((d[0], "%s %s %s %s %s :: %s" % (d[1:5] + (d[5], d[6]))))
    return out


def report(a, units, results, say):
    """The verdict of a run from every unit's (code, items): prints it, returns the exit code."""
    items = [i for _, found in results for i in found if "place" in i]
    machine = [i for _, found in results for i in found if i["kind"] == "machine"]
    changed = [i for i in items if i["kind"] == "changed"]
    missing = [i for _, found in results for i in found if i["kind"] == "missing"]
    declared = [i for i in items if i["kind"] == "declared"]
    new = [i for i in items if i["kind"] == "new"]
    same = [i for i in items if i["kind"] == "same"]
    codes = [c for c, _ in results]
    stale = []
    if a.set:
        stale = stale_lines(pictures.read_declarations(a.changes), items, any(u.place == "tablet" for u in units))
    review = [i for i in items if i["kind"] in ("new", "declared", "changed")]
    if review:
        out = pathlib.Path(a.work) / "pictures-review"
        pictures.write_review(out, review, "agent_tour: the pictures of %s against the approved list" % a.work)
        say("review: %s" % (out / "review.html"))
    say("pictures: %d of %d the same%s%s" % (len(same), len(items), "; %d new" % len(new) if new else "",
                                          "; %d declared" % len(declared) if declared else ""))
    exit_code = 0
    if any(c == 3 for c in codes) or machine:
        for i in machine:
            say("agent_tour: not judged: %s: %s" % (i["key"], i["message"]))
        exit_code = 3
    if any(c not in (0, 3) for c in codes):
        say("agent_tour: fail: a stop did not pass (the runner's exit code %s)" % ", ".join(str(c) for c in codes if c not in (0, 3)))
        exit_code = 1
    if not items and not machine and all(c == 0 for c in codes):
        say("agent_tour: fail: the runs took no picture: nothing was compared")
        exit_code = 1
    if changed or missing or stale:
        if changed:
            say("agent_tour: fail: %d picture%s differ%s from the approved picture and no line of %s declares %s:" % (
                len(changed), "" if len(changed) == 1 else "s", "s" if len(changed) == 1 else "", pathlib.Path(a.changes).name,
                "it" if len(changed) == 1 else "them"))
            for i in changed:
                say("  %s %s %s %s  %s" % (pictures.stop_name(i["suite"], i["test"]), i["picture"], i["place"], i["renderer"], i["message"]))
            say("agent_tour: look at the sheet, then write one line for each (or one with * for the many) in %s, with the reason:" % a.changes)
            for i in changed:
                say("  %s %s %s %s %s :: <why it changed>" % (pictures.stop_name(i["suite"], i["test"]), i["picture"], i["place"],
                                                              i["renderer"], pictures.disc_of(i["suite"], i["test"])))
        for i in missing:
            say("agent_tour: fail: approved but not taken: %s" % i["key"])
        for number, text in stale:
            say("agent_tour: fail: %s line %d names no differing picture: %s" % (pathlib.Path(a.changes).name, number, text))
        exit_code = 1
    if exit_code == 0:
        say("agent_tour: pass: %d picture%s the same%s%s" % (len(same), "" if len(same) == 1 else "s",
                                                           ", %d new" % len(new) if new else "", ", %d declared" % len(declared) if declared else ""))
    return exit_code


def parser():
    p = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    p.add_argument("--bin", required=True, help="the bin folder of the build (build_windows.ps1 -Bin)")
    p.add_argument("--pak", required=True, help="the US game.pak")
    p.add_argument("--work", required=True, help="the folder of the run")
    p.add_argument("--renderer", default="d3d12", choices=RENDERERS + ("all",))
    p.add_argument("--disc", default=US_DISC, choices=DISCS + ("all",), help="the disc's tour (the other discs' data: --disc-pak, else the kept data)")
    p.add_argument("--disc-pak", action="append", help="DISC=PATH: another disc's game.pak")
    p.add_argument("--stops", default="quick", choices=("quick", "recorded", "all", "declared"))
    p.add_argument("--stop", action="append", help="one stop, as `Stage N` (instead of --stops)")
    p.add_argument("--set", choices=SETS, help="a part of the set of the gate: landing (what a landing that can move a picture takes) or whole")
    p.add_argument("--jobs", type=int, default=2, help="the units that play at once")
    p.add_argument("--changes", default=str(TREE / pictures.CHANGES_NAME), help="the declarations (default: this tree's PICTURES-CHANGE.txt)")
    p.add_argument("--manifest", default=str(pictures.MANIFEST), help="the approved list (default: this tree's)")
    p.add_argument("--store", default=pictures.STORE, help="the picture store")
    p.add_argument("--tablet", help="the tablet's serial: its 12 stops play too")
    p.add_argument("--apk", help="--tablet: the test app (platform/android/build_apk.ps1 -Debuggable -Harness)")
    p.add_argument("--android-bin", help="--tablet: the test build's bin folder (<build>/android/test-bin-arm64-v8a)")
    p.add_argument("--tablet-lock", default=TABLET_LOCK)
    p.add_argument("--tablet-wait", type=float, default=30.0, help="--tablet: minutes to wait for the tablet's lock")
    return p


def main(argv=None):
    a = parser().parse_args(argv)
    if a.tablet and not (a.apk and a.android_bin):
        print("agent_tour: --tablet needs --apk and --android-bin", file=sys.stderr)
        return 2
    try:
        units = units_of(a)
        paks = disc_paks(a, units)
    except pictures.DeclarationError as error:
        print("declarations: %s" % error)
        return 2
    except SystemExit as stop:
        print(stop, file=sys.stderr)
        return 2
    pathlib.Path(a.work).mkdir(parents=True, exist_ok=True)
    commit = head_commit()
    single = len(units) == 1
    print("agent_tour: %s, run in %s" % (", ".join(u.text() for u in units), a.work), flush=True)
    lock = threading.Lock()

    def say(text):
        with lock:
            print(text, flush=True)

    def one(unit):
        if unit.place == "tablet":
            return play_tablet_unit(a, unit, single, paks, commit, say)
        return play(a, unit, single, paks, commit)

    try:
        with concurrent.futures.ThreadPoolExecutor(max(1, a.jobs)) as pool:
            results = list(pool.map(one, units))
        return report(a, units, results, say)
    except pictures.DeclarationError as error:
        print("declarations: %s" % error)
        return 2


if __name__ == "__main__":
    sys.exit(main())
