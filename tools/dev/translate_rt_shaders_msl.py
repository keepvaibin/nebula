# SPDX-License-Identifier: GPL-3.0-only
"""The tracer's shaders as MSL, for inspection on any machine (the build's own route is platform/apple/tools/
rt_shaders_msl.cpp, through the same runtime/src/render/mtl/shader_msl.cpp tracer mode).

  translate_rt_shaders_msl.py --out DIR (--dxc DXC | --spirv DIR) [--sources RT_DIR] [--tool RT_SHADERS_MSL] [--ios]
                              [--names NAME ...] [--root TREE]

For each row of runtime/src/render/rt/shaders.tsv: the SPIR-V (dxc with compile_rt_shaders.py's flags, or a folder of
<name>.spv), its resources read from the module, each placed by the tracer's rule (shader_msl.h kRt*, rt_backend.h's
counts, compile_rt_shaders.py's shifts) and refused when the rule has no place for it; the instructions SPIRV-Cross's
MSL back end refuses; then, given the tool, <name>.metal and a check that every resource stands at its index. Writes
DIR/report.tsv and DIR/rt_shaders_msl_bindings.h (a constexpr table: each shader's threadgroup size and the Metal
indices it reads). Exit 1 when anything is refused or a check fails."""
import argparse
import os
import re
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

# SPIR-V numbers (SPIRV-Headers unified1/spirv.core.grammar.json).
OP_NAME, OP_ENTRY_POINT, OP_EXECUTION_MODE, OP_CAPABILITY = 5, 15, 16, 17
OP_TYPE_INT, OP_TYPE_FLOAT, OP_TYPE_IMAGE, OP_TYPE_SAMPLER = 21, 22, 25, 26
OP_TYPE_ARRAY, OP_TYPE_RUNTIME_ARRAY, OP_TYPE_STRUCT, OP_TYPE_POINTER = 28, 29, 30, 32
OP_VARIABLE, OP_DECORATE, OP_TYPE_ACCELERATION_STRUCTURE = 59, 71, 5341
EXECUTION_MODE_LOCAL_SIZE = 17
DECORATION_BLOCK, DECORATION_BUFFER_BLOCK, DECORATION_BINDING, DECORATION_SET = 2, 3, 33, 34
STORAGE_UNIFORM_CONSTANT, STORAGE_UNIFORM, STORAGE_PUSH_CONSTANT, STORAGE_BUFFER = 0, 2, 9, 12
CAPABILITY_RAY_QUERY = 4472

# Instructions SPIRV-Cross's MSL back end refuses (spirv_msl.cpp, emit_instruction: SPIRV_CROSS_THROW).
REFUSED = {
    6021: "OpRayQueryGetIntersectionInstanceShaderBindingTableRecordOffsetKHR (\"BindingTableRecordOffset is not supported in MSL.\")",
    4447: "OpConvertUToAccelerationStructureKHR (\"ConvertUToAccelerationStructure is not supported in MSL.\")",
}


def rows_of(sources):
    with open(os.path.join(sources, "shaders.tsv"), encoding="utf-8") as f:
        return [p for p in (line.rstrip("\r\n").split("\t") for line in list(f)[1:]) if len(p) == 4]


def constants(path, pattern):
    with open(path, encoding="utf-8") as f:
        text = f.read()
    return {m.group(1): int(m.group(2)) for m in re.finditer(pattern, text)}


