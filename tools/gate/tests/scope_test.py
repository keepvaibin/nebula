"""scope_test.py: which checks a change needs (tools/gate/scope.py and scope.tsv, package T4), without the game, a build, a
graphics card or a tablet.

    python -X utf8 -I tools/gate/tests/scope_test.py [--work FOLDER]

  each_row                  every row of the tree's own table, read as the gate reads it: what a changed file of that row runs
  repo_in_every_run         the repository checks are in every plan, whatever a row names (scope.ALWAYS)
  repo_group_is_in_every_run  the repository checks are in every plan, whatever a row names (scope.ALWAYS)
  union_of_rows             a change of several files runs the union of every row each file matches, whatever the order
  a_change_that_can_move_a_picture_takes_the_set   the paths of the game, the engine, the host, the importer, the test driver, the runner,
                            the tour and its recordings and PICTURES-CHANGE.txt take the group `pictures` (and the tablet's 12 stops);
                            documents, plan data, the gate's own tools and the unit tests take none
  the_group_brings_the_tablet_and_the_main_levels_checks  `--scope pictures` is engine3, tour3, pictures and tablet12
  the_set_is_every_picture_check_of_the_main_level  what the group selects (every renderer, both other discs on Direct3D 12, the tablet's
                            part, the declared check) and what it leaves out; a sibling play is taken by its name
  github_group              the group `github` (no local check: read after the move) is on the rows of code, the test driver, the
                            renderers, the importer, Android and the unit tests, and `github-first` (waited for before the move) on
                            `.github/**` and in the full run; documents, tools, test data and pictures have neither (the forms and
                            the rows of the real tree are in github_after_test.py)
  outside_every_row_is_full a file no row names makes the run full (and so does a change with no file)
  mods_off_in_full          mods = off keeps the mod suites and LumaLink's check out of the full run (the main level) and out
                            of the nightly's; --mods short or full brings them back, and one switch does it for both
  documents_only_no_build   a change of documents only selects the repository checks, and the gate, run on that selection
                            with a fake check runner, runs those four and starts no build
  groups_name_real_checks   every pattern of every group names a check the gate registers (a renamed check would leave a
                            group empty and a landing unchecked)
  selection_follows_needs   a tour on one renderer brings its build, pictures and replays and no other renderer's; the
                            declared-pictures check does not pull every picture check in
  table_errors              a table with an unknown group, a mods value that is none, no header or no mods line is refused
  changed_files_from_git    the files of a branch from its merge base with main, in a temporary repository
Exit 0 when every case holds.
"""
import contextlib
import io
import os
import pathlib
import subprocess
import sys
import tempfile
import unittest

HERE = pathlib.Path(__file__).resolve().parent
sys.path.insert(0, str(HERE.parent))
import gate  # noqa: E402
import scope  # noqa: E402

TABLE = scope.read_table()


def plan(*paths):
    return scope.plan_for(list(paths), TABLE)


