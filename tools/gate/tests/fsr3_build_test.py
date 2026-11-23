"""fsr3_build_test.py: AMD's FSR 3 is in the Windows, Linux and Android packages, and a package without it is refused.

python -X utf8 -I tools/gate/tests/fsr3_build_test.py

- tools/check_fsr3_package.py: planted programs and packages (a Linux tar.gz, an apk with two processors, a bare program): one
  that holds the text FSR 3's code prints is passed; the same without it, a package with one processor short of it, a tar.gz with no
  files/nebula and an apk with no libmain.so are refused (exit 1, or 2 when the package cannot be read).
- tools/check_fsr3_start_log.py: a start check's log that says FSR 3 was made, or names what the device lacks, is told; a log that says
  nothing about FSR 3 (a program built without it) and one that says FSR 3 failed and does not go on without it are refused; one that went on without it is told.
- cmake/fidelityfx_vk_shaders.cmake, configured by cmake alone: nebula_fsr3_left_out stops the configure when NEBULA_REQUIRE_FSR3 is ON
  (environment or variable, the variable first) and says so once otherwise; nebula_fidelityfx_android leaves FSR 3 out with its reason
  where the build runs on another system than Windows with no shader headers given (planted SDK folder, no download), and where
  NEBULA_FSR3 is OFF, and stops there in a package build.
- platform/linux/build_linux.sh: a package build without -s stops with the reason before it builds anything; so does a -s folder that
  is not FidelityFX's.
- The workflows: the Linux job of release.yml and of check.yml needs the ffx-shaders job, takes its artifact, gives the build script
  -s and sets NEBULA_REQUIRE_FSR3; the shader job is a Windows job that uploads the artifact the Linux job downloads (the same name);
  each package job names the package check; the Mac job sets neither (its upscaler is MetalFX).
Exit 0 when every case holds.
"""
import io
import os
import pathlib
import re
import shutil
import subprocess
import sys
import tarfile
import tempfile
import zipfile

HERE = pathlib.Path(__file__).resolve().parent
TREE = HERE.parents[2]
PACKAGE_TOOL = TREE / "tools" / "check_fsr3_package.py"
LOG_TOOL = TREE / "tools" / "check_fsr3_start_log.py"
MARK = b"[fsr3] shared resource context failed"
failures = []


def check(ok, what):
    if not ok:
        failures.append(what)


def run_tool(tool, *args):
    done = subprocess.run([sys.executable, "-X", "utf8", "-I", str(tool), *map(str, args)], capture_output=True, text=True)
    return done.returncode, done.stdout


def tar_gz(path, files):
    with tarfile.open(path, "w:gz") as archive:
        for name, data in files.items():
            info = tarfile.TarInfo(name)
            info.size = len(data)
            archive.addfile(info, io.BytesIO(data))


def zip_of(path, files):
    with zipfile.ZipFile(path, "w", zipfile.ZIP_DEFLATED) as archive:
        for name, data in files.items():
            archive.writestr(name, data)


def bash():
    for candidate in (r"C:\Program Files\Git\bin\bash.exe", r"C:\Program Files\Git\usr\bin\bash.exe", "/bin/bash", "/usr/bin/bash"):
        if pathlib.Path(candidate).is_file():
            return candidate
    return shutil.which("bash")


def job_text(workflow, job):
    """A job's lines: from its name at two spaces to the next job."""
    match = re.search(r"^  %s:\n(.*?)(?=^  [a-z0-9-]+:\n|\Z)" % re.escape(job), workflow, flags=re.S | re.M)
    return match.group(1) if match else ""