def rule(root):
    """(set, binding, kind) -> (metal space, index): the tracer's map, from the headers that define it."""
    k = constants(os.path.join(root, "runtime/include/galaxy/render/mtl/shader_msl.h"),
                  r"inline constexpr std::uint32_t (kRt\w+) = (\d+)u;")
    n = constants(os.path.join(root, "runtime/include/galaxy/render/rt/rt_backend.h"), r"(kPass\w+) = (\d+)u")
    n["kPassMeshBuffers"] = n["kPassMeshArenas"] + 1
    # shader_msl.cpp's tracer block is written for these counts.
    assumed = {"kPassBuffers": 4, "kPassTextures": 4, "kPassReads": 12, "kPassMeshBuffers": 9, "kPassWrites": 8}
    for name, value in assumed.items():
        if n[name] != value:
            raise SystemExit("rt_backend.h %s is %d; shader_msl.cpp's tracer block places %d" % (name, n[name], value))
    place = {(0, 0, "as"): ("buffer", k["kRtBufferScene"])}
    at = 1
    for i in range(n["kPassBuffers"]):
        place[(0, at + i, "buffer")] = ("buffer", k["kRtBufferRenderer"] + i)
    at += n["kPassBuffers"]
    for i in range(n["kPassTextures"]):
        place[(0, at + i, "texture")] = ("texture", k["kRtTextureRenderer"] + i)
    at += n["kPassTextures"]
    for i in range(n["kPassReads"]):
        place[(0, at + i, "texture")] = ("texture", k["kRtTextureRead"] + i)
    at += n["kPassReads"]
    for i in range(n["kPassMeshBuffers"]):
        place[(0, at + i, "buffer")] = ("buffer", k["kRtBufferMesh"] + i)
    # compile_rt_shaders.py's shifts: s at 100, u at 200, b at 300 (rt_backend_vk.cpp kSamplerBinding, kImageBinding,
    # kCountersBinding = 200 + kPassWrites, kWriteBufferBinding, kConstantsBinding, kPassConstantsBinding).
    place[(0, 100, "sampler")] = ("constexpr", 0)
    for i in range(n["kPassWrites"]):
        place[(0, 200 + i, "storage")] = ("texture", k["kRtTextureWritten"] + i)
    place[(0, 200, "buffer")] = ("buffer", k["kRtBufferProbeOut"])
    place[(0, 200 + n["kPassWrites"], "buffer")] = ("buffer", k["kRtBufferCounters"])
    place[(0, 201 + n["kPassWrites"], "buffer")] = ("buffer", k["kRtBufferWritten"])
    place[(0, 300, "cbuffer")] = ("buffer", k["kRtBufferFrame"])
    place[(0, 301, "cbuffer")] = ("buffer", k["kRtBufferPass"])
    place[(1, 0, "texture[]")] = ("argument", k["kRtBufferTextureArray"])
    used = {}
    for (_, _, kind), (space, index) in place.items():
        if space in ("buffer", "texture", "argument") and not (kind == "buffer" and index == k["kRtBufferProbeOut"]):
            key = ("buffer" if space == "argument" else space, index)
            if key in used:
                raise SystemExit("two resources at Metal %s %d" % key)
            used[key] = kind
    for (space, index) in used:
        if index > 30:
            raise SystemExit("Metal %s index %d: past 30" % (space, index))
    return place


def reflect(words):
    """The module's resources [(set, binding, kind, name)], LocalSize, capabilities and instruction counts."""
    if len(words) < 5 or words[0] != 0x07230203:
        raise ValueError("not a SPIR-V module")
    names, types, decorations, variables, ops, caps = {}, {}, {}, [], {}, set()
    threads = (0, 0, 0)
    at = 5
    while at < len(words):
        count, op = words[at] >> 16, words[at] & 0xFFFF
        if count == 0 or at + count > len(words):
            raise ValueError("a broken instruction at word %d" % at)
        w = words[at:at + count]
        ops[op] = ops.get(op, 0) + 1
        if op == OP_NAME:
            raw = b"".join(x.to_bytes(4, "little") for x in w[2:])
            names[w[1]] = raw.split(b"\0", 1)[0].decode("utf-8", "replace")
        elif op == OP_CAPABILITY:
            caps.add(w[1])
        elif op == OP_EXECUTION_MODE and count >= 6 and w[2] == EXECUTION_MODE_LOCAL_SIZE:
            threads = (w[3], w[4], w[5])
        elif op == OP_DECORATE:
            decorations.setdefault(w[1], {})[w[2]] = w[3:]
        elif op == OP_TYPE_IMAGE:
            types[w[1]] = ("image", w[7], w[8])  # sampled (1 texture, 2 storage), format
        elif op == OP_TYPE_SAMPLER:
            types[w[1]] = ("sampler",)
        elif op == OP_TYPE_ACCELERATION_STRUCTURE:
            types[w[1]] = ("as",)
        elif op in (OP_TYPE_ARRAY, OP_TYPE_RUNTIME_ARRAY):
            types[w[1]] = ("array", w[2], op == OP_TYPE_RUNTIME_ARRAY)
        elif op == OP_TYPE_STRUCT:
            types[w[1]] = ("struct",)
        elif op == OP_TYPE_POINTER:
            types[w[1]] = ("pointer", w[2], w[3])
        elif op == OP_VARIABLE:
            variables.append((w[2], w[1], w[3]))  # id, pointer type, storage class
        at += count
    resources = []
    for var, pointer, storage in variables:
        d = decorations.get(var, {})
        if DECORATION_SET not in d:
            if storage == STORAGE_PUSH_CONSTANT:
                resources.append((-1, -1, "push", names.get(var, "")))
            continue
        base = types[pointer][2]
        runtime = False
        while types.get(base, ("",))[0] == "array":
            runtime = runtime or types[base][2]
            base = types[base][1]
        t = types.get(base, ("?",))
        if t[0] == "as":
            kind = "as"
        elif t[0] == "image":
            kind = "storage" if t[1] == 2 else "texture"
        elif t[0] == "sampler":
            kind = "sampler"
        elif t[0] == "struct":
            block = decorations.get(base, {})
            kind = "buffer" if storage == STORAGE_BUFFER or DECORATION_BUFFER_BLOCK in block else \
                   "cbuffer" if storage == STORAGE_UNIFORM and DECORATION_BLOCK in block else "?"
        else:
            kind = "?"
        if runtime:
            kind += "[]"
        resources.append((d[DECORATION_SET][0], d[DECORATION_BINDING][0], kind, names.get(var, "")))
    return resources, threads, caps, ops