class Rows(unittest.TestCase):
    def test_each_row(self):
        cases = [
            (("docs/plan/04-GATE-AND-MAIN.md", "README.md", "docs/plan/ledger.tsv"), ["repo"]),
            (("tests/pictures/approved.tsv",), ["repo"]),
            (("PICTURES-CHANGE.txt",), ["repo", "engine3", "tour3", "pictures", "tablet12"]),
            (("tools/plan/plan.py", "tools/gate/gate.py"), ["repo", "tools"]),
            (("native/petari/src/Game/Foo.cpp",), ["repo", "build", "unit", "start", "engine3", "tour3", "pictures", "android", "tablet12", "github"]),
            (("runtime/src/render/renderer_common.cpp",), ["repo", "build", "unit", "start", "engine3", "tour3", "pictures", "validate", "android", "tablet12", "github"]),
            (("runtime/include/galaxy/render/renderer.h",), ["repo", "build", "unit", "start", "engine3", "tour3", "pictures", "validate", "android", "tablet12", "github"]),
            (("runtime/src/audio/mixer.cpp",), ["repo", "build", "unit", "start", "engine3", "tour3", "pictures", "android", "tablet12", "github"]),
            (("platform/sdl/host.cpp",), ["repo", "build", "unit", "start", "engine3", "tour3", "pictures", "android", "tablet12", "github"]),
            (("crates/nebula-import/src/lib.rs",), ["repo", "engine3", "tour3", "pictures", "import", "android", "tablet12", "github"]),
            (("platform/android/app/build.gradle",), ["repo", "android", "tablet12", "github"]),   # repo: in every run (scope.ALWAYS)
            (("runtime/src/render/vk/renderer_vk.cpp",), ["repo", "build", "unit", "start", "engine3", "tour3", "pictures",
                                                          "validate", "android", "tablet12", "github"]),
            (("tests/engine/boot/test.json",), ["repo", "build", "engine3", "tour3", "pictures", "tablet12"]),
            (("tests/android/walk/test.json",), ["repo", "android", "tablet12", "github"]),
            (("tests/replays/stage.txt",), ["repo", "build", "engine3", "tour3", "pictures", "tablet12"]),
            # The list of the unit-test programs: the unit tests and the build, no game group, not full (no Android build).
            (("cmake/unit_tests.cmake",), ["repo", "build", "unit", "github-first"]),
            (("tests/mods/anim/test.json",), ["repo"]),
            (("tools/test_driver/luma_test/luma_test.c",), ["repo", "tools", "build", "unit", "start", "engine3", "tour3", "pictures", "android", "tablet12", "github"]),
            (("tools/lumacore/luma_test.py",), ["repo", "tools", "engine3", "tour3", "pictures", "tablet12"]),
            # The tablet's own runner: the Android build and the tablet's part, no PC tour and no GitHub part (it is no game code).
            (("tools/lumacore/luma_test_android.py",), ["repo", "tools", "android", "tablet12"]),
        ]
        for paths, want in cases:
            p = plan(*paths)
            self.assertFalse(p.full, paths)
            # The speed measure's groups are on no row: a plan with an engine or a tour group has measure_pc, and measure_tablet with the
            # Android build too (scope.measure_groups; gate_line_test.py has the cases by name).
            want = list(want) + scope.measure_groups(want)
            self.assertEqual(p.groups, [g for g in scope.GROUP_NAMES if g in want], paths)

    def test_a_change_that_can_move_a_picture_takes_the_set(self):
        """The rule of brief AF: a path that can move a picture takes the group `pictures` (and the tablet's 12 stops with it); a
        document, plan data or a tool that is not part of the game takes none; a path no row names is full, which takes them too."""
        moves = [("native/petari/src/Game/Foo.cpp",), ("runtime/src/audio/a.cpp",), ("runtime/src/render/a.cpp",),
                 ("runtime/include/galaxy/render/a.h",), ("platform/sdl/a.cpp",), ("crates/nebula-import/src/a.rs",),
                 ("tools/test_driver/luma_test/a.c",), ("tools/lumacore/luma_test.py",), ("tools/gate/gate_tour.py",),
                 ("tools/verify/step_hash.py",), ("tools/verify/tours/tour_enterable.txt",), ("tests/engine/a/test.json",),
                 ("tests/replays/a.tsv",), ("PICTURES-CHANGE.txt",), ("somewhere/no/row/names.bin",)]
        for paths in moves:
            p = plan(*paths)
            self.assertTrue(p.takes_pictures, paths)
            if not p.full:
                for name in ("pictures", "engine3", "tour3", "tablet12"):
                    self.assertIn(name, p.groups, paths)
        stays = [("docs/plan/04-GATE-AND-MAIN.md",), ("README.md",), ("docs/plan/ledger.tsv",), ("tools/gate/gate.py",),
                 ("tools/gate/pictures.py",), ("tools/gate/scope.tsv",), ("tools/plan/plan.py",), ("tests/pictures/approved.tsv",),
                 ("tests/mods/a/test.json",), ("tests/measure/a.json",), ("tests/pace/a.json",), ("tests/unit/a.cpp",),
                 ("cmake/unit_tests.cmake",), (".github/workflows/check.yml",), ("platform/android/app/a.kt",)]
        for paths in stays:
            p = plan(*paths)
            self.assertFalse(p.takes_pictures, paths)
            self.assertNotIn("pictures", p.groups, paths)
            self.assertFalse(any(g in p.groups for g in ("engine3", "tour3")), "no PC picture is taken: %s" % (paths,))

    def test_the_group_brings_the_tablet_and_the_main_levels_checks(self):
        groups, full = scope.groups_from_text("pictures")
        self.assertFalse(full)
        self.assertEqual(groups, [g for g in scope.GROUP_NAMES if g in ("engine3", "tour3", "pictures", "tablet12")])
        self.assertTrue(scope.Plan(groups, False, [], {}).takes_pictures)
        self.assertFalse(scope.Plan(["repo"], False, [], {}).takes_pictures)
        self.assertTrue(scope.Plan([], True, [], {}).takes_pictures, "a full run takes every picture the main level does")
        added = scope.with_group(plan("docs/a.md"), "pictures")
        self.assertIn("tablet12", added.groups)

    def test_github_group(self):
        # The group `github` switches GitHub's part of a landing on (package T8a); it stands for no local check.
        self.assertIn("github", scope.GROUP_NAMES)
        self.assertEqual(scope.check_patterns(["github"]), [], "no local check")
        for paths in (("docs/a.md",), ("README.md",), ("tools/gate/gate.py", "tools/plan/plan.py"), ("tests/pictures/approved.tsv",),
                      ("tests/engine/boot/test.json",), ("tests/replays/stage.txt",), ("tests/mods/a/test.json",),
                      ("tests/measure/a.json",), ("tests/pace/a.json",), ("tools/lumacore/luma_test.py",), ("PICTURES-CHANGE.txt",)):
            self.assertNotIn("github", plan(*paths).groups, paths)
        for paths in (("tools/test_driver/a.c",), ("native/a.cpp",), ("runtime/src/render/a.cpp",), ("runtime/include/galaxy/render/a.h",),
                      ("runtime/src/audio/a.cpp",), ("platform/sdl/a.cpp",), ("crates/nebula-import/a.rs",), ("platform/android/a.kt",),
                      ("runtime/src/render/vk/a.cpp",), ("tests/unit/a.cpp",), ("tests/android/a.json",)):
            self.assertIn("github", plan(*paths).groups, paths)
            self.assertNotIn("github-first", plan(*paths).groups, "these wait for nothing: %s" % (paths,))
        for paths in ((".github/workflows/check.yml",), (".github/actions/compiler-cache/action.yml",)):
            self.assertIn("github-first", plan(*paths).groups, paths)
        self.assertEqual(plan(".github/workflows/check.yml").groups, ["repo", "github-first"])
        full = plan("somewhere/no/row/names.bin")
        self.assertTrue(full.full)
        groups, is_full = scope.groups_from_text(full.text())
        self.assertTrue(is_full)
        self.assertIn("github", groups, "the full run has it")
        self.assertEqual(scope.groups_from_text("repo,github"), (["repo", "github"], False))
        self.assertEqual(scope.groups_from_text("repo"), (["repo"], False))

    def test_android_group_is_the_build_only(self):
        # The Android app is compiled from the folders platform/android/build_apk.ps1 and the CMake of platform/sdl name; each of
        # them has the group `android` (the build), and none of them brings the tablet's part with it. GitHub's two Android
        # builds are nightly, so this machine proves the build.
        script = (pathlib.Path(scope.__file__).resolve().parents[2] / "platform" / "android" / "build_apk.ps1").read_text(encoding="utf-8")
        cmake = (pathlib.Path(scope.__file__).resolve().parents[2] / "platform" / "sdl" / "CMakeLists.txt").read_text(encoding="utf-8")
        for folder in ("$root/native", "$root/platform/sdl", "crates/nebula-import"):
            self.assertIn(folder, script, "build_apk.ps1 builds from " + folder)
        self.assertIn('"${nebula_root}/runtime/src/', cmake, "platform/sdl's CMake compiles runtime/src")
        self.assertIn('"${nebula_root}/runtime/include"', cmake, "and takes runtime/include")
        self.assertIn("$driver/luma_test/luma_test.c", script, "the test build compiles the test driver")
        for paths in (("native/a.cpp",), ("platform/sdl/a.cpp",), ("runtime/src/audio/a.cpp",), ("runtime/include/galaxy/a.h",),
                      ("runtime/src/render/a.cpp",), ("runtime/include/galaxy/render/a.h",), ("tools/test_driver/luma_test/a.c",),
                      ("crates/nebula-import/src/a.rs",)):
            groups = plan(*paths).groups
            self.assertIn("android", groups, paths)
            # Every one of them can move a picture, so it takes the tablet's 12 stops with the pictures (the group `pictures`).
            self.assertIn("tablet12", groups, paths)
            self.assertIn("pictures", groups, paths)
        for paths in (("docs/a.md",), ("tools/gate/gate.py",), ("tests/unit/a.cpp",), ("tests/engine/a/test.json",),
                      ("tools/lumacore/luma_test.py",)):
            self.assertNotIn("android", plan(*paths).groups, paths)
        # What a plan with the build and no tablet runs: the build, not the tablet's part.
        self.assertEqual(scope.check_patterns(["android"]), ["build/android"])

    def test_union_of_rows(self):
        paths = ["docs/a.md", "crates/nebula-import/src/x.rs", "runtime/src/audio/a.cpp", "platform/android/app/a.kt"]
        a = plan(*paths)
        self.assertEqual(a.groups, [g for g in scope.GROUP_NAMES if g in (
            "repo", "build", "unit", "start", "engine3", "tour3", "pictures", "import", "android", "tablet12", "github", "measure_pc",
            "measure_tablet")])
        self.assertEqual(plan(*reversed(paths)).groups, a.groups)
        self.assertEqual(plan("docs/a.md", "native/b.cpp").groups, plan("native/b.cpp").groups,
                         "a document beside code adds nothing of its own")

    def test_outside_every_row_is_full(self):
        self.assertTrue(plan("installer/build.ps1").full)
        self.assertTrue(plan("native/a.cpp", "installer/b.ps1").full)
        self.assertEqual(plan("native/a.cpp", "installer/b.ps1").unmatched, ["installer/b.ps1"])
        self.assertTrue(plan().full, "no changed file: nothing says what changed")
        self.assertTrue(plan("CMakeLists.txt").full)
        self.assertTrue(plan("tests/unknown-suite/a.json").full, "a test of a suite no row names")
        self.assertFalse(plan("docs/a.md").full)

    def test_a_row_may_name_full(self):
        table = scope.parse("mods = off\npattern\tgroups\ndocs/**\trepo\n.github/**\tfull\n")
        self.assertTrue(scope.plan_for([".github/workflows/check.yml"], table).full)
        self.assertFalse(scope.plan_for(["docs/a.md"], table).full)

    def test_repo_group_is_in_every_run(self):
        """Whatever the table says, the cheap repository checks run: a row that names other groups, a row that names none
        of repo, a file no row names (full), and a table whose rows never name repo at all."""
        self.assertIn("repo", plan("platform/android/app/build.gradle").groups)
        self.assertIn("repo", plan("platform/android/app/build.gradle", "tests/android/walk/test.json").groups)
        table = scope.parse("mods = off\npattern\tgroups\nplatform/android/**\tandroid,tablet12\ndocs/**\ttools\n")
        for path in ("platform/android/a.kt", "docs/a.md", "unknown/a.txt"):
            self.assertIn("repo", scope.plan_for([path], table).groups, path)
        self.assertEqual(scope.plan_for(["platform/android/a.kt"], table).groups, ["repo", "android", "tablet12"])
        self.assertEqual(scope.plan_for(["docs/a.md"], table).groups, ["repo", "tools"])
        for path in ("platform/android/x.kt", "tests/pictures/x.png", "tests/mods/a/test.json"):    # the tree's own table
            self.assertIn("repo", scope.plan_for([path], TABLE).groups, path)
        checks = scope.select({"repo/audit": [], "build/android": []}, scope.plan_for(["platform/android/a.kt"], TABLE).groups)
        self.assertIn("repo/audit", checks, "the check the group stands for is selected")
        self.assertIn("repo", scope.groups_from_text(scope.plan_for(["platform/android/a.kt"], TABLE).text())[0])

    def test_paths_with_backslashes_match(self):
        self.assertEqual(plan("docs\\plan\\a.md").groups, ["repo"])

    def test_the_site_is_repo_only(self):
        """A change that holds only files of website/ (a post's pictures, a page's json) runs the repository checks and is not full;
        beside a game file it adds nothing to what the game file brings."""
        for paths in (("website/content/blog/x/a.jpg", "website/content/pages/home.json"), ("website/content/blog/x/index.md",),
                      ("website/index.html", "website/scripts/render-blog.mjs", "website/package-lock.json")):
            p = plan(*paths)
            self.assertFalse(p.full, paths)
            self.assertEqual(p.groups, ["repo"], paths)
            self.assertEqual(scope.check_patterns(p.groups), ["repo/*"], paths)
        game = plan("runtime/src/audio/mixer.cpp")
        both = plan("website/content/pages/home.json", "runtime/src/audio/mixer.cpp")
        self.assertEqual(both.groups, game.groups)
        self.assertFalse(both.full)
        self.assertTrue(plan("website/a.jpg", "installer/b.ps1").full, "a file of the site does not excuse one no row names")
        self.assertTrue(scope.path_matches("website/content/blog/x/a.jpg", "website/**"))
        self.assertFalse(scope.path_matches("websites/a.jpg", "website/**"))

    def test_patterns(self):
        self.assertTrue(scope.path_matches("docs/a/b/c.md", "docs/**"))
        self.assertTrue(scope.path_matches("a/b/c.md", "*.md"), "a pattern with no folder names a file anywhere")
        self.assertFalse(scope.path_matches("a/b/c.mdx", "*.md"))
        self.assertFalse(scope.path_matches("docsx/a.md", "docs/**"))
        self.assertFalse(scope.path_matches("runtime/src/render/a.cpp", "runtime/*"), "one star stays in one folder")


