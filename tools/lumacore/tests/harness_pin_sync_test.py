"""harness_pin_sync_test.py: the pinned LumaLink's nebula/ is a copy synced by content (N-1070).

python -X utf8 -I tools/lumacore/tests/harness_pin_sync_test.py

On a made-up tree in a temporary folder, mod_tests.sync_by_content:
- copies the input paths (files and folders) and nothing else;
- a second sync writes nothing and leaves the copies' times alone, also after the tree's file is written again with
  the same bytes (what git does on a rebase or checkout; cargo goes by file times);
- writes a file whose bytes changed, and removes one the tree no longer has.
mod_tests.check_nebula_inputs, on made-up dep-info and build script output: a build that read only input paths
passes; one that read another tree path names it.
Exit 0 when every case holds.
"""
import os
import pathlib
import sys
import tempfile
import time

HERE = pathlib.Path(__file__).resolve().parent
sys.path.insert(0, str(HERE.parent))
import mod_tests  # noqa: E402

failures = []


def check(ok, what):
    if not ok:
        failures.append(what)


sync = getattr(mod_tests, "sync_by_content", None)
inputs = getattr(mod_tests, "check_nebula_inputs", None)
check(sync is not None and inputs is not None, "mod_tests has sync_by_content and check_nebula_inputs")

if sync is not None and inputs is not None:
    with tempfile.TemporaryDirectory() as tmp:
        t = pathlib.Path(tmp)
        tree, cache = t / "tree", t / "pin"
        files = {"Cargo.toml": "[workspace]\n", "Cargo.lock": "# lock\n", "crates/nebula-import/src/lib.rs": "pub fn a() {}\n",
                 "crates/nebula-import/target/x.rlib": "built\n", "crates/luma-validate/build.rs": "fn main() {}\n",
                 "localization/NebulaText.tsv": "id\ttext\n", "native/host/host_main.cpp": "int main() {}\n"}
        for rel, text in files.items():
            (tree / rel).parent.mkdir(parents=True, exist_ok=True)
            (tree / rel).write_text(text, encoding="utf-8")
        nebula = cache / "nebula"

        check(sync(tree, nebula) == 5, "the first sync writes the five input files")
        check(not (nebula / "native").exists() and not (nebula / "crates/nebula-import/target").exists(),
              "paths outside the inputs and build folders are not copied")
        lib = nebula / "crates/nebula-import/src/lib.rs"
        before = lib.stat().st_mtime_ns
        time.sleep(0.05)
        (tree / "crates/nebula-import/src/lib.rs").write_text("pub fn a() {}\n", encoding="utf-8")
        check(sync(tree, nebula) == 0 and lib.stat().st_mtime_ns == before,
              "a tree file written again with the same bytes leaves its copy and its time alone")
        (tree / "localization/NebulaText.tsv").write_text("id\ttext\nN1\thi\n", encoding="utf-8")
        (tree / "crates/luma-validate/build.rs").unlink()
        check(sync(tree, nebula) == 2 and (nebula / "localization/NebulaText.tsv").read_text(encoding="utf-8").endswith("hi\n")
              and not (nebula / "crates/luma-validate/build.rs").exists(),
              "a changed file is written and a removed one is removed")

        deps = cache / "target/release/deps"
        build = cache / "target/release/build/luma-validate-1"
        deps.mkdir(parents=True)
        build.mkdir(parents=True)
        (deps / "nebula_import.d").write_text(
            "%s: %s %s\n" % (deps / "nebula_import.dll", os.path.join("nebula", "crates", "nebula-import", "src", "lib.rs"),
                             os.path.join("crates", "lumalink-core", "src", "lib.rs")), encoding="utf-8")
        (build / "output").write_text("cargo:rerun-if-changed=%s\n"
                                      % (cache / "nebula/crates/luma-validate/../../localization/NebulaText.tsv"),
                                      encoding="utf-8")
        check(inputs(cache) is None, "a build that read only input paths passes")
        (deps / "luma_validate.d").write_text(
            "%s: %s\n" % (deps / "luma_validate.rlib", os.path.join("nebula", "native", "include", "nebula_game_api.h")),
            encoding="utf-8")
        outside = inputs(cache)
        check(outside is not None and outside.endswith("nebula_game_api.h"),
              "a build that read another tree path names it")

if failures:
    print("harness_pin_sync_test.py: FAILED:\n  " + "\n  ".join(failures))
    sys.exit(1)
print("harness_pin_sync_test.py: pass")
