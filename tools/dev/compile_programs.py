"""Every program in every output variant, into one file. compile_programs.py dxil|dxbc|spirv OUT.bin [SPIRV_TOOL]
compile_programs.py record: records the program set at the store version (store_programs.txt, see check_store_programs).

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
STORE = os.path.join(ROOT, "runtime", "include", "galaxy")
DXC = os.environ.get("GALAXY_DXC", "dxc")
FXC = os.environ.get("GALAXY_FXC", "fxc")  # dxbc: Direct3D 11, shader model 5.0
JOBS = int(os.environ.get("JOBS", "24"))
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
# Direct3D 11 (dxbc): no temporal upscaler, frame generation or tracer reads motion, the reactive mask, the layer or a
# traced form, so only these are compiled; and its pixel programs are the bound-table form only (shader model 5.0 has no
# unbounded arrays), stored at the variant + BOUND_OFFSET with 16 maps to a table (d3d11::kBoundTableSlots).
DXBC_FORMS = ("base", "sample")
DXBC_TABLE = ["NEBULA_BOUND_TABLES", "NEBULA_TABLE_SLOTS=16"]
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


def vertex_unused_inputs(data):
    """A SPIR-V vertex module's input variables no instruction names (only their names, their decorations and the entry
    point's list), as their locations (-1 a built-in): every one is fetched for every draw all the same (l89). None for
    a module that is not a vertex stage or not SPIR-V."""
    count = len(data) // 4
    words = struct.unpack("<%dI" % count, data[:count * 4])
    if count < 5 or words[0] != 0x07230203:
        return None
    vertex = False
    inputs, location, used = set(), {}, set()
    at = 5
    while at < count:
        n, op = words[at] >> 16, words[at] & 0xFFFF
        if n == 0 or at + n > count:
            break
        if op == 15:  # OpEntryPoint: execution model (0 vertex), id, name, interface ids
            vertex = vertex or words[at + 1] == 0
        elif op == 59 and n >= 4:  # OpVariable: type, id, storage class (1 input), initializer
            if words[at + 3] == 1:
                inputs.add(words[at + 2])
            used.update(words[at + 4:at + n])
        elif op == 71 and n >= 4 and words[at + 2] == 30:  # OpDecorate id Location n
            location[words[at + 1]] = words[at + 3]
        elif op not in (5, 6, 71, 72):  # OpName, OpMemberName, OpDecorate, OpMemberDecorate
            used.update(words[at + 1:at + n])
        at += n
    if not vertex:
        return None
    return sorted(location.get(v, -1) for v in inputs - used)


def read(path):
    with open(path, encoding="utf-8", errors="replace", newline="") as f:
        return f.read().replace("\r\n", "\n")


def included_shaders(text, seen):
    """The .hlsl files a source includes from the materials folder (recursively, each once), as one text: the .hlsli are
    in every stage's key already."""
    out = ""
    for name in re.findall(r'^\s*#\s*include\s+"([^"]+\.hlsl)"', text, re.M):
        if name not in seen:
            seen.add(name)
            inner = read(os.path.join(MATERIALS, name))
            out += "\0" + name + "\0" + inner + included_shaders(inner, seen)
    return out


def spirv_tool_id():
    """What decides galaxy_native_spirv's output, not its bytes (an MSVC link is not reproducible, and the host's CPU
    flags cannot change SPIR-V): the tool's own sources with every header of the repository they include, and the
    glslang, SPIRV-Tools and SPIRV-Headers archives cmake/vulkan_deps.cmake pins (with the build options it gives
    them). The SDL builds run the Windows Vulkan build's tool (platform/sdl/CMakeLists.txt NEBULA_HOST_TOOLS)."""
    parts = []
    pending = [os.path.join(ROOT, "tools", "dev", "native_spirv.cpp"),
               os.path.join(ROOT, "runtime", "src", "render", "vk", "shader_compile_vk.cpp")]
    seen = set()
    while pending:
        path = os.path.normpath(pending.pop(0))
        if path in seen:
            continue
        seen.add(path)
        text = read(path)
        parts.append(os.path.relpath(path, ROOT).replace("\\", "/") + "\0" + text)
        for name in re.findall(r'^\s*#\s*include\s+"([^"]+)"', text, re.M):
            for base in (os.path.dirname(path), os.path.join(ROOT, "runtime", "include")):
                candidate = os.path.join(base, name)
                if os.path.isfile(candidate):
                    pending.append(candidate)
                    break
            else:
                raise SystemExit(f"compile_programs.py: {path} includes {name}, found in neither its folder nor runtime/include")
    deps = read(os.path.join(ROOT, "cmake", "vulkan_deps.cmake"))
    # Each pinned archive of the compiler (URL and its SHA-256), and the options its build is given.
    pins = re.findall(r"(https://\S*(?:glslang|SPIRV)\S*)\s+(?:URL_HASH\s+SHA256=)?([0-9a-f]{64})", deps)
    if len(pins) != 3:
        raise SystemExit(f"compile_programs.py: cmake/vulkan_deps.cmake pins {len(pins)} compiler archives, not glslang, SPIRV-Tools and SPIRV-Headers")
    options = re.findall(r"^set\(((?:ENABLE|GLSLANG|SPIRV)_\w+ .*)\)$", deps, re.M)
    return "\0".join(sorted(parts) + [" ".join(p) for p in pins] + options)


