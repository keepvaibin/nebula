"""x86 instructions per guest instruction of the translator's output, by class.

Lowers the built-in PowerPC probe corpus (crates/nebula-recomp-core/src/asm_probe.rs)
with `nebula-recomp asm-probe` once per configuration, compiles every probe
shard with the module's code-generation flags and counts x86 instructions:

  static   instructions of each probe function in the assembly (hot and cold
           paths), for the Win64 ABI: `zig c++ -target x86_64-windows-gnu`
           (clang with mingw-w64/libc++ headers; the same calling convention as
           the shipped clang-cl module). Needs --zig.
  dynamic  instructions one call of each probe retires, measured with
           `valgrind --tool=callgrind` on an x86-64 Linux build of the same
           shards linked with tools/perf/asm_probe/asm_probe_driver.cpp, from
           the corpus's fixed register state (asm_probe.rs DRIVER_STATE). Needs
           --dynamic, clang++ and valgrind.

Usage:
  python tools/perf/asm_probe.py --recomp target/debug/nebula-recomp \
      --config base=release --config hw=release,hw-float-conversions \
      [--zig path/to/zig] [--dynamic] [--out build/asm-probe] [--markdown out.md]

A configuration is NAME=OPTIONS, OPTIONS being the `asm-probe --options` list
(translate-module flag names without dashes; `release` is the installer's
option set). The first configuration is the baseline for the delta columns.
"""
import argparse
import concurrent.futures
import csv
import os
import re
import subprocess
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parents[2]
STUB = REPO / 'tools' / 'perf' / 'asm_probe' / 'stubs' / 'probe_stub.h'
DRIVER = REPO / 'tools' / 'perf' / 'asm_probe' / 'asm_probe_driver.cpp'
CODEGEN = ['-std=c++20', '-O2', '-mavx2', '-mfma', '-mmovbe', '-ffp-contract=off', '-fwrapv',
           '-fno-strict-aliasing', '-DGALAXY_BUILDING_GAME_MODULE=1']


def run(command, **kwargs):
    result = subprocess.run(command, capture_output=True, text=True, **kwargs)
    if result.returncode != 0:
        sys.exit(f"command failed: {' '.join(map(str, command))}\n{result.stdout}\n{result.stderr}")
    return result.stdout


def read_probes(src):
    with open(src / 'probes.csv', newline='') as handle:
        return list(csv.DictReader(handle))


def is_instruction(line):
    stripped = line.strip()
    return (line.startswith('\t') and stripped and not stripped.startswith('.')
            and not stripped.startswith('#'))


def static_counts(asm_text):
    """Instructions per `fn_XXXXXXXX` function in a clang assembly file."""
    counts = {}
    current = None
    for line in asm_text.splitlines():
        # The function symbol itself (Itanium or MSVC mangling), not static
        # locals such as `_ZZN6rmge0111fn_..E..shared_call_return_sites__`.
        match = re.match(
            r'^(?:_ZN6rmge0111fn_([0-9A-F]{8})E\w*|\?fn_([0-9A-F]{8})@rmge01@@\S*|fn_([0-9A-F]{8})):',
            line)
        if match:
            current = (match.group(1) or match.group(2) or match.group(3)).upper()
            counts[current] = 0
            continue
        if current is None:
            continue
        if line.startswith('.Lfunc_end') or line.strip().startswith('.seh_endproc'):
            current = None
            continue
        if is_instruction(line):
            counts[current] += 1
    return counts


def compile_static(zig, src, workdir):
    shards = sorted(src.glob('functions_*.cpp'))
    flags = CODEGEN + ['-g0', '-target', 'x86_64-windows-gnu', '-include', str(STUB),
                       '-I', str(REPO / 'runtime' / 'include'), '-I', str(src)]

    def one(shard):
        out = workdir / (shard.stem + '.s')
        run([zig, 'c++', *flags, '-S', str(shard), '-o', str(out)])
        return static_counts(out.read_text())

    totals = {}
    with concurrent.futures.ThreadPoolExecutor(max_workers=os.cpu_count() or 4) as pool:
        for counts in pool.map(one, shards):
            totals.update(counts)
    return totals