def compile_spirv(dxc, sources, name, file, profile, entry, target):
    command = [dxc, "-nologo", "-T", profile, "-E", entry, "-O3", "-I", sources,
               "-I", os.path.join(os.path.dirname(sources), "materials"), os.path.join(sources, file), "-Fo", target,
               "-spirv", "-fspv-target-env=vulkan1.2", "-fvk-t-shift", "0", "all", "-fvk-s-shift", "100", "all",
               "-fvk-u-shift", "200", "all", "-fvk-b-shift", "300", "all", "-DNEBULA_VULKAN=1"]
    done = subprocess.run(command, capture_output=True, text=True)
    return "" if done.returncode == 0 and os.path.exists(target) else (done.stdout + done.stderr).strip()


def check_msl(text, placed, caps):
    """What the MSL lacks of the map; empty when every resource stands at its index."""
    faults = []
    if re.search(r"unsupported|#error", text, re.IGNORECASE):
        faults.append("says unsupported/#error")
    if not re.search(r"\bkernel void main0\(", text):
        faults.append("no kernel main0")
    if CAPABILITY_RAY_QUERY in caps:
        # SPIRV-Cross writes the types qualified (raytracing::), Apple's samples under `using namespace`.
        if not re.search(r"intersection_query<(raytracing::)?instancing, (raytracing::)?triangle_data>", text):
            faults.append("no intersection_query<instancing, triangle_data>")
        if not re.search(r"acceleration_structure<(raytracing::)?instancing>", text):
            faults.append("no acceleration_structure<instancing>")
    for name, kind, space, index in placed:
        if space == "constexpr":
            if not re.search(r"constexpr sampler " + re.escape(name) + r"\b", text):
                faults.append("%s: no constexpr sampler" % name)
        elif space == "argument":
            if not re.search(r"spvDescriptorSetBuffer1\s*&\s*\w+\s*\[\[buffer\(%d\)\]\]" % index, text):
                faults.append("%s: no set-1 argument buffer at buffer(%d)" % (name, index))
            elif not re.search(re.escape(name) + r"\s*\[\[id\(0\)\]\]", text):
                faults.append("%s: not [[id(0)]] in the argument buffer" % name)
        else:
            attribute = "buffer" if space == "buffer" else "texture"
            found = re.findall(r"\b" + re.escape(name) + r"\s*\[\[" + attribute + r"\((\d+)\)\]\]", text)
            if found != [str(index)]:
                faults.append("%s: %s(%s), the map says %d" % (name, attribute, ",".join(found) or "none", index))
    return faults


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--out", required=True)
    parser.add_argument("--dxc")
    parser.add_argument("--spirv")
    parser.add_argument("--sources", default=os.path.join(ROOT, "runtime", "src", "render", "rt"))
    parser.add_argument("--tool")
    parser.add_argument("--ios", action="store_true")
    parser.add_argument("--names", nargs="*")
    parser.add_argument("--root", default=ROOT, help="the tree whose shader_msl.h and rt_backend.h define the map")
    args = parser.parse_args()
    if not args.dxc and not args.spirv:
        parser.error("--dxc or --spirv")
    os.makedirs(args.out, exist_ok=True)
    place = rule(args.root)
    rows = [r for r in rows_of(args.sources) if not args.names or r[0] in args.names]
    report, table, bad = [], [], 0
    modules = []
    for name, file, profile, entry in rows:
        if args.spirv:
            path = os.path.join(args.spirv, name + ".spv")
            error = "" if os.path.exists(path) else "no " + path
        else:
            path = os.path.join(args.out, name + ".spv")
            error = compile_spirv(args.dxc, args.sources, name, file, profile, entry, path)
        if error:
            report.append([name, "no", "dxc: " + error.replace("\n", " ")[:300], "", ""])
            bad += 1
            continue
        with open(path, "rb") as f:
            data = f.read()
        words = [int.from_bytes(data[i:i + 4], "little") for i in range(0, len(data) - len(data) % 4, 4)]
        resources, threads, caps, ops = reflect(words)
        placed, unplaced = [], []
        for res_set, binding, kind, res_name in resources:
            where = place.get((res_set, binding, kind))
            if where is None:
                unplaced.append("%s (set %d binding %d %s)" % (res_name, res_set, binding, kind))
            else:
                placed.append((res_name, kind, where[0], where[1]))
        refused = ["%s x%d" % (REFUSED[op], ops[op]) for op in sorted(REFUSED) if op in ops]
        modules.append((name, path, placed, caps))
        table.append((name, threads, placed))
        report.append([name, "pending" if not (unplaced or refused) else "no",
                       "; ".join(["refused by SPIRV-Cross: " + r for r in refused] + ["no place in the map: " + u for u in unplaced]),
                       " ".join("%s=%s%s" % (n, s[0], "" if s == "constexpr" else i) for n, _, s, i in placed),
                       "%d,%d,%d" % threads])
        bad += 1 if (unplaced or refused) else 0
    # The tool, where there is one: <name>.metal each, then the check.
    by_name = {r[0]: r for r in report}
    if args.tool:
        ready = [m for m in modules if by_name[m[0]][1] == "pending"]
        if ready:
            done = subprocess.run([args.tool, "--files", "ios" if args.ios else "macos", args.out] + [m[1] for m in ready],
                                  capture_output=True, text=True)
            lines = {line.split(" ", 2)[1].rstrip(":"): line for line in done.stdout.splitlines() if " " in line}
            for name, _, placed, caps in ready:
                line = lines.get(name, "FAILED %s: the tool said nothing" % name)
                row = by_name[name]
                if line.startswith("OK "):
                    with open(os.path.join(args.out, name + ".metal"), encoding="utf-8") as f:
                        faults = check_msl(f.read(), placed, caps)
                    row[1] = "yes" if not faults else "yes, check failed"
                    row[2] = "; ".join(faults)
                    bad += 1 if faults else 0
                else:
                    row[1], row[2] = "no", line.split(":", 1)[1].strip() if ":" in line else line
                    bad += 1
    else:
        for row in report:
            if row[1] == "pending":
                row[1] = "not run (no --tool)"
    with open(os.path.join(args.out, "report.tsv"), "w", encoding="utf-8", newline="\n") as f:
        f.write("shader\ttranslated\terror\tmetal indices\tthreads\n")
        for row in report:
            f.write("\t".join(row) + "\n")
    # The constexpr table: what each shader reads, by Metal index (shader_msl.h kRt*).
    lines = ["// Generated by tools/dev/translate_rt_shaders_msl.py; do not edit.",
             "// Each tracer shader in RtShader's order (shaders.tsv): its threadgroup size (dispatchThreadgroups'",
             "// threadsPerThreadgroup) and the Metal indices it reads (bit n: buffer or texture n; shader_msl.h kRt*).",
             "#pragma once", "#include <cstdint>", "", "namespace galaxy::render::mtl {", "",
             "struct RtMslShader {", "    const char* name;", "    std::uint32_t threads[3];", "    std::uint32_t buffers;",
             "    std::uint32_t textures;", "    bool texture_table;  // reads set 1 (kRtBufferTextureArray)", "};", "",
             "inline constexpr RtMslShader kRtMslShaders[] = {"]
    for name, threads, placed in table:
        buffers = sum(1 << i for _, _, s, i in placed if s == "buffer")
        textures = sum(1 << i for _, _, s, i in placed if s == "texture")
        texture_table = any(s == "argument" for _, _, s, _ in placed)
        lines.append("    {\"%s\", {%du, %du, %du}, 0x%08Xu, 0x%08Xu, %s}," % (name, threads[0], threads[1], threads[2], buffers,
                                                                          textures, "true" if texture_table else "false"))
    lines += ["};", "", "}  // namespace galaxy::render::mtl", ""]
    with open(os.path.join(args.out, "rt_shaders_msl_bindings.h"), "w", encoding="utf-8", newline="\n") as f:
        f.write("\n".join(lines))
    for row in report:
        print("%-14s %-22s %s" % (row[0], row[1], row[2][:200]))
    print("MSL rows %d, refused or failed %d" % (len(report), bad))
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