class Mods(unittest.TestCase):
    def needs(self, level, *extra):
        known, planned, _ = gate.read_lists()
        args = gate.parser().parse_args(["--main", "--tablet", "T", *extra])
        args.mode = level
        return set(gate.run_checks(args, known, planned, dry=True).needs)

    def is_mod(self, name):
        return name.startswith("mods") or name == "lumalink"

    def test_mods_off_keeps_the_mod_suites_out_of_full_and_the_nightly(self):
        self.assertEqual(TABLE.mods, "off", "the tree's table says off now")
        _, planned, _ = gate.read_lists()
        for level in ("quick", "main", "long", "final"):
            got = {n for n in self.needs(level) if self.is_mod(n) and n not in planned}
            self.assertEqual(got, set(), level)
        # `full` is the main level on the native game: it holds no mod check.
        self.assertNotIn("lumalink", self.needs("main"))
        self.assertNotIn("mods-asan/d3d12", self.needs("long"))

    def test_the_switch_brings_them_back(self):
        main = self.needs("main", "--mods", "short")
        self.assertTrue({"lumalink", "mods/d3d12", "mods/vulkan", "mods/d3d11"} <= main)
        long_run = self.needs("long", "--mods", "full")
        self.assertIn("mods-asan/d3d12", long_run)
        self.assertNotIn("mods/d3d12", long_run, "the long run's tours carry the mod tests on the sanitizer build")

    def test_the_table_line_is_the_default(self):
        old = gate.table_mods
        try:
            gate.table_mods = lambda: "full"
            self.assertIn("lumalink", self.needs("main"))
        finally:
            gate.table_mods = old

    def test_a_landing_never_carries_them_by_default(self):
        self.assertFalse(any(self.is_mod(n) for n in self.needs(gate.LAND_LEVEL)))