def float_library(workdir):
    """The runtime's exact float library (runtime/src/ppc_float.cpp over the
    Berkeley SoftFloat subset CMakeLists.txt builds), for probes that still
    take an exact float path. Built once per output directory."""
    library = workdir / 'libprobe_float.a'
    if library.exists():
        return library
    objects_dir = workdir / 'float_objects'
    objects_dir.mkdir(parents=True, exist_ok=True)
    cmake = (REPO / 'CMakeLists.txt').read_text()

    def names(variable):
        block = re.search(r'set\(\s*' + variable + r'\s*\n(.*?)\n\)', cmake, re.S).group(1)
        return [line.strip() for line in block.splitlines()
                if line.strip() and not line.strip().startswith('#')]

    root = REPO / 'third_party' / 'berkeley-softfloat-3' / 'source'
    sources = [root / f'{name}.c' for name in names('SOFTFLOAT_COMMON_NAMES')]
    sources += [root / '8086-SSE' / f'{name}.c' for name in names('SOFTFLOAT_SPECIALIZE_NAMES')]
    softfloat_flags = ['-O2', '-DSOFTFLOAT_FAST_INT64=1', '-DSOFTFLOAT_FAST_DIV32TO16=1',
                       '-DSOFTFLOAT_FAST_DIV64TO32=1', '-DINLINE_LEVEL=5',
                       '-I', str(REPO / 'runtime' / 'softfloat'), '-I', str(root / 'include'),
                       '-I', str(root / '8086-SSE')]
    cc = os.environ.get('CC', 'clang')
    clang = os.environ.get('CXX', 'clang++')

    def one(source):
        obj = objects_dir / (source.stem + '.o')
        run([cc, *softfloat_flags, '-c', str(source), '-o', str(obj)])
        return obj

    with concurrent.futures.ThreadPoolExecutor(max_workers=os.cpu_count() or 4) as pool:
        objects = list(pool.map(one, sources))
    ppc_float = objects_dir / 'ppc_float.o'
    run([clang, '-std=c++20', '-O2', '-mavx2', '-mfma', '-DSOFTFLOAT_FAST_INT64=1',
         '-include', str(STUB), '-I', str(REPO / 'runtime' / 'include'),
         '-I', str(REPO / 'runtime' / 'softfloat'), '-I', str(root / 'include'),
         '-c', str(REPO / 'runtime' / 'src' / 'ppc_float.cpp'),
         '-o', str(ppc_float)])
    run(['ar', 'rcs', str(library), str(ppc_float), *map(str, objects)])
    return library


def compile_dynamic(src, workdir, probes):
    clang = os.environ.get('CXX', 'clang++')
    flags = CODEGEN + ['-include', str(STUB), '-I', str(REPO / 'runtime' / 'include'), '-I', str(src)]
    units = sorted(src.glob('functions_*.cpp')) + [src / 'module.cpp', DRIVER]

    def one(unit):
        obj = workdir / (unit.stem + '.o')
        run([clang, *flags, '-c', str(unit), '-o', str(obj)])
        return obj

    with concurrent.futures.ThreadPoolExecutor(max_workers=os.cpu_count() or 4) as pool:
        objects = list(pool.map(one, units))
    binary = workdir / 'probe_driver'
    run([clang, '-rdynamic', *map(str, objects), str(float_library(workdir.parent.parent)),
         '-o', str(binary)])
    run([str(binary), *[p['address'] for p in probes]])
    out = workdir / 'callgrind.out'
    run(['valgrind', '--tool=callgrind', f'--callgrind-out-file={out}', str(binary),
         *[p['address'] for p in probes]])
    # Inclusive cost, so out-of-line helpers (checked store slow paths, the
    # multiple-store helper, checkpoint policies) count; then the probe
    # callees' own per-call cost is subtracted again.
    report = run(['callgrind_annotate', '--inclusive=yes', '--threshold=100', str(out)])
    inclusive = {}
    for line in report.splitlines():
        match = re.match(r'\s*([\d,]+)\s.*fn_([0-9A-F]{8})\b', line)
        if match:
            inclusive[match.group(2).upper()] = int(match.group(1).replace(',', ''))
    iterations = 1000
    by_name = {p['name']: p for p in probes}
    calls = {p['address'][2:].upper(): iterations for p in probes}
    for probe in probes:
        for callee, count in parse_callees(probe):
            calls[by_name[callee]['address'][2:].upper()] += iterations * count
    per_call = {address: inclusive[address] / calls[address] for address in inclusive}
    counts = {}
    for probe in probes:
        address = probe['address'][2:].upper()
        if address not in per_call:
            continue
        own = per_call[address]
        for callee, count in parse_callees(probe):
            own -= count * per_call.get(by_name[callee]['address'][2:].upper(), 0.0)
        counts[address] = own
    return counts


