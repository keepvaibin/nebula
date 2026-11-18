"""One mod's entry libraries read as each platform's loader will read them, on this machine, with no tool of any
target. LumaLink's nine-platform build test runs it (LUMA_PLATFORM_CHECK, set by the gate's lumalink check).

python -X utf8 -I check_luma_platforms.py <file.luma | folder holding <platform>/mod.*> [--need p1,p2,...]

For a .luma: the platforms are luma.json's code.platforms and each library is code/<platform>/mod.<ending>
(luma-format 6). For a folder (a build's code folder): every platform folder in it. --need names platforms that
must be there (8.5: windows-x64,linux-x64,android-arm64,macos-arm64,ios-arm64); default: all that are listed.

For each library, from its bytes:
  form        PE32+ / ELF64 / Mach-O 64, a shared library, of the platform's machine
  export      luma_mod (luma.h) is exported
  imports     none from the system (loader contract 8, L504): PE import and delay-import tables empty; ELF no
              DT_NEEDED and no undefined dynamic symbol; Mach-O no dylib load command and no undefined symbol.
              What is found is printed by name: this is the evidence the contract's macOS and iOS row waits for.
  platform    ELF: no executable stack, constructors not in .init_array (luma.h keeps them from the system's loader),
              Android: every PT_LOAD aligned to 16 KiB or more; Mach-O: LC_BUILD_VERSION's platform (macOS 1, iOS 2)
              and a code signature (ad hoc; iOS is signed again on the device).
One line a platform: OK, or FAIL with every reason. Exit 0 when every needed platform is OK.
"""
import json
import os
import struct
import sys
import zipfile

LIBRARY = {"windows-x64": "mod.dll", "windows-arm64": "mod.dll", "linux-x64": "mod.so", "linux-arm64": "mod.so",
           "macos-arm64": "mod.dylib", "macos-x64": "mod.dylib", "android-arm64": "mod.so", "android-x64": "mod.so",
           "ios-arm64": "mod.dylib"}
FIVE = ["windows-x64", "linux-x64", "android-arm64", "macos-arm64", "ios-arm64"]


def text_at(b, at):
    end = b.find(b"\0", at)
    return b[at:end if end >= 0 else len(b)].decode("utf-8", "replace")


def check_pe(b, platform):
    bad, notes = [], []
    if len(b) < 0x40 or b[:2] != b"MZ":
        return ["not a PE file"], notes
    pe = struct.unpack_from("<I", b, 0x3C)[0]
    if b[pe:pe + 4] != b"PE\0\0":
        return ["no PE signature"], notes
    machine, sections, _, _, _, opt_size, characteristics = struct.unpack_from("<HHIIIHH", b, pe + 4)
    want = {"windows-x64": 0x8664, "windows-arm64": 0xAA64}[platform]
    if machine != want:
        bad.append("machine 0x%04x, not 0x%04x" % (machine, want))
    if not characteristics & 0x2000:
        bad.append("not a DLL")
    opt = pe + 24
    if struct.unpack_from("<H", b, opt)[0] != 0x20B:
        return bad + ["not PE32+"], notes
    count = struct.unpack_from("<I", b, opt + 108)[0]
    dirs = [struct.unpack_from("<II", b, opt + 112 + 8 * i) for i in range(min(count, 16))]
    table = opt + opt_size
    secs = [struct.unpack_from("<8sIIII", b, table + 40 * i) for i in range(sections)]

    def off(rva):
        for _, vsize, va, rsize, raw in secs:
            if va <= rva < va + max(vsize, rsize):
                return raw + rva - va
        return None

    names = []
    if len(dirs) > 0 and dirs[0][0]:
        e = off(dirs[0][0])
        if e is not None:
            n_names, = struct.unpack_from("<I", b, e + 24)
            name_table = off(struct.unpack_from("<I", b, e + 32)[0])
            for i in range(n_names):
                names.append(text_at(b, off(struct.unpack_from("<I", b, name_table + 4 * i)[0])))
    if "luma_mod" not in names:
        bad.append("luma_mod is not exported (exports: %s)" % (", ".join(names[:8]) or "none"))
    imported = []
    for index, what in ((1, "import"), (13, "delay import")):
        if len(dirs) > index and dirs[index][0]:
            e = off(dirs[index][0])
            step = 20 if index == 1 else 32
            name_at = 12 if index == 1 else 4
            while e is not None and e + step <= len(b) and any(b[e:e + step]):
                rva = struct.unpack_from("<I", b, e + name_at)[0]
                imported.append("%s %s" % (what, text_at(b, off(rva)) if off(rva) is not None else "?"))
                e += step
    if imported:
        bad.append("imports from the system: " + ", ".join(imported))
    notes.append("PE32+ %s, %d exports, 0 imports" % ("x64" if machine == 0x8664 else "arm64", len(names)))
    return bad, notes


