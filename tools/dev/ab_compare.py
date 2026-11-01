#!/usr/bin/env python3
# ab_compare.py NATIVE_DUMP GX_DUMP: the A/B input dump (GALAXY_DUMP_DRAW_INPUTS) compared field by field (owner 10-31
# 20:55: never line by line). Draw n of the native run against draw n of the GX run: the native material's constants
# (j3d_material.hlsli MaterialConstants) against what the GX draw reads for the same field - the vertex constants'
# channel colours (VertexConstants chan_material / chan_ambient) and the uber pixel constants (PixelConstants tev_regs,
# konst, alpha_refs, fog colour, tex_dims) - then the shared inputs (projection, inline matrices, matrix places, maps,
# coordinate scales, render state). Prints each differing field with both values; "identical" when none differ.
import re
import sys

TOL = 0.5 / 255.0  # one half of an 8-bit step


def parse(path):
    draws = []
    cur = None
    for line in open(path, encoding='utf-8', errors='replace'):
        line = line.rstrip('\n')
        m = re.match(r'draw (\d+) shader (\d+) path (\w+)', line)
        if m:
            cur = {'shader': int(m.group(2)), 'path': m.group(3), 'words': {'vs': {}, 'material': {}, 'ps': {}}, 'lines': {}}
            draws.append(cur)
            continue
        if cur is None:
            continue
        m = re.match(r'(vs|material|ps)\[(\d+)\]((?: [0-9a-f]{8}\([^)]*\))+)', line)
        if m:
            base = int(m.group(2))
            vals = re.findall(r' ([0-9a-f]{8})\(([^)]*)\)', m.group(3))
            for j, (hexw, fl) in enumerate(vals):
                cur['words'][m.group(1)][base + j] = (int(hexw, 16), float(fl) if fl not in ('inf', '-inf', 'nan', '-nan') else 0.0)
            continue
        key = line.split(' ', 1)[0]
        cur['lines'].setdefault(key, []).append(line)
    return draws


def f(d, sec, i):
    return d['words'][sec].get(i, (0, 0.0))[1]


def u(d, sec, i):
    return d['words'][sec].get(i, (0, 0.0))[0]


def vec(d, sec, i, n=4):
    return [f(d, sec, i + k) for k in range(n)]


def fmt(v):
    return '(' + ', '.join('%.4g' % x for x in v) + ')'


def as255(v):
    return '(' + ', '.join('%d' % round(x * 255) for x in v) + ')'


