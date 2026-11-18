# Agent 2 hand-over (2026-11-18, stop order 08:25)

Agent 1 takes over the mod system: LumaCore, LumaKit, the LumaScript compiler, LumaLink, the importer and checker, the
Mods screens, docs/modding, the reference mods, luma_test.py, mod_tests.py and tools/test_driver.

## Branches

| Branch | Tip | Not on main | State |
|---|---|---|---|
| mods/n3008 | this commit (after d3647fc8c) | about 133 commits | The working branch. It holds mods/m2 and mods/m2-w whole. It never went through the main gate. |
| mods/work | 0619c0abd | 28 commits, not in mods/n3008 | Older. Compare with n3008 before using it; most of its work was redone on m2. |
| mods/m2, mods/m2-w | 2d5e90fed, dc34c29b9 | all of it is in mods/n3008 | Can be deleted (origin's mods/m2-w is behind the local branch, but n3008 has all of it). |
| mods/m2-rb | 875870e60 | nothing (wholly on main) | Local only. Can be deleted. |
| wip/agent-2 | 5b6806981 | one commit | An untracked early draft of same_mod_runs.py from Nebula-A2-m2. The finished tool is 6332a038c. Throw it away. |

To land mods/n3008: wait for its GitHub run (pushed with this commit), then run the main gate on it. My last full
gate command was `D:/NebulaWork/agent2/gate-full.cmd` (gate.py --full with LumaLink and --seed-from).

## Done today on mods/n3008 (each with a test that fails before; the proofs are in the ledger and PROGRESS-AGENT-2.md)

- M6 part 1: M-145, M-146, M-151, M-156, M-157, M-159, M-160, M-166, M-167, M-169; M-168 in part (NSND left).
- M-144, M-150, M-161 (7ca329d3a): a check that does not finish is never a pass. N-2070 and N-2071 were found and
  fixed. Harness (5d034864e): `unreadable`, `cut_files`, and bare tests that import.
- N-4090 (4b092f808): L206 says the whole id rule. Agent 4 filed the row in their own ledger (link/work, maybe not
  pushed); close it as fixed with this commit.
- N-4094 (d3647fc8c): a library whose code ran stays loaded until the game's end, each build is loaded from its own
  copy on every system, and LumaCore gains `keep_text`. Tests reload/1144, 1145, 1146 pass on a2x4 (before: 1144
  crashed after the take-out). Reload and loader suites: 33 of 33. **Not run: classes, objects, guard** (stopped).
  - Owed: lumart (LumaLink repository) must call `core->keep_text` in lumart_text_keep, and lumart_end must stop
    freeing the store. After that, compiled-form.md lines 384-392 ("freed with the library") must say the store is
    the game's.
  - Owed: Linux, macOS and Android build mod_plugins.cpp's new copy path (getpid, kill, errno) for the first time in
    CI.
  - The N-4094 row is in Agent 4's ledger; close it there with d3647fc8c and the three tests.

## Open, in the planned order (02-AGENT-2-MODS.md)

1. **M6, the rest** (see the M6 section of 02-AGENT-2-MODS.md and A-findings-mods.md in NEBULA-AUDIT-2026-11-17):
   - The budget object (plan item 1): nothing coded. The cook passes `Stats` through cook_entry and cook_file
     (cook/mod.rs:231), but the format readers below them take no context. About 11 whole-file reads of a mod's files
     (overlay.rs 535, 653, 675, 816, 958, 1006, 1143; overlay_patches.rs 252, 357; beside.rs 88;
     placement_args.rs 176) should go through one reader that allocates fallibly and asks the budget.
   - M-143: mod_import.cpp's unpack (around line 370) has no free-space floor; a ratio limit would refuse legitimate
     blank textures, so the floor is the better guard. M-158: ARCHIVE_DEPTH_MAX 8 (cook/mod.rs:227), with the work
     not bounded across levels. Both: nothing coded.
   - M-152, M-162: shaders compiled outside the game's process with a time limit (D3D12 does it already; D3D11 and
     Vulkan compile in the process); a check that each shader ends; post programs registered at run time take the
     cook's check. Nothing coded.
   - M-168 NSND, N-2069 (a stream decoded whole), N-2044 (corpus in the gate), N-2046 (fuzz targets in the gate).
