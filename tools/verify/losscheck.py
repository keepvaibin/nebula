"""losscheck.py RUN_FOLDER...: fails when a run lost anything the game asked for: its [loss] counts at exit (galaxy/loss_report.h), or
the game's lines about materials without a native shader. A run without a [loss] line (it did not end by itself) is reported as such.
Exit 1 on any loss or a missing report."""
import os
import re
import sys

GAME_LINES = re.compile(r"has no native program|folds to a program not in the set|changes what a program record cannot hold")
failed = 0
for run in sys.argv[1:]:
    path = os.path.join(run, "run.log")
    if not os.path.exists(path):
        print(f"{run}: no run.log")
        failed += 1
        continue
    report = None
    game = []
    for line in open(path, errors="replace"):
        if line.startswith("[loss] ") and not line.startswith("[loss] first"):
            report = line.strip()
        elif GAME_LINES.search(line):
            game.append(line.strip()[:120])
    counts = re.findall(r"([\w-]+)=(\d+)", report or "")
    lost = [f"{k}={v}" for k, v in counts if v != "0"]
    if report is None or lost or game:
        failed += 1
        print(f"{os.path.basename(run)}: {'no [loss] report' if report is None else ' '.join(lost) or 'counts 0'}; {len(game)} game lines")
        for g in game[:12]:
            print("   ", g)
    else:
        print(f"{os.path.basename(run)}: nothing lost")
print(f"{len(sys.argv) - 1} runs, {failed} failed")
sys.exit(1 if failed else 0)
