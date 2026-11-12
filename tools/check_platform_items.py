"""check_platform_items.py: plan 2.9. The things a platform package must declare at release, because no mod can
add them afterwards: permissions, usage texts, file and link associations. One row an item; each row names the
service that needs it. Reads a Nebula source tree and prints PRESENT or MISSING per row; exit 1 if any required
row is missing. Run: python check_platform_items.py <tree> [--tsv out.tsv]
The package builds run it and stop on a missing row: installer/build-native-setup.ps1, platform/apple/build_macos.sh,
platform/android/build_apk.ps1 (the director's table, agents/luma/d/platform_items; this copy is the build's).

A row: platform, item, why (the service or screen that cannot work without it), file (glob in the tree), pattern
(a regular expression that must match in one of the files), required (yes, or "when <service> ships")."""
import glob
import os
import re
import sys

ROWS = [
    # Android
    ("android", "permission INTERNET", "net.internet, net.local: any socket",
     "platform/android/**/AndroidManifest.xml", r'uses-permission[^>]*android\.permission\.INTERNET"', "yes"),
    ("android", "permission ACCESS_NETWORK_STATE", "net: know whether there is a network before a request",
     "platform/android/**/AndroidManifest.xml", r'android\.permission\.ACCESS_NETWORK_STATE"', "yes"),
    ("android", "permission CHANGE_WIFI_MULTICAST_STATE", "net.local: UDP broadcast and multicast (finding a peer on the local network)",
     "platform/android/**/AndroidManifest.xml", r'android\.permission\.CHANGE_WIFI_MULTICAST_STATE"', "yes"),
    ("android", "permission RECORD_AUDIO", "capture.audio",
     "platform/android/**/AndroidManifest.xml", r'android\.permission\.RECORD_AUDIO"', "yes"),
    ("android", "permission CAMERA", "capture.video",
     "platform/android/**/AndroidManifest.xml", r'android\.permission\.CAMERA"', "yes"),
    ("android", "feature microphone not required", "a device without one must still install the game",
     "platform/android/**/AndroidManifest.xml", r'uses-feature[^>]*android\.hardware\.microphone"[^>]*required="false"', "yes"),
    ("android", "feature camera not required", "a device without one must still install the game",
     "platform/android/**/AndroidManifest.xml", r'uses-feature[^>]*android\.hardware\.camera(\.any)?"[^>]*required="false"', "yes"),
    ("android", "permission VIBRATE", "input.rumble",
     "platform/android/**/AndroidManifest.xml", r'android\.permission\.VIBRATE"', "yes"),
    ("android", "open a .luma (VIEW)", "6.1 import: Open with",
     "platform/android/**/AndroidManifest.xml", r'pathPattern="[^"]*\\\\\.luma"', "yes"),
    ("android", "share to Nebula (SEND)", "6.1 import: Share",
     "platform/android/**/AndroidManifest.xml", r'android\.intent\.action\.SEND"', "yes"),
    ("android", "link scheme nebula://", "6.1 import by link; mods.install from a page",
     "platform/android/**/AndroidManifest.xml", r'android:scheme="nebula"', "yes"),
    # Apple (macOS now; iOS takes the same keys when 8.3 opens)
    ("macos", "usage text: microphone", "capture.audio (without the text the system refuses silently)",
     "platform/apple/**/*.plist*", r"NSMicrophoneUsageDescription", "yes"),
    ("macos", "usage text: camera", "capture.video",
     "platform/apple/**/*.plist*", r"NSCameraUsageDescription", "yes"),
    ("macos", "usage text: local network", "net.local (macOS and iOS ask before the first local connection)",
     "platform/apple/**/*.plist*", r"NSLocalNetworkUsageDescription", "yes"),
    ("macos", "entitlement audio-input true", "capture.audio under the hardened runtime",
     "platform/apple/**/*.entitlements", r"com\.apple\.security\.device\.audio-input</key>\s*<true/>", "yes"),
    ("macos", "entitlement camera true", "capture.video under the hardened runtime",
     "platform/apple/**/*.entitlements", r"com\.apple\.security\.device\.camera</key>\s*<true/>", "yes"),
    ("macos", "document type .luma", "6.1 import: Open with, double click",
     "platform/apple/**/*.plist*", r"CFBundleDocumentTypes[\s\S]*luma", "yes"),
    ("macos", "exported type for .luma", "the system knows the file kind",
     "platform/apple/**/*.plist*", r"UTExportedTypeDeclarations[\s\S]*luma", "yes"),
    ("macos", "link scheme nebula://", "6.1 import by link",
     "platform/apple/**/*.plist*", r"CFBundleURLSchemes[\s\S]*nebula", "yes"),
    # Windows (the installer writes the registry)
    ("windows", "file association .luma", "6.1 import: double click, Open with",
     "installer/src/Setup/*.cs", r'CreateSubKey\(@"Software\\Classes\\\.luma"\)', "yes"),
    ("windows", "link scheme nebula://", "6.1 import by link",
     "installer/src/Setup/*.cs", r'SetValue\("URL Protocol", ""\)', "yes"),
    ("windows", "launcher takes a nebula:// link", "6.1 import by link",
     "installer/src/Launcher/*.cs", r'"--open-link"', "yes"),
    ("linux", "SDL host imports --import's files (the desktop entry's %u)", "6.1 import: Open with, by link",
     "platform/sdl/host_window_sdl.cpp", r'launch_args\(\)\.imports', "yes"),
    # Linux
    ("linux", "desktop entry with the .luma type", "6.1 import: Open with",
     "**/*.desktop*", r"MimeType=.*luma", "yes"),
    ("linux", "desktop entry handles nebula://", "6.1 import by link",
     "**/*.desktop*", r"x-scheme-handler/nebula", "yes"),
    ("linux", "shared MIME type for *.luma", "the system knows the file kind",
     "**/*.xml", r'glob pattern="\*\.luma"', "yes"),
]

