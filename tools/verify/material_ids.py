"""Compare the importer's key and shader ID with the current generator.

Usage: python -I tools/verify/material_ids.py --exe <material_ids.exe> --work <private-output> [dump.bin ...]
Runs on captured material programs and a seeded synthetic corpus. No game data
or generated shader source is stored in the repository.
"""
import argparse
import importlib.util
import pathlib
import random
import re
import subprocess


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--exe', type=pathlib.Path, required=True)
    ap.add_argument('--work', type=pathlib.Path, required=True)
    ap.add_argument('dumps', type=pathlib.Path, nargs='*')
    args = ap.parse_args()
    repo = pathlib.Path(__file__).resolve().parents[2]
    spec = importlib.util.spec_from_file_location('material_generator', repo / 'tools/dev/gen_j3d_materials.py')
    gen = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(gen)
    table = repo / 'native/petari/src/JSystem/J3DGraphBase/J3DMaterialShaders.inc'
    ids = {int(h, 16): int(i) for h, i in re.findall(r'\{0x([0-9A-Fa-f]+)ull, (\d+)u\}', table.read_text())}
    args.work.mkdir(parents=True, exist_ok=True)
    rng = random.Random(0xE04)
    synthetic = bytearray()
    for i in range(10000):
        rec = bytearray(rng.randbytes(gen.RECORD))
        rec[:4] = bytes((rng.randrange(17), rng.randrange(9), rng.randrange(3), rng.randrange(5)))
        rec[662] = i & 1
        for g in range(8):
            rec[592 + 8 * g + 5] = (i >> 1) & 1
        synthetic.extend(rec)
    synth = args.work / 'synthetic.bin'
    synth.write_bytes(synthetic)
    paths = [synth] + args.dumps
    total = captured = missing = 0
    for path in paths:
        data = path.read_bytes()
        if len(data) % gen.RECORD:
            raise SystemExit(f'{path.name}: partial program record')
        result = subprocess.run([str(args.exe.resolve()), str(table), str(path.resolve())],
                                capture_output=True, text=True, check=True, creationflags=0x08000000)
        actual = result.stdout.splitlines()
        records = len(data) // gen.RECORD
        if len(actual) != records:
            raise SystemExit(f'{path.name}: expected {records} results, got {len(actual)}')
        for i, line in enumerate(actual):
            rec = gen.to_native(data[i * gen.RECORD:(i + 1) * gen.RECORD])
            key = gen.fnv1a64(gen.lookup_bytes(rec))
            expected = f'{key:016x} {ids.get(key, 0)}'
            if line != expected:
                raise SystemExit(f'{path.name} record {i}: {line} != {expected}')
            if path != synth:
                captured += 1
                missing += key not in ids
        total += records
        print(f'{path.name}: {records} keys and IDs equal')
    print(f'PASS: {total} records, {captured} captured, 0 key/ID mismatches; '
          f'{missing} captured records absent from this build table (coverage unproven)')


if __name__ == '__main__':
    main()
