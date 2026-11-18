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
  lumalink check and the mods lanes are carried onto Agent 1's new gate.py (lanes, checks with needs): the mods lanes run the
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
  host's deterministic mode are Agent 1's files; Agent 1 takes the row.

## 2026-11-17 N-2023, N-2024, N-2050, N-2051: a call's needs are proved at build time

- N-2023 and N-2024 fixed (docs/plan/M2-NEEDS-PROOF.md). LumaScript refuses at build time a game call whose needs
  (luma_functions.json) nothing proves at that place (S0154, with what would prove them). Proofs: the handler's set
  (system, stage, player: tools/lumacore/guarantees.json; on stage ready and on stage "NAME" prove stage, on player
  ready and on play prove player, on stage ended proves system), `can(F)`, `holds("SET")` (new, S0156 for a name that
  is not a set), an if-let on a result that is null without them, a call that makes them (makes in the rows), a hook
  or an override's own row, and the left side of `and`/`or` inside its right side. A wait ends every proof. The kernel
  answers holds at run time (LumaCore.holds; LUMA_CAN_CALL 1, API 161), so the C side can ask the same.
- N-2050 fixed: the rows' merge passed a method's needs to its callers, so 3431 rows needed an object only some
  stages make; a member call now passes on only needs every scene has (657 left). N-2051 fixed: a call under a test
  of MR::isExistSceneObj(X) passes on neither X nor the scene (484 left). needs_fixture_test.py fails before each.
- LumaKit (52 refusals to 0): camera settles only with a stage; cutscene, shop, talk, ride, player forms and replay
  touch the player only while holds("player"), checked again after every wait; actor, item and projectile kill and
  give only with their set. 18 kit-* test mods got the same guards after their waits. New game test k12-sets: every
  handler's order and the sets it proves across two stages and a death (EggStarGalaxy, HoneyBeeKingdomGalaxy).

## 2026-11-17 M-002, M-003, M-005, M-007, M-008, M-028: what a mod gives the kernel cannot end the game

- M-007 fixed: own_class, as_class, is_a, class_data, overrider and call_method read a mod's object, its vtable and
  the RTTI behind it only through guard::probe, a byte copy that runs under a guard frame of its own (asm leaf per ABI:
  win-x64, sysv-x64, aarch64). A stale object, a freed block or a number that is no pointer answers -3 or null. A
  vtable read once is kept per thread (64 slots) until a mod's library closes. A class name for the NoRow report is
  copied through the probe a page at a time.
- M-005 fixed: hook, unhook and can_call take a LumaFunction* only when it is one of the rows (luma::isRow);
  call_method with a null result for a virtual that has one is -1, not a write through null.
- M-003 fixed: a mod's block record lives in the kernel's slots, with 16 guard bytes each side. A write in front of a
  block or past its end turns that mod off at free or take-out (MEMORY, Memory.WrittenBefore/After in 14 languages);
  the game goes on.
- M-008 fixed: an override that runs on another thread while its mod is taken out proceeds to the base's code from
  its own call record, and its mod's library closes only after the call left. Harness 1.2 adds thread_start, a thread
  the game does not wait for.
- M-002 fixed: every mod taken out waits for the grace (it was only one with a hook), call_mod counts its call off
  the game thread as a hook's is, and a finished mod record is freed after a second grace, since modAt reads the
  gone list from any thread. Test guard 1653: a library's call_mod of a callback on another thread while its giver,
  which has no hook, is taken out; before, the giver's library closed under the call.
- M-028 fixed: time handlers and draw points are called by the guard itself (LumaServiceKernel.call_words), not through
  a provider trampoline that was resumable only while the compiler made its call a tail jump.
  guard_frames_fixture_test.py fails when a provider passes its own function to the kernel's call.
- Tests: loader 1115 and 1122 (new), objects 1306 and 1307, classes 1409 (new), guard 1653 (new). Each failed on
  the kernel before its fix: on a2m4, 1115 a guard fault outside the mods' code, 1122 NEB-C02, 1306 and 1307 NEB-M03;
  on the build before M-008, 1409 a guard catch in lc-late's closed library, then NEB-C02 (slotOriginal's panic); on
  the build before M-002, 1653 a guard catch in the giver's closed library (call_mod -1).
  guard_frames_fixture_test.py named the two trampolines. All six pass on d3d12, vulkan and d3d11.

## 2026-11-17 M-013, M-009: a take-out waits for no other thread