2. M3, M4, M10, M7, M8, M9, M12, M14, M11, M13; also M-031, N-2068, the M-114 follow-up (its note in the ledger).
3. N-4095 (Agent 4, BOARD 07:02): define_class refuses overriding a virtual of a base that is not first in the class;
   15 records are affected. Nothing coded. The test is described on BOARD.
4. The default `--parallel` for mod_tests.py (the highest count at which pictures and facts equal a run one at a
   time): not measured.
5. BOARD: gate.py needs a repository check running `validator_rules.py --check` (never asked).

## How to prove things

- Bins: `D:/NebulaWork/agent2/build-x4.cmd` (game "all", host, then bins-x4.sh into build/luma-bins/a2x4; 4 jobs).
  After a change to luma.h the whole game is generated again (about 7 minutes).
- Game tests: `D:/NebulaWork/agent2/run-some-x.cmd <bins> <work> d3d12 1 <log> <suite or suite/test>...`
  (mod_tests.py with LumaLink from build/a2-cargo and LUMALINK_TREE lumalink-work/lumalink-a2w).
- Mods unit tests: `D:/NebulaWork/agent2/build-x-d11.cmd`, then build/a2x-d11/galaxy_mods_tests.exe ("mods tests
  passed").
- The validator: CARGO_TARGET_DIR=D:/NebulaWork/build/a2x-cargo `cargo test --offline --locked -j 4 -p luma-validate`.
- Before-fix bins: build/luma-bins/a2w12 (before M-144) and a2x3 (before N-4094).

## Traps written nowhere else

- mod_tests.py builds the test mods against the tree's native/include/luma.h, and bins-x*.sh builds luma-test and
  nebula-import from the tree: do not edit the tree while either runs.
- Hiding nebula_import.dll turns every mod off, the game half (luma-test) too: such a test must be `"bare": true`.
- `LumaFunction.invoke`: a pointer parameter's args[n] points at the pointer (`&name`, not `name`).
- NameObj::getName has no function row (it is inline): read the field `NameObj.mName` instead.
- On Linux, macOS and Android, dlopen of a path already loaded returns the old library: a reload needs a new file.
- A restore with shutil.copy2 or `cp -p` keeps the old file time, so ninja does not rebuild: touch the files.
- git add of capabilities.json may carry another change's row: stage it with a filtered patch (`git apply --cached`).
- Use `C:/Program Files/Git/cmd/git.exe`; the msys git sees thousands of files as changed (line endings).

## Folders on D:

- Keep: MarioGalaxyRecomp/Nebula-A2-x (mods/n3008); build/a2x-game, a2x-host, a2x-d11, a2x-cargo (incremental
  builds); build/luma-bins/a2x4 (current bins); build/a2-cargo and lumalink-work/lumalink-a2w (the LumaLink the runs
  use); agent2/ (the scripts above).
- Keep until mods/work is decided: MarioGalaxyRecomp/Nebula-A2.
- Can be deleted: MarioGalaxyRecomp/Nebula-A2-m2, -side, -w (their work is in n3008 or on main);
  build/a2-m144-*, build/a2-n4094-*, build/luma-bins/a2x3 and a2w12 (before-fix bins, once no proof needs them).

## Conformance suites and last results

- The suites are tests/mods/<suite> in this tree (suite.json each), run by tools/lumacore/mod_tests.py. The older
  lanes under D:/NebulaWork/agents/luma are no longer the source.
- Today, d3d12, on a2x3 and a2x4: startval and import 15 of 15; reload and loader 33 of 33. Logs:
  build/a2-m144-after.log, a2-m144-after2.log, a2-n4094-after.log. No full run of every suite today.
