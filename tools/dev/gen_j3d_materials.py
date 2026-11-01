"""E-04 step 3: the J3D material set. Reads material program dumps (GALAXY_DUMP_MATERIAL_PROGRAMS: raw
NebulaMaterialProgram records, nebula_game_api.h), reduces each to its shader key (only what a shader of it uses:
stages, their inputs, ops, swaps and konst selections, indirect texturing, texture coordinate generation, lit
channels, alpha test, fog type; not blend, depth, culling or values) and reports the distinct keys.

    python -I tools/dev/gen_j3d_materials.py census <dump.bin> [more dumps...]
    python -I tools/dev/gen_j3d_materials.py emit <out dir> <dump.bin>...   (a trial set)
    python -I tools/dev/gen_j3d_materials.py generate <repo> <dump.bin>...  (the repository's set and tables)
"""
import struct
import sys
from collections import Counter

RECORD = 756


def stage(rec, i):
    base = 16 + i * 34
    b = rec[base:base + 34]
    return {
        "coord": b[0], "map": b[1], "channel": b[2],
        "color_in": tuple(b[3:7]), "color_op": b[7], "color_bias": b[8], "color_scale": b[9], "color_clamp": b[10],
        "color_reg": b[11],
        "alpha_in": tuple(b[12:16]), "alpha_op": b[16], "alpha_bias": b[17], "alpha_scale": b[18],
        "alpha_clamp": b[19], "alpha_reg": b[20],
        "ras_swap": b[21], "tex_swap": b[22], "konst_color": b[23], "konst_alpha": b[24],
        "ind": tuple(b[25:34]),
    }


def parse(rec):
    tev_stages, tex_gens, channels, ind_stages, cull, dither, zloc, origin = rec[0:8]
    loads = struct.unpack_from("<H", rec, 8)[0]
    p = {
        "tev_stages": tev_stages, "tex_gens": tex_gens, "channels": channels, "ind_stages": ind_stages,
        "zloc": zloc, "loads": loads,
        "stages": [stage(rec, i) for i in range(min(tev_stages, 16))],
        "swap": [tuple(rec[560 + 4 * t:564 + 4 * t]) for t in range(4)],
        "ind_order": [tuple(rec[576 + 4 * t:580 + 4 * t]) for t in range(4)],
        "tex_gen": [tuple(rec[592 + 8 * t:597 + 8 * t]) for t in range(8)],
        "channel": [tuple(rec[656 + 8 * c:662 + 8 * c]) for c in range(4)],
        "fog_type": rec[688],
        "alpha_compare": tuple(rec[712:715]),
    }
    return p


ALPHA_KONST = 6
COLOR_KONST = 14


def shader_key(p):
    """What a native shader of this program depends on (values excluded)."""
    stages = []
    for s in p["stages"]:
        uses_tex = s["map"] != 0xFF and (8 in s["color_in"] or 9 in s["color_in"] or 4 in s["alpha_in"])
        uses_ras = s["channel"] != 0xFF and (10 in s["color_in"] or 11 in s["color_in"] or 5 in s["alpha_in"])
        uses_kc = COLOR_KONST in s["color_in"]
        uses_ka = ALPHA_KONST in s["alpha_in"]
        stages.append((
            s["coord"] if uses_tex or any(s["ind"]) else 0xFF, s["map"] if uses_tex else 0xFF,
            s["channel"] if uses_ras else 0xFF,
            s["color_in"], s["color_op"], s["color_bias"], s["color_scale"], s["color_clamp"], s["color_reg"],
            s["alpha_in"], s["alpha_op"], s["alpha_bias"], s["alpha_scale"], s["alpha_clamp"], s["alpha_reg"],
            p["swap"][s["ras_swap"] & 3] if uses_ras else (), p["swap"][s["tex_swap"] & 3] if uses_tex else (),
            s["konst_color"] if uses_kc else 0, s["konst_alpha"] if uses_ka else 0,
            s["ind"] if p["ind_stages"] else (),
        ))
    lit = []
    for c in p["channel"][:2 * min(p["channels"], 2)]:
        lit.append(c)
    return (
        tuple(stages),
        # The texgens' types and sources only (the shader takes the draw's matrix indices and post-transforms).
        tuple((t[0], t[1]) for t in p["tex_gen"][:p["tex_gens"]]),
        tuple(p["ind_order"][:p["ind_stages"]]),
        tuple(lit),
        p["alpha_compare"],
    )