- M-013: draw.native's take-out no longer waits for the render thread. The slot is retired (its generation moves on)
  and wiped when the game sees no call in it; a replace takes a free slot. The trampoline counts its call as a hook
  list reader (CodeHold), so the mod's library and memory stay until the call has returned.
- M-009: a mod class's objects are made on the game thread only; the game's arenas take no lock. Its creator called on
  another thread makes nothing and says so once in the log. modGone's spin for makes in progress is gone.
- Tests: world 2493-native-late-take-out (fn-late's first call holds the render thread until its stop; the test takes
  it out once the call says it entered, and the game's tick must then advance while the call still runs) and classes
  1410-make-off-thread. A first form of 2493, which held each call about 80 ms and hoped the take-out came inside
  one, missed that window on vulkan and d3d11. On vulkan and d3d11 the game thread does not move while a PRESENT
  call holds the render thread (ticks_while_held 0 over 1.7 s), so 2493 runs on d3d12 (ticks_while_held 138 there).
  On the kernel before, the take-out waited for the call, which waited for the stop (stopped_seen 0), and 1410 made
  a Kuribo off the game thread (off_made_null 0).
- Ledger: M-013, M-009 fixed; N-2052 opened (renderer feature list, found by Agent 1).

## 2026-11-17 M-110, M-112, N-2053: no silent class id, achievements stop costing, menu pictures are the machine's no more

- M-110: kClassId of a type with no class row stops the build (a static_assert in the primary template) instead of
  naming its objects NameObj. gen_lumacore.py refuses a class in two headers. class_rows_fixture_test.py (in the gate)
  compares the committed rows with what the generator makes now, and compiles a probe each way.
- M-112: achievements are matched by event number, read once; each event's callback walks only its own achievements
  and ends its subscription once all are unlocked (NebulaModApi.unsubscribe; a callback may end its own while the
  event runs). Test events 6902: "none waits for stage_start now", and coin's stays for coin-nine.
- N-2053: a picture of the host's menu waits until the pipelines are made (pipelines_left) and ten frames drawn since
  are presented, so its status line no longer reads "Preparing graphics" on a loaded machine. Found as safestart
  6605's failure in the regression.
- The types fixture's driver gives guard::probe (0e028f210): the M-007 commit had broken its link in the gate.

## 2026-11-17 M-010, N-2052, N-2054, M-129: class rows come back, a needed renderer feature is refused, test runs check as a player's game does, calls cost no text

- M-010: a finished mod's class rows are used again from the next scene, each use with new numbers (classCount() +
  generation * 1024 + row). Test classes 1411-rows-again: lc-many defines 400 classes at each of four starts, a scene
  change between; before, the starts defined 400, 400, 224 and 0.
- N-2052: the game's renderer feature list (native/luma/luma_renderer.json, none in this release) is in the
  self-description folder of every package, and the game gives it and the device's features to the validator. Test
  startval 3136-start-feature: a mod needing ray_tracing is refused with L204; before, it started.
- N-2054 (found while proving N-2052): the game in a test run had no self-description, so the in-game checks that
  need the game's names, types, services or renderer features never ran in a test. Each run's bin/luma is now a link
  to one copy of the bins' luma/ (harness_self_description_test.py).
- M-129: a service's call of a mod's function makes its guard name once per service and kind and finds the mod's record
  through a cache. Test time 2219-calls-cost: at most 8 texts and 8 scans in the first 1000 calls; before, no fact (a
  text and a scan each call).

## 2026-11-17 M-013 is not proved yet: 2493 waits for N-1056

- The b540dd3a1 entry above says 2493 proves M-013 on d3d12. It does not: its fact ticks_while_held counts
  LiveActor::calcAnim calls (about 130 a frame), not frames, and on every renderer the game thread stops about one
  frame into a render-thread hold. Rerun three times on the bins of b540dd3a1: stopped_seen 0 each time. The two
  passes before fell inside that one frame by chance.
- The cause is the render queue (Agent 1, N-1056; 77e311fe0 did not change it in 2493). 2493 is in
  world/pending.json, without a renderer list, and M-013 is open until it passes on all three renderers. The kernel
  side of M-013 stays: a take-out no longer waits for the call itself.

## 2026-11-17 M-029, M-011, N-2055: no pointer a mod gives a service can end the game

- M-029 (and M-011, its short form): every service function, in the game's kernel and in the host's providers, read
  and wrote the pointers a mod gave it directly, so an unmapped address, read-only data or a function outside the
  mod's code ended the game. Each pointer now goes through the kernel; a wrong one is LUMA_E_ARGUMENT, reported, and
  the call does nothing (no out written, nothing started; a take into a buffer it cannot write takes nothing). Large
  copies run outside the I/O and mix locks. calling-services.md states the rule.
