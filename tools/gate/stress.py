"""stress.py: the stress lane (package T12), without the game.

A landing's checks never run more copies of the game than the machine can carry (machine.py). Heavy load is tested on purpose,
apart: the nightly run's lane `stress` plays twelve copies of the game side by side, whatever the rule for the graphics card says
(pressure on the card is the point), so that a fault which needs it shows. The rule for the PC's own memory still holds (2 GB a
copy in 70% of what is free): when fewer than twelve fit there, the lane runs what fits and says how many.

The twelve: for each renderer one lane that plays the engine suite and then the 12-stop tour (`stress/engine/<renderer>`,
`stress/tour/<renderer>/1`), and three more lanes that play the 12-stop tour again (`stress/tour/<renderer>/2` to `/4`): nine
more in all. Each has its own run folder and its own save folder; every watch the landing lanes turn on is on.

A test that fails with the twelve copies running is run again alone (kinds.Settler). Passing alone, its kind is `stress`: it never
fails a run, is not in flakes.tsv and does not count toward setting a test aside. Failing alone too, it is a plain fail. Each
stress result keeps the run folder whole (a file keep.txt in it: the thinning of old runs and the pruning of passed tests leave
such folders alone) and opens one repair (`plan.py repair add`, source stress, kind stress; the step tool never gives it first).
Nobody counts runs to chase it: if the watch's line does not name the cause, the next change adds a watch that will.

    python -X utf8 -I tools/gate/gate.py --long --only "stress/*"      the lane alone, by name (on main's newest commit)
"""
import pathlib
import subprocess
import sys

import machine

HERE = pathlib.Path(__file__).resolve().parent
TREE = HERE.parent.parent
PLAN_TOOL = TREE / "tools" / "plan" / "plan.py"
RENDERERS = ("d3d12", "vulkan", "d3d11")
COPIES = 12
# Tour lanes of each renderer: the first follows the engine suite in its lane, the rest play the tour only.
TOURS = 4
# The lines the engine's watches print (luma_test.py CONSTANTS_WATCH, tools/gate/gate.py WATCHES): a stress result names one when
# the failed run's log holds it.
WATCH_TAGS = ("[constants] watch",)
KEEP = machine.KEEP_FILE


def lanes():
    """[[(check, kind, renderer, n)]]: the twelve lanes, each the checks it plays in order. kind is engine or tour; n the number of a
    tour lane of that renderer (None for the engine suite)."""
    out = []
    for r in RENDERERS:
        out.append([("stress/engine/%s" % r, "engine", r, None), ("stress/tour/%s/1" % r, "tour", r, 1)])
        out += [[("stress/tour/%s/%d" % (r, n), "tour", r, n)] for n in range(2, TOURS + 1)]
    return out


def check_names():
    return [c[0] for lane in lanes() for c in lane]


def copies_for(memory_free):
    """(copies, by the memory, note): the copies the lane plays at once: twelve whatever the card holds; fewer when 70% of the PC's
    free memory does not hold twelve at 2 GB each (no reading of the memory: twelve)."""
    copies, _, by_memory = machine.copies_at_once(None, memory_free, machine.DEFAULT_NEED, COPIES)
    copies = max(1, copies)
    if copies >= COPIES:
        return copies, by_memory, "stress: %d game copies at once, whatever the graphics card's rule says (pressure on the card is the point)" % copies
    return copies, by_memory, ("stress: %d of %d game copies at once: the PC has %s of memory free and 70%% of it holds %d copies of %s; "
                               "the rest wait their turn" % (copies, COPIES, machine.gb(memory_free), by_memory,
                                                             machine.gb(machine.MEMORY_PER_COPY)))


def watch_line(folder):
    """The first line of an engine watch in a kept run's logs, or ''."""
    for log in sorted(pathlib.Path(folder).rglob("run.log")) if folder and pathlib.Path(folder).is_dir() else []:
        try:
            for line in log.read_text(encoding="utf-8", errors="replace").splitlines():
                if any(tag in line for tag in WATCH_TAGS):
                    return line.strip()[:300]
        except OSError:
            continue
    return ""


