"""E-04 step 3: the J3D material set. Reads material program dumps (GALAXY_DUMP_MATERIAL_PROGRAMS: raw
NebulaMaterialProgram records, nebula_game_api.h), reduces each to its shader key (only what a shader of it uses:
stages, their inputs, ops, swaps and constant selections, indirect texturing, texture coordinate generation, lit
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


def native_tex_gen(file_type, file_source):
    """The file's texgen type and source as the records' encoding (J3DNativeTexGen.hpp kind/source): kind bits 0-1 (0 a
    matrix, 1 emboss, 2/3 colour 0/1), bit 2 projective, bit 3 implicit w, bits 4-6 emboss light; source bits 0-3 the
    row (0 position, 1 normal, 2 colour, 3 binormal, 4 tangent, 5 + n coordinate n), bits 4-6 emboss coordinate."""
    row, emboss, kind, light = 5, 5, 0, 0
    if file_source <= 3:
        row = (0, 1, 3, 4)[file_source]
    elif 4 <= file_source <= 11:
        row = 5 + file_source - 4
    elif 12 <= file_source <= 18:
        emboss = file_source - 12
    elif file_source in (19, 20):
        row = 2
    if 2 <= file_type <= 9:
        kind, light = 1, file_type - 2
    elif file_type == 10:
        kind = 2 if file_source == 19 else 3
    projective = 1 if file_type == 0 else 0
    form = 1 if file_source <= 3 else 0
    return kind | projective << 2 | form << 3 | light << 4, row | (emboss & 7) << 4


def native_channel(diffuse, attenuation):
    """GpuDiffuseFn and GpuAttnFn as the records' encoding (J3DNativeChannel.hpp): diffuse 0 none, 1 signed, 2 clamped
    (none with specular attenuation); attenuation 0 none, 1 specular, 2 directional, 3 spot."""
    return (0 if attenuation == 0 else diffuse & 3), (1 if attenuation == 0 else 3 if attenuation == 1 else 2)


def to_native(rec):
    """A dumped record in the renderer's encoding: texgens written before it (tex_gen reserved[0] 0) and, when
    channel[0].reserved[0] is 0, all four channels' lighting."""
    rec = bytearray(rec)
    if rec[656 + 6] == 0:
        for c in range(4):
            base = 656 + 8 * c
            rec[base + 4], rec[base + 5] = native_channel(rec[base + 4], rec[base + 5])
        rec[656 + 6] = 1
    for g in range(min(rec[1], 8)):
        base = 592 + 8 * g
        if rec[base + 5] == 0:
            rec[base], rec[base + 1] = native_tex_gen(rec[base], rec[base + 1])
            rec[base + 5] = 1
    return bytes(rec)


def read(paths):
    records = []
    for path in paths:
        data = open(path, "rb").read()
        if len(data) % RECORD:
            raise SystemExit(f"{path}: {len(data)} bytes is not a whole number of {RECORD}-byte records")
        records += [to_native(data[i:i + RECORD]) for i in range(0, len(data), RECORD)]
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
      "ras.rgb", "ras.aaa", "float3(1.0, 1.0, 1.0)", "float3(128.0, 128.0, 128.0) / 255.0", "k.rgb",
      "float3(0.0, 0.0, 0.0)"]
CA = ["prev.a", "r0.a", "r1.a", "r2.a", "t.a", "ras.a", "ka", "0.0"]
# The fixed constant selections (1, 7/8 ... 1/8 as the combiner's 8-bit values 255, 223 ... 32), its HALF input (128)
# and its bias (+-128), on the 0-1 scale.
FRACTION = ["1.0", "223.0 / 255.0", "191.0 / 255.0", "159.0 / 255.0", "128.0 / 255.0", "96.0 / 255.0", "64.0 / 255.0",
            "32.0 / 255.0"]
BIAS = ["", " + 128.0 / 255.0", " - 128.0 / 255.0"]
SCALE = ["", " * 2.0", " * 4.0", " * 0.5"]
DEST = ["prev", "r0", "r1", "r2"]
COMPARE = ["false", "a8 < r8", "a8 == r8", "a8 <= r8", "a8 > r8", "a8 != r8", "a8 >= r8", "true"]


