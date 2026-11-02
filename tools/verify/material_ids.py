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


def semantic_bytes(key):
    stages, texgens, indirect, channels, alpha = key
    out = bytearray([len(stages)])
    for s in stages:
        out.extend(s[:3])
        out.extend(s[3])
        out.extend(s[4:9])
        out.extend(s[9])
        out.extend(s[10:15])
        for swap in s[15:17]:
            out.append(len(swap))
            out.extend(swap)
        out.extend(s[17:19])
        out.append(len(s[19]))
        out.extend(s[19])
    for group in (texgens, indirect, channels):
        out.append(len(group))
        for item in group:
            out.extend(item)
    out.extend(alpha)
    return bytes(out)


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--exe', type=pathlib.Path, required=True)
    ap.add_argument('--work', type=pathlib.Path, required=True)
    ap.add_argument('--cxx', type=pathlib.Path, help='also compile and compare the current game lookup (clang++)')
    ap.add_argument('dumps', type=pathlib.Path, nargs='*')
    args = ap.parse_args()
    repo = pathlib.Path(__file__).resolve().parents[2]
    spec = importlib.util.spec_from_file_location('material_generator', repo / 'tools/dev/gen_j3d_materials.py')
    gen = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(gen)
    table = repo / 'native/petari/src/JSystem/J3DGraphBase/J3DMaterialShaders.inc'
    ids = {int(h, 16): int(i) for h, i in re.findall(r'\{0x([0-9A-Fa-f]+)ull, (\d+)u\}', table.read_text())}
    args.work.mkdir(parents=True, exist_ok=True)
    reference = None
    if args.cxx:
        # Compile the actual function and table, rather than a second handwritten
        # approximation of what the game does. Only its surrounding type aliases
        # and file-reading main are supplied here.
        game = table.with_name('J3DNativeMaterial.cpp').read_text()
        start = game.index('    u32 nativeShaderOf(')
        end = game.index('\n    // The register file', start)
        source = args.work / 'game-material-id.cpp'
        source.write_text('#include <nebula_game_api.h>\n#include <cstring>\n'
                          '#include <fstream>\n#include <iostream>\n'
                          'using u8 = uint8_t; using u32 = uint32_t; using u64 = uint64_t;\n'
                          'struct J3DShaderEntry { u64 hash; u32 material; };\n'
                          'const J3DShaderEntry sJ3DShaders[] = {\n'
                          + table.read_text() + '\n{0xFFFFFFFFFFFFFFFFull, 0u}};\n'
                          + game[start:end] + '\n'
                          'int main(int argc, char** argv) {\n'
                          ' if (argc != 2 || sizeof(NebulaMaterialProgram) != 756) return 2;\n'
                          ' std::ifstream f(argv[1], std::ios::binary); NebulaMaterialProgram p;\n'
                          ' if (!f) return 3;\n'
                          ' while (f.read(reinterpret_cast<char*>(&p), sizeof(p))) {\n'
                          '  auto id = nativeShaderOf(p);\n'
                          '  std::cout << (id ? id - NEBULA_SHADER_J3D_FIRST + 1 : 0) << "\\n";\n'
                          ' } return f.eof() && f.gcount() == 0 ? 0 : 4;\n}\n')
        reference = args.work / 'game-material-id.exe'
        subprocess.run([str(args.cxx), '-std=c++20', '-O2', '-I', str(repo / 'native/include'),
                        str(source.resolve()), '-o', str(reference.resolve())], check=True,
                       capture_output=True, creationflags=0x08000000)
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
        game_ids = None
        if reference:
            normalized = args.work / 'normalized.bin'
            normalized.write_bytes(b''.join(gen.to_native(data[i:i + gen.RECORD])
                                           for i in range(0, len(data), gen.RECORD)))
            game_result = subprocess.run([str(reference.resolve()), str(normalized.resolve())],
                                         capture_output=True, text=True, check=True, creationflags=0x08000000)
            game_ids = game_result.stdout.splitlines()
        records = len(data) // gen.RECORD
        if len(actual) != records:
            raise SystemExit(f'{path.name}: expected {records} results, got {len(actual)}')
        if game_ids is not None and len(game_ids) != records:
            raise SystemExit(f'{path.name}: wrong game reference result count')
        for i, line in enumerate(actual):
            rec = gen.to_native(data[i * gen.RECORD:(i + 1) * gen.RECORD])
            key = gen.fnv1a64(gen.lookup_bytes(rec))
            semantic = semantic_bytes(gen.shader_key(gen.parse(rec))).hex()
            expected = f'{key:016x} {ids.get(key, 0)} {semantic}'
            if line != expected:
                raise SystemExit(f'{path.name} record {i}: {line} != {expected}')
            if game_ids is not None and int(game_ids[i]) != ids.get(key, 0):
                raise SystemExit(f'{path.name} record {i}: game ID {game_ids[i]} != {expected}')
            if path != synth:
                captured += 1
                missing += key not in ids
        total += records
        print(f'{path.name}: {records} keys and IDs equal')
    print(f'PASS: {total} records, {captured} captured, 0 key/ID mismatches; '
          f'{missing} captured records absent from this build table (coverage unproven)')


if __name__ == '__main__':
    main()