SKIP = ("/build/", "/target/", "/third_party/", "/.git/", "/node_modules/", "/deps/")


def files_for(tree, pattern):
    found = []
    for path in glob.glob(os.path.join(tree, pattern), recursive=True):
        norm = path.replace("\\", "/")
        if os.path.isfile(path) and not any(part in norm for part in SKIP) and os.path.getsize(path) < 4 * 1024 * 1024:
            found.append(path)
    return found


def main():
    if len(sys.argv) < 2:
        sys.exit(__doc__)
    tree = sys.argv[1]
    out = sys.argv[sys.argv.index("--tsv") + 1] if "--tsv" in sys.argv else None
    lines, missing = [], 0
    for platform, item, why, pattern, regex, required in ROWS:
        state, where = "MISSING", ""
        files = files_for(tree, pattern)
        if not files:
            where = "no file matches " + pattern
        for path in files:
            try:
                text = open(path, encoding="utf-8", errors="replace").read()
            except OSError:
                continue
            if re.search(regex, text):
                state, where = "PRESENT", os.path.relpath(path, tree).replace("\\", "/")
                break
        if state == "MISSING" and required == "yes":
            missing += 1
        lines.append((platform, item, state, why, where))
    width = max(len(l[1]) for l in lines)
    for platform, item, state, why, where in lines:
        print(f"{platform:8s} {item:{width}s} {state:8s} {why}" + (f"  [{where}]" if where else ""))
    if out:
        with open(out, "w", encoding="utf-8", newline="\n") as f:
            f.write("platform\titem\tstate\tneeded by\twhere\n")
            for l in lines:
                f.write("\t".join(l) + "\n")
    print(f"{len(lines)} items, {missing} missing")
    sys.exit(1 if missing else 0)


if __name__ == "__main__":
    main()