# ---- the package check
with tempfile.TemporaryDirectory(prefix="fsr3-build-") as folder:
    tmp = pathlib.Path(folder)
    program = b"\x7fELF" + bytes(100) + MARK + b" : " + bytes(50)
    bare = b"\x7fELF" + bytes(300)
    top = "Nebula-2.0.0-beta.1-linux-x64/"
    tar_gz(tmp / "with.tar.gz", {top + "files/nebula": program, top + "install.sh": b"#!/bin/sh\n"})
    code, out = run_tool(PACKAGE_TOOL, tmp / "with.tar.gz")
    check(code == 0 and "holds FSR 3" in out, "a Linux package whose program holds FSR 3 passes: %s" % out)
    tar_gz(tmp / "without.tar.gz", {top + "files/nebula": bare, top + "install.sh": b"#!/bin/sh\n"})
    code, out = run_tool(PACKAGE_TOOL, tmp / "without.tar.gz")
    check(code == 1 and "no FSR 3" in out, "a Linux package whose program was built without FSR 3 is refused: %s" % out)
    tar_gz(tmp / "empty.tar.gz", {top + "install.sh": b"#!/bin/sh\n"})
    code, out = run_tool(PACKAGE_TOOL, tmp / "empty.tar.gz")
    check(code == 2 and "files/nebula" in out, "a Linux package with no program cannot be judged: %s" % out)
    zip_of(tmp / "both.apk", {"lib/arm64-v8a/libmain.so": program, "lib/x86_64/libmain.so": program, "AndroidManifest.xml": b"x"})
    code, out = run_tool(PACKAGE_TOOL, tmp / "both.apk")
    check(code == 0 and out.count("holds FSR 3") == 2, "an app whose two programs hold FSR 3 passes: %s" % out)
    zip_of(tmp / "one.apk", {"lib/arm64-v8a/libmain.so": program, "lib/x86_64/libmain.so": bare})
    code, out = run_tool(PACKAGE_TOOL, tmp / "one.apk")
    check(code == 1 and "lib/x86_64/libmain.so: has no FSR 3" in out, "an app with one processor's program short of FSR 3 is refused: %s" % out)
    zip_of(tmp / "none.apk", {"AndroidManifest.xml": b"x"})
    code, out = run_tool(PACKAGE_TOOL, tmp / "none.apk")
    check(code == 2 and "libmain.so" in out, "an app with no program cannot be judged: %s" % out)
    (tmp / "NebulaVk.exe").write_bytes(program)
    (tmp / "NebulaD11.exe").write_bytes(bare)
    code, out = run_tool(PACKAGE_TOOL, tmp / "NebulaVk.exe")
    check(code == 0, "a bare program that holds FSR 3 passes: %s" % out)
    code, out = run_tool(PACKAGE_TOOL, tmp / "NebulaVk.exe", tmp / "NebulaD11.exe")
    check(code == 1 and "NebulaVk.exe: NebulaVk.exe: holds FSR 3" in out and "NebulaD11.exe: NebulaD11.exe: has no FSR 3" in out,
          "of two programs, the one without it is refused and the one with it is still told: %s" % out)
    code, out = run_tool(PACKAGE_TOOL, tmp / "missing.exe")
    check(code == 2, "a file that is not there cannot be judged: %s" % out)
    # The mark is the text the sources print: the checks above would pass with any text, so the tool's mark is read from the code.
    sources = [TREE / "runtime" / "src" / "render" / "temporal_upscaler.cpp", TREE / "runtime" / "src" / "render" / "vk" / "temporal_upscaler_vk.cpp"]
    check(all(MARK.decode() in s.read_text(encoding="utf-8") for s in sources),
          "the text the package check looks for is in both FSR 3 sources (D3D12 and Vulkan)")

    # ---- the start log
    made = "[vulkan] FSR 3: the device has what it needs\n[fsr3] context made: render 569x320, output 853x480\nstart check: 600 frames drawn, 1 pictures shown\n"
    lacks = "[vulkan] FSR 3 is not offered on this device: it lacks shaderFloat16, subgroupSizeControl\nstart check: 600 frames drawn\n"
    nothing = "[vulkan] features: depth-clamp=1 temporal-upscaling=0\nstart check: 600 frames drawn\n"
    failed = "[vulkan] FSR 3: the device has what it needs\n[fsr3] context creation failed: -3\n"
    went_on = failed + "[Renderer] the temporal upscaler could not be made on this device: the game goes on without it\nstart check: 600 frames drawn, 0 pictures shown\n"
    for name, text, want, word in (("made", made, 0, "was made"), ("lacks", lacks, 0, "lacks shaderFloat16, subgroupSizeControl"),
                                   ("nothing", nothing, 1, "says nothing about FSR 3"), ("failed", failed, 1, "did not go on without it"),
                                   ("went_on", went_on, 0, "went on without it and drew 600 frames")):
        (tmp / (name + ".log")).write_text(text, encoding="utf-8")
        code, out = run_tool(LOG_TOOL, tmp / (name + ".log"))
        check(code == want and word in out, "a start log that %s: exit %d and '%s': %s" % (name, want, word, out))
    code, out = run_tool(LOG_TOOL, tmp / "no-such.log")
    check(code == 2, "a log that is not there cannot be judged: %s" % out)

    # ---- the build rule, by cmake alone
    candidates = [str(p) for p in pathlib.Path("C:/Program Files").glob(
        "Microsoft Visual Studio/*/*/Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/cmake.exe")] + [
        "C:/Program Files/CMake/bin/cmake.exe", shutil.which("cmake")]
    cmake = next((c for c in candidates if c and pathlib.Path(c).is_file()), None)
    check(cmake is not None, "cmake is found")
    if cmake:
        sdk = tmp / "sdk"
        (sdk / "sdk").mkdir(parents=True)

        def project(name, body):
            folder = tmp / name
            folder.mkdir()
            (folder / "CMakeLists.txt").write_text(
                "cmake_minimum_required(VERSION 3.24)\nproject(%s NONE)\ninclude(\"%s/cmake/fidelityfx_android.cmake\")\n%s\n"
                % (name, TREE.as_posix(), body), encoding="utf-8")
            return folder

        def configure(folder, *args, **env):
            build = pathlib.Path(tempfile.mkdtemp(dir=tmp))
            full = {k: v for k, v in os.environ.items() if k not in ("NEBULA_REQUIRE_FSR3", "NEBULA_FFX_VK_SHADERS")}
            full.update(env)
            done = subprocess.run([cmake, "-S", str(folder), "-B", str(build), *args], env=full, capture_output=True, text=True)
            # CMake wraps a long message at its own width.
            return done.returncode, " ".join((done.stdout + done.stderr).split())

        rule = project("rule", 'nebula_fsr3_left_out("a test reason")\nmessage(STATUS "REACHED")')
        code, said = configure(rule)
        check(code == 0 and "FSR 3 left out: a test reason" in said and "REACHED" in said, "a developer's build says FSR 3 is left out and goes on: %s" % said[-300:])
        check(said.count("FSR 3 left out") == 1, "and says it once")
        code, said = configure(rule, NEBULA_REQUIRE_FSR3="ON")
        check(code != 0 and "which must hold it: a test reason" in said and "REACHED" not in said,
              "a package build (the environment's NEBULA_REQUIRE_FSR3=ON) stops with the reason: %s" % said[-300:])
        code, said = configure(rule, "-DNEBULA_REQUIRE_FSR3=ON")
        check(code != 0 and "which must hold it" in said, "so does the variable: %s" % said[-300:])
        code, said = configure(rule, "-DNEBULA_REQUIRE_FSR3=OFF", NEBULA_REQUIRE_FSR3="ON")
        check(code == 0 and "REACHED" in said, "the variable comes before the environment: %s" % said[-300:])

        off = project("off", 'nebula_fidelityfx_android("h" "p" "n")\nmessage(STATUS "REACHED")')
        code, said = configure(off, "-DNEBULA_FSR3=OFF")
        check(code == 0 and "FSR 3 left out: NEBULA_FSR3 is OFF" in said, "NEBULA_FSR3=OFF is said once in a developer's build: %s" % said[-300:])
        code, said = configure(off, "-DNEBULA_FSR3=OFF", NEBULA_REQUIRE_FSR3="ON")
        check(code != 0 and "NEBULA_FSR3 is OFF" in said and "REACHED" not in said, "and stops a package build: %s" % said[-300:])

        # A build on another system than Windows (the Linux package's) with no shader headers: CMAKE_HOST_WIN32 is taken away so the
        # branch runs here; the SDK is a planted folder (nothing is downloaded).
        linux = project("linux", 'unset(CMAKE_HOST_WIN32)\nnebula_fidelityfx_android("h" "p" "n")\nmessage(STATUS "REACHED")')
        args = ["-DFETCHCONTENT_SOURCE_DIR_FIDELITYFX_SDK=" + sdk.as_posix()]
        code, said = configure(linux, *args)
        check(code == 0 and "FSR 3 left out: its shader compiler runs on Windows" in said and "REACHED" in said and "NEBULA_FFX_VK_SHADERS" in said,
              "a developer's Linux build with no shader headers says why it leaves FSR 3 out: %s" % said[-400:])
        code, said = configure(linux, *args, NEBULA_REQUIRE_FSR3="ON")
        check(code != 0 and "which must hold it: its shader compiler runs on Windows" in said and "REACHED" not in said,
              "a Linux package build with no shader headers stops, naming the shader compiler and the variable: %s" % said[-400:])
        wrong = tmp / "wrong-shaders"
        wrong.mkdir()
        code, said = configure(linux, *args, "-DNEBULA_FFX_VK_SHADERS=" + wrong.as_posix())
        check(code != 0 and "luma-history fix" in said, "shader headers without nebula-luma16.txt are refused: %s" % said[-300:])

    # ---- the Linux build script
    shell = bash()
    check(shell is not None, "bash is found")
    if shell:
        llvm = tmp / "llvm"
        (llvm / "bin").mkdir(parents=True)
        for tool in ("clang", "clang++", "ld.lld"):
            (llvm / "bin" / tool).write_text("#!/bin/sh\n", encoding="utf-8")
            os.chmod(llvm / "bin" / tool, 0o755)
        deps = tmp / "deps"
        deps.mkdir()
        script = (TREE / "platform" / "linux" / "build_linux.sh").as_posix()

        def build(*args, **env):
            full = {k: v for k, v in os.environ.items() if k != "NEBULA_REQUIRE_FSR3"}
            full.update(env)
            done = subprocess.run([shell, script, "-d", deps.as_posix(), "-l", llvm.as_posix(), "-w", (tmp / "work").as_posix(), *args],
                                  env=full, capture_output=True, text=True, timeout=120)
            return done.returncode, done.stdout + done.stderr

        code, said = build(NEBULA_REQUIRE_FSR3="ON")
        check(code == 2 and "makes a package" in said and "-s" in said and not (tmp / "work").exists(),
              "a package build with no -s stops before it builds anything: %s" % said[-300:])
        empty = tmp / "not-ffx"
        empty.mkdir()
        code, said = build("-s", empty.as_posix())
        check(code == 2 and "nebula-luma16.txt" in said and not (tmp / "work").exists(), "a -s folder that is not FidelityFX's shader folder is refused: %s" % said[-300:])
        code, said = build("-s", empty.as_posix(), NEBULA_REQUIRE_FSR3="ON")
        check(code == 2 and "nebula-luma16.txt" in said, "also in a package build: %s" % said[-300:])

