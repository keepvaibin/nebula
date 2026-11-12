"""Every program in every output variant, into one file. compile_programs.py dxil|spirv OUT.bin [SPIRV_TOOL]

The generated programs are runtime/src/render/programs/ (build_programs.py); table.tsv maps every text's hash to the program
that compiled the same. The written shaders are `WRITTEN` in NebulaShaderId order, from 1. Compiled stages are kept by
content in OUT's folder, so a rebuild compiles what changed. Format: core/program_table.cpp."""
import hashlib
import os
import re
import struct
import subprocess
import sys
from concurrent.futures import ThreadPoolExecutor

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
MATERIALS = os.path.join(ROOT, "runtime", "src", "render", "materials")
PROGRAMS = os.path.join(ROOT, "runtime", "src", "render", "programs")
DXC = os.environ.get("GALAXY_DXC", "dxc")
JOBS = int(os.environ.get("JOBS", "4"))
SITE = 1 << 63

WRITTEN = """whirlpool ocean_bowl screen_blur flat_color clip_fill big_bubble whirlpool_accelerator spin_driver_path spider_thread
tex_modulate water_plant ocean_ring_pipe_inside ocean_sphere ocean_sphere_env ocean_ring ocean_ring_bloom water_road
ocean_ring_pipe_outside color_tex_alpha vertex_color dd_texture dd_texture_color tex_vertex_alpha tex_alpha_lerp trample_star
tex_const_alpha font_text j2d_picture fur particle ocean_bowl_bloom mii_face mii_texture image_effect gaussian_blur dof_final
normal_map movie water_camera frame_ripple frame_depth particle_instances glyph_instances""".split()
# Vertex stages of another's pixel stage, never widened: particle_instances (particle.hlsl's pixel stage), glyph_instances
# (font_text.hlsl's).
VERTEX_ONLY = {"particle_instances", "glyph_instances"}

VARIANTS = {
    "base": (0, []),
    "reactive": (1, ["NATIVE_REACTIVE"]),
    "layer": (2, ["NATIVE_REACTIVE", "NATIVE_LAYER"]),
    "motion": (3, ["NATIVE_MOTION"]),
    "sample": (4, ["NATIVE_SAMPLE"]),
}
# Ray tracing's forms of a pixel program, stored after the bound-table forms and never in one (a device that traces
# has descriptor indexing): every program's prepass and traced forms.
TRACED_VARIANTS = {
    "prepass": (10, ["NATIVE_PREPASS"]),
    "traced": (11, ["NATIVE_TRACED"]),
    "traced_motion": (12, ["NATIVE_TRACED", "NATIVE_MOTION"]),
    "traced_reactive": (13, ["NATIVE_TRACED", "NATIVE_REACTIVE"]),
    "traced_layer": (14, ["NATIVE_TRACED", "NATIVE_REACTIVE", "NATIVE_LAYER"]),
    # Vulkan without geometry shaders: the prepass names the item only (D11). SPIR-V only.
    "prepass_item": (15, ["NATIVE_PREPASS", "NATIVE_PREPASS_ITEM"]),
}
VERTEX_FORMS = ("base", "motion")
# A Vulkan device without descriptor indexing reads each pixel program's bound-table form (program.hlsli
# NEBULA_BOUND_TABLES): stored at the variant + BOUND_OFFSET, STORED variants to a row (core/program_table.h).
BOUND_OFFSET = 5
STORED = 16
READS_WORLD = 1
LIT = 2
SUMS_KEY = 4  # a pixel program that sums its light set's first light: light_term or enh_add_light of light 0
# A written material that sums the game's lights (native_material.hlsli NATIVE_LIT): its traced form has a vertex
# form of its own (the light varyings), and its pixel program sums its key at the pixel there (SUMS_KEY).
WRITTEN_LIT = re.compile(r"^\s*#\s*define\s+NATIVE_LIT\s+1", re.M)


def native_coords(path):
    """The texture coordinates a written shader's vertex stage reads: its NATIVE_COORDS, from the file or the .hlsl it includes."""
    text = read(path)
    found = re.search(r"^\s*#\s*define\s+NATIVE_COORDS\s+(\d+)", text, re.M)
    if found:
        return int(found.group(1))
    for name in re.findall(r'^\s*#\s*include\s+"([^"]+\.hlsl)"', text, re.M):
        inner = native_coords(os.path.join(MATERIALS, name))
        if inner is not None:
            return inner
    return None