def parse_callees(probe):
    for item in (probe.get('callees') or '').split():
        name, _, count = item.partition(':')
        yield name, int(count)


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('--recomp', default=str(REPO / 'target' / 'debug' / 'nebula-recomp'))
    parser.add_argument('--config', action='append', required=True)
    parser.add_argument('--zig')
    parser.add_argument('--dynamic', action='store_true')
    parser.add_argument('--out', default=str(REPO / 'target' / 'asm-probe'))
    parser.add_argument('--markdown')
    args = parser.parse_args()
    if not args.zig and not args.dynamic:
        sys.exit('nothing to measure: pass --zig and/or --dynamic')

    configs = []
    for spec in args.config:
        name, _, options = spec.partition('=')
        configs.append((name, options))
    results = {}
    probes = None
    for name, options in configs:
        base = Path(args.out) / name
        src = base / 'src'
        build = base / 'build'
        src.mkdir(parents=True, exist_ok=True)
        build.mkdir(parents=True, exist_ok=True)
        run([args.recomp, 'asm-probe', '--output', str(src), '--options', options])
        probes = read_probes(src)
        static = compile_static(args.zig, src, build) if args.zig else {}
        dynamic = compile_dynamic(src, build, probes) if args.dynamic else {}
        results[name] = (static, dynamic)

    baseline = configs[0][0]
    header = ['probe', 'class', 'guest', 'executed']
    for name, _ in configs:
        if args.zig:
            header += [f'{name} static', f'{name} static/guest']
        if args.dynamic:
            header += [f'{name} dynamic', f'{name} dynamic/executed']
    rows = []
    for probe in probes:
        address = probe['address'][2:].upper()
        guest = int(probe['guest_instructions'])
        executed = int(probe['executed'])
        row = [probe['name'], probe['class'], str(guest), str(executed)]
        for name, _ in configs:
            static, dynamic = results[name]
            if args.zig:
                value = static.get(address)
                row += [str(value) if value is not None else '-',
                        f'{value / guest:.1f}' if value is not None else '-']
            if args.dynamic:
                value = dynamic.get(address)
                row += [f'{value:.0f}' if value is not None else '-',
                        f'{value / executed:.1f}' if value is not None else '-']
        rows.append(row)
    lines = ['| ' + ' | '.join(header) + ' |', '|' + '---|' * len(header)]
    lines += ['| ' + ' | '.join(row) + ' |' for row in rows]
    # Totals over the measured (non-callee) probes.
    measured = [p for p in probes if p['class'] != 'callee']
    total = ['**all probes**', '', str(sum(int(p['guest_instructions']) for p in measured)),
             str(sum(int(p['executed']) for p in measured))]
    for name, _ in configs:
        static, dynamic = results[name]
        if args.zig:
            value = sum(static.get(p['address'][2:].upper(), 0) for p in measured)
            total += [str(value), f"{value / sum(int(p['guest_instructions']) for p in measured):.1f}"]
        if args.dynamic:
            value = sum(dynamic.get(p['address'][2:].upper(), 0) for p in measured)
            total += [f'{value:.0f}', f"{value / sum(int(p['executed']) for p in measured):.1f}"]
    lines.append('| ' + ' | '.join(total) + ' |')
    text = '\n'.join(lines) + f'\n\nBaseline: {baseline}. Configurations: ' + '; '.join(f'{n}={o}' for n, o in configs) + '\n'
    print(text)
    if args.markdown:
        Path(args.markdown).write_text(text)


if __name__ == '__main__':
    main()
