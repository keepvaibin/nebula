"""release_workflow_test.py: the version and the mod switch of a release (release.yml, cmake/nebula_options.cmake).

python -X utf8 -I tools/gate/tests/release_workflow_test.py

- The workflow's "Version and notes" step is run by bash on a planted folder (a CMakeLists.txt with the project's version, a
  .github/release-notes folder): v2.0.0-beta.1 is a pre-release (prefix Nebula-Beta, suffix -beta.1, mod support OFF) that needs
  .github/release-notes/2.0.0-beta.1.md; v2.0.0 is a release as before (prefix Nebula, no suffix, mod support ON); a dry run
  names its version or takes CMakeLists.txt's; a tag that is not the project's version, or has another label, is refused.
- Every package job takes the two outputs as NEBULA_MODS and NEBULA_VERSION_SUFFIX; the LumaKit job runs only with mod support;
  the jobs that need it go on when it is skipped; a pre-release is created as a pre-release with its notes file.
- cmake/nebula_options.cmake, configured by cmake alone: NEBULA_MODS=OFF (as a variable or in the environment, the variable
  first) defines NEBULA_NO_MODS for every program, the version's label is -beta.<n> or nothing.
Exit 0 when every case holds.
"""
import os
import pathlib
import re
import shutil
import subprocess
import sys
import tempfile

HERE = pathlib.Path(__file__).resolve().parent
TREE = HERE.parents[2]
WORKFLOW = (TREE / ".github" / "workflows" / "release.yml").read_text(encoding="utf-8").replace("\r\n", "\n")
failures = []


def check(ok, what):
    if not ok:
        failures.append(what)


def step_script(name):
    """The run: block of the step with this name."""
    lines = WORKFLOW.split("\n")
    at = next(i for i, line in enumerate(lines) if line.strip() == "- name: " + name)
    run = next(i for i in range(at, len(lines)) if lines[i].strip() == "run: |")
    indent = len(lines[run]) - len(lines[run].lstrip())
    body = []
    for line in lines[run + 1:]:
        if line.strip() and len(line) - len(line.lstrip()) <= indent:
            break
        body.append(line[indent + 2:] if line.strip() else "")
    return "\n".join(body)


def job_text(job):
    """A job's lines: from its name at two spaces to the next job."""
    match = re.search(r"^  %s:\n(.*?)(?=^  [a-z0-9-]+:\n|\Z)" % re.escape(job), WORKFLOW, flags=re.S | re.M)
    return match.group(1) if match else ""


def bash():
    for candidate in (r"C:\Program Files\Git\bin\bash.exe", r"C:\Program Files\Git\usr\bin\bash.exe", "/bin/bash", "/usr/bin/bash"):
        if pathlib.Path(candidate).is_file():
            return candidate
    return shutil.which("bash")


def version_step(tmp, tag="", dry_version="", notes=(), project="2.0.0"):
    """Runs the step in a planted folder; (exit code, {output: value}, the output text)."""
    work = pathlib.Path(tempfile.mkdtemp(dir=tmp))
    (work / "CMakeLists.txt").write_text("cmake_minimum_required(VERSION 3.24)\nproject(Nebula VERSION %s LANGUAGES C CXX)\n" % project, encoding="utf-8")
    (work / ".github" / "release-notes").mkdir(parents=True)
    for version in notes:
        (work / ".github" / "release-notes" / (version + ".md")).write_text("Notes.\n", encoding="utf-8")
    out = work / "outputs.txt"
    script = "git() { echo 0123456789abcdef0123456789abcdef01234567; }\n" + step_script("Version and notes")
    env = dict(os.environ, GITHUB_OUTPUT=str(out), TAG=tag, INPUT_VERSION=dry_version)
    done = subprocess.run([bash(), "-c", script], cwd=work, env=env, capture_output=True, text=True)
    values = {}
    if out.is_file():
        for line in out.read_text(encoding="utf-8").splitlines():
            key, _, value = line.partition("=")
            values[key] = value
    return done.returncode, values, done.stdout + done.stderr


