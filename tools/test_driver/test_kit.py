"""test_kit.py --out FOLDER [--cxx CLANG++]: the test kit LumaLink installs for `lumalink test` (the game ships none of it:
tools/release_check.ps1 rule 1).

FOLDER gets:
    nebula_test_extension.dll (Windows x64) or libnebula_test_extension.so (Linux x64): the test runs' host extension
        (test_driver.cmake's nebula_test_extension, the same sources), built here with clang++
    luma_test.c                      the game half's source; LumaLink builds it as a mod against the installed game's SDK
    tools/lumacore/luma_test.py      the runner, laid out as in this tree (it reads the three files below by their place)
    tools/lumacore/capabilities.json
    tools/verify/settings_args.py
    runtime/include/galaxy/store_version.inc
    kit.json                         {"nebula": commit, "version": this tree's, "store_version": n, "extension": ABI}

The runner drives Windows x64 and Linux x64 (luma_test.py HOSTS); elsewhere the kit is the runner's files alone and
kit.json says "extension": null. On Windows run it in a Visual Studio x64 environment (tools/enter_msvc_environment.ps1):
clang++ takes the C++ library and the Windows SDK from it. Python 3, standard library only.
"""
import argparse
import json
import os
import pathlib
import platform
import re
import shutil
import subprocess
import sys

ROOT = pathlib.Path(__file__).resolve().parents[2]
EXTENSION = ROOT / "tools" / "test_driver" / "extension"
SOURCES = [EXTENSION / n for n in ("test_extension.cpp", "harness.cpp", "test_services.cpp", "data_guard.cpp")] + [
    ROOT / "runtime" / "src" / "hidden_start.cpp"]
RUNNER = ["tools/lumacore/luma_test.py", "tools/lumacore/capabilities.json", "tools/verify/settings_args.py",
          "runtime/include/galaxy/store_version.inc"]


def host():
    machine = platform.machine().lower()
    x64 = machine in ("amd64", "x86_64")
    if platform.system() == "Windows" and x64:
        return "windows-x64"
    if platform.system() == "Linux" and x64:
        return "linux-x64"
    return None


def commit():
    try:
        return subprocess.run(["git", "-C", str(ROOT), "rev-parse", "HEAD"], capture_output=True, text=True,
                              check=True).stdout.strip()
    except (OSError, subprocess.CalledProcessError):
        return "unknown"


def version():
    m = re.search(r"project\(Nebula VERSION ([0-9.]+)", (ROOT / "CMakeLists.txt").read_text(encoding="utf-8"))
    if not m:
        sys.exit("test_kit: CMakeLists.txt names no Nebula version")
    return m.group(1)


def extension_abi():
    m = re.search(r"#define NEBULA_HOST_EXTENSION_VERSION (\d+)u",
                  (ROOT / "native" / "include" / "nebula_host_extension.h").read_text(encoding="utf-8"))
    return int(m.group(1))


def build_extension(cxx, target, out):
    # The build machine's folders stay out of the library (assert and panic texts name sources by their place).
    common = ["-std=c++20", "-O2", "-DNDEBUG", "-shared", "-ffile-prefix-map=%s=nebula" % ROOT,
              "-ffile-prefix-map=%s=nebula" % ROOT.as_posix(), "-I", str(ROOT / "native" / "include"),
              "-I", str(ROOT / "runtime" / "include"), "-I", str(EXTENSION), "-Wall", "-Wextra", "-Wno-missing-field-initializers"]
    if target == "windows-x64":
        name = "nebula_test_extension.dll"
        # The C++ runtime linked in: the extension loads into any installed game, with or without the Visual C++
        # runtime on the PC.
        flags = ["--target=x86_64-pc-windows-msvc", "-fms-runtime-lib=static", "-fuse-ld=lld", "-D_CRT_SECURE_NO_WARNINGS",
                 "-DNOMINMAX", "-DWIN32_LEAN_AND_MEAN"]
        libs = ["-lshell32"]
    else:
        name = "libnebula_test_extension.so"
        flags = ["-fPIC", "-fvisibility=hidden"]
        libs = ["-ldl", "-pthread"]
    library = out / name
    command = [cxx] + flags + common + [str(s) for s in SOURCES] + libs + ["-o", str(library)]
    print("test_kit: building %s" % name, flush=True)
    result = subprocess.run(command)
    if result.returncode != 0:
        sys.exit("test_kit: %s did not build (exit %d)" % (name, result.returncode))
    for leftover in (out / "nebula_test_extension.lib", out / "nebula_test_extension.exp", out / "nebula_test_extension.pdb"):
        leftover.unlink(missing_ok=True)
    return name


def main():
    parser = argparse.ArgumentParser(description="Builds the test kit LumaLink installs.")
    parser.add_argument("--out", required=True, help="the kit's folder (made empty first)")
    parser.add_argument("--cxx", default=os.environ.get("CXX") or "clang++", help="clang++ (LLVM 17 or later)")
    args = parser.parse_args()
    out = pathlib.Path(args.out).resolve()
    if out.exists():
        shutil.rmtree(out)
    out.mkdir(parents=True)
    target = host()
    library = build_extension(args.cxx, target, out) if target else None
    shutil.copyfile(ROOT / "tools" / "test_driver" / "luma_test" / "luma_test.c", out / "luma_test.c")
    for rel in RUNNER:
        (out / rel).parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(ROOT / rel, out / rel)
    kit = {"nebula": commit(), "version": version(),
           "store_version": int((ROOT / RUNNER[3]).read_text(encoding="utf-8").strip()),
           "extension": extension_abi() if library else None, "platform": target, "library": library}
    (out / "kit.json").write_text(json.dumps(kit, indent=2) + "\n", encoding="utf-8")
    print("test_kit: %s ready (%s)" % (out, target or "the runner alone: no runner adapter for this system"))


if __name__ == "__main__":
    main()