- Tests, one per service, each giving every pointer wrong (lt_bad.h: unmapped, read-only, data as a function, a game
  function), then a good call: time 2220, movie 3106, player 2963, system 2235, fs 2315, mods 2255, input 2514, audio
  2612, audio 2613 (microphone, camera), assets 2808, net 2711. On the bins before (a2m10 for the kernel, a2m13 for
  the host) each crashed the game; on a2m14 each passes.
- N-2055: test mods imported the harness service as required; the game's catalog leaves it out, so the checker
  refused them (L505: time 2216, loader 1109 and 1116, classes 1409 and 1410). They import it as optional now.
- For Agent 1: stage 3902 (P-177, a view group cube with Obj_arg0 -1) and 3903 (P-178, a flat-model shadow of a
  one-joint model) fail on the current game and wait in stage/pending.json for the fixes.

## 2026-11-17 N-2056: a test run's check binds the test services

- Since N-2054 the start's check ran in every test run, but it bound service imports against the shipped catalog
  only. It refused every mod that imports the test extension's test. services: 20 loader, registry and mods tests
  failed on a2m14 and on a2m10 before it.
- LumaValidateInput gains test_services: the test. services this run's host extensions give, with name, version and
  permission. They are added to the catalog in order, so an import binds as the kernel binds it. The game fills the
  list from the extensions; a player's game has none, so a test. import is L505 there, as luma-json.md says. A kept
  report depends on the list.
- Tests: crates/nebula-import validate_c a_test_run_s_services_bind_as_the_catalog_s, with a new fixture
  (pe-x64/service_test.dll, test.svc 1.3). Registry 2101-2116 pass. Loader 1101, 1102, 1110, 1112, 1118, mods 2242
  and registry 2103, 2104 now expect the check's finding: the check refuses those mods before the loader, as in a
  player's game. A game without the importer's library turns every mod off, so the loader's own lines for these cases
  are not reachable from a start.
- For Agent 1: stage 3904 (P-164, First Comet unlocks during the comet star's get sequence) fails on the current game
  (First Star unlocks, First Comet does not) and waits in stage/pending.json. 2493 passes on d3d12, vulkan and d3d11
  with engine/e13a merged; it moves to world/suite.json when mods/m2 stands on 77e311fe0 and df726e744.

## 2026-11-17 M-030, M-065: extra players are reused, and their draw slots freed

- M-030: player.make made a new MarioActor in the scene's memory each time, and remove only ended it, so a mod that
  makes and removes in a loop filled the scene's memory. An ended extra player is now kept (dead, hidden) and the
  next make stands it again at the new home. Test player 2964: three makes, then forty removes and makes; before,
  MarioActor::init2 ran 43 times, now 3, and the last made stands (picture f120).
- M-065: set_drawn's four slots kept removed players, so a later hide (of the game's player) was not held and
  outlived its mod. A player's slots are freed when it goes. Test player 2965: three hidden extras removed, a fourth
  hidden, then the game's player; the mod is taken out; before, the player stayed hidden (isAllHidden 1), now 0.
- Player suite 5 of 5 on d3d12 with the new game (a2m16).

## 2026-11-18 mods/m2-w: engine/e13a merged; M-013, M-051, M-039, M-034, M-055