class Groups(unittest.TestCase):
    def setUp(self):
        known, planned, _ = gate.read_lists()
        args = gate.parser().parse_args(["--main", "--tablet", "T"])
        args.mode = "main"      # the main level registers every name a landing's level does, and the other discs' tours
        self.dry = gate.run_checks(args, known, planned, dry=True)
        self.needs = self.dry.needs

    def test_groups_name_real_checks(self):
        import fnmatch
        for g in scope.GROUP_NAMES:
            for pattern in scope.check_patterns([g]):
                self.assertTrue(any(fnmatch.fnmatchcase(n, pattern) for n in self.needs),
                                "group %s: no check of a landing is named %s" % (g, pattern))

    def test_selection_follows_needs(self):
        chosen = scope.select(self.needs, ["tour1"])
        self.assertEqual(chosen, {"tour/d3d12", "pictures/tour/d3d12", "replays/d3d12", "pictures/declared", "build/windows",
                                  "data/RMGE01"})
        self.assertNotIn("tour/vulkan", chosen)
        self.assertNotIn("pictures/engine/d3d12", chosen, "the declared-pictures check brings its own name, not the pictures'")
        three = scope.select(self.needs, ["tour3"])
        self.assertTrue({"tour/vulkan", "tour/d3d11", "replays/vulkan", "replays/d3d11", "pictures/tour/vulkan"} <= three)
        self.assertNotIn("engine/vulkan", three)
        tablet = scope.select(self.needs, ["tablet12"])
        self.assertTrue({"tablet/prepare", "build/android", "tour/tablet", "tablet/remove", "tour/tablet-pc", "build/windows",
                         "data/RMGE01"} <= tablet, "the tablet's part needs the data too")

    def test_the_set_is_every_picture_check_of_the_main_level(self):
        """The group `pictures` selects the main level's picture checks and what they need, and takes a sibling play (a stop a
        declaration names, on another disc's Direct3D 11 or Vulkan) by its name; it leaves the other checks of a landing out."""
        chosen = scope.select(self.needs, scope.groups_from_text("pictures")[0])
        must = {"engine/d3d12", "engine/vulkan", "engine/d3d11", "pictures/engine/vulkan", "tour/d3d12", "tour/vulkan", "tour/d3d11",
                "pictures/tour/d3d11", "tour-RMGP01/d3d12", "tour-RMGJ01/d3d12", "pictures/tour-RMGP01/d3d12",
                "pictures/tour-RMGJ01/d3d12", "replays/d3d12", "replays/vulkan", "replays/d3d11", "tour/tablet", "pictures/tablet",
                "tablet/prepare", "build/android", "pictures/declared", "data/RMGP01", "data/RMGJ01", "build/windows"}
        self.assertTrue(must <= chosen, sorted(must - chosen))
        for left_out in ("validate/d3d12", "unit/windows", "start/vulkan", "rust", "import/collision/RMGE01", "repo/audit"):
            self.assertNotIn(left_out, chosen)
        needs = dict(self.needs)
        needs["tour-RMGP01/vulkan"] = {"build/windows", "data/RMGE01", "data/RMGP01"}
        needs["pictures/tour-RMGP01/vulkan"] = {"tour-RMGP01/vulkan"}
        needs["pictures/declared"] = set(needs["pictures/declared"]) | {"pictures/tour-RMGP01/vulkan"}
        again = scope.select(needs, scope.groups_from_text("pictures")[0])
        self.assertTrue({"tour-RMGP01/vulkan", "pictures/tour-RMGP01/vulkan"} <= again)
        self.assertNotIn("tour-RMGP01/vulkan", scope.select(needs, scope.groups_from_text("tour3")[0]), "the other groups do not")

    def test_validate_is_in_every_renderer_landing_and_nowhere_else(self):
        """T12: the debug layers' lane (validate/<renderer>, the 12-stop tour with the layer on) belongs to the two renderer rows."""
        chosen = scope.select(self.needs, ["validate"])
        self.assertEqual(chosen, {"validate/d3d12", "validate/vulkan", "validate/d3d11", "build/windows", "data/RMGE01"})
        for path in ("runtime/src/render/renderer_d3d12.cpp", "runtime/src/render/vk/vk_context.cpp",
                     "runtime/include/galaxy/render/renderer.h"):
            self.assertIn("validate", plan(path).groups, path)
        for path in ("native/petari/src/Game/Foo.cpp", "runtime/src/audio/mixer.cpp", "tools/gate/gate.py", "tests/engine/boot/test.json"):
            self.assertNotIn("validate", plan(path).groups, path)

    def test_the_tablets_runner_runs_the_tablets_part_with_what_it_needs(self):
        groups = plan("tools/lumacore/luma_test_android.py").groups
        self.assertEqual(groups, ["repo", "tools", "android", "tablet12"])
        chosen = scope.select(self.needs, groups)
        self.assertTrue({"build/android", "tablet/prepare", "tour/tablet", "tablet/remove", "tour/tablet-pc", "build/windows",
                         "data/RMGE01"} <= chosen, "the tablet's part with the builds and data it needs: %s" % sorted(chosen))
        self.assertNotIn("tour/d3d12", chosen, "no PC tour of its own")
        self.assertNotIn("engine/d3d12", chosen)

    def test_documents_only_selects_repo_and_no_build(self):
        chosen = scope.select(self.needs, plan("docs/a.md").groups)
        self.assertEqual(chosen, {"repo/settings", "repo/audit", "repo/scripts", "repo/code", "repo/strictness"})
        self.assertFalse(any(n.startswith(("build", "unit", "start", "tour", "engine", "data")) for n in chosen))

    def test_a_run_of_documents_runs_the_repo_checks_and_starts_no_build(self):
        ran = []
        saved = {k: getattr(gate, k) for k in ("settings_table", "repo_audit", "parse_scripts", "repo_code", "repo_strictness", "build_windows",
                                              "unit_tests", "game_tests", "start_check", "harness")}

        def fake(name):
            def make(*a, **k):
                def fn(log):
                    ran.append(name)
                    return True, "fake"
                return fn
            return make

        def forbidden(name):
            def make(*a, **k):
                def fn(log):
                    raise AssertionError("%s was started" % name)
                return fn
            return make
        try:
            for k in ("settings_table", "repo_audit", "parse_scripts", "repo_code", "repo_strictness"):
                setattr(gate, k, fake(k))
            for k in ("build_windows", "unit_tests", "game_tests", "start_check", "harness"):
                setattr(gate, k, forbidden(k))
            with tempfile.TemporaryDirectory() as t:
                known, planned, _ = gate.read_lists()
                args = gate.parser().parse_args(["--main", "--tablet", "T", "--work", t, "--build-root", t, "--status", "",
                                                 "--board", ""])
                args.mode = gate.LAND_LEVEL
                selected = scope.select(self.needs, plan("docs/a.md").groups)
                with contextlib.redirect_stdout(io.StringIO()):
                    g = gate.run_checks(args, known, planned, selected)
            self.assertEqual(sorted(ran), ["parse_scripts", "repo_audit", "repo_code", "repo_strictness", "settings_table"])
            self.assertEqual(sorted(r["check"] for r in g.results), ["repo/audit", "repo/code", "repo/scripts", "repo/settings", "repo/strictness"])
            self.assertEqual(g.verdict, "pass")
        finally:
            for k, v in saved.items():
                setattr(gate, k, v)


