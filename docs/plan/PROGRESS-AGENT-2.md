# Progress: Agent 2 (mods: LumaCore, LumaKit, LumaScript, LumaLink)

One dated entry per package step. Newest last. Ledger rows are in ledger.tsv (agent 2); new rows are N-2xxx.

## 2026-11-17 Baseline: the mod tests and LumaLink

- The mod conformance suites lived outside both repositories (D:/NebulaWork/agents/luma lanes). They are now in the
  game tree as tests/mods (c0fc5b303), and LumaKit's and LumaScript's game tests in LumaLink as tests/game (13eb3e2).
- tools/lumacore/mod_tests.py runs both: every mod with source is built with `lumalink build`, every suite run on the
  renderer asked. Without LUMALINK the pinned LumaLink (tools/lumacore/lumalink.commit) is fetched and built
  (9773373b3). The gate's mods rows run it per renderer.
- Stale tests fixed, each failing before: the functions fixture's planted marks (N-2007), harness 19, 10 and 06a
  (N-2008), kd-lib's kit schema lost in the import (N-2009), suite order 2245/2246 (N-2010), modlog 3127 and 3129
  (N-2011), LumaKit kit-camera (N-2014).
- LumaLink: a mod of C alone builds (N-2001, 7433bbd, 46e4b30; docs bdac49f54); an author's own C may have frames
  over 4 KiB (N-2005, a1a3e01); a fact's text name ends before its value (N-2013, f83c070).

## 2026-11-17 M0 The mod tests in the gate

- The gate's mods rows ran no test ("0 of 0"): the gate runs Python with -I, so mod_tests.py could not import its
  neighbours (af6be4c9e), and it had no --commit, which the gate passes (fe204003b). N-2021.
- Full gate on fe204003b: mods on D3D12 477 of 480, Vulkan 475 of 478. Not passing:
  - lumalink/- not-run: the gate gives LUMALINK, not LUMALINK_TREE. Fixed on mods/m2 (the checkout the program was
    built in is used).
  - models/3202-model-missing and refs/3132-missing-class: the placeholder model draws with no pipeline (N-2022). The
    program set is Agent 1's; fixed on engine/next e3a2026ed, closes when main has it.
- D3D11 references of safestart and aftercrash hold the Mods list's blurred key-label glyphs (N-2016, Agent 1 on it).

## 2026-11-17 M5 Faults and the start after a crash

- M-027 fixed (3dc47e80f): the guard's word scan read past the mod's frames. ctest galaxy_luma_guard_words_tests
  fails on the old range; tests/mods guard 1619b to 1621b.
- M-031, M-032 (b32f454fd, abc38ec4e): a crash names the mod; the next start turns that mod off and the Mods page
  asks Keep it off / Turn it on again / Remove. tests/mods/aftercrash 6701 to 6713 (start, play and stop crashes);
  galaxy_mods_tests crash_record_text, crash_record_files. M-031 stays open: macOS has no fatal handler (platform,
  Agent 1, board 08:44), and the Android run of aftercrash is not done yet.
- M-100 fixed (860bae7a4, aeb549faa): the safe-start notice covers both lists; tests/mods/safestart 6601 to 6605 with
  pictures on three renderers, and in Japanese and German on their discs (a757b105c).
- N-2019 fixed (ecdf110eb, data loss): the mods' file requests queued at quit were dropped (the fs worker was
  detached). The host now finishes them; tests/mods/fs 2313 and 2314 fail on the old host.
- N-2012 fixed (fb6859aa8): a mod's main camera left the game's watch point behind, so sound, the ocean ring and
  LumaKit's camera.now read a mixed camera. tests/mods/world 2471-view.
- LumaLink M-035, M-153, M-163, M-164 fixed (6039139, 50e28b7, 183ce97), each with a unit test that fails before.
- Open from M5: N-2020 (a mod gets no call when the game quits).