def compare(nat, gx):
    out = []

    def field(name, a, b, tol=TOL, show=fmt):
        if any(abs(x - y) > tol for x, y in zip(a, b)):
            out.append('  %-26s native %s  gx %s' % (name, show(a), show(b)))

    # MaterialConstants (words): reg 0, kcolor 16, mat_color 32, amb_color 40, ind_mtx 48, fog 72, fog_color 76,
    # alpha_ref 80, tex_scale 84, ovr 116.
    # PixelConstants (words): tev_regs 64, konst 80, alpha_refs 96, fog_params 100 (colour at 104), tex_dims 108.
    # VertexConstants (words): chan_ambient 32, chan_material 40.
    for k in range(4):
        field('reg[%d] / tev_regs[%d]' % (k, k), vec(nat, 'material', 4 * k), vec(gx, 'ps', 64 + 4 * k), show=as255)
        field('kcolor[%d] / konst[%d]' % (k, k), vec(nat, 'material', 16 + 4 * k), vec(gx, 'ps', 80 + 4 * k), show=as255)
    for c in range(2):
        field('mat_color[%d] / chan_material' % c, vec(nat, 'material', 32 + 4 * c), vec(gx, 'vs', 40 + 4 * c),
              show=as255)
        field('amb_color[%d] / chan_ambient' % c, vec(nat, 'material', 40 + 4 * c), vec(gx, 'vs', 32 + 4 * c),
              show=as255)
    field('alpha_ref / alpha_refs', vec(nat, 'material', 80, 2), vec(gx, 'ps', 96, 2), show=as255)
    if f(nat, 'material', 79) != 0.0:
        field('fog_color / fog_params rgb', vec(nat, 'material', 76, 3), vec(gx, 'ps', 104, 3), show=as255)
    mask = 0
    for line in nat['lines'].get('map_mask', []):
        mask = int(line.split()[1], 16)
    # A hand-written shader (ids below 256) keeps its own constants after the register file (84 on): no coordinate
    # scales to compare there; the register file (sendWithRegisters) maps as above.
    for i in range(8 if nat['shader'] >= 256 else 0):
        if not mask & (1 << i):
            continue
        field('tex_scale[%d].xy / tex_dims.zw' % i, vec(nat, 'material', 84 + 4 * i, 2), vec(gx, 'ps', 110 + 4 * i, 2),
              tol=1e-4)
        field('tex_scale[%d].zw / tex_dims.xy' % i, vec(nat, 'material', 86 + 4 * i, 2), vec(gx, 'ps', 108 + 4 * i, 2),
              tol=1e-4)
    # Shared vertex inputs: projection (0-15), inline position matrix (VertexConstants 56..67 after the header words).
    field('projection', vec(nat, 'vs', 0, 16), vec(gx, 'vs', 0, 16), tol=1e-5)
    # Lines both runs write the same way (matrix places, maps, coordinates, render state, rows).
    texgens = u(gx, 'vs', 48)

    def norm(key, line):
        # Image identities are addresses (each run's heap); the format, size and sampling identify the map.
        line = re.sub(r' image [0-9a-f]+', '', line)
        if key == 'post_texgen':
            line = ' '.join(line.split()[:1 + texgens])
        return line

    for key in sorted(set(nat['lines']) | set(gx['lines'])):
        # The drawing state's coordinate scales: a native draw reads its material's (tex_scale, compared above).
        if key in ('map_mask', 'draw') or key.startswith('coord['):
            continue
        m = re.match(r'(map|coord)\[(\d+)\]', key)
        if m and not (mask if m.group(1) == 'map' else (1 << texgens) - 1) & (1 << int(m.group(2))):
            continue
        a = [norm(key, x) for x in nat['lines'].get(key, [])]
        b = [norm(key, x) for x in gx['lines'].get(key, [])]
        if key in ('row', 'post') or key.startswith('row[') or key.startswith('post['):
            continue
        if a != b:
            for x, y in zip(a, b):
                if x != y:
                    out.append('  %-26s native %s\n  %-26s gx     %s' % (key, x, '', y))
    rows_a = [l for k, v in nat['lines'].items() if k.startswith('row[') for l in v]
    rows_b = [l for k, v in gx['lines'].items() if k.startswith('row[') for l in v]
    bad = 0
    for x, y in zip(rows_a, rows_b):
        xa = [float(t) for t in x.split()[1:]]
        ya = [float(t) for t in y.split()[1:]]
        if any(abs(p - q) > 1e-3 * max(1.0, abs(q)) for p, q in zip(xa, ya)):
            bad += 1
    note = None
    if bad:
        note = '  matrix rows                %d of %d differ (animation between runs moves them)' % (bad, len(rows_a))
    return out, note


def main():
    nat = parse(sys.argv[1])
    gx = parse(sys.argv[2])
    if any(d['path'] != 'native' for d in nat) or any(d['path'] != 'gx' for d in gx):
        print('warning: the first dump must hold native draws and the second GX draws')
    # Draws pair by their shader sequence (a run may draw a few more or fewer: objects that appear between runs);
    # unpaired draws are listed, not compared.
    import difflib
    pairs = []
    unpaired = []
    matcher = difflib.SequenceMatcher(None, [d['shader'] for d in nat], [d['shader'] for d in gx], autojunk=False)
    for op, a0, a1, b0, b1 in matcher.get_opcodes():
        if op == 'equal':
            pairs.extend(zip(range(a0, a1), range(b0, b1)))
        else:
            unpaired.append('native draws %d-%d (shaders %s) / gx draws %d-%d (shaders %s)' % (
                a0 + 1, a1, sorted(set(d['shader'] for d in nat[a0:a1])), b0 + 1, b1,
                sorted(set(d['shader'] for d in gx[b0:b1]))))
    differing = 0
    for i, j in pairs:
        out, note = compare(nat[i], gx[j])
        if out:
            differing += 1
            print('draw %d / gx %d (shader %d):' % (i + 1, j + 1, nat[i]['shader']))
            print('\n'.join(out + ([note] if note else [])))
    for u in unpaired:
        print('unpaired: ' + u)
    print('%d draws compared (native %d, gx %d), %d differ, %d unpaired runs' % (len(pairs), len(nat), len(gx),
                                                                                 differing, len(unpaired)))


main()
