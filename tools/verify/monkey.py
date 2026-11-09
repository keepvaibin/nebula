"""monkey.py BIN TAG SHARDS FRAMES_PER_STOP SEED: every tour stop played by random input (run, jump, spin, crouch, pointer) in parallel shards, looking for crashes.
Each shard is a real-time run of its share of the stops; a line per shard: crash lines, dump, the stop it was at, loss counters."""
import os, random, re, shutil, subprocess, sys, threading
BIN, TAG, SHARDS, FRAMES, SEED = sys.argv[1], sys.argv[2], int(sys.argv[3]), int(sys.argv[4]), int(sys.argv[5])
# Folders as the other verify scripts take them: NEBULA_BINS (frozen builds, one folder each with pak.path), NEBULA_RUNS (output),
# NEBULA_TEST_SAVES (an empty saves folder to copy).
for _name in ('NEBULA_BINS', 'NEBULA_RUNS', 'NEBULA_TEST_SAVES'):
    if _name not in os.environ:
        sys.exit(f'{_name} is not set')
VERIFY = os.path.dirname(os.path.abspath(__file__)).replace(chr(92), '/')
BINDIR = os.environ['NEBULA_BINS'].replace(chr(92), '/') + '/' + BIN
ROOT = os.environ['NEBULA_RUNS'].replace(chr(92), '/') + f'/mk_{TAG}'
SAVES = os.environ['NEBULA_TEST_SAVES'].replace(chr(92), '/')
lines = [l for l in open(os.environ.get('MONKEY_STOPS', VERIFY + '/tours/tour_enterable.txt'), encoding='utf8').read().splitlines() if l.strip() and not l.startswith('#')]
rng = random.Random(SEED)
rng.shuffle(lines)
shards = [lines[i::SHARDS] for i in range(SHARDS)]
BASH = 'C:/Program Files/Git/bin/bash.exe'
if not os.path.exists(BASH):
    BASH = 'bash'

def run(index, stops):
    out = f'{ROOT}/s{index}'
    shutil.rmtree(out, ignore_errors=True)
    os.makedirs(out)
    tour = f'{out}/tour.txt'
    open(tour, 'w', encoding='utf8').write('\n'.join(stops) + '\n')
    r = random.Random(SEED * 1000 + index)
    total = 2600 + FRAMES * len(stops)
    presses, sticks, shakes = [], [], []
    frame = max(1200, FRAMES + 200)
    while frame < total:
        presses.append(f'{frame}:{r.choice([3, 8, 15, 25, 25, 60, 120])}:{r.choice(["A", "A", "A", "B", "Z", "C", "A+Z"])}')
        frame += r.randint(25, 110)
    frame = max(1200, FRAMES + 200)
    while frame < total:
        length = r.randint(40, 240)
        angle = r.uniform(-1, 1), r.uniform(-1, 1)
        sticks.append(f'{frame}:{length}:{angle[0]:.2f}:{angle[1]:.2f}')
        frame += length + r.randint(0, 30)
    frame = max(1300, FRAMES + 300)
    while frame < total:
        shakes.append(f'{frame}:8')
        frame += r.randint(60, 300)
    extra = os.environ.get('MONKEY_EXTRA', '')
    script = f'''
V={VERIFY}
export VERIFY_DIR=$V NEBULA_WORK="{out}" NEBULA_PAK="$(cat {BINDIR}/pak.path)"
W=$(cygpath -w "{out}")
export NEBULA_SETTINGS="$W\\\\settings" NEBULA_SAVES="$W\\\\saves"
cp -r "{SAVES}" "{out}/saves"
. $V/common.sh; VERIFY=$V
run_build "{BINDIR}" r {int(total / 60) + 120} {extra} GALAXY_TEST_GALAXY_TOUR="$(cygpath -w "{tour}")" GALAXY_TEST_GALAXY_TOUR_FRAMES={FRAMES} GALAXY_MAX_FPS=60 GALAXY_TEST_EVENT_FLAGS=SpinAndStarPointer GALAXY_PRESENT_SNAPSHOT_EVERY=1000000 \\
  "GALAXY_INPUT_AUTOPRESS_SCRIPT={';'.join(presses)}" "GALAXY_INPUT_STICK_SCRIPT={';'.join(sticks)}" "GALAXY_INPUT_SHAKE_SCRIPT={';'.join(shakes)}" >/dev/null 2>&1
'''
    open(f'{out}/run.sh', 'w', newline='\n').write(script)
    subprocess.run([BASH, f'{out}/run.sh'], capture_output=True)
    log = f'{out}/runs/r/run.log'
    text = open(log, encoding='utf8', errors='replace').read() if os.path.exists(log) else ''
    crash = len(re.findall(r'crash:', text, re.I))
    last = re.findall(r'tour: \d+/\d+ (\S+ \d+)', text)
    loss = re.findall(r'\[loss\] draws([^\n]*)', text)
    print(f's{index}: stops {len(last)}/{len(stops)} crash {crash} dumps {len([f for f in os.listdir(out + "/runs/r") if f.endswith(".dmp")]) if os.path.exists(out + "/runs/r") else "?"} last {last[-1] if last else "?"} |{loss[-1][:70] if loss else ""}', flush=True)
    if crash:
        for m in re.findall(r'(?:fatal:|crash:|fatal stack:)[^\n]*', text)[:16]:
            print('   ', m[:150], flush=True)

threads = [threading.Thread(target=run, args=(i, s)) for i, s in enumerate(shards)]
for t in threads:
    t.start()
for t in threads:
    t.join()
print('monkey done')