## 2026-11-17 M2 Call safety (first part)

- M-014, M-113 fixed (959dc09b1): the build finds what each of the 27,116 game functions needs (scene objects
  reached through MR::getSceneObj< T >, to a fixed point over the callers); 11,338 need something. A mod's call
  whose needs are missing is not made: it gives the empty value and the mod log says Call.NotNow in 14 languages.
  needs_fixture_test.py (11 planted functions, 4 planted faults); tests/mods/needs 6801 (crashed before).
- A refused call that returns a reference gives a zeroed object, never null (50d2f1714): LumaKit's camera.now read
  through the null. LumaLink kit-boot and kit-boot-menus call LumaKit at the first boot frames.
- M-173 fixed: lifecycle.md says what a call in the boot frames gives.
- M-001 fixed (5fd2e8768, 553600e16): a fault in a library mod's code (LumaKit) ended the game. It is now resumed at
  the calling mod's boundary; the caller is turned off, the library and its other users go on. tests/mods/guard 1643.
  Decided: the caller is charged, not the library (board).
- M-015, M-111 fixed (eff71d620, 54d490ab5): a new events service gives the game's 28 events to mod code at the
  next frame's start, where it may do anything. The events' return-value promises are gone (nebula_mod_api.h 5);
  events.md names the function that raises each, for a mod that must act at the moment. tests/mods/events 6901,
  6902; LumaLink s-events; tools/gate/tests/events_page_test.py.
- Open in M2: N-2023 (LumaScript proves needs at build time), N-2024 (safe handlers), M-038, M-045, M-049, M-076,
  M-077 (LumaKit at any time).

## 2026-11-17 M2 Call safety (second part)

- M-001 is open again: the planner's decision of 12:33 (board) changes who is turned off (by need along the chain of
  mods between the boundary and the fault), adds a boundary on a library's calls up into a mod's code, a notice to the
  mods left with cut code, file+offset names, and a growing library table. 5fd2e8768 stays as the resume point.
- M-076, M-077 fixed (0c05e409d): save.game's export and offer asked in the boot wait for the game's read of the
  files. tests/mods/save 2272 (the game ended before).
- N-2025, N-2031 fixed (7162d1b98, be9ab36cc): mod_tests.py ran tests the copy did not hold, then (my N-2025 fix)
  a reused copy ran only an earlier --only's tests. harness_mod_tests_copy_test.py, both cases fail before.
- M-045 fixed (LumaLink 08bb7f3): kit.area, kit.gravity and kit.player.form act for player 1 alone. kit-extra.
- A probe of every LumaKit call at every moment (LumaLink kit-any-time, kit-any-time-title: 196 calls at boot, the
  title, a stage ready, play, leaving, each frame of the load, the next stage) found four faults that ended the game
  or the mod's run, each fixed:
  - N-2026 (d3e9454ae): calls that read the save files before the system archive made them (item.give of lives at
    boot). The needs pass now knows the game system's objects: UserFile, CometScheduler; 2,143 functions need one.
  - N-2027 (d3e9454ae): calls that read the player's actor while a scene is made (MarioAccess::incLife during the
    next stage's load): MarioActor, needed by 3,674 functions.
  - N-2028 (LumaLink 1eea14f): a kit.actor kind with no look ended the game at its spawn (draw buffer, actions).
  - N-2029 (LumaLink 5ab668b): camera.side, replay.current_pose and replay.view were script errors with nothing there.
- M-038, M-049 fixed: shop.open and item.give at the boot and on the title, through the needs (the calls are not made
  when their objects are missing); Lives(n) holds to the game's 99 (LumaLink fe9bf25), a large n held the frame.
- N-2030 fixed (51b4f65a4): tasks.md wrote `p.stop`; the language takes `p.stop()`.
- Runs on d3d12: the LumaLink kit suite 31 of 32 (kit-any-time-title expected no player on the title; the title is
  a game scene with the player in it, so the expectation was corrected); after the lives change, kit-any-time,
  kit-any-time-title, kit-item and kit-shop 4 of 4.