def repair_note(found):
    """The repair's note: the test, then the watch's line or the first failure line."""
    return "%s: %s" % (found["test"], watch_line(found.get("folder")) or found["message"])


def repair_command(live, found, commit, evidence):
    """The `plan.py repair add` command of one stress result."""
    return [sys.executable, "-X", "utf8", "-I", str(PLAN_TOOL), "--live", str(live), "repair", "add", "--source", "stress",
            "--kind", "stress", "--check", found["check"], "--renderer", found["renderer"], "--commit", commit or "-",
            "--evidence", str(evidence), "--note", repair_note(found)]


def keep_whole(run_folder, results):
    """Marks a gate run's folder as kept whole: keep.txt names each stress result (machine.thin_runs and prune_runs leave it alone)."""
    path = pathlib.Path(run_folder) / KEEP
    lines = [] if path.is_file() else ["kept whole: a stress lane's result, the evidence of the repairs it opened (tools/gate/stress.py)"]
    lines += ["%s %s on %s: %s" % (f["check"], f["test"], f["renderer"], f["message"]) for f in results]
    with open(path, "a", encoding="utf-8", newline="\n") as f:
        f.write("\n".join(lines) + "\n")


def open_repairs(live, run_folder, commit, results, run=subprocess.run, say=print):
    """Keeps the run folder whole and opens one repair for each stress result. Returns the repair ids (the command's output) or
    the reason one could not be opened; a repair that could not be opened is said, never a fault of the run."""
    keep_whole(run_folder, results)
    ids = []
    for found in results:
        evidence = found.get("folder") or run_folder
        done = run(repair_command(live, found, commit, evidence), capture_output=True, text=True, cwd=str(TREE))
        said = (done.stdout or "").strip().splitlines()
        if done.returncode == 0 and said:
            ids.append(said[-1])
            say("gate.py: stress: repair %s opened for %s %s; the run folder %s is kept whole" % (said[-1], found["check"], found["test"], run_folder))
        else:
            say("gate.py: stress: the repair for %s %s could not be opened (exit %d): %s" % (
                found["check"], found["test"], done.returncode, ((done.stderr or "") + (done.stdout or "")).strip()[-200:]))
    return ids


def flake_command(live, found, commit, evidence):
    """The `plan.py repair add` command of a test set aside (the third flake in seven days): source flake, kind fail, check
    `flake:<test>`, so that `plan.py next` gives it before a step and a landed repair ends the time the test is set aside."""
    return [sys.executable, "-X", "utf8", "-I", str(PLAN_TOOL), "--live", str(live), "repair", "add", "--source", "flake",
            "--kind", "fail", "--check", "flake:" + found["test"], "--renderer", found["renderer"], "--commit", commit or "-",
            "--evidence", str(evidence), "--note",
            "%s on %s failed once and passed alone %d times in 7 days, so it is set aside (its failure no longer counts): %s" % (
                found["test"], found["renderer"], found.get("count", 3), found["message"])]


def open_flake_repairs(live, run_folder, commit, results, run=subprocess.run, say=print):
    """One repair for each test the run set aside; the run folder is kept whole (its rerun/first folders are the evidence). Returns the
    repair ids; a repair that could not be opened is said, never a fault of the run."""
    keep_whole(run_folder, results)
    ids = []
    for found in results:
        done = run(flake_command(live, found, commit, run_folder), capture_output=True, text=True, cwd=str(TREE))
        said = (done.stdout or "").strip().splitlines()
        if done.returncode == 0 and said:
            ids.append(said[-1])
            say("gate.py: flake: repair %s opened for %s on %s, which is set aside; the run folder %s is kept whole" % (
                said[-1], found["test"], found["renderer"], run_folder))
        else:
            say("gate.py: flake: the repair for %s on %s could not be opened (exit %d): %s" % (
                found["test"], found["renderer"], done.returncode, ((done.stderr or "") + (done.stdout or "")).strip()[-200:]))
    return ids
