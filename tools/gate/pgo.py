"""pgo.py: the game's profile (native/pgo), made from the tour and checked for staleness (E0.15, P-014).

    python -X utf8 -I tools/gate/pgo.py make --bin GATE_BINS --pak PAK --work WORK [--llvm LLVM_BIN]
    python -X utf8 -I tools/gate/pgo.py check BUILD_LOG [--limit 5]

make: the game module built with -fprofile-instr-generate (native/build.ps1, x86-64-v3 only) into WORK/build, put in a
copy of the gate's bin folder of the same commit (GATE_BINS: build_windows.ps1's -Bin), the tour's 102 stops played
on Direct3D 12 in fixed steps (tools/gate/gate_tour.py) with each process writing its counts to WORK/raw, and the
counts merged into native/pgo/game.profdata. native/pgo/game.profdata.txt says what made it: the commit, the stops
that passed, the LLVM.

check: a build with the profile (NEBULA_PGO) says, for each source file, how many of its functions have counts that
no longer match the code ("profile data may be out of date: of N functions, M have mismatched data") and which files
the profile does not know ("no profile data available for file"). The build keeps those warnings (they are not errors:
counts that still match are used). check reads the build's log and fails when more than LIMIT percent of the
functions are out of date or any file is unknown: the profile is to be made again (make). The release workflow runs it
on the players' build.
"""
import argparse
import datetime
import json
import os
import pathlib
import re
import shutil
import subprocess
import sys

HERE = pathlib.Path(__file__).resolve().parent
TREE = HERE.parent.parent
sys.path.insert(0, str(HERE))
sys.path.insert(0, str(TREE / "tools" / "verify"))

OUT_OF_DATE = re.compile(r"profile data may be out of date: of (\d+) functions?, (\d+) (?:has|have) mismatched data")
UNKNOWN = re.compile(r"no profile data available for file \"([^\"]+)\"")


def staleness(text):
    """(functions, out of date, unknown files) from a build's log. A file compiled twice (two CPU levels) counts
    twice: the share is the same."""
    functions = mismatched = 0
    for m in OUT_OF_DATE.finditer(text):
        functions += int(m.group(1))
        mismatched += int(m.group(2))
    unknown = sorted(set(m.group(1).replace("\\", "/") for m in UNKNOWN.finditer(text)))
    return functions, mismatched, unknown


def check(log, limit):
    text = pathlib.Path(log).read_text(encoding="utf-8", errors="replace")
    functions, mismatched, unknown = staleness(text)
    if "Nebula game profile:" not in text:
        # Nothing to judge would pass anything: the log must be a fresh profile build's, whose configure names the
        # profile (native/CMakeLists.txt NEBULA_PGO).
        print("pgo check: %s is not the log of a build with the profile" % log)
        return 1
    share = 100.0 * mismatched / functions if functions else 0.0
    ok = share <= limit and not unknown
    print("pgo check: %s: %d of %d functions out of date (%.2f %%, limit %.1f %%); %d files unknown to the profile%s"
          % ("pass" if ok else "FAIL", mismatched, functions, share, limit, len(unknown),
             (": " + ", ".join(unknown[:12]) + (" ..." if len(unknown) > 12 else "")) if unknown else ""))
    if not ok:
        print("the profile is to be made again: python -X utf8 -I tools/gate/pgo.py make ...")
    return 0 if ok else 1