- Open in M2: M-001 (as above), N-2023, N-2024, M-031 (Android aftercrash run); the full M2 regression on all three
  renderers.

## 2026-11-17 M1 M-001: a fault in another mod's code

- M-001 fixed, as the planner decided at 12:33 (board). Nebula mods/m2 (rebased onto engine/int f8a8a43b7):
  kernel 0db33cebb, Mods page 10c00dfd1, tests 305c2a62a, docs 53e6733a2, luma.h 7ca155215 and 47cbeee62; LumaLink mods/m2
  19e3a91, 341b31d, 8bb18e5.
  - Who is turned off: the host records the chain of mod code between the boundary and the fault, with each run's
    outermost frame. The kernel starts at the mod whose code faulted and moves to its caller while the caller needs
    it (luma_guard_walk.h); that mod is turned off. A calls LumaKit and LumaKit's code faults: A. LumaKit's hook calls
    B's callback and B's code faults: B (5fd2e8768 took that as LumaKit's own fault: the boundary was LumaKit's).
    The resume point stays the boundary.
  - LumaCore.call_mod: a library's call of code another mod gave it runs under a boundary of the giver; a fault there
    turns the giver off and the caller goes on to its next user in the same frame. LumaScript makes every call of
    another mod's function value through it; a value the mod made itself stays a plain call. In C, luma.h documents it.
  - The cut notice: a mod that stays on with code on the cut stack gets LumaModInfo.cut (LumaScript `on mods.cut`)
    before its next call there, with the export that was cut, the caller and the place. The LumaScript runtime first
    puts back its own calls in progress (the mod its reports go to, its scratch). The kernel holds nothing across a
    cut: tMod and the reporting state are put back by run() at the boundary. LumaKit is pure LumaScript, so its
    callbacks all go through call_mod; reference/lumakit/index.md lists them per module.
  - Naming: the log, the mod log, the crash record and the player's notice name the mod turned off, the mod whose
    code faulted and the place as <id>/<file>+offset. Faults in a mod's code under other mods are counted per mod;
    after two different callers the Mods page says the library is the common place and offers to turn it off or the
    two callers on again (14 languages). Nothing else is automatic.
  - The library span table grows by chunks of 64 and is read by the fault handler without a lock (was 512 slots and a
    silent drop).
- Tests, each failing before: tests/mods/guard 1643-1652 (0 of 10 pass on the bins before M-001, 10 of 10 after);
  galaxy_luma_guard_walk_tests (the need walk, 2000 libraries); LumaLink tests/game script k11-callback, k11-cut (both
  fail with the LumaLink before) and lumascript emit_small a_function_value_entry_is_one_for_every_module.
- Two faults of the first M-001 code, found reading it again, fixed before it went anywhere: "needs through" (every
  call_mod, and the walk in the fault handler) walked the need graph by recursion, exponential in a graph of shared
  needs (67 million steps, 81 ms, for one check on 26 layers of two mods each needing both below); each mod now keeps
  every mod it needs as one sorted set made at its bind (LumaNeeds.hpp), a check is a binary search 918f98174; and a
  library's fault count and callers, written on the faulting thread and read by the Mods page, are published with
  atomics 6aeb306b5. luma_guard_walk_tests: 40 such layers, and random graphs against a graph search.
- LumaKit did not link with the first call_mod build: the function value entry was static in the first module that
  needed it (LumaLink 8bb18e5). Found by the LumaLink kit suite; the crate test above fails before.