check(bash() is not None, "bash is found")
with tempfile.TemporaryDirectory(prefix="release-workflow-") as folder:
    tmp = pathlib.Path(folder)
    code, out, said = version_step(tmp, tag="v2.0.0-beta.1", notes=["2.0.0-beta.1"])
    check(code == 0 and out.get("version") == "2.0.0-beta.1" and out.get("prefix") == "Nebula-Beta" and out.get("suffix") == "-beta.1"
          and out.get("mods") == "OFF", "v2.0.0-beta.1: a pre-release, no mod support: %s %s" % (out, said))
    code, out, said = version_step(tmp, tag="v2.0.0-beta.1", notes=["2.0.0"])
    check(code != 0 and "release-notes/2.0.0-beta.1.md" in said, "v2.0.0-beta.1 without its notes is refused: %s" % said)
    code, out, said = version_step(tmp, tag="v2.0.0", notes=["2.0.0"])
    check(code == 0 and out.get("version") == "2.0.0" and out.get("prefix") == "Nebula" and out.get("suffix") == ""
          and out.get("mods") == "ON", "v2.0.0: a release as before, mod support: %s %s" % (out, said))
    code, out, said = version_step(tmp, tag="v2.0.0", notes=[])
    check(code != 0 and "release-notes/2.0.0.md" in said, "v2.0.0 without its notes is refused: %s" % said)
    code, out, said = version_step(tmp, tag="v2.1.0", notes=["2.1.0"])
    check(code != 0 and "not the version in CMakeLists.txt" in said, "a tag that is not the project's version is refused: %s" % said)
    code, out, said = version_step(tmp, tag="v2.0.0-rc.1", notes=["2.0.0-rc.1"])
    check(code != 0, "a label other than -beta.<n> is refused: %s" % said)
    code, out, said = version_step(tmp, tag="")
    check(code == 0 and out.get("version") == "2.0.0" and out.get("mods") == "ON" and out.get("suffix") == "",
          "a dry run with no version builds the project's version with mod support: %s %s" % (out, said))
    code, out, said = version_step(tmp, dry_version="2.0.0-beta.1", notes=["2.0.0-beta.1"])
    check(code == 0 and out.get("version") == "2.0.0-beta.1" and out.get("mods") == "OFF" and out.get("suffix") == "-beta.1"
          and out.get("prefix") == "Nebula-Beta", "a dry run that names the beta builds it as the beta: %s %s" % (out, said))
    code, out, said = version_step(tmp, dry_version="v2.0.0-beta.1", notes=["2.0.0-beta.1"])
    check(code == 0 and out.get("version") == "2.0.0-beta.1", "a dry run's version may carry the v: %s" % said)
    code, out, said = version_step(tmp, dry_version="2.0.0-beta.1", notes=[])
    check(code != 0 and "release-notes/2.0.0-beta.1.md" in said, "a dry run that names the beta needs its notes too: %s" % said)
    code, out, said = version_step(tmp, dry_version="2.0.0-beta.2", notes=["2.0.0-beta.2"], project="2.0.1")
    check(code != 0, "a dry run's version of another project version is refused: %s" % said)

    # ---- the jobs
    for job in ("windows-hosts", "windows-game", "windows-setup", "linux", "macos", "android"):
        text = job_text(job)
        check("NEBULA_MODS: ${{ needs.release.outputs.mods }}" in text and "NEBULA_VERSION_SUFFIX: ${{ needs.release.outputs.suffix }}" in text,
              "the %s job takes the mod switch and the version's label from the release job" % job)
    lumakit = job_text("lumakit")
    check("needs.release.outputs.mods == 'ON'" in lumakit.split("runs-on")[0], "the lumakit job runs only with mod support")
    for job in ("windows-setup", "linux", "macos", "android"):
        head = job_text(job).split("runs-on")[0]
        check("needs.lumakit.result == 'skipped'" in head and "!cancelled()" in head, "the %s job goes on when the lumakit job is skipped" % job)
    check("needs.release.result == 'success'" in job_text("windows-setup").split("runs-on")[0] and
          "needs.windows-game.result == 'success'" in job_text("windows-setup").split("runs-on")[0],
          "and still needs the jobs it takes its files from")
    draft = step_script("The draft release")
    check("*-*) prerelease=--prerelease" in draft and '--notes-file ".github/release-notes/$VERSION.md"' in draft,
          "a version with a label is created as a pre-release, with its notes file")
    check("workflow_dispatch" in WORKFLOW and "      version:\n        description: A dry run's version" in WORKFLOW,
          "a dry run can be told its version")

    # ---- the Mac job takes the compiler check.yml pins; the Linux start check runs the validation layer; the notes hold measured
    # numbers, not placeholders
    macos = job_text("macos")
    check("brew install llvm" not in macos and "LLVM-$LLVM_VERSION-macOS-ARM64.tar.xz" in macos,
          "the Mac job takes the pinned compiler of check.yml, not Homebrew's newest")
    check("runs-on: macos-15-intel" in job_text("macos-intel"), "the Intel Mac's start check runs on an Intel Mac")
    linux = job_text("linux")
    check("--gpu-validation 1" in linux and "vulkan-validationlayers" in linux and "VK-LIVE-ERR" in linux,
          "the Linux start check runs the validation layer and fails on its errors")
    check("GLIBC_" in linux and "GITHUB_STEP_SUMMARY" in linux, "the Linux job prints the glibc and libstdc++ the program needs")
    notes = (TREE / ".github" / "release-notes" / "2.0.0-beta.1.md").read_text(encoding="utf-8")
    check("_LINUX" not in notes, "the beta's notes hold the measured glibc and libstdc++, not a placeholder")

    # ---- cmake/nebula_options.cmake
    # Visual Studio's cmake first (the build's own): another on the PATH may be an MSYS one, which reads Windows paths its own way.
    candidates = [str(p) for p in pathlib.Path("C:/Program Files").glob(
        "Microsoft Visual Studio/*/*/Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/cmake.exe")] + [
        "C:/Program Files/CMake/bin/cmake.exe", shutil.which("cmake")]
    cmake = next((c for c in candidates if c and pathlib.Path(c).is_file()), None)
    check(cmake is not None, "cmake is found")
    if cmake:
        project = tmp / "options"
        project.mkdir()
        (project / "CMakeLists.txt").write_text(
            'cmake_minimum_required(VERSION 3.24)\nproject(options NONE)\n'
            'include("%s/cmake/nebula_options.cmake")\n'
            'get_directory_property(defs COMPILE_DEFINITIONS)\n'
            'message(STATUS "RESULT defs=[${defs}] suffix=[${NEBULA_VERSION_SUFFIX}] mods=[${NEBULA_MODS}]")\n' % TREE.as_posix(), encoding="utf-8")

        def configure(*args, **env):
            build = pathlib.Path(tempfile.mkdtemp(dir=tmp))
            full = {k: v for k, v in os.environ.items() if k not in ("NEBULA_MODS", "NEBULA_VERSION_SUFFIX")}
            full.update(env)
            done = subprocess.run([cmake, "-S", str(project), "-B", str(build), *args], env=full, capture_output=True, text=True)
            match = re.search(r"RESULT defs=\[(.*?)\] suffix=\[(.*?)\] mods=\[(.*?)\]", done.stdout)
            return done.returncode, (match.groups() if match else None), done.stdout + done.stderr

        code, got, said = configure()
        check(code == 0 and got == ("", "", "ON"), "by default: mods, no label, NEBULA_NO_MODS not defined: %s" % (got,))
        code, got, said = configure("-DNEBULA_MODS=OFF")
        check(code == 0 and got and "NEBULA_NO_MODS=1" in got[0] and got[2] == "OFF", "-DNEBULA_MODS=OFF defines NEBULA_NO_MODS: %s" % (got,))
        code, got, said = configure(NEBULA_MODS="OFF")
        check(code == 0 and got and "NEBULA_NO_MODS=1" in got[0], "the environment's NEBULA_MODS=OFF does too: %s" % (got,))
        code, got, said = configure("-DNEBULA_MODS=ON", NEBULA_MODS="OFF")
        check(code == 0 and got and got[0] == "" and got[2] == "ON", "the variable comes before the environment: %s" % (got,))
        code, got, said = configure(NEBULA_VERSION_SUFFIX="-beta.1")
        check(code == 0 and got and got[1] == "-beta.1", "the environment's version label is taken: %s" % (got,))
        code, got, said = configure("-DNEBULA_VERSION_SUFFIX=-beta.12")
        check(code == 0 and got and got[1] == "-beta.12", "so is the variable's: %s" % (got,))
        code, got, said = configure("-DNEBULA_VERSION_SUFFIX=-rc.1")
        check(code != 0 and "-beta.<n>" in said, "a label that is not -beta.<n> stops the configure: %s" % said[-200:])

if failures:
    print("release_workflow_test.py: FAILED:\n  " + "\n  ".join(failures))
    sys.exit(1)
print("release_workflow_test.py: pass")
