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