- N-2036 fixed 9c0f16984: the guard read a mod library's unwind tables (.pdata unwind info, .eh_frame_hdr) with no bound,
  in the fault handler (function_at is new in M-001, frame_register is from M5). luma_guard_unwind.h reads within the
  image; the unit test feeds every truncation, hostile counts and offsets, a looping chain and 20000 random tables
  (an address sanitizer build of the test overflows on the old reading); tools/fuzz/luma_unwind_fuzz.cpp ran 43
  million inputs, every point reached, no fault. N-2037 (open, M6): the system's own unwinder still reads a mod's
  exception directory unchecked.
- N-2035 fixed a0a3a9f9f: needs 6801 expected MarioHolder where the call has named MarioActor since d3e9454ae.
- Open, fixed limits found here: N-2032 (256 mods), N-2033 (256 cost slots), N-2034 (16 take-outs a frame).
- call_mod's cost (guard/1649) is not measured yet: it is a timed run, and since 11-17 16:05 (plan 04 section 10) it
  waits for the machine alone under measure.lock. It goes into the next entry.
- Runs on d3d12 (6aeb306b5, the US pak of store 35): guard, guard-crash, aftercrash, loader, loader5, needs, reload, gone, modscreen, modlog, safestart, startcheck and the LumaLink script and kit suites, 182 of 182 (437 mods built, none failed; 6606 and 6607 on the Japanese and European paks). LumaKit rebuilt with LumaLink 8bb18e5 (1c858d763); build_lumakit --verify passes.

## 2026-11-17 Plan 04 step 3 and what closing M-001 found

- The quick gate of mods/m2 ef3d179ef failed its Windows build: luma.h's LUMA_MOD, LUMA_MOD_NO_IMPORTS and
  LUMA_LIBRARY_MOD left out LumaModInfo.cut, which M-001 added, so a mod built with -Wall -Wextra -Werror (the
  harness's game half is) did not compile. A fault of the first M-001 code, before it reached main: 47cbeee62, with
  tools/lumacore/tests/harness_sdk_macros_test.py (the five forms as C17 and C++20 on four targets; 16 of 40 before,
  40 of 40 after). My own bins had built the game half without -Wextra, so my regression did not see it.
- N-2038 fixed (LumaLink f468f20, pin 59e178605): mods are built with -ffp-contract=off, as the game is. Before, every
  arm64 target fused a*b+c, so a mod's floats, LumaScript's sin and cos included, differed from the PC's.
  lumalink-core tests/same_float.rs fails before on all five arm64 targets. Agent 1's sincosf case (N-1033) cannot
  happen in a mod (no builtins, no system library, every link refuses an undefined name).
- N-2039 fixed 5023c2cf3: the gate's new lumalink check builds the pinned LumaLink (mod_tests.pinned_lumalink, its nebula/
  a link to the tree under test) and runs every one of its tests with clang, the SDK and the build's names; the mod
  tests then use that program and its tests/game. Before, the gate ran none of them, and the nine-platform build test
  skipped itself for want of its checker (now tools/lumacore/check_luma_platforms.py). The first full run found one
  stale test (33 services where the SDK has 34; LumaLink 9cf2ebe). Run alone on the a2m3 bins: 380 passed, 0 failed.
