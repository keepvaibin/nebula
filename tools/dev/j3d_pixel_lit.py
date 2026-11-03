"""A generated J3D material's per-pixel lighting (j3d_material.hlsli): its colour channels as a function both stages can
call, and in the NATIVE_PIXEL_LIT variant the pixel stage calling it, plainly or under enhanced lighting. Applied by
gen_j3d_materials.py to each material it writes.

    python -I tools/dev/j3d_pixel_lit.py <materials/j3d dir>    rewrites a generated set in place
"""
import glob
import os
import re
import sys

CHANNEL = re.compile(r"^    o\.color1?(?:\.[a-z]+)? = .*;$", re.M)
LIT = re.compile(r"^    o\.color\.rgb = (.*?) \* saturate\(\((.*?) \* 255\.0 \+ (.*?)\) / 255\.0\);$")
LIGHT = re.compile(r"j3d_light\((\d+)u, (\d+)u, (\d+)u, v\.eye, nrm\)\.rgb")
NORMAL_ROWS = """    const uint nq = v.pn_row & 31u;
    const float3 nt = float3(dot(native_normal_row(nq).xyz, v.nrm), dot(native_normal_row(nq + 1u).xyz, v.nrm), dot(native_normal_row(nq + 2u).xyz, v.nrm));
    const float3 nrm = dot(nt, nt) > 0.0 ? normalize(nt) : nt;
"""


def env_coordinates(text):
    """The texture coordinates generated from the normal: what an environment map reads."""
    found = set()
    for m in re.finditer(r"// coordinate (\d+):[^\n]*\n\s*const float4 c = float4\(v\.nrm, 1\.0\);", text):
        found.add(m.group(1))
    return found


def names(line):
    return (line.replace("v.eye", "eye").replace("v.color0", "vtx0").replace("v.color1", "vtx1")
            .replace("o.color1", "lit1").replace("o.color", "lit0"))


def enhanced_channel(line, metal):
    m = LIT.match(line)
    if m is None:
        return ["    " + names(line).strip()]
    mat, amb, terms = m.group(1), m.group(2), m.group(3)
    out = ["    {", f"        const float3 enh_mat = {names(mat)};", f"        const float3 enh_amb = {names(amb)};",
           "        const float3 enh_amb_lin = enh_lin(enh_amb);", "        float3 enh_acc = enh_amb_lin * enh_sky(g_enh);"]
    first = True
    for n, atten, diffuse in LIGHT.findall(terms):
        out.append(f"        enh_add_light({n}u, {'true' if atten == '3' else 'false'}, {diffuse}u, {'true' if first else 'false'}, "
                   f"{metal}, eye, enh_amb, enh_amb_lin, enh_acc);")
        first = False
    out += ["        enh_acc *= enh_sky_reflection(g_enh, enh_amb_lin);",
            "        lit0.rgb = enh_mat * enh_srgb(enh_shoulder(max(enh_acc, 0.0)));", "    }"]
    return out


def pixel_lit(text):
    if "NATIVE_PIXEL_LIT" in text:
        return text
    newline = "\r\n" if "\r\n" in text else "\n"
    text = text.replace("\r\n", "\n")
    env = env_coordinates(text)
    lit = "j3d_light(" in text
    if not lit and not env:
        return text.replace("\n", newline)
    metal = "true" if env else "false"
    head = text.index("void material_vertex(")
    block = ""
    if lit:
        lines = CHANNEL.findall(text)
        first = text.index(lines[0])
        last = text.index(lines[-1]) + len(lines[-1]) + 1
        assert text[first:last].strip("\n").split("\n") == lines, "channels are together"
        body = "".join("    " + names(line).strip() + "\n" for line in lines)
        enhanced = "".join(row + "\n" for line in lines for row in enhanced_channel(line, metal))
        block = ("void j3d_channels(float3 eye, float3 nrm, float4 vtx0, float4 vtx1, out float4 lit0, out float4 lit1) {\n"
                 "    lit0 = vtx0;\n    lit1 = vtx1;\n" + body + "}\n"
                 "#if NATIVE_PIXEL_LIT\n"
                 "void j3d_channels_enhanced(float3 eye, float3 nrm, float4 vtx0, float4 vtx1, out float4 lit0, out float4 lit1) {\n"
                 "    lit0 = vtx0;\n    lit1 = vtx1;\n" + enhanced + "}\n#endif\n")
        vertex = ("#if NATIVE_PIXEL_LIT\n    o.color = v.color0;\n    o.color1 = v.color1;\n    o.lit_eye = v.eye;\n    o.lit_nrm = nrm;\n"
                  "#else\n    j3d_channels(v.eye, nrm, v.color0, v.color1, o.color, o.color1);\n#endif\n")
        text = text[:first] + vertex + text[last:]
    else:
        # An unlit environment map: only the variant needs the normal.
        # (A channel enabled with no lights has the rows already, for every variant.)
        has_rows = NORMAL_ROWS in text
        at = text.index(NORMAL_ROWS) + len(NORMAL_ROWS) if has_rows else text.index("{\n", head) + 2
        text = (text[:at] + "#if NATIVE_PIXEL_LIT\n" + ("" if has_rows else NORMAL_ROWS)
                + "    o.lit_eye = v.eye;\n    o.lit_nrm = nrm;\n#endif\n" + text[at:])
    text = text[:head] + block + text[head:]
    begin = (f"#if NATIVE_PIXEL_LIT\n    j3d_pixel_begin(i);\n"
             + ("    j3d_pixel_channels(i);\n" if lit else f"    if (j3d_enhanced()) enh_key_highlight(g_enh, i.lit_eye);\n")
             + "#endif\n")
    at = text.index("float4 material_color(NativeVaryings i) {\n") + len("float4 material_color(NativeVaryings i) {\n")
    text = text[:at] + begin + text[at:]
    for coordinate in sorted(env):
        text = re.sub(r"const float4 t = (tex\[\d+\]\.Sample\(samp\[\d+\], j3d_texel\(i\.uv\[" + coordinate + r"\], " + coordinate + r"u\)[^;\n]*);",
                      lambda m: f"const float4 t = J3D_ENV({m.group(1)});", text)
    finish = "#if NATIVE_PIXEL_LIT\n    if (j3d_enhanced()) prev.rgb = enh_finish(prev.rgb, g_enh);\n#endif\n"
    marker = "    prev.rgb = j3d_fog(prev.rgb, i.pos);\n"
    at = text.rfind(marker)
    assert at >= 0, "the material's fog"
    text = text[:at] + finish + text[at:]
    return text.replace("\n", newline)


if __name__ == "__main__":
    changed = 0
    for path in sorted(glob.glob(os.path.join(sys.argv[1], "m*.hlsl"))):
        with open(path, encoding="utf-8", newline="") as f:
            old = f.read()
        new = pixel_lit(old)
        if new != old:
            with open(path, "w", encoding="utf-8", newline="") as f:
                f.write(new)
            changed += 1
    print("materials rewritten:", changed)