def read(paths):
    records = []
    for path in paths:
        data = open(path, "rb").read()
        if len(data) % RECORD:
            raise SystemExit(f"{path}: {len(data)} bytes is not a whole number of {RECORD}-byte records")
        records += [data[i:i + RECORD] for i in range(0, len(data), RECORD)]
    return records


def census(paths):
    records = read(paths)
    programs = set(records)
    keys = Counter(shader_key(parse(r)) for r in programs)
    stages = Counter(len(k[0]) for k in keys)
    print(f"records {len(records)}, distinct programs {len(programs)}, distinct shader keys {len(keys)}")
    print("stages per key:", dict(sorted(stages.items())))
    lit = sum(1 for k in keys if any(c[0] for c in k[3]))
    ind = sum(1 for k in keys if k[2])
    fog = sum(1 for k in keys if k[4])
    print(f"lit {lit}, indirect {ind}, fogged {fog}")


class Unsupported(Exception):
    pass


CC = ["prev.rgb", "prev.aaa", "r0.rgb", "r0.aaa", "r1.rgb", "r1.aaa", "r2.rgb", "r2.aaa", "t.rgb", "t.aaa",
      "ras.rgb", "ras.aaa", "float3(1.0, 1.0, 1.0)", "float3(0.5, 0.5, 0.5)", "k.rgb", "float3(0.0, 0.0, 0.0)"]
CA = ["prev.a", "r0.a", "r1.a", "r2.a", "t.a", "ras.a", "ka", "0.0"]
FRACTION = ["1.0", "0.875", "0.75", "0.625", "0.5", "0.375", "0.25", "0.125"]
BIAS = ["", " + 0.5", " - 0.5"]
SCALE = ["", " * 2.0", " * 4.0", " * 0.5"]
DEST = ["prev", "r0", "r1", "r2"]
COMPARE = ["false", "a8 < r8", "a8 == r8", "a8 <= r8", "a8 > r8", "a8 != r8", "a8 >= r8", "true"]