def program_set():
    """The program texts a cooked game.pak names: the importer's text hashes (table.tsv's stage and hash), as one digest."""
    names = sorted("".join(line.split("\t")[:2]) for line in read(os.path.join(PROGRAMS, "table.tsv")).splitlines() if line)
    return hashlib.sha256("\n".join(names).encode()).hexdigest()


def check_store_programs(record=False):
    """A game.pak names its programs by text hash; a host without one of them skips its draws. The store version
    (store_version.inc) is what makes an older game.pak set up again, so the program set may change only with it:
    store_programs.txt holds the version and the program set it was recorded at, and the build stops when they differ."""
    version = read(os.path.join(STORE, "store_version.inc")).strip()
    digest = program_set()
    path = os.path.join(STORE, "store_programs.txt")
    if record:
        with open(path, "w", encoding="utf-8", newline="\n") as f:
            f.write(f"{version} {digest}\n")
        print(f"recorded the program set at store version {version}")
        return True
    recorded = read(path).split() if os.path.exists(path) else []
    if recorded == [version, digest]:
        return True
    if recorded[:1] == [version]:
        print(f"The program texts (runtime/src/render/programs/table.tsv) changed but the store version did not ({version}): "
              "a game.pak cooked before names programs this build does not have, and their draws would be skipped. "
              "Raise runtime/include/galaxy/store_version.inc by one, then run python tools/dev/compile_programs.py record "
              "and commit both files.")
    else:
        print(f"store_programs.txt was recorded at store version {' '.join(recorded[:1]) or 'none'}, the store is {version}: "
              "run python tools/dev/compile_programs.py record and commit runtime/include/galaxy/store_programs.txt.")
    return False