def check_elf(b, platform):
    bad, notes = [], []
    if b[:4] != b"\x7fELF" or len(b) < 64:
        return ["not an ELF file"], notes
    if b[4] != 2 or b[5] != 1:
        return ["not 64-bit little-endian ELF"], notes
    e_type, machine = struct.unpack_from("<HH", b, 16)
    phoff, shoff = struct.unpack_from("<QQ", b, 32)
    phentsize, phnum, shentsize, shnum, _ = struct.unpack_from("<HHHHH", b, 54)
    want = 62 if platform.endswith("x64") else 183
    if machine != want:
        bad.append("machine %d, not %d" % (machine, want))
    if e_type != 3:
        bad.append("not a shared object (type %d)" % e_type)
    stack_seen = False
    for i in range(phnum):
        p_type, flags, _, _, _, _, _, align = struct.unpack_from("<IIQQQQQQ", b, phoff + phentsize * i)
        if p_type == 1 and platform.startswith("android") and align < 16384:
            bad.append("a PT_LOAD aligned to %d, under 16 KiB (devices with 16 KiB pages refuse it)" % align)
        if p_type == 0x6474E551:
            stack_seen = True
            if flags & 1:
                bad.append("executable stack")
    if not stack_seen:
        bad.append("no PT_GNU_STACK (the stack is executable by default)")
    secs = [struct.unpack_from("<IIQQQQIIQQ", b, shoff + shentsize * i) for i in range(shnum)]
    needed, undefined, exported, init_array = [], [], [], False
    for name, kind, _, _, offset, size, link, _, _, entsize in secs:
        if kind == 14 and size:
            init_array = True
        if kind == 6:  # .dynamic
            strtab = secs[link][4]
            for at in range(offset, offset + size, 16):
                tag, value = struct.unpack_from("<qQ", b, at)
                if tag == 1:
                    needed.append(text_at(b, strtab + value))
                if tag == 25 or tag == 27:
                    if tag == 27 and value:
                        init_array = True
        if kind == 11:  # .dynsym
            strtab = secs[link][4]
            for at in range(offset + 24, offset + size, 24):
                st_name, info, _, shndx, _, _ = struct.unpack_from("<IBBHQQ", b, at)
                sym = text_at(b, strtab + st_name)
                if not sym:
                    continue
                if shndx == 0:
                    undefined.append(sym)
                elif info >> 4 in (1, 2):
                    exported.append(sym)
    if "luma_mod" not in exported:
        bad.append("luma_mod is not exported (exports: %s)" % (", ".join(exported[:8]) or "none"))
    if needed:
        bad.append("needs libraries: " + ", ".join(needed))
    if undefined:
        bad.append("undefined symbols: " + ", ".join(undefined[:12]))
    if init_array:
        bad.append("has .init_array (the system's loader would run the mod's constructors before the game allows)")
    notes.append("ELF64 %s, %d exports, 0 imports" % ("x86-64" if machine == 62 else "aarch64", len(exported)))
    return bad, notes