def konst_color(sel):
    if sel < 8:
        return f"float3({FRACTION[sel]}, {FRACTION[sel]}, {FRACTION[sel]})"
    if 0x0C <= sel <= 0x0F:
        return f"material.konst[{sel - 0x0C}].rgb"
    if 0x10 <= sel <= 0x1F:
        comp = "rgba"[(sel - 0x10) // 4]
        return f"material.konst[{(sel - 0x10) % 4}].{comp * 3}"
    raise Unsupported(f"konst colour selection {sel}")


def konst_alpha(sel):
    if sel < 8:
        return FRACTION[sel]
    if 0x10 <= sel <= 0x1F:
        return f"material.konst[{(sel - 0x10) % 4}].{'rgba'[(sel - 0x10) // 4]}"
    raise Unsupported(f"konst alpha selection {sel}")


def swizzle(table):
    return "".join("rgba"[c & 3] for c in table) if table else "rgba"


def emit_vertex(p, out):
    out.append("void material_vertex(NativeVertex v, inout NativeVaryings o) {")
    # The channels first: a colour texgen (SRTG) reads channel 0's result.
    emit_channels(p, out)
    for i, (gen_type, source, matrix, normalize, post) in enumerate(p["tex_gen"][:p["tex_gens"]]):
        if gen_type == 10:
            out.append(f"    o.uv[{i}] = float4(o.color.rg, 1.0, 0.0);")
            continue
        if gen_type not in (0, 1):
            raise Unsupported(f"texgen type {gen_type} (bump)")
        if source == 0:
            coord = "float4(v.pos, 1.0)"
        elif source == 1:
            coord = "float4(v.nrm, 1.0)"
        elif source == 2:
            coord = "float4(v.tangent, 1.0)"  # GX_TG_BINRM: XF row 3 (Dolphin BinormalT), as the uber shader reads it
        elif source == 3:
            coord = "float4(v.binormal, 1.0)"  # GX_TG_TANGENT: XF row 4 (BinormalB)
        elif 4 <= source <= 11:
            coord = f"float4(v.uv[{source - 4}], 1.0, 1.0)"
        else:
            raise Unsupported(f"texgen source {source}")
        out.append(f"    {{  // coordinate {i}: type {gen_type}, source {source}")
        out.append(f"        const float4 c = {coord};")
        # The texgen's matrix from matrix memory by the index the shape loads (its current matrix, which GX takes over
        # the material's texgen id: GX_TEXMTX0 30 + 3k, 60 the identity) or the vertex's own; its post-transform matrix
        # by the program's row (GX_PTTEXMTX0's 0 + 3k, 61 the identity).
        # As the uber shader: the draw's texture matrix rows (inline_tex_matrices: the matrix its current matrix index
        # names) or, where the shape carries per-vertex indices, the vertex's matrix from the matrix rows; then the
        # post-transform the draw's texgen names (inline_post_matrices: the material's, or a shape's env map or
        # projection), 61 the identity.
        out.append(f"        float3 res;")
        out.append(f"        if (((vs_header.w >> {i}u) & 1u) != 0u) {{")
        out.append(f"            const uint tm = native_tex_matrix(v, {i}u);")
        rows = [f"dot(c, native_matrix_row(tm + {r}u))" for r in range(3)]
        third = rows[2] if gen_type == 0 else "1.0"
        out.append(f"            res = float3({rows[0]}, {rows[1]}, {third});")
        out.append("        } else {")
        irows = [f"dot(c, inline_tex_matrices[{3 * i + r}])" for r in range(3)]
        ithird = irows[2] if gen_type == 0 else "1.0"
        out.append(f"            res = float3({irows[0]}, {irows[1]}, {ithird});")
        out.append("        }")
        out.append(f"        const uint pt = native_post({i}u);")
        out.append("        const bool posted = (pt & 0x3Fu) != 61u;")
        out.append("        if (posted) {")
        out.append("            if ((pt & 0x100u) != 0u) res = dot(res, res) > 0.0 ? normalize(res) : res;")
        prow = [f"dot(float4(res, 1.0), inline_post_matrices[{3 * i + r}])" for r in range(3)]
        out.append(f"            res = float3({prow[0]}, {prow[1]}, {prow[2]});")
        out.append("        }")
        # A projected coordinate's q of 0: GX halves and clamps s and t (as the uber shader and Dolphin).
        projected = "true" if gen_type == 0 else "posted"
        out.append(f"        if ({projected} && res.z == 0.0) res.xy = clamp(res.xy * 0.5, -1.0, 1.0);")
        out.append(f"        o.uv[{i}] = float4(res, {projected} ? 1.0 : 0.0);")
        out.append("    }")
    out.append("}")


def emit_channels(p, out):
    chans = p["channels"]
    lit_any = any(p["channel"][k][0] for k in range(2 * min(chans, 2)))
    if lit_any:
        out.append("    const uint nq = v.pn_row & 31u;")
        out.append("    const float3 nt = float3(dot(native_normal_row(nq).xyz, v.nrm), dot(native_normal_row(nq + 1u).xyz,"
                   " v.nrm), dot(native_normal_row(nq + 2u).xyz, v.nrm));")
        out.append("    const float3 nrm = dot(nt, nt) > 0.0 ? normalize(nt) : nt;")
    for ch in range(2):
        target = "o.color" if ch == 0 else "o.color1"
        vtx = f"v.color{ch}"
        if ch >= chans:
            out.append(f"    {target} = {vtx};")
            continue
        for part, (enable, mat_src, amb_src, mask, diffuse, atten) in (("rgb", p["channel"][2 * ch]),
                                                                      ("a", p["channel"][2 * ch + 1])):
            mat = f"{vtx}.{part}" if mat_src else f"material.mat_color[{ch}].{part}"
            if not enable:
                out.append(f"    {target}.{part} = {mat};")
                continue
            amb = f"{vtx}.{part}" if amb_src else f"material.amb_color[{ch}].{part}"
            xf_atten = {0: 1, 1: 3, 2: 2}[atten]
            xf_diffuse = 0 if atten == 0 else diffuse
            terms = " + ".join(f"j3d_light({n}u, {xf_atten}u, {xf_diffuse}u, v.eye, nrm).{part}"
                               for n in range(8) if (mask >> n) & 1) or "0.0"
            out.append(f"    {target}.{part} = {mat} * saturate(({amb} * 255.0 + {terms}) / 255.0);")


def emit_pixel(p, out):
    out.append("float4 material_color(NativeVaryings i) {")
    # A pass that replaces the stages (j3d_material.hlsli j3d_override): texture 0 at coordinate 0, the channel colour,
    # stage 0's konst alpha.
    t0 = ("tex[0].Sample(samp[0], j3d_texel(i.uv[0], 0u) / material.tex_scale[0].zw)" if p["tex_gens"] > 0
          else "float4(0.0, 0.0, 0.0, 0.0)")
    k0 = konst_alpha(p["stages"][0]["konst_alpha"]) if p["stages"] else "1.0"
    out.append("    [branch] if (material.ovr.x != 0.0) {")
    out.append(f"        return j3d_override({t0}, i.color, {k0}, i.pos);")
    out.append("    }")
    out.append("    float4 prev = material.reg[0];")
    out.append("    float4 r0 = material.reg[1];")
    out.append("    float4 r1 = material.reg[2];")
    out.append("    float4 r2 = material.reg[3];")
    ind_used = p["ind_stages"] > 0 and any(any(s["ind"]) for s in p["stages"])
    if ind_used:
        out.append("    float2 tevcoord = float2(0.0, 0.0);")
        for n, (coord, imap, scale_s, scale_t) in enumerate(p["ind_order"][:p["ind_stages"]]):
            out.append(f"    const float3 ind{n} = round(tex[{imap}].Sample(samp[{imap}], "
                       f"j3d_texel(i.uv[{coord}], {coord}u) / float2({1 << scale_s}.0, {1 << scale_t}.0) / "
                       f"material.tex_scale[{imap}].zw).abg * 255.0);")
    for n, s in enumerate(p["stages"]):
        out.append(f"    {{  // stage {n}")
        uses_tex = s["map"] != 0xFF and (8 in s["color_in"] or 9 in s["color_in"] or 4 in s["alpha_in"])
        ind = s["ind"]
        if uses_tex:
            coord = f"j3d_texel(i.uv[{s['coord']}], {s['coord']}u) / material.tex_scale[{s['map']}].zw"
            texel_coord = f"j3d_texel(i.uv[{s['coord']}], {s['coord']}u)"
            if ind_used and any(ind):
                ind_stage, fmt, bias, matrix, wrap_s, wrap_t, add_prev, lod, ind_alpha = ind
                if ind_alpha:
                    raise Unsupported("bump alpha")
                out.append(f"        float2 texel = {texel_coord};")
                for axis, wrap in (("x", wrap_s), ("y", wrap_t)):
                    if wrap == 6:
                        out.append(f"        texel.{axis} = 0.0;")
                    elif wrap:
                        out.append(f"        texel.{axis} = texel.{axis} - {256 >> (wrap - 1)}.0 * "
                                   f"floor(texel.{axis} / {256 >> (wrap - 1)}.0);")
                shift = [0, 3, 4, 5][fmt]
                add = -128 if fmt == 0 else 1
                out.append(f"        float3 sv = floor(ind{ind_stage} / {1 << shift}.0);")
                for b, comp in enumerate("xyz"):
                    if bias & (1 << b):
                        out.append(f"        sv.{comp} += {add}.0;")
                if 1 <= matrix <= 3:
                    m = matrix - 1
                    out.append(f"        const float2 off = float2(dot(material.ind_mtx[{2 * m}].xyz, sv), "
                               f"dot(material.ind_mtx[{2 * m + 1}].xyz, sv));")
                elif 5 <= matrix <= 7:
                    out.append("        const float2 off = texel * sv.x / 256.0;")
                elif 9 <= matrix <= 11:
                    out.append("        const float2 off = texel * sv.y / 256.0;")
                else:
                    out.append("        const float2 off = float2(0.0, 0.0);")
                out.append("        tevcoord = " + ("tevcoord + " if add_prev else "") + "texel + off;")
                coord = f"tevcoord / material.tex_scale[{s['map']}].zw"
            out.append(f"        const float4 t = tex[{s['map']}].Sample(samp[{s['map']}], {coord})"
                       f".{swizzle(p['swap'][s['tex_swap'] & 3])};")
        else:
            out.append("        const float4 t = float4(0.0, 0.0, 0.0, 0.0);")
        ch = s["channel"]
        ras = {4: "i.color", 5: "i.color1"}.get(ch, "float4(0.0, 0.0, 0.0, 0.0)")
        if ch in (7, 8):
            raise Unsupported("bump alpha raster")
        out.append(f"        const float4 ras = {ras}.{swizzle(p['swap'][s['ras_swap'] & 3])};")
        if 14 in s["color_in"]:
            out.append(f"        const float3 k = float4({konst_color(s['konst_color'])}, 0.0).rgb;")
        if 6 in s["alpha_in"]:
            out.append(f"        const float ka = {konst_alpha(s['konst_alpha'])};")
        a, b, c, d = (CC[x] for x in s["color_in"])
        a, b, c = (f"j3d_in({x})" if x.split(".")[0] in DEST else x for x in (a, b, c))
        if s["color_op"] <= 1:
            sign = "-" if s["color_op"] == 1 else "+"
            expr = f"({d} {sign} lerp({a}, {b}, {c}){BIAS[s['color_bias']]}){SCALE[s['color_scale']]}"
        else:
            op = s["color_op"]
            if op in (14, 15):
                cmp = ">" if op == 14 else "=="
                expr = (f"{d} + float3(round({a} * 255.0) {cmp} round({b} * 255.0)) * {c}")
            else:
                pack = {8: "round(x.r * 255.0)", 10: "round(x.g * 255.0) * 256.0 + round(x.r * 255.0)",
                        12: "dot(round(x * 255.0), float3(1.0, 256.0, 65536.0))"}[op & ~1]
                cmp = ">" if op % 2 == 0 else "=="
                pa, pb = pack.replace("x", f"({a})"), pack.replace("x", f"({b})")
                expr = f"{d} + (({pa}) {cmp} ({pb}) ? {c} : float3(0.0, 0.0, 0.0))"
        if s["color_clamp"]:
            expr = f"saturate({expr})"
        out.append(f"        const float3 rc = {expr};")
        a, b, c, d = (CA[x] for x in s["alpha_in"])
        a, b, c = (f"j3d_in({x})" if x.split(".")[0] in DEST else x for x in (a, b, c))
        if s["alpha_op"] <= 1:
            sign = "-" if s["alpha_op"] == 1 else "+"
            aexpr = f"({d} {sign} lerp({a}, {b}, {c}){BIAS[s['alpha_bias']]}){SCALE[s['alpha_scale']]}"
        elif s["alpha_op"] in (14, 15):
            cmp = ">" if s["alpha_op"] == 14 else "=="
            aexpr = f"{d} + (round({a} * 255.0) {cmp} round({b} * 255.0) ? {c} : 0.0)"
        else:
            raise Unsupported(f"alpha compare op {s['alpha_op']} on colour channels")
        if s["alpha_clamp"]:
            aexpr = f"saturate({aexpr})"
        out.append(f"        const float ra = {aexpr};")
        out.append(f"        {DEST[s['color_reg'] & 3]}.rgb = rc;")
        out.append(f"        {DEST[s['alpha_reg'] & 3]}.a = ra;")
        out.append("    }")
    # What reaches the PE: the last stage's destination registers (whichever it wrote), their low 8 bits.
    if p["stages"]:
        last = p["stages"][-1]
        out.append(f"    prev = j3d_out(float4({DEST[last['color_reg'] & 3]}.rgb, {DEST[last['alpha_reg'] & 3]}.a));")
    else:
        out.append("    prev = j3d_out(prev);")
    comp0, op, comp1 = p["alpha_compare"]
    if not (comp0 == 7 and comp1 == 7) and not (op == 1 and (comp0 == 7 or comp1 == 7)):
        logic = ["&&", "||", "!=", "=="][op & 3]
        c0 = COMPARE[comp0 & 7].replace("r8", "round(material.alpha_ref.x * 255.0)")
        c1 = COMPARE[comp1 & 7].replace("r8", "round(material.alpha_ref.y * 255.0)")
        out.append("    const float a8 = round(prev.a * 255.0);")
        out.append(f"    if (!(({c0}) {logic} ({c1}))) {{")
        out.append("        discard;")
        out.append("    }")
    out.append("    prev.rgb = j3d_fog(prev.rgb, i.pos);")
    out.append("    return prev;")
    out.append("}")


def write_if_changed(path, text):
    """Writes `path` only when its content differs, so the build recompiles only the shaders that changed."""
    try:
        with open(path, "r", encoding="utf-8", newline="") as f:
            if f.read() == text:
                return False
    except FileNotFoundError:
        pass
    with open(path, "w", encoding="utf-8", newline="\n") as f:
        f.write(text)
    return True


def emit(paths, out_dir):
    import os
    records = read(paths)
    programs = sorted(set(records))
    keys = {}
    for r in programs:
        keys.setdefault(shader_key(parse(r)), r)
    os.makedirs(out_dir, exist_ok=True)
    made, failed = 0, Counter()
    table = []
    for key, rec in sorted(keys.items(), key=lambda kv: repr(kv[0])):
        p = parse(rec)
        lines = []
        try:
            emit_vertex(p, lines)
            emit_pixel(p, lines)
        except Unsupported as e:
            failed[str(e)] += 1
            continue
        made += 1
        ident = made
        write_if_changed(os.path.join(out_dir, f"m{ident:04d}.hlsl"),
                         f"// J3D material {ident} (tools/dev/gen_j3d_materials.py from the game's material set; E-04)\n"
                         '#include "j3d_material.hlsli"\n\n' + "\n".join(lines) + "\n")
        table.append((ident, key))
    print(f"distinct keys {len(keys)}, emitted {made}, unsupported {sum(failed.values())}: {dict(failed)}")
    return table, programs


# The bytes a program's lookup ignores (J3DNativeMaterial.cpp nativeShaderOf zeroes the same): cull, dither, early
# depth, origin, differ flags, fog range adjustment, blend, depth and the texture matrices' places.
LOOKUP_ZERO = [(4, 8), (12, 16), (689, 712), (716, 724), (724, 756)]


def lookup_bytes(rec):
    b = bytearray(rec)
    for start, end in LOOKUP_ZERO:
        b[start:end] = bytes(end - start)
    return bytes(b)


def fnv1a64(data):
    h = 0xCBF29CE484222325
    for byte in data:
        h ^= byte
        h = (h * 0x100000001B3) & 0xFFFFFFFFFFFFFFFF
    return h


def generate(repo, paths, subset=None):
    """The material set in the repository: runtime/src/render/materials/j3d/m<NNNN>.hlsl, its CMake list, and the
    game's table (each dumped program's FNV-1a hash -> its material's index, 1-based). Files are written only when
    their content changed. `subset` (dump files): the build list and the game's table hold only the materials those
    programs use (numbered as in the whole set), the others staying on the GX path: the probe loop's build."""
    import glob
    import os
    out_dir = os.path.join(repo, "runtime", "src", "render", "materials", "j3d")
    table, programs = emit(paths, out_dir)
    keep = {f"m{ident:04d}.hlsl" for ident, _ in table}
    for old in glob.glob(os.path.join(out_dir, "m*.hlsl")):
        if os.path.basename(old) not in keep:
            os.remove(old)
    index_of = {key: ident for ident, key in table}
    chosen = None
    if subset:
        chosen = {index_of.get(shader_key(parse(r))) for r in read(subset)} - {None}
    listed = [ident for ident, _ in table if chosen is None or ident in chosen]
    write_if_changed(os.path.join(out_dir, "list.cmake"),
                     "# Generated by tools/dev/gen_j3d_materials.py: the J3D material set (NebulaShaderId J3D_FIRST + n - 1).\n"
                     "set(galaxy_j3d_materials\n" + "".join(f"    m{ident:04d}\n" for ident in listed) + ")\n")
    pairs = {}
    for rec in programs:
        ident = index_of.get(shader_key(parse(rec)))
        if ident is not None and (chosen is None or ident in chosen):
            pairs[fnv1a64(lookup_bytes(rec))] = ident
    pairs = sorted(pairs.items())
    inc = os.path.join(repo, "native", "petari", "src", "JSystem", "J3DGraphBase", "J3DMaterialShaders.inc")
    write_if_changed(inc,
                     "// Generated by tools/dev/gen_j3d_materials.py: a material program's FNV-1a hash -> its J3D material (1-based;\n"
                     "// NebulaShaderId NEBULA_SHADER_J3D_FIRST + n - 1). Sorted by hash.\n" +
                     "".join(f"    {{0x{h:016X}ull, {ident}u}},\n" for h, ident in pairs))
    print(f"table: {len(pairs)} programs -> {len(listed)} of {len(table)} materials")


def main():
    if len(sys.argv) >= 3 and sys.argv[1] == "census":
        census(sys.argv[2:])
    elif len(sys.argv) >= 4 and sys.argv[1] == "emit":
        emit(sys.argv[3:], sys.argv[2])
    elif len(sys.argv) >= 4 and sys.argv[1] == "generate":
        # generate REPO DUMP... [--subset DUMP...]
        args = sys.argv[3:]
        subset = args[args.index("--subset") + 1:] if "--subset" in args else None
        dumps = args[:args.index("--subset")] if "--subset" in args else args
        generate(sys.argv[2], dumps, subset)
    else:
        raise SystemExit(__doc__)


if __name__ == "__main__":
    main()