def konst_color(sel):
    if sel < 8:
        return f"float3({FRACTION[sel]}, {FRACTION[sel]}, {FRACTION[sel]})"
    if 0x0C <= sel <= 0x0F:
        return f"material.kcolor[{sel - 0x0C}].rgb"
    if 0x10 <= sel <= 0x1F:
        comp = "rgba"[(sel - 0x10) // 4]
        return f"material.kcolor[{(sel - 0x10) % 4}].{comp * 3}"
    raise Unsupported(f"constant colour selection {sel}")


def konst_alpha(sel):
    if sel < 8:
        return FRACTION[sel]
    if 0x10 <= sel <= 0x1F:
        return f"material.kcolor[{(sel - 0x10) % 4}].{'rgba'[(sel - 0x10) // 4]}"
    raise Unsupported(f"constant alpha selection {sel}")


def swizzle(table):
    return "".join("rgba"[c & 3] for c in table) if table else "rgba"


def emit_vertex(p, out):
    out.append("void material_vertex(NativeVertex v, inout NativeVaryings o) {")
    # The channels first: a colour texgen (SRTG) reads channel 0's result.
    emit_channels(p, out)
    for i, (gen_kind, source, matrix, normalize, post) in enumerate(p["tex_gen"][:p["tex_gens"]]):
        kind, projective, row = gen_kind & 3, (gen_kind >> 2) & 1, source & 15
        if kind in (2, 3):
            out.append(f"    o.uv[{i}] = float4(o.color.rg, 1.0, 0.0);")
            continue
        if kind != 0:
            raise Unsupported(f"texgen kind {kind} (emboss)")
        if row == 0:
            coord = "float4(v.pos, 1.0)"
        elif row == 1:
            coord = "float4(v.nrm, 1.0)"
        elif row == 3:
            coord = "float4(v.tangent, 1.0)"  # the binormal row (Dolphin BinormalT), as the uber shader read it
        elif row == 4:
            coord = "float4(v.binormal, 1.0)"  # the tangent row (BinormalB)
        elif 5 <= row <= 12:
            coord = f"float4(v.uv[{row - 5}], 1.0, 1.0)"
        else:
            raise Unsupported(f"texgen source row {row}")
        gen_type = 0 if projective else 1  # three outputs or two
        out.append(f"    {{  // coordinate {i}: kind {kind}, projective {projective}, row {row}")
        out.append(f"        const float4 c = {coord};")
        # The texgen's matrix from matrix memory by the index the shape loads (its current matrix, taken over
        # the material's texgen id: GPU_TEXMTX0 30 + 3k, 60 the identity) or the vertex's own; its post-transform matrix
        # by the program's row (GPU_PTTEXMTX0's 0 + 3k, 61 the identity).
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
        # A projected coordinate's q of 0: s and t are halved and clamped (as the uber shader and Dolphin).
        projected = "true" if gen_type == 0 else "posted"
        out.append(f"        if ({projected} && res.z == 0.0) res.xy = clamp(res.xy * 0.5, -1.0, 1.0);")
        out.append(f"        o.uv[{i}] = float4(res, {projected} ? 1.0 : 0.0);")
        out.append("    }")
    out.append("    j3d_vertex_override(v, o);")
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
            xf_atten = atten  # the records' encoding is the renderer's (to_native)
            xf_diffuse = diffuse
            terms = " + ".join(f"j3d_light({n}u, {xf_atten}u, {xf_diffuse}u, v.eye, nrm).{part}"
                               for n in range(8) if (mask >> n) & 1) or "0.0"
            out.append(f"    {target}.{part} = {mat} * saturate(({amb} * 255.0 + {terms}) / 255.0);")


def alpha_expression(s):
    """Stage `s`'s alpha combiner over the names t, ras, ka, prev and r0-r2."""
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
    return aexpr


def emit_pixel(p, out):
    out.append("float4 material_color(NativeVaryings i) {")
    # A pass that replaces the stages (j3d_material.hlsli j3d_override): texture 0 at coordinate 0, the channel colour,
    # stage 0's constant alpha.
    t0 = ("tex[0].Sample(samp[0], j3d_texel(i.uv[0], 0u) / material.tex_scale[0].zw)" if p["tex_gens"] > 0
          else "float4(0.0, 0.0, 0.0, 0.0)")
    k0 = konst_alpha(p["stages"][0]["konst_alpha"]) if p["stages"] else "1.0"
    out.append("    [branch] if (material.ovr.x != 0.0) {")
    # Stage 0's own alpha over texture 0 at coordinate 0 and no channel (mode 5: J3DModelX list 11, a one-stage
    # program without channels that keeps the material's stage 0 alpha).
    a0 = "1.0"
    if p["stages"]:
        out.append(f"        const float4 t = {t0};")
        out.append("        const float4 ras = float4(0.0, 0.0, 0.0, 0.0);")
        out.append("        const float4 prev = material.reg[0];")
        out.append("        const float4 r0 = material.reg[1];")
        out.append("        const float4 r1 = material.reg[2];")
        out.append("        const float4 r2 = material.reg[3];")
        out.append(f"        const float ka = {k0};")
        a0 = alpha_expression(p["stages"][0])
        t0 = "t"
    out.append(f"        return j3d_override({t0}, i.color, {k0}, {a0}, i.pos);")
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
        out.append(f"        const float ra = {alpha_expression(s)};")
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


def shader_key_bytes(key):
    """Cooker's shader identity; no source load masks or inactive records."""
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


def generate(repo, paths, subset=None):
    """The material set in the repository: runtime/src/render/materials/j3d/m<NNNN>.hlsl, its CMake list, and the
    game's table (each dumped program's FNV-1a hash -> its material's index, 1-based). Files are written only when
    their content changed. `subset` (dump files): the build list and the game's table hold only the materials those
    programs use (numbered as in the whole set), the others not built: the probe loop's build."""
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
    cook_keys = {}
    for ident, key in table:
        if chosen is not None and ident not in chosen:
            continue
        h = fnv1a64(shader_key_bytes(key))
        if h in cook_keys:
            raise RuntimeError('cook shader key hash collision')
        cook_keys[h] = ident
    write_if_changed(os.path.join(repo, 'crates', 'nebula-import', 'src', 'cook', 'material_shader_keys.inc'),
                     '// Generated by tools/dev/gen_j3d_materials.py: semantic shader key -> one-based J3D ID.\n'
                     '&[\n' + ''.join(f'    (0x{h:016x}u64, {ident}u32),\n' for h, ident in sorted(cook_keys.items())) + ']\n')
    write_if_changed(os.path.join(out_dir, "list.cmake"),
                     "# Generated by tools/dev/gen_j3d_materials.py: the J3D material set (NebulaShaderId J3D_FIRST + n - 1).\n"
                     "set(galaxy_j3d_materials\n" + "".join(f"    m{ident:04d}\n" for ident in listed) + ")\n")
    print(f"cook catalog: {len(listed)} of {len(table)} precompiled materials")



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