- mods/m2-w is mods/m2 2d5e90fed merged with engine/e13a ff596d04c (b920c5fd8; main 87b6d3db5 is below it). On it,
  with bins a2w1 (d3d12): stage 3902, 3903 and 3904 pass and join stage/suite.json (P-177, P-178, P-164; 3903 now
  expects the scene's ten Kuribos, not two), and world 2493 passes and joins world/suite.json (M-013 fixed).
- M-051 (LumaLink 9c1df9c): a text field's on_done was held by LumaKit's edit task while the keyboard was open; when
  its mod was taken out, the task let the closure go after the grace had freed the mod's code, and the game crashed
  in lumakit (NEB-M03). on_done is now let go while the mod stops. LumaLink kit-ui-gone fails before, passes after.
- M-039 (LumaLink 4aeb8cf): kit.shop checks its keeper again when a row is taken; a keeper killed while the list
  shows sells nothing. The header and shop.md say MR::payCoin. LumaLink kit-shop-keeper fails before (sold, 10 coins
  paid), passes after.
- M-034: the LumaKit pages' three lost words (screen.md, ui-kit.md; replay.md was whole) and ui/kit.lum's header.
  tools/gate/tests/lumakit_pages_test.py finds them on 2d5e90fed's pages.
- M-055: a WebSocket server that pings without end and reads nothing made the service queue a pong a ping, taking the
  mod's send budget. A pong waiting whole in the queue is now replaced by the newest ping's (RFC 6455 5.5.3).
  net 2712-net-ws-ping-flood (200000 pings, then a 3 MiB ws_send): before, the server's sends were refused at frame
  14 and the pings never all went; after, a pass. It runs on the real clock: the fixed steps wait up to a second a
  frame for bytes this process sent itself that the server never takes.
- M-098, M-092: evidence only. Every exit of a running game is finish_and_exit: TerminateProcess, std::_Exit or execv,
  so no static destructor runs under the net and device threads on Windows, Linux, macOS or Android. iOS's own exit()
  at termination is the case left; no iOS device here.
- LumaLink pin 4aeb8cf (mods/m2-w, on f329a8b); installer/lumakit/lumakit.luma packed with it and the merge's
  self-description (store 37); build_lumakit --verify passes.

## 2026-11-18 mods/m2-w: M-093, M-083 (a mod id with a '.')

- luma.md 4 lets a mod id hold a '.' (speedrun.timer), and the manifest reader takes one. mods.ids' record reader,
  its kept() list and fs's data folder each had their own id rule without '.': such a mod's ids key made the whole
  record unreadable at the next start (moved to mod-ids.json.bad), FORGET found nothing to forget, and fs gave it no
  data folder (write -6). The rule is now one function, galaxy::luma::valid_mod_id (luma.h), which every reader of a
  mod id uses; runtime luma_tests checks it.
- New tests, mods suite, with mod dot.ids: 2257a-c (ids number, the next start reads the record, FORGET takes it)
  and 2258a-d (data file written, read back at the next start, forgotten, gone). Before, on bins a2w2: 2257b fails
  (Mods.IdsUnreadable), 2257c fails ("dot.ids is not on the REMOVED MODS line: nothing to forget"), 2258a fails
  (write -6).

## 2026-11-18 mods/m2-w: N-2058 (services past 64, a crash), N-2059 (a character's own clips)

- Board 00:28: the rows handed to me since 22:30 (N-3008, N-4011, N-4014, N-4015, N-4019 to N-4021) and P-116's cook
  part (N-2057, open) are taken; order on the board 01:05.
- N-2058, the crash in N-3008: the kernel's services table held 64 and panicked past it. It is now chunks of 64 that
  never move (a reader on any thread is safe while the game thread adds) and a frame-start list that grows. The test
  extension gives GALAXY_TEST_EXTRA_SERVICES test services (test.extra.n0 ...); registry 2118-many-services asks for
  200. Before (bins a2w5b, old game): crashed, "LumaCore: more than 64 services". After (a2w5): registry 18 of 18.
- N-2059 (N-4011): an animation file in an archive with no model (MarioAnime.arc) moves the model of the archive its
  name gives (Mario.anim.glb: ObjectData/Mario.arc); none there: Cook.Anim.NoModel, 14 languages. The cook's test fails
  before (Cook.Model.NoGameModel) and passes after; nebula-import and luma-validate tests all pass.
- tools/nebula_text.py --check failed on main's mods.cpp (an environment variable's name read as shown text, from my
  M-031 commit); it is listed as not shown, and the check passes. The check is in neither the gate nor GitHub's jobs.
- N-2060: the full run one at a time on bins a2m16 (the parallel series' baseline) was 556 of 569: two not run (other
  region discs) and eleven refusal tests that still looked for the loader's line after N-2054 moved the refusal to the
  start's check. They expect the check's mod log row now; loader5, types and varargs 28 of 28 on a2w5.
- engine/e13a a8796ab68 merged (8ce3d9560), at Agent 1's request.

## 2026-11-18 mods/n3008: N-3008 evidence, N-2057, N-4002, N-4003, N-4004, the key hold

- N-3008 evidence: on bins a2w6 (before) classes/1412-past-1024 stops at 1024 classes (fact defined 1024, wanted
  1101) and loader/1140-many-mods' take-out never ends (takeout.lt-crowd-20 not within 600 frames); on a2w7 (after)
  both pass, with 1407-make. 1412's mod did not build at all until its alias arguments left LT_START (a macro's
  argument split at their commas).