def make(a):
    work = pathlib.Path(a.work).resolve()
    llvm = pathlib.Path(a.llvm)
    build = work / "build"
    raw = work / "raw"
    shutil.rmtree(raw, ignore_errors=True)
    raw.mkdir(parents=True)
    log = work / "build.log"
    # The module and its self-description (the game half's calls are checked against it), as build_windows.ps1
    # puts them in a bin folder.
    for target in ("smg_game", "luma_self_description", "luma_services_file"):
        with log.open("ab") as out:
            code = subprocess.run(["powershell", "-NoProfile", "-ExecutionPolicy", "Bypass", "-File",
                                   str(TREE / "native" / "build.ps1"), "-Petari", str(TREE / "native" / "petari"),
                                   "-Build", str(build), "-ProfileFlags:-fprofile-instr-generate", "-NoBaseline",
                                   "-Llvm", str(llvm), "-Target", target], stdout=out, stderr=subprocess.STDOUT).returncode
        if code != 0:
            print("pgo make: the instrumented build of %s failed (exit %d): %s" % (target, code, log))
            return 1
    bins = work / "bins"
    shutil.rmtree(bins, ignore_errors=True)
    shutil.copytree(a.bin, bins)
    shutil.copyfile(build / "smg_game.dll", bins / "smg_game.dll")
    shutil.copyfile(build / "smg_game.luma-placement.json", bins / "smg_game.luma-placement.json")
    shutil.copytree(build / "luma", bins / "luma", dirs_exist_ok=True)
    for name in ("luma_types.json", "luma_functions.json", "luma_numbers.json", "luma_services.json"):
        shutil.copyfile(build / "luma_types" / name, bins / "luma" / name)
    for name in ("smg_game.luma-placement.json", "smg_game.luma-self.json"):
        shutil.copyfile(build / name, bins / "luma" / name)
    shutil.copyfile(build / "luma_types" / "luma_types.json", bins / "luma_types.json")
    shutil.copyfile(build / "luma_types" / "luma_services.json", bins / "luma_services.json")
    import gate_tour
    import step_hash  # noqa: F401 - the tour as the gate writes it
    suite = gate_tour.write(work / "tour")
    env = dict(os.environ)
    # %p: one file per process; %m: per module signature, so a stray other module's counts never merge in.
    env["LLVM_PROFILE_FILE"] = str(raw / "game-%m-%p.profraw")
    with (work / "tour.log").open("wb") as out:
        code = subprocess.run([sys.executable, "-X", "utf8", "-I", str(TREE / "tools/lumacore/luma_test.py"), "run",
                               str(suite), "--bin", str(bins), "--pak", a.pak, "--work", str(work / "runs"),
                               "--renderer", "d3d12", "--who", "pgo", "--lock", str(work / "harness.lock")],
                              stdout=out, stderr=subprocess.STDOUT, env=env).returncode
    tour_text = (work / "tour.log").read_text(encoding="utf-8", errors="replace")
    profraws = sorted(raw.glob("*.profraw"))
    if not profraws:
        print("pgo make: the tour wrote no counts (exit %d): %s" % (code, work / "tour.log"))
        return 1
    target = TREE / "native" / "pgo" / "game.profdata"
    merged = subprocess.run([str(llvm / "llvm-profdata.exe"), "merge", "-o", str(target)] + [str(p) for p in profraws],
                            capture_output=True, text=True)
    if merged.returncode != 0:
        print("pgo make: llvm-profdata merge failed: %s" % merged.stderr.strip()[-400:])
        return 1
    version = subprocess.run([str(llvm / "clang.exe"), "--version"], capture_output=True, text=True).stdout.splitlines()[0]
    commit = subprocess.run(["git", "-C", str(TREE), "rev-parse", "--short", "HEAD"], capture_output=True, text=True).stdout.strip()
    summary = [l for l in tour_text.splitlines() if re.match(r"^\d+ of \d+ ", l.strip())]
    (target.parent / "game.profdata.txt").write_text(
        "The game's profile (tools/gate/pgo.py make): the tour's stops on Direct3D 12, fixed steps.\n"
        "commit: %s\nmade: %s\nllvm: %s\nprocesses: %d\ntour: %s (exit %d)\n" % (
            commit, datetime.date.today().isoformat(), version, len(profraws),
            summary[-1].strip() if summary else "?", code), encoding="utf-8")
    print("pgo make: %s from %d processes (tour exit %d)" % (target, len(profraws), code))
    return 0 if code == 0 else 1


def main(argv=None):
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = p.add_subparsers(dest="command", required=True)
    m = sub.add_parser("make")
    m.add_argument("--bin", required=True)
    m.add_argument("--pak", required=True)
    m.add_argument("--work", required=True)
    m.add_argument("--llvm", default="C:/Program Files/LLVM/bin")
    c = sub.add_parser("check")
    c.add_argument("log")
    c.add_argument("--limit", type=float, default=5.0, help="the most percent of functions out of date")
    a = p.parse_args(argv)
    return make(a) if a.command == "make" else check(a.log, a.limit)


if __name__ == "__main__":
    sys.exit(main())
