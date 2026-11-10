#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""The programs' file as Metal libraries (docs/APPLE.md, "Shaders").

The Metal renderer's programs are MSL text (programs_msl.bin, written by tools/programs_msl). Metal compiles a text in two
steps: the text to its own bytecode (a tenth of a second a program on a 2017 MacBook Pro, which a first start would pay
for every program of the game), then the bytecode to the GPU's code as a pipeline is made (the system keeps that on
disk). This does the first step at build time, with Apple's compiler: the app carries the bytecode, and no player's
machine compiles a text.

  compile_metallib.py --programs programs_msl.bin --out-blob programs_metal.bin --out-dir <folder> --work <folder>
                      [--sdk macosx] [--min-os 12.0] [--jobs N] [--developer-dir <Xcode's Developer folder>] [--emit-only]

Written to --out-dir: programs_2_4.metallib (MSL 2.4: every vertex program, and the pixel programs' bound-table forms)
and programs_3_0.metallib (MSL 3.0: the pixel programs' array forms, whose arrays have no length; read only where the
system has Metal 3). --out-blob is programs_msl.bin's table with, in place of each text, the record that names its
function (kRecord below; runtime/src/render/mtl/pipelines_mtl.mm reads it). A text that several rows share is one
function.

Many programs go into one source file, each in a namespace of its own (their types have the same names): a function is
`p<number>::m`. --work keeps the sources and each compiled file by its source's hash, so a build after a change
compiles the files that changed. --emit-only writes the sources and the table and compiles nothing. --developer-dir
names the Xcode whose compiler is used when it is not the machine's selected one (xcode-select): only this tool's own
commands see it, so the rest of a build keeps its tools.
"""
import argparse
import concurrent.futures
import hashlib
import os
import re
import struct
import subprocess
import sys

MAGIC = b"NBPROG02"
ROW_BYTES = 96
STORED = 10        # program_table.h kProgramStored
BOUND_OFFSET = 5   # program_table.h kProgramBoundOffset: a pixel program's bound-table forms follow its variants
RECORD_MAGIC = b"NBMTLFN1"
RECORD_BYTES = 40  # the magic, library u32 (0: 2.4, 1: 3.0), attributes u32 (a vertex program's inputs, a bit each), name[24]
BATCH = 48
LIBRARIES = (("programs_2_4.metallib", "2.4"), ("programs_3_0.metallib", "3.0"))
HEADER = ('#pragma clang diagnostic ignored "-Wmissing-prototypes"\n'
          '#pragma clang diagnostic ignored "-Wmissing-braces"\n'
          "#include <metal_stdlib>\n#include <simd/simd.h>\nusing namespace metal;\n")
DROPPED = {b"#include <metal_stdlib>", b"#include <simd/simd.h>", b"using namespace metal;"}
ENTRY = re.compile(rb"\bmain0\b")
ATTRIBUTE = re.compile(rb"\[\[attribute\((\d+)\)\]\]")


def body_of(text):
    """A program's text as it stands in a namespace: the includes are the file's, and its function is `m`."""
    lines = [line for line in text.split(b"\n") if line.strip() not in DROPPED]
    return ENTRY.sub(b"m", b"\n".join(lines))


TOOL_ENVIRONMENT = None  # the commands' environment when --developer-dir names an Xcode


def run(command):
    done = subprocess.run(command, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, env=TOOL_ENVIRONMENT)
    return done.returncode, done.stdout.decode("utf-8", "replace")


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--programs", required=True)
    parser.add_argument("--out-blob", required=True)
    parser.add_argument("--out-dir", required=True)
    parser.add_argument("--work", required=True)
    parser.add_argument("--sdk", default="macosx")
    parser.add_argument("--min-os", default="12.0")
    parser.add_argument("--jobs", type=int, default=max(1, (os.cpu_count() or 2) - 1))
    parser.add_argument("--developer-dir", default="")
    parser.add_argument("--emit-only", action="store_true")
    args = parser.parse_args()
    if args.developer_dir:
        global TOOL_ENVIRONMENT
        TOOL_ENVIRONMENT = dict(os.environ, DEVELOPER_DIR=args.developer_dir)

    with open(args.programs, "rb") as file:
        blob = file.read()
    if len(blob) < 64 or blob[:8] != MAGIC:
        sys.exit(f"{args.programs} is not a programs file")
    vertex_rows, pixel_rows = struct.unpack_from("<II", blob, 8)
    rows = vertex_rows + pixel_rows
    table_end = 64 + rows * ROW_BYTES

    # Each stored text's function: one for each distinct text of a library.
    functions = [{}, {}]   # by library: text hash -> number
    texts = [[], []]       # by library: the texts in number order
    records = bytearray()
    out = bytearray(blob[:table_end])
    record_at = {}         # (library, number) -> offset of its record
    for row in range(rows):
        base = 64 + row * ROW_BYTES
        pixel = row >= vertex_rows
        for stored in range(STORED):
            offset, size = struct.unpack_from("<II", blob, base + 16 + stored * 8)
            if size == 0:
                continue
            text = blob[offset:offset + size]
            library = 1 if pixel and stored < BOUND_OFFSET else 0
            key = hashlib.sha1(text).digest()
            number = functions[library].get(key)
            if number is None:
                number = len(texts[library])
                functions[library][key] = number
                texts[library].append(text)
                attributes = 0
                if not pixel:
                    for found in ATTRIBUTE.finditer(text):
                        if int(found.group(1)) < 32:
                            attributes |= 1 << int(found.group(1))
                name = f"p{number}::m".encode()
                record_at[(library, number)] = table_end + len(records)
                records += RECORD_MAGIC + struct.pack("<II", library, attributes) + name.ljust(24, b"\0")
            struct.pack_into("<II", out, base + 16 + stored * 8, record_at[(library, number)], RECORD_BYTES)
    out += records

    os.makedirs(args.work, exist_ok=True)
    os.makedirs(args.out_dir, exist_ok=True)
    sources = [[], []]
    for library in (0, 1):
        for first in range(0, len(texts[library]), BATCH):
            parts = [HEADER.encode()]
            for number in range(first, min(first + BATCH, len(texts[library]))):
                parts.append(b"namespace p%d {\n" % number + body_of(texts[library][number]) + b"\n}\n")
            source = b"".join(parts)
            # The compiled file's name: the source and everything the compile depends on.
            key = hashlib.sha1(source + f"|{LIBRARIES[library][1]}|{args.sdk}|{args.min_os}".encode()).hexdigest()
            path = os.path.join(args.work, f"{key}.metal")
            if not os.path.exists(path):
                with open(path + ".partial", "wb") as file:
                    file.write(source)
                os.replace(path + ".partial", path)
            sources[library].append(path)
    print(f"programs: {len(texts[0])} functions in MSL 2.4 ({len(sources[0])} files), "
          f"{len(texts[1])} in MSL 3.0 ({len(sources[1])} files)")

    if not args.emit_only:
        code, where = run(["xcrun", "-sdk", args.sdk, "-f", "metal"])
        if code != 0:
            sys.exit("Apple's Metal compiler is not installed (it comes with Xcode): " + where.strip())
        minimum = {"macosx": "-mmacosx-version-min=", "iphoneos": "-mios-version-min=",
                   "iphonesimulator": "-mios-simulator-version-min="}[args.sdk] + args.min_os
        # The arithmetic the programs state, as the other renderers' compilers keep it (MTLCompileOptions.fastMathEnabled NO).
        common = ["xcrun", "-sdk", args.sdk, "metal", "-c", minimum, "-fno-fast-math", "-Wno-unused-variable"]

        def compile_one(job):
            library, source = job
            air = source[:-len(".metal")] + ".air"
            if os.path.exists(air):
                return None
            version = LIBRARIES[library][1]
            code, said = run(common + [f"-std=metal{version}", source, "-o", air + ".partial"])
            if code != 0 and "-std" in said:
                # Compilers before the one language name for every system take the system's own.
                system = "macos" if args.sdk == "macosx" else "ios"
                code, said = run(common + [f"-std={system}-metal{version}", source, "-o", air + ".partial"])
            if code != 0:
                return f"{source}:\n{said}"
            os.replace(air + ".partial", air)
            return None

        jobs = [(library, source) for library in (0, 1) for source in sources[library]]
        failed = []
        with concurrent.futures.ThreadPoolExecutor(max_workers=args.jobs) as pool:
            for count, result in enumerate(pool.map(compile_one, jobs), 1):
                if result is not None:
                    failed.append(result)
                if count % 50 == 0 or count == len(jobs):
                    print(f"programs: {count} of {len(jobs)} files compiled", flush=True)
        if failed:
            sys.exit("Metal's compiler refused:\n" + "\n".join(failed[:3]))
        for library in (0, 1):
            target = os.path.join(args.out_dir, LIBRARIES[library][0])
            listing = os.path.join(args.work, f"library{library}.txt")
            with open(listing, "w") as file:
                file.write("\n".join(source[:-len(".metal")] + ".air" for source in sources[library]) + "\n")
            # An empty library is still a library: a file the app can open.
            code, said = run(["xcrun", "-sdk", args.sdk, "metallib", "-o", target + ".partial", "@" + listing]
                             if sources[library] else
                             ["xcrun", "-sdk", args.sdk, "metallib", "-o", target + ".partial"])
            if code != 0:
                sys.exit(f"metallib refused {target}:\n{said}")
            os.replace(target + ".partial", target)
            print(f"programs: {target} {os.path.getsize(target)} bytes")
    else:
        for library in (0, 1):
            with open(os.path.join(args.out_dir, f"sources{library}.txt"), "w") as file:
                file.write("\n".join(sources[library]) + "\n")

    with open(args.out_blob + ".partial", "wb") as file:
        file.write(out)
    os.replace(args.out_blob + ".partial", args.out_blob)
    print(f"programs: {args.out_blob} {len(out)} bytes")


if __name__ == "__main__":
    main()
