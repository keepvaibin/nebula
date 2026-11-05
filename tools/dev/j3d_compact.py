"""A generated J3D material's constants, reduced to what it reads: compact(text) returns the shader with its own
MaterialConstants (the values it uses, in slot order) and the mask of those slots (J3DNativeMaterial packs by it). Texture
coordinates lose the scale that cancels, registers the wrap they cannot need. crates/nebula-import/src/cook/shader_source.rs
compact is the same in Rust."""
import re

SLOTS = (["reg1", "reg2", "reg3"] + [f"kcolor{i}" for i in range(4)] + [f"mat_color{i}" for i in range(2)]
         + [f"amb_color{i}" for i in range(2)] + [f"ind_mtx{i}" for i in range(6)] + ["fog", "fog_color", "alpha_ref"]
         + [f"tex_scale{i}" for i in range(8)] + ["ovr", "ovr_coord"])
ALWAYS = {"fog", "fog_color", "ovr", "ovr_coord"}
WHITE = "float4(1.0, 1.0, 1.0, 1.0)"
DECLARATION = re.compile(r"^\s*(?:const )?float4 (prev|r0|r1|r2) = material\.reg\[\d\];$")
SAMPLE = re.compile(r"j3d_texel\(i\.uv\[(\d)\], \du\)((?: / float2\([0-9.]+, [0-9.]+\))?) / material\.tex_scale\[(\d)\]\.zw")
TEXEL = re.compile(r"j3d_texel\(i\.uv\[(\d)\], \du\)")
WRAP = re.compile(r"j3d_in\((prev|r0|r1|r2)\.(rgb|aaa|a)\)")
NAMED = re.compile(r"material\.(reg|kcolor|mat_color|amb_color|ind_mtx|tex_scale)\[(\d+)\]")
USED = re.compile(r"material\.([a-z_]+[0-9]*)\b")


def drop_unused_registers(lines):
    """Register declarations no line of their scope reads (the replacing pass's block, then the rest)."""
    begin = next((n for n, x in enumerate(lines) if "if (material.ovr.x != 0.0) {" in x), None)
    end = begin
    if begin is not None:
        end = next(n for n in range(begin, len(lines)) if lines[n].strip() == "}")

    def scope_of(n):
        if begin is not None and begin < n < end:
            return lines[begin:end]
        return lines[end:] if end is not None else lines

    out = []
    for n, line in enumerate(lines):
        m = DECLARATION.match(line)
        if m:
            name = re.compile(r"\b%s\b" % m.group(1))
            if sum(1 for x in scope_of(n) if name.search(x)) <= 1:
                continue
        out.append(line)
    return out


def cancel_scales(text):
    maps, texels = {}, set()
    for m in SAMPLE.finditer(text):
        maps.setdefault(m.group(1), set()).add(m.group(3))
    for m in TEXEL.finditer(text):
        if not SAMPLE.match(text, m.start()):
            texels.add(m.group(1))

    def plain(m):
        coord = m.group(1)
        if coord in texels or len(maps[coord]) != 1:
            return m.group(0)
        return f"j3d_coord(i.uv[{coord}]){m.group(2)}"

    text = SAMPLE.sub(plain, text)
    return TEXEL.sub(lambda m: f"(j3d_coord(i.uv[{m.group(1)}]) * material.tex_scale[{m.group(1)}].xy)", text)


def drop_wraps(lines):
    """A register read through its low 8 bits needs that only if it can hold more: a clamped stage wrote it, or not."""
    in_range = {"prev.rgb": True, "prev.a": True}
    for r in ("r0", "r1", "r2"):
        in_range[f"{r}.rgb"] = in_range[f"{r}.a"] = False
    rc_clamped = ra_clamped = False

    def drop(m):
        part = "rgb" if m.group(2) == "rgb" else "a"
        return f"{m.group(1)}.{m.group(2)}" if in_range[f"{m.group(1)}.{part}"] else m.group(0)

    out = []
    main = False
    for line in lines:
        s = line.strip()
        if s == "float4 prev = %s;" % WHITE:
            main = True
        if main:
            if s.startswith("const float3 rc = "):
                line = WRAP.sub(drop, line)
                rc_clamped = s.startswith("const float3 rc = saturate(")
            elif s.startswith("const float ra = "):
                line = WRAP.sub(drop, line)
                ra_clamped = s.startswith("const float ra = saturate(")
            m = re.match(r"(prev|r0|r1|r2)\.(rgb|a) = (rc|ra);$", s)
            if m:
                in_range[f"{m.group(1)}.{m.group(2)}"] = rc_clamped if m.group(3) == "rc" else ra_clamped
            m = re.match(r"prev = j3d_out\(float4\((prev|r0|r1|r2)\.rgb, (prev|r0|r1|r2)\.a\)\);$", s)
            if m and in_range[f"{m.group(1)}.rgb"] and in_range[f"{m.group(2)}.a"]:
                line = line.replace("j3d_out(float4(", "(float4(")
            if s == "prev = j3d_out(prev);":
                line = line.replace("j3d_out(prev)", "prev")
        out.append(line)
    return out


def compact(text):
    text = text.replace("material.reg[0]", WHITE)
    lines = drop_unused_registers(text.split("\n"))
    text = cancel_scales("\n".join(lines))
    text = "\n".join(drop_wraps(text.split("\n")))
    text = NAMED.sub(lambda m: f"material.{m.group(1)}{m.group(2)}", text)
    used = {m.group(1) for m in USED.finditer(text)} | ALWAYS
    used.discard("hlsli")
    unknown = used - set(SLOTS)
    if unknown:
        raise ValueError(f"constants outside the slots: {sorted(unknown)}")
    mask = sum(1 << n for n, name in enumerate(SLOTS) if name in used)
    members = "".join(f"    float4 {name};\n" for name in SLOTS if name in used)
    head = f"// constants 0x{mask:08x}\nstruct MaterialConstants {{\n{members}}};\n"
    include = '#include "j3d_material.hlsli"'
    if include not in text:
        raise ValueError("no framework include")
    return text.replace(include, head + include, 1), mask