def read(path):
    with open(path, encoding="utf-8", errors="replace", newline="") as f:
        return f.read().replace("\r\n", "\n")


def main():
    backend, out = sys.argv[1], sys.argv[2]
    args = sys.argv[3:]
    spirv_tool = args[0] if args else None
    cache = out + ".stages"
    os.makedirs(cache, exist_ok=True)
    headers = "".join(read(os.path.join(MATERIALS, n)) for n in sorted(os.listdir(MATERIALS)) if n.endswith(".hlsli"))
    tool_id = backend + (DXC if backend == "dxil" else spirv_tool or "")
    if backend == "spirv" and spirv_tool:
        # Compiler location/binding rewrites affect bytes even with unchanged HLSL.
        with open(spirv_tool, "rb") as compiler:
            tool_id += hashlib.sha256(compiler.read()).hexdigest()

    # (stage, id) -> {variant index: job}
    jobs = {}
    rows = {}
    flags = {}
    coords = {}

    def add(stage, row, source, text, entry, defines, variant, bound_form=True):
        key = hashlib.sha1(("\0".join([tool_id, headers, text, stage, entry] + defines)).encode()).hexdigest()
        jobs[key] = (source, stage, entry, defines)
        rows.setdefault((stage, row), {})[variant] = key
        if backend == "spirv" and stage == "ps" and bound_form:
            bound = defines + ["NEBULA_BOUND_TABLES"]
            bound_key = hashlib.sha1(("\0".join([tool_id, headers, text, stage, entry] + bound)).encode()).hexdigest()
            jobs[bound_key] = (source, stage, entry, bound)
            rows[(stage, row)][variant + BOUND_OFFSET] = bound_key

    def add_program(stage, row, source, entry):
        text = read(source)
        if stage == "vs":
            for name in VERTEX_FORMS:
                index, defines = VARIANTS[name]
                add(stage, row, source, text, entry, defines, index)
            # A vertex stage that de-lights its colour (programs.rs, D2) does so in its traced forms alone; the tracer's
            # draws and their prepass run those (program_table.cpp program_bytes).
            if "traced_vertex_gain(" in text:
                for name in ("traced", "traced_motion"):
                    index, defines = TRACED_VARIANTS[name]
                    add(stage, row, source, text, entry, defines, index, False)
            flags[(stage, row)] = READS_WORLD if "world_" in text else 0
            return
        for name, (index, defines) in VARIANTS.items():
            if name == "sample" and "discard" not in text:
                continue
            add(stage, row, source, text, entry, defines, index)
        for name, (index, defines) in TRACED_VARIANTS.items():
            if name == "prepass_item" and backend != "spirv":
                continue
            add(stage, row, source, text, entry, defines, index, False)
        flags[(stage, row)] = (LIT if "program_lit.hlsli" in text else 0) | (
            SUMS_KEY if re.search(r"(light_term|enh_add_light)\(i\.lit_light, 0u", text) or WRITTEN_LIT.search(text) else 0)

    for number, name in enumerate(WRITTEN, 1):
        source = os.path.join(MATERIALS, name + ".hlsl")
        row = SITE | number << 1
        count = native_coords(source)
        if count is None:
            print(f"{source} defines no NATIVE_COORDS")
            return 1
        coords[("vs", row)] = coords[("vs", row | 1)] = count
        add_program("vs", row, source, "native_vs")
        if WRITTEN_LIT.search(read(source)):
            for name in ("traced", "traced_motion"):
                index, defines = TRACED_VARIANTS[name]
                add("vs", row, source, read(source), "native_vs", defines, index)
        if name in VERTEX_ONLY:
            continue
        add_program("ps", row, source, "native_ps")
        # Lines and points are widened in this form.
        text = read(source)
        for form in VERTEX_FORMS:
            index, defines = VARIANTS[form]
            add("vs", row | 1, source, text, "native_vs", defines + ["NATIVE_WIDE"], index)
        flags[("vs", row | 1)] = 0

    table = {}
    for line in read(os.path.join(PROGRAMS, "table.tsv")).splitlines():
        stage, text_hash, group = line.split("\t")
        table[("vs" if stage == "v" else "ps", int(text_hash, 16))] = group
    groups = {}
    for (stage, text_hash), group in table.items():
        if group not in groups:
            add_program(stage, int(group[1:], 16), os.path.join(PROGRAMS, group + ".hlsl"), "program_" + stage)
            groups[group] = (stage, int(group[1:], 16))

    def compile_job(item):
        key, (source, stage, entry, defines) = item
        target = os.path.join(cache, key)
        if os.path.exists(target) and os.path.getsize(target) != 0:
            return None
        temp = target + ".tmp"
        if backend == "dxil":
            command = [DXC, "-nologo", "-T", stage + "_6_0", "-E", entry, "-O3", "-WX", "-Qstrip_debug", "-Qstrip_reflect",
                       "-I", MATERIALS, "-Fo", temp, source]
            for d in defines:
                command.insert(1, "-D" + d + "=1")
        else:
            command = [spirv_tool, source, MATERIALS, stage, entry, temp] + defines
        result = subprocess.run(command, capture_output=True, text=True)
        if result.returncode != 0 or not os.path.exists(temp):
            return f"{source} {stage} {' '.join(defines)}\n{result.stdout[:800]}{result.stderr[:1500]}"
        os.replace(temp, target)
        return None

    with ThreadPoolExecutor(JOBS) as pool:
        failed = [r for r in pool.map(compile_job, jobs.items()) if r]
    for f in failed[:40]:
        print("FAILED", f)
    if failed:
        print(len(failed), "of", len(jobs), "failed")
        return 1

    # Two programs compiling the same mean the set's dedupe missed: the build fails.
    seen = {}
    for stage, row in groups.values():
        with open(os.path.join(cache, rows[(stage, row)][0]), "rb") as f:
            digest = (stage, hashlib.sha256(f.read()).digest())
        if digest in seen:
            print(f"programs {seen[digest]:016x} and {row:016x} compile the same")
            return 1
        seen[digest] = row

    # Every text's hash reads its program's row.
    for (stage, text_hash), group in table.items():
        rows.setdefault((stage, text_hash), rows[(stage, groups[group][1])])
        flags.setdefault((stage, text_hash), flags[(stage, groups[group][1])])

    interface = hashlib.sha1(headers.encode()).hexdigest()
    vertex = sorted(r for s, r in rows if s == "vs")
    pixel = sorted(r for s, r in rows if s == "ps")
    at = 64 + (len(vertex) + len(pixel)) * (16 + 8 * STORED)
    placed = {}
    chunks = []
    sizes = {}

    def place(key):
        nonlocal at
        if key in placed:
            return placed[key]
        with open(os.path.join(cache, key), "rb") as f:
            data = f.read()
        digest = hashlib.sha256(data).digest()
        if digest in sizes:
            placed[key] = sizes[digest]
            return placed[key]
        pad = -at % 16
        chunks.append(b"\0" * pad + data)
        at += pad
        placed[key] = sizes[digest] = (at, len(data))
        at += len(data)
        return placed[key]

    packed = []
    for stage, ids in (("vs", vertex), ("ps", pixel)):
        for row in ids:
            words = [(0, 0)] * STORED
            for variant, key in rows[(stage, row)].items():
                words[variant] = place(key)
            packed.append(struct.pack("<QII", row, flags[(stage, row)], coords.get((stage, row), 0)) + b"".join(struct.pack("<II", *w) for w in words))
    head = b"NBPROG03" + struct.pack("<II", len(vertex), len(pixel)) + interface.encode() + b"\0" * 8
    blob = head + b"".join(packed) + b"".join(chunks)
    temp = out + ".tmp"
    with open(temp, "wb") as f:
        f.write(blob)
    os.replace(temp, out)
    print(f"programs {backend}: vertex {len(vertex)} pixel {len(pixel)} ids, {len(jobs)} stages, {len(sizes)} distinct, {len(blob)} bytes")
    live = set(jobs)
    for name in os.listdir(cache):
        if name not in live and not name.endswith(".tmp"):
            os.remove(os.path.join(cache, name))
    return 0


if __name__ == "__main__":
    sys.exit(main())