- N-2040 fixed 07d4228e0: compiled-form.md's targets and flags as LumaLink builds them.
- N-2041 fixed 297bcef3e (plan 04, section 13 step 3): mod_tests.py runs tests side by side (--parallel; one runner
  per test, or per chain of tests continuing from one another, each with its own lock and log, the longest first by
  the last run's times) and has a quick set (--set quick: tests/mods/quick.json, 62 tests, one or more of every suite
  here and in LumaLink's tests/game; refused when a suite has none or a name is not a test). A selective run builds
  only the mods its tests need. The gate's quick level runs the quick set; each mods lane runs --mod-parallel tests at once (2: the lanes run side by side too).
  tools/lumacore/tests/harness_mod_tests_parallel_test.py fails before. On the a2m3 bins (d3d12): the quick set
  62 of 62 in 170 s four at a time (104 s of it the mod build, beside a cargo build), 389 s one at a time; facts and
  pictures the same for 61 of 62 (order within a frame of facts from two threads aside). fs/2301 is not: its completion
  events landed at frame 12 one at a time and frame 11 side by side (N-2047, open).
- Rows for the planned checks Agent 1's planned.tsv names: N-2043 (mod tests on the tablet), N-2044 (bad files),
  N-2045 (two mods), N-2046 (fuzz).
- events_page_test.py (M-111) ended in FileNotFoundError when given a --work folder that did not exist (the gate makes
  it first): 1ee8d0663.
- N-2042 open: four ui tests have no Direct3D 11 reference pictures (harness-error on d3d11 in every gate).

## 2026-11-17 N-2047, N-2048: answers at a fixed frame; mods/m2 on Agent 1's branch

- mods/m2 is rebased on engine/n1011 e76e79c4f (Agent 1's joined branch for the first move of main). The gate's
  lumalink check and the mods lanes are carried onto his new gate.py (lanes, checks with needs): the mods lanes run the
  quick set at the quick level and --mod-parallel tests at once (2), and one helper builds the pinned LumaLink once for
  the lumalink check and every lane. planned.tsv names N-2043 to N-2046. gate_levels_test and gate_lock_test pass.
- N-2047 and N-2048 fixed d363f6b7c: fs's worker and assets' cooking thread posted their answers when done, so in fixed
  steps the frame a mod got them followed the disk's and the cooker's speed (fs/2301: frame 12 alone, 11 side by side;
  a 32 MiB cook: frame 16). Both now have the kernel's deterministic-mode frame_start, as net has: a request or cook
  asked in one frame is answered at the next frame's start. fs/2301 and assets/2807 pin those frames and fail before.
  The fs and assets suites: 22 of 22, the same one at a time and four at a time (D:/NebulaWork/build/a2-fsdet).
  Every other provider that posts from its own thread was read: net had it already, system answers on the game thread
  in deterministic mode, capture and input read devices the tests do not have.
- N-2042 fixed a80085385: the Direct3D 11 references of ui 3813, 3815, 3816 and 3817, made on NebulaD11 and looked at
  against the Direct3D 12 and Vulkan ones (within 7 of 255 everywhere, one edge pixel of 3817 apart as Vulkan is). The
  four pass on d3d11. They were not in known-failures.tsv, so without them mods/d3d11 would have failed the main gate.
- N-2037 fixed 28d67f310 (a crash fault, done at once): the checker reads a mod's Windows unwind tables as the
  system's unwinder will, before the mod loads. Before, a .pdata entry or its unwind data outside the image, or a chain
  that loops, reached RtlLookupFunctionEntry and RtlVirtualUnwind in the guard's fault handler unchecked. 21 lies (x64
  and arm64) are refused and fail before; a byte sweep finds no panic; 417 real mod libraries still read.
- N-2016 fixed 51e02aaeb: the Direct3D 11 references of safestart 6604 to 6607 and aftercrash 6704 and 6709 held
  the aliased key pictures Agent 1 fixed in N-1032 (465dbbe23). Made again on NebulaD11 from a2m4 (on 37ce7f3cc),
  each equals the Direct3D 12 one; the old ones failed the full d3d11 run (rmse 1.26 to 1.65). With the new ones
  6604 to 6607 and 6709 pass on d3d11; 6704 waited behind 6702 (N-2049). These were not in known-failures.tsv, so
  mods/d3d11 would have failed the main gate.
- N-2049 open: in fixed steps the settings screen's status line follows the pipeline workers' speed. With a new cache
  the workers go on making pipelines after the start's set, and while they run the line reads "Preparing graphics:
  N%" in place of the page's own. aftercrash 6702 and 6709 failed so under load on Vulkan and 6702 on d3d11, and pass
  unloaded. It cannot be a known-failures line (a pass would fail the gate). The renderer, the status line and the
  host's deterministic mode are Agent 1's files; he takes the row.
