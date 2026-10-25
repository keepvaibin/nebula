"""Where the game thread spends its time: a GALAXY_PROFILE_GAME run's game-profile.txt, named from the game module's
link map (lld-link /MAP, e.g. cmake -DCMAKE_SHARED_LINKER_FLAGS="-Xlinker /MAP:<dir>/smg_game.map").

usage: python game_profile.py <game-profile.txt> <smg_game.map> [--from-ms N] [--to-ms N] [--top N]

Prints the functions taking the most samples by themselves (the innermost frame named in the game module) and with
their callees (any frame), each as a share of the samples, and the samples outside the game module by module.
Samples are a millisecond apart, so --from-ms/--to-ms pick a stretch of the run from the game thread's start.
"""
import argparse
import bisect
import collections
import re
import subprocess

parser = argparse.ArgumentParser()
parser.add_argument('profile')
parser.add_argument('map')
parser.add_argument('--from-ms', type=int, default=0)
parser.add_argument('--to-ms', type=int, default=1 << 30)
parser.add_argument('--top', type=int, default=30)
args = parser.parse_args()

modules = []
samples = []
for line in open(args.profile, encoding='utf-8', errors='replace'):
    parts = line.split()
    if not parts:
        continue
    if parts[0] == 'module':
        modules.append((int(parts[1], 16), int(parts[2], 16), ' '.join(parts[3:])))
    elif parts[0] == 'sample':
        samples.append([int(p, 16) for p in parts[1:]])
samples = samples[args.from_ms:args.to_ms]
game = next((m for m in modules if m[2].lower() == 'smg_game.dll'), None)
if game is None:
    raise SystemExit('the profile has no smg_game.dll')

preferred = 0x180000000
symbols = []
pattern = re.compile(r'^\s*[0-9a-fA-F]{4}:[0-9a-fA-F]{8}\s+(\S+)\s+([0-9a-fA-F]{16})')
for line in open(args.map, encoding='utf-8', errors='replace'):
    m = re.search(r'Preferred load address is ([0-9a-fA-F]+)', line)
    if m:
        preferred = int(m.group(1), 16)
        continue
    m = pattern.match(line)
    if m:
        symbols.append((int(m.group(2), 16) - preferred, m.group(1)))
symbols.sort()
starts = [rva for rva, _ in symbols]

names = {}


def name_of(address):
    if not (game[0] <= address < game[0] + game[1]):
        return None
    rva = address - game[0]
    i = bisect.bisect_right(starts, rva) - 1
    return symbols[i][1] if i >= 0 else '?'


def module_of(address):
    for base, size, name in modules:
        if base <= address < base + size:
            return name
    return '?'


own = collections.Counter()
inclusive = collections.Counter()
outside = collections.Counter()
for frames in samples:
    named = [name_of(a) for a in frames]
    if named[0] is None:
        outside[module_of(frames[0])] += 1
        # The work a call out of the game does belongs to its game caller too.
        caller = next((n for n in named if n is not None), None)
        if caller is not None:
            own['(calls out) ' + caller] += 1
    else:
        own[named[0]] += 1
    for n in set(n for n in named if n is not None):
        inclusive[n] += 1

mangled = list(set(list(own) + list(inclusive)))
plain = [m.replace('(calls out) ', '') for m in mangled]
try:
    out = subprocess.run([r'C:\Program Files\LLVM\bin\llvm-undname.exe'], input='\n'.join(plain), capture_output=True,
                         text=True).stdout
    # llvm-undname writes each name, its demangled form and a blank line.
    lines = out.splitlines()
    results = [lines[i + 1] for i in range(0, len(lines) - 1, 3)] if len(lines) >= 2 else []
    if len(results) == len(plain):
        for m, r in zip(mangled, results):
            names[m] = ('(calls out) ' if m.startswith('(calls out) ') else '') + r
except OSError:
    pass


def show(counter, title):
    total = len(samples)
    print(f'\n{title} ({total} samples)')
    for name, count in counter.most_common(args.top):
        print(f'{100.0 * count / total:6.2f}%  {count:6d}  {names.get(name, name)[:150]}')


show(own, 'By itself')
show(inclusive, 'With its callees')
show(outside, 'Outside the game module, by module')