class TableErrors(unittest.TestCase):
    def bad(self, text, word):
        with self.assertRaises(scope.ScopeError) as caught:
            scope.parse(text)
        self.assertIn(word, str(caught.exception))

    def test_errors(self):
        self.bad("mods = off\npattern\tgroups\ndocs/**\trepo,nonsense\n", "nonsense")
        self.bad("mods = sometimes\npattern\tgroups\ndocs/**\trepo\n", "mods is one of")
        self.bad("mods = off\ndocs/**\trepo\n", "header")
        self.bad("pattern\tgroups\ndocs/**\trepo\n", "no `mods = off` line")
        self.bad("mods = off\npattern\tgroups\ndocs/**\n", "a pattern and its groups")
        self.bad("mods = off\npattern\tgroups\n", "no row")

    def test_the_trees_own_table_reads(self):
        self.assertTrue(len(TABLE.rows) > 10)
        self.assertEqual(scope.groups_from_text("repo,tools"), (["repo", "tools"], False))
        self.assertEqual(scope.groups_from_text("full")[1], True)
        with self.assertRaises(scope.ScopeError):
            scope.groups_from_text("repo,what")


class FromGit(unittest.TestCase):
    def test_changed_files_from_git(self):
        with tempfile.TemporaryDirectory() as t:
            def sh(*a):
                r = subprocess.run([gate.GIT, "-C", t, *a], capture_output=True, text=True)
                self.assertEqual(r.returncode, 0, r.stderr)
                return r.stdout.strip()
            sh("init", "-q", "-b", "main")
            for k, v in (("user.name", "keepvaibin"), ("user.email", "122556909+keepvaibin@users.noreply.github.com"),
                         ("commit.gpgsign", "false")):
                sh("config", k, v)
            (pathlib.Path(t) / "a.txt").write_text("a", encoding="utf-8")
            sh("add", "-A")
            sh("commit", "-q", "-m", "one")
            sh("checkout", "-q", "-b", "work")
            (pathlib.Path(t) / "docs").mkdir()
            (pathlib.Path(t) / "docs" / "x.md").write_text("x", encoding="utf-8")
            sh("add", "-A")
            sh("commit", "-q", "-m", "two")
            sh("checkout", "-q", "main")
            (pathlib.Path(t) / "native").mkdir()
            (pathlib.Path(t) / "native" / "main-only.cpp").write_text("x", encoding="utf-8")
            sh("add", "-A")
            sh("commit", "-q", "-m", "main moves on")
            sh("checkout", "-q", "work")
            paths, why = scope.changed_paths(gate.GIT, t, "main")
            self.assertEqual((paths, why), (["docs/x.md"], None), "from the merge base: main's own new file is not the branch's")
            self.assertEqual(scope.plan_for(paths, TABLE).groups, ["repo"])
            paths, why = scope.changed_paths(gate.GIT, t, "nothere")
            self.assertEqual(paths, [])
            self.assertIn("no merge base", why)


def main():
    result = unittest.main(argv=[sys.argv[0]], exit=False, verbosity=1).result
    print("scope_test: %s (%d tests)" % ("pass" if result.wasSuccessful() else "FAIL", result.testsRun))
    return 0 if result.wasSuccessful() else 1


if __name__ == "__main__":
    sys.exit(main())