# ---- the workflows
for name in ("release.yml", "check.yml"):
    text = (TREE / ".github" / "workflows" / name).read_text(encoding="utf-8").replace("\r\n", "\n")
    maker = job_text(text, "ffx-shaders")
    check("runs-on: windows-2022" in maker and "./.github/actions/ffx-vk-shaders" in maker and "name: ffx-vk-shaders" in maker
          and "actions/upload-artifact" in maker, "%s: the ffx-shaders job is a Windows job that makes the headers and uploads them" % name)
    linux = job_text(text, "linux")
    head = linux.split("steps:")[0]
    check("ffx-shaders" in head.split("runs-on")[0] and "NEBULA_REQUIRE_FSR3: 'ON'" in head, "%s: the Linux job needs the shader job and sets NEBULA_REQUIRE_FSR3" % name)
    check("actions/download-artifact" in linux and "name: ffx-vk-shaders" in linux and '-s "$RUNNER_TEMP/ffx-vk-shaders"' in linux,
          "%s: the Linux job takes the artifact and gives the build script -s" % name)
    check("tools/check_fsr3_package.py" in linux and "tools/check_fsr3_start_log.py" in linux and "upscaler=fsr3" in linux,
          "%s: the Linux job checks the program holds FSR 3 and starts it with FSR 3 chosen" % name)
    mac = job_text(text, "macos") or job_text(text, "mac")
    check("NEBULA_REQUIRE_FSR3" not in mac, "%s: the Mac job does not ask for FSR 3" % name)
    android = job_text(text, "android")
    check("NEBULA_REQUIRE_FSR3: 'ON'" in android and "tools/check_fsr3_package.py" in android, "%s: the Android job asks for FSR 3 and checks the app" % name)
release = (TREE / ".github" / "workflows" / "release.yml").read_text(encoding="utf-8").replace("\r\n", "\n")
check("NEBULA_REQUIRE_FSR3: 'ON'" in job_text(release, "windows-hosts"), "release.yml: the Windows programs are built asking for FSR 3")
check("tools/check_fsr3_package.py" in job_text(release, "windows-setup"), "release.yml: the programs the Setup packs are checked for FSR 3")
check("NEBULA_REQUIRE_FSR3: 'ON'" in job_text((TREE / ".github" / "workflows" / "check.yml").read_text(encoding="utf-8").replace("\r\n", "\n"), "windows"),
      "check.yml: the Windows programs are built asking for FSR 3")
table = (TREE / "tools" / "gate" / "github_jobs.tsv").read_text(encoding="utf-8")
check("FSR 3 shaders (Windows)\tmust" in table, "the landing's job table names the shader job as a job that must be green")

if failures:
    print("fsr3_build_test.py: FAILED:\n  " + "\n  ".join(failures))
    sys.exit(1)
print("fsr3_build_test.py: pass")
