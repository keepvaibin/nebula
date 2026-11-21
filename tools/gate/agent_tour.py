"""agent_tour.py: the tour of the gate, on one renderer, for the coding agent's work folder (brief T10 part B).

    python -X utf8 -I tools/gate/agent_tour.py --bin BIN --pak PAK --work WORK [--renderer d3d12|vulkan|d3d11] [--stops quick|recorded]

It does what the gate does for a tour and nothing else: it writes the tour of tools/verify/tours/tour_enterable.txt (gate_tour.py), puts
the step hash on every test from its first step (step_hash.with_hash), and plays it with the runner (tools/lumacore/luma_test.py run)
on that renderer: the game window hidden, no sound, the US game data PAK. The stops:
  quick     the 12 stops of the gate's quick tour (gate.py QUICK_STOPS), one of each kind of stage; the default;
  recorded  the 20 stops whose step hashes are kept in tests/replays (step_hash.py RECORDED_STOPS), the 12 above and 8 more: the run
            that `step_hash.py record WORK` needs.
WORK holds the run: WORK/tour (the tests), WORK/runs (what the runner wrote), WORK/run.lock while it runs. Exit 0 when every stop
passed, else the runner's code (1 a stop did not pass, 3 the machine was not fit). The commit the runner writes into each result (and
`record` into the references' first line) is this tree's HEAD.
"""
import argparse
import pathlib
import subprocess
import sys

HERE = pathlib.Path(__file__).resolve().parent
TREE = HERE.parent.parent
sys.path.insert(0, str(HERE))
sys.path.insert(0, str(TREE / "tools" / "verify"))
import gate_tour  # noqa: E402
import step_hash  # noqa: E402

RUNNER = TREE / "tools" / "lumacore" / "luma_test.py"
GIT = "C:/Program Files/Git/cmd/git.exe"
RENDERERS = ("d3d12", "vulkan", "d3d11")


def quick_stops():
    """The gate's twelve quick stops (gate.py QUICK_STOPS)."""
    import gate
    return list(gate.QUICK_STOPS)


def stops_for(which):
    return quick_stops() if which == "quick" else list(step_hash.RECORDED_STOPS)


def head_commit(tree=TREE):
    r = subprocess.run([GIT, "-C", str(tree), "rev-parse", "--short=9", "HEAD"], capture_output=True, text=True)
    return r.stdout.strip() if r.returncode == 0 and r.stdout.strip() else "unknown"


def command(suite, bin_dir, pak, work, renderer, commit):
    """The runner's command line, the way the gate's harness source builds it (gate.py HarnessSource.command) with the game data given."""
    return [sys.executable, "-X", "utf8", "-I", str(RUNNER), "run", str(suite), "--bin", str(bin_dir), "--pak", str(pak),
            "--work", str(work), "--who", "agent", "--lock", str(pathlib.Path(work) / "run.lock"), "--commit", commit,
            "--renderer", renderer]


def main(argv=None):
    p = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    p.add_argument("--bin", required=True, help="the bin folder of the build (build_windows.ps1 -Bin)")
    p.add_argument("--pak", required=True, help="the US game.pak")
    p.add_argument("--work", required=True, help="the folder of the run")
    p.add_argument("--renderer", default="d3d12", choices=RENDERERS)
    p.add_argument("--stops", default="quick", choices=("quick", "recorded"))
    a = p.parse_args(argv)
    work = pathlib.Path(a.work)
    work.mkdir(parents=True, exist_ok=True)
    suite = gate_tour.write(work / "tour", stops_for(a.stops))
    step_hash.with_hash(suite.parent)
    print("agent_tour: %s stops on %s, run in %s" % (a.stops, a.renderer, work), flush=True)
    return subprocess.run(command(suite, a.bin, a.pak, work, a.renderer, head_commit())).returncode


if __name__ == "__main__":
    sys.exit(main())