def check_macho(b, platform):
    bad, notes = [], []
    if len(b) < 32 or struct.unpack_from("<I", b, 0)[0] != 0xFEEDFACF:
        return ["not a 64-bit Mach-O file"], notes
    cputype, _, filetype, ncmds, _, _, _ = struct.unpack_from("<iiIIIII", b, 4)
    want = 0x01000007 if platform.endswith("x64") else 0x0100000C
    if cputype != want:
        bad.append("cpu 0x%08x, not 0x%08x" % (cputype, want))
    if filetype != 6:
        bad.append("not a dynamic library (file type %d)" % filetype)
    at = 32
    dylibs, undefined, exported, system, signed, minimum = [], [], [], None, False, None
    for _ in range(ncmds):
        cmd, size = struct.unpack_from("<II", b, at)
        if cmd in (0xC, 0x80000018, 0x8000001F, 0x80000023, 0x20):  # load, weak, re-export, upward, lazy
            dylibs.append(text_at(b, at + struct.unpack_from("<I", b, at + 8)[0]))
        if cmd == 0x32:
            system, minos = struct.unpack_from("<II", b, at + 8)
            minimum = "%d.%d" % (minos >> 16, (minos >> 8) & 255)
        if cmd == 0x1D:
            signed = True
        if cmd == 2:
            symoff, nsyms, stroff, _ = struct.unpack_from("<IIII", b, at + 8)
            for i in range(nsyms):
                strx, n_type, _, _, _ = struct.unpack_from("<IBBHQ", b, symoff + 16 * i)
                if n_type & 0xE0:  # a debugging entry
                    continue
                name = text_at(b, stroff + strx)
                if n_type & 1 and (n_type & 0x0E) == 0:
                    undefined.append(name)
                elif n_type & 1 and (n_type & 0x0E) == 0x0E:
                    exported.append(name)
        at += size
    if "_luma_mod" not in exported:
        bad.append("luma_mod is not exported (exports: %s)" % (", ".join(exported[:8]) or "none"))
    if dylibs:
        bad.append("loads libraries: " + ", ".join(dylibs))
    if undefined:
        bad.append("undefined symbols: " + ", ".join(undefined[:12]))
    want_system = 2 if platform.startswith("ios") else 1
    if system != want_system:
        bad.append("LC_BUILD_VERSION platform %s, not %d" % (system, want_system))
    if not signed:
        bad.append("no code signature (arm64 refuses an unsigned image; ad hoc is enough before the device signs)")
    notes.append("Mach-O %s, %s %s, %d exports, 0 imports, signed %s"
                 % ("x86-64" if cputype == 0x01000007 else "arm64", "iOS" if system == 2 else "macOS", minimum, len(exported), signed))
    return bad, notes


def check(platform, data):
    if platform.startswith("windows"):
        return check_pe(data, platform)
    if platform.startswith(("linux", "android")):
        return check_elf(data, platform)
    return check_macho(data, platform)


def main():
    args = sys.argv[1:]
    if not args:
        print(__doc__)
        return 2
    source = args[0]
    need = args[args.index("--need") + 1].split(",") if "--need" in args else None
    found = {}
    if os.path.isdir(source):
        for platform in sorted(os.listdir(source)):
            if platform in LIBRARY and os.path.isfile(os.path.join(source, platform, LIBRARY[platform])):
                found[platform] = open(os.path.join(source, platform, LIBRARY[platform]), "rb").read()
    else:
        with zipfile.ZipFile(source) as z:
            manifest = json.loads(z.read("luma.json").decode("utf-8"))
            listed = (manifest.get("code") or {}).get("platforms") or []
            names = set(z.namelist())
            for platform in listed:
                if platform not in LIBRARY:
                    print("%-14s FAIL  not a platform of luma-format 6" % platform)
                    return 1
                entry = "code/%s/%s" % (platform, LIBRARY[platform])
                if entry in names:
                    found[platform] = z.read(entry)
                else:
                    found[platform] = None
            for name in sorted(names):
                parts = name.split("/")
                if parts[0] == "code" and len(parts) > 1 and parts[1] and parts[1] not in listed:
                    print("%-14s FAIL  code/%s/ is in the file and not in code.platforms" % (parts[1], parts[1]))
                    return 1
    need = need or sorted(found)
    failed = 0
    for platform in need:
        if platform not in found:
            print("%-14s FAIL  not in the mod (the mod's code does not run there)" % platform)
            failed += 1
            continue
        data = found[platform]
        if data is None:
            print("%-14s FAIL  listed in code.platforms, no code/%s/%s" % (platform, platform, LIBRARY[platform]))
            failed += 1
            continue
        try:
            bad, notes = check(platform, data)
        except (struct.error, IndexError, TypeError) as error:
            bad, notes = ["truncated or malformed: %s" % error], []
        if bad:
            failed += 1
            print("%-14s FAIL  %s" % (platform, "; ".join(bad)))
        else:
            print("%-14s OK    %d bytes; %s" % (platform, len(data), "; ".join(notes)))
    for platform in sorted(set(found) - set(need)):
        print("%-14s (present, not needed by this check)" % platform)
    print("%d of %d needed platforms OK" % (len(need) - failed, len(need)))
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
