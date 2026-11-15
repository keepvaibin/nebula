"""build_lumakit.py: builds LumaKit, the library mod that comes with the game, from its source in the LumaLink
repository. The packages ship it as installer/lumakit/lumakit.luma (the Setup's luma/mods, the Linux package's
luma/mods, the Android app's assets, the Mac app's Resources/luma/mods).

    python tools/build_lumakit.py --lumalink <lumalink> --kit <LumaLink's lumakit folder> --names <folder>
                                  --game <game.pak> --output installer/lumakit/lumakit.luma
    python tools/build_lumakit.py --lumalink <lumalink> --kit <LumaLink's lumakit folder> --names <folder>
                                  --verify installer/lumakit/lumakit.luma

Every platform the kit's luma.json lists (code.platforms) is built in a copy of its own, at the same time, with
`lumalink build --names <names> --sdk native/include --platform <platform>`. The interface each build writes
(code/lumakit.lumi) must be the same from every platform; the platforms' code folders then go into one copy.

--output packs that copy with `lumalink pack`. Packing checks every rule, and three of LumaKit's read the game's files,
so it needs the player's imported game (--game): the kit is packed on a PC that has it, and the file is kept in the
tree. --verify is the release build's check of that file (no game needed): `lumalink verify` passes only when the file
is exactly what this source builds to with this game's SDK and self-description.

--names is the folder LumaLink's --names takes: a game build's luma/ function rows, with luma_types.json,
luma_functions.json, luma_numbers.json, luma_services.json, smg_game.luma-placement.json and smg_game.luma-self.json.
The SDK headers (--sdk) are added in its sdk folder, as an installed game has them. LumaLink finds its C compiler as it
does for any mod: LUMALINK_CLANG, then the one installed with it."""
import argparse
import concurrent.futures
import filecmp
import json
import os
import shutil
import subprocess
import sys
import tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


def fresh_copy(source, destination):
    """The kit's source in `destination`, without what an earlier build wrote under code/ (its *.kit schemas stay)."""
    shutil.copytree(source, destination)
    code = os.path.join(destination, "code")
    if os.path.isdir(code):
        for name in os.listdir(code):
            path = os.path.join(code, name)
            if name.endswith(".kit"):
                continue
            if os.path.isdir(path):
                shutil.rmtree(path)
            else:
                os.remove(path)


def build(lumalink, names, sdk, folder, platform):
    command = [lumalink, "build", "--names", names, "--sdk", sdk, "--platform", platform, folder]
    done = subprocess.run(command, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, encoding="utf-8",
                          errors="replace")
    return platform, done.returncode, done.stdout


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    parser.add_argument("--lumalink", required=True, help="the lumalink program")
    parser.add_argument("--kit", required=True, help="LumaKit's source folder (the LumaLink repository's lumakit)")
    parser.add_argument("--names", required=True, help="the game build's self-description folder")
    task = parser.add_mutually_exclusive_group(required=True)
    task.add_argument("--output", help="the .luma to write (needs --game)")
    task.add_argument("--verify", help="a .luma that must be exactly this source's build")
    parser.add_argument("--game", help="the imported game (Nebula's game.pak), for --output")
    parser.add_argument("--sdk", default=os.path.join(ROOT, "native", "include"), help="the game's SDK headers")
    arguments = parser.parse_args()

    if arguments.output and not arguments.game:
        parser.error("--output needs --game: packing checks the rules that read the game's files")
    kit = os.path.abspath(arguments.kit)
    with open(os.path.join(kit, "luma.json"), encoding="utf-8") as file:
        platforms = json.load(file)["code"]["platforms"]
    if not platforms:
        sys.exit(f"{kit}/luma.json lists no platforms")

    work = tempfile.mkdtemp(prefix="lumakit-")
    try:
        # The self-description as an installed game has it: the game's SDK headers in its sdk folder (pack reads them
        # there).
        names = os.path.join(work, "names")
        shutil.copytree(arguments.names, names)
        os.makedirs(os.path.join(names, "sdk"), exist_ok=True)
        for name in os.listdir(arguments.sdk):
            if name.startswith("luma") and os.path.isfile(os.path.join(arguments.sdk, name)):
                shutil.copy2(os.path.join(arguments.sdk, name), os.path.join(names, "sdk", name))
        folders = {}
        for platform in platforms:
            folders[platform] = os.path.join(work, platform, "lumakit")
            fresh_copy(kit, folders[platform])
        failed = []
        with concurrent.futures.ThreadPoolExecutor(max_workers=len(platforms)) as pool:
            jobs = [pool.submit(build, arguments.lumalink, names, arguments.sdk, folders[p], p) for p in platforms]
            for job in concurrent.futures.as_completed(jobs):
                platform, code, log = job.result()
                if code == 0:
                    print(f"built {platform}")
                else:
                    failed.append(platform)
                    print(f"::group::lumakit {platform}: lumalink build failed (exit {code})")
                    print(log)
                    print("::endgroup::")
        if failed:
            sys.exit(f"LumaKit did not build for {', '.join(sorted(failed))}")

        # One interface for every platform: the builds agree on it byte for byte.
        first = os.path.join(folders[platforms[0]], "code", "lumakit.lumi")
        for platform in platforms[1:]:
            if not filecmp.cmp(first, os.path.join(folders[platform], "code", "lumakit.lumi"), shallow=False):
                sys.exit(f"code/lumakit.lumi of {platforms[0]} and {platform} differ")

        merged = os.path.join(work, "merged", "lumakit")
        fresh_copy(kit, merged)
        os.makedirs(os.path.join(merged, "code"), exist_ok=True)
        shutil.copy2(first, os.path.join(merged, "code", "lumakit.lumi"))
        for platform in platforms:
            shutil.copytree(os.path.join(folders[platform], "code", platform), os.path.join(merged, "code", platform))

        if arguments.verify:
            verify = [arguments.lumalink, "verify", "--source", merged, os.path.abspath(arguments.verify)]
            if subprocess.run(verify).returncode != 0:
                sys.exit(f"{arguments.verify} is not this LumaKit source's build: pack it again (--output, --game)")
            print(f"{arguments.verify}: LumaKit for {', '.join(platforms)}, as this source builds it")
        else:
            output = os.path.abspath(arguments.output)
            os.makedirs(os.path.dirname(output), exist_ok=True)
            pack = [arguments.lumalink, "pack", merged, "--no-installed", "--names", names, "--game",
                    os.path.abspath(arguments.game), "--output", output]
            if subprocess.run(pack).returncode != 0:
                sys.exit("lumalink pack failed")
            print(f"{output}: LumaKit for {', '.join(platforms)}")
    finally:
        shutil.rmtree(work, ignore_errors=True)


if __name__ == "__main__":
    main()