def main():
    if sys.argv[1:] == ["record"]:
        return 0 if check_store_programs(record=True) else 1
    if not check_store_programs():
        return 1
    backend, out = sys.argv[1], sys.argv[2]
    args = sys.argv[3:]
    spirv_tool = args[0] if args else None
    cache = out + ".stages"
    os.makedirs(cache, exist_ok=True)
    headers = "".join(read(os.path.join(MATERIALS, n)) for n in sorted(os.listdir(MATERIALS)) if n.endswith(".hlsli"))
    # A stage's key: the compiler, every .hlsli of the materials folder, the source with the .hlsl it includes, the stage,
    # entry and defines; the compiler's options are fixed here (compile_job) or in its sources (spirv_tool_id).
    tool_id = backend + (DXC if backend == "dxil" else FXC if backend == "dxbc" else
                         hashlib.sha256(spirv_tool_id().encode()).hexdigest())

    # (stage, id) -> {variant index: job}
    jobs = {}
    rows = {}
    flags = {}
    coords = {}

    def add(stage, row, source, text, entry, defines, variant, bound_form=True):
        text = text + included_shaders(text, set())
        if backend == "dxbc":
            # Both stages declare the tables (program.hlsli); only a pixel program reads its stored bound form.
            defines = defines + DXBC_TABLE
            if stage == "ps":
                variant += BOUND_OFFSET
            bound_form = False
        key = hashlib.sha1(("\0".join([tool_id, headers, text, stage, entry] + defines)).encode()).hexdigest()
        jobs[key] = (source, stage, entry, defines)
        rows.setdefault((stage, row), {})[variant] = key
        if backend == "spirv" and stage == "ps" and bound_form:
            bound = defines + ["NEBULA_BOUND_TABLES"]
            bound_key = hashlib.sha1(("\0".join([tool_id, headers, text, stage, entry] + bound)).encode()).hexdigest()
            jobs[bound_key] = (source, stage, entry, bound)
            rows[(stage, row)][variant + BOUND_OFFSET] = bound_key

    def kept(name):
        return backend != "dxbc" or name in DXBC_FORMS

    def add_program(stage, row, source, entry):
        text = read(source)
        if stage == "vs":
            for name in filter(kept, VERTEX_FORMS):
                index, defines = VARIANTS[name]
                add(stage, row, source, text, entry, defines, index)
            # A vertex stage that de-lights its colour (programs.rs, D2) does so in its traced forms alone; the tracer's
            # draws and their prepass run those (program_table.cpp program_bytes). Direct3D 11 has no tracer: none there.
            if backend != "dxbc" and ("traced_vertex_gain(" in text or "traced_map_gain(" in text):  # c93: a map's gain too
                for name in ("traced", "traced_motion"):
                    index, defines = TRACED_VARIANTS[name]
                    add(stage, row, source, text, entry, defines, index, False)
            flags[(stage, row)] = READS_WORLD if "world_" in text else 0
            return
        for name, (index, defines) in VARIANTS.items():
            if (name == "sample" and "discard" not in text) or not kept(name):
                continue
            add(stage, row, source, text, entry, defines, index)
        for name, (index, defines) in TRACED_VARIANTS.items():
            if (name == "prepass_item" and backend != "spirv") or backend == "dxbc":
                continue
            add(stage, row, source, text, entry, defines, index, False)
        flags[(stage, row)] = (LIT if "program_lit.hlsli" in text else 0) | (
            SUMS_KEY if (re.search(r"(light_term|enh_add_light|pbr_add_light)\(i\.lit_light, 0u", text) and "TRACED_UNKEYED;" not in text)
            or WRITTEN_LIT.search(text) else 0)

    for number, name in enumerate(WRITTEN, 1):
        source = os.path.join(MATERIALS, name + ".hlsl")
        row = SITE | number << 1
        count = native_coords(source)
        if count is None:
            print(f"{source} defines no NATIVE_COORDS")
            return 1
        coords[("vs", row)] = coords[("vs", row | 1)] = count
        add_program("vs", row, source, "native_vs")
        if WRITTEN_LIT.search(read(source)) and backend != "dxbc":
            for name in ("traced", "traced_motion"):
                index, defines = TRACED_VARIANTS[name]
                add("vs", row, source, read(source), "native_vs", defines, index)
        if name in VERTEX_ONLY:
            continue
        add_program("ps", row, source, "native_ps")
        # Lines and points are widened in this form.
        text = read(source)
        for form in filter(kept, VERTEX_FORMS):
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
        elif backend == "dxbc":
            command = [FXC, "/nologo", "/T", stage + "_5_0", "/E", entry, "/O3", "/Qstrip_debug", "/Qstrip_reflect",
                       "/I", MATERIALS, "/Fo", temp, source]
            for d in defines:
                command.insert(1, "/D" + (d if "=" in d else d + "=1"))
        else:
            command = [spirv_tool, source, MATERIALS, stage, entry, temp] + defines
        result = subprocess.run(command, capture_output=True, text=True)
        if result.returncode != 0 or not os.path.exists(temp):
            return f"{source} {stage} {' '.join(defines)}\n{result.stdout[:800]}{result.stderr[:1500]}"
        os.replace(temp, target)
        return None

    print(f"programs {backend}: {len(jobs)} stages, {JOBS} jobs", flush=True)
    with ThreadPoolExecutor(JOBS) as pool:
        failed = [r for r in pool.map(compile_job, jobs.items()) if r]
    for f in failed[:40]:
        print("FAILED", f)
    if failed:
        print(len(failed), "of", len(jobs), "failed")
        return 1

    # No form without ray tracing fetches what only a traced form reads (l89): a SPIR-V vertex module's every input is
    # read. DXIL keeps its signature as declared; its drivers fetch only what a shader reads.
    if backend == "spirv":
        unread = []
        for key, (source, stage, entry, defines) in sorted(jobs.items()):
            if stage != "vs" or "NATIVE_TRACED" in defines:
                continue
            with open(os.path.join(cache, key), "rb") as f:
                locations = vertex_unused_inputs(f.read())
            if locations:
                unread.append(f"{os.path.basename(source)} {' '.join(defines) or 'base'}: input locations {locations}")
        for u in unread[:40]:
            print("UNREAD INPUT", u)
        if unread:
            print(len(unread), "vertex modules declare an input they never read")
            return 1

    # Two programs compiling the same in every form mean the set's dedupe missed: the build fails. (Two may share a base
    # form where another form differs: build_programs.py keeps a text whose base leaves out what other forms keep apart.)
    seen = {}
    for stage, row in groups.values():
        digest = hashlib.sha256()
        for variant, key in sorted(rows[(stage, row)].items()):
            with open(os.path.join(cache, key), "rb") as f:
                digest.update(struct.pack("<I", variant) + hashlib.sha256(f.read()).digest())
        digest = (stage, digest.digest())
        # fxc folds texts dxc keeps apart into the same bytes: the set's dedupe is judged on the other backends.
        if digest in seen and backend != "dxbc":
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