- N-4002: tools/lumacore/instrument.py proves a `const char *` parameter `in` when the callee and every overrider only
  read it during the call (followed through aliases, casts, the strchr family and calls to parameters themselves
  proven); gen_types.py marks them and writes luma_texts.left with the reason for each one left. texts_fixture_test.py
  (45 rows: 20 in, 25 left with their reasons) is in the gate's fixture tests.
- N-4004: LUMA_NULLABLE before a pointer field reaches the types file as "nullable": true (luma-validate Field.nullable);
  a mark on a field that is no pointer, any other mark, or a mark twice stops the build by name.
- N-4003: LumaCore.weak and weak_get, one table of weak references in the kernel for every mod, emptied by the game
  itself (NameObj's destructor, NebulaArena::free, an arena's end): a mod holding references adds no hook. Unit test
  galaxy_luma_weak_tests (a model through 800 000 random steps and four threads; it fails with the hash's removal
  broken, with the generation not moved on, and without the lock); 1308-weak in the game with two mods (the same
  handle, one taken out, a new scene, hostile objects and handles, no hook on NameObj::~NameObj or NebulaArena::free).
- Harness: a key step holds the steps after it until the menus have taken the key (NebulaHostForExtension.
  menu_keys_waiting): beside other runs the window's thread took 6709's Escape after the picture meant to show it.
  busy_window keeps that thread busy; 21-key-waits gives keys to a window busy for 1.5 s.
- 3107-movie-loose called MR::startMovie(s32), which the game does not have; it calls MR::startMoviePlayer.

## 2026-11-18 mods/n3008: the text proof's helpers, 1123, 3107's pictures, the harness clock, faults GitHub found

- N-4002 (e0ef5207b): the text proof also reads the functions with no row that a proven one calls (static inline
  helpers, the kernel's own) and overrides defined in a header, which it had passed over: an override that kept the
  text could make its base `in`. texts_fixture_test.py: 51 rows; luma_texts.left lists 2306 parameters left kept with
  a reason each (most are an object's name given to its constructor). M-114's note names what could still be proven:
  texts given to mods' events and to the kernel's services, through function pointers.
- LumaLink pinned at 592614e (M-123, M-132, N-4014, N-4015) and LumaKit built with it (e5a34f52b; verify passes).
- loader/1123-passes-game-thread (P-040 condition 3, for Agent 3): a mod hooks a function inside each pass the plain
  module may move to workers. On bins a2w9: 0 calls off the game thread for every hook; on it, the s32 frustum judge
  472336, MR::isClipped 1217661, ShadowController::updateDirection 24698, PlanetGravity::calcGravity 2831612, the
  three animation calls 101279 to 125895. The f32 judge is never called in EggStarGalaxy and is checked off the game
  thread only (87680a431).
- N-2057: 3107-movie-loose checks its pictures (the mod's one-colour movie, looked at, references made from a2w9).
  Before (bins a2w8, importer older than the fix): fails, rmse 125.6, no cook.movie fact; after: passes.
- GitHub run 44303790236 on mods/n3008 failed two jobs, both mine:
  - Linux did not link: native_settings_keys_waiting and busy_window existed on the Windows window only. The SDL and
    Apple windows count the keys they hold now (b8182f504); the next run's Linux job links.
  - galaxy_luma_weak_tests read past its cells under the undefined-behaviour sanitizer (a range end drawn twice). It
    is a game unit test now (native/tests, the game's compiler and flags; MSVC cannot build LumaWeak.cpp), draws once,
    and stops on any index past the cells: object 6081 of 6000 with the old line, passes with the fix (20864244d).
- Run 44308720250: LeakSanitizer in the game's unit tests: a GrowTable dropped the directories it retires (32 and 64
  bytes in the weak test) and the grow test never freed its tables. A retired directory is now reachable from the one
  that replaced it, and release() frees a test's table (b723b7a24).
- busy_window's window message had the show-instance message's number (my slip): every 21-key-waits run showed its
  hidden window and the window thread never waited. Own number, and a compile-time check that the window procedure's
  messages differ (fails with the old number) (e513a1e64).
- With the window really busy, 21-key-waits showed the harness's clock fault: steps with a frame ran at that test
  frame while held steps let test frames go on, so after a hold every due step ran in one frame and the end and the
  facts after it carried the frames held (end step 160, ended at 693). Test frames now stand still while the steps are
  held (4e3de9ba5); harness_steps_test.py's run held by a menu key ends at frame 59 with the old harness, 30 now.
- same_mod_runs.py (6332a038c): unsteady tests (facts differ between two runs one at a time: device, network and
  thread events) judged by verdict and pictures; loopback ports and shader file names written as one.
