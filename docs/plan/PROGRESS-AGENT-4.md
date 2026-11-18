# Progress: Agent 4 (mods: the LumaLink repository)

One dated entry per package step. Newest last. Ledger rows are in ledger.tsv (agent 4); new rows are N-4xxx.

Packages, in order (plan file 05, section 5.1): M10 camera, look and first person; M7 the compiler; M8 LumaLink;
M12 platforms for mods; M14 the six reference mods; M11 documentation, from the build; Agent 4's part of M13.

## 2026-11-17 Start

- Work trees: Nebula-A4 on link/work from engine/n1011 bbeeab141 (main was at 7b541c50b); lumalink-a4 on link/work
  from LumaLink mods/m2 9cf2ebe.
- 70 rows of M10, M7, M8, M12 and M11 carry agent 4; 61 of them open.

## 2026-11-17 M8 first sweep (LumaLink 01aa60e)

- Fixed with tests: M-023, M-141, M-142, M-147, M-148, M-149, M-165, N-2002, N-2006. Each test fails on the old
  build except the two that test a new part (files::Part, running.rs): M-149's comma path was run on the old link
  flag and failed (ld.lld: cannot find linker script .../sd).
- LumaLink's workspace tests: 29 test programs, all pass. The window's tests take under a second now (N-2002).
- Docs: compiled-form.md (arm64 Linux atomics, --script), packing-and-sharing-a-mod.md, and every
  lumalink-commands page written again by tools/modding/lumalink_commands.py from this LumaLink.
- M8 left: none open in this package but what M11 owns of its docs.

## 2026-11-17 M10 and M7 first sweep (LumaLink b32a890)

- M10 fixed with tests: M-056 (the game's mirror follows a mod's main camera; world/2473-mirror, the player put at the
  twin-falls mirror object), M-060 and M-067 (galaxy_luma_input_tests), M-064 (a hidden body's carried object shown
  again once let go; player/2963-hidden-carry), M-175 (kit.camera keeps the body hidden across a new player actor;
  LumaLink kit/kit-camera-fp), N-4001 (new: a mod's rumble lasted 60 reads a second, now the game's steady clock).
  M-066 not-a-fault: the joint matrices already hold the body scale (Skeleton::evaluate with mBaseScale).
- M7 fixed with tests: M-018, M-020 (taken from Agent 2), M-116, M-118, M-119, M-121, M-122, M-124 to M-128, M-130,
  M-131, M-133, M-134, M-137, M-139, M-140; M-006 (M12) in the same LumaLink commit. In the game: LumaLink's script
  suite (25 tests with the new script/m7-sweep) and the camera kit tests pass on d3d12; the input suite (13) passes.
- Open, waiting on others: M-114's text half (N-4002, `in` marks), M-132 (N-4003, a kernel watch), M-123 (a planner
  question; N-4004, field marks). Host rows M-057, M-058, M-061 to M-063, M-069 wait for Agent 3's app/work; M-068 is
  a request to Agent 1.

## 2026-11-17 M12 platforms for mods

- M-012 fixed: the microphone and camera services on Android and Linux through one SDL backend (SDL's recording
  stream at the mod's rate and channels, SDL's camera converted to RGBA at the camera's own size), replacing the AAudio
  and Camera2 code; on macOS and iOS through AVFoundation (runtime/src/luma_capture_apple.mm); the item, its lock and
  the hand-offs shared in luma_capture_backend.h. Proof: audio/2606-capture-mic and 2607-capture-camera on the tablet
  (real microphone blocks (48 kHz mono) and real camera pictures (640×480 RGBA) from the tablet's devices); Linux and Apple
  compile in GitHub's check (run 44250438876 on c50533c71: the Linux and Mac jobs green; M-074 the same Mac job).
- M-074 fixed: draw.native 1.1 calls a Metal mod's function (LUMA_DRAW_NATIVE_METAL, LumaDrawNativeMetal with
  get_proc_addr); world/2491-native's mod clears through Metal too and still passes on d3d12. backend() answers the
  renderer (2 for Vulkan on MoltenVK, 4 for Metal), not 0 on every Apple build.
- New N-4005 (open): no Mac in the gate, so draw.native on Metal and the capture services on macOS and iOS have
  compiled but never run.
- Files of others touched, one line each: native/petari/src/Game/System/LumaServices.cpp (draw.native's minor 1),
  native/host/host_main.cpp (backend()), platform/sdl and platform/apple CMake (the capture sources and frameworks).

## 2026-11-17 M14 reference mods, M10 look (LumaLink 104f6c8)

- The six reference mods, each a worked example of docs/modding (examples/<name>, a tutorial each), built from the
  public docs and the released tools: first-person (kit.camera's eye rig with look: the player turns the view, Mario
  runs where he looks), a-new-object (the Sky Lift, a new object kind the player rides), a-level-timer (a HUD timer
  whose label is the game's own text in every disc language), ghost-time-trial (a ghost replay, the same steps to
  0.002 units on every platform), a-stage-edit (Good Egg Remix: five coins added along a walk, a timer block taken
  out) and a-new-character (Robo: a model from any 3D tool bound to Mario's skeleton with `lumalink add character`,
  his own actions, voice and jump).
- Gated: mod_tests runs docs/modding/examples as the origin examples/ (N-4007), every example's tests in the mods
  level and one of each in the quick set. examples/together: the six at once, and with each one installed and
  switched off in turn (the other five still do what each does alone, the one off does nothing). 48 of 48 pass on
  Direct3D 12, Vulkan and Direct3D 11; the tablet builds all 20 example mods (N-4016).
- a-level-timer's label tests run in every language the discs have (US English, Spanish and French; EU English,
  Spanish, French, German, Italian and Dutch on RMGP01; Japanese on RMGJ01).
- Fixed with tests: N-4006 (look step, M10), N-4008 (kit.camera look, M10), N-4009 (list models joints, M8),
  N-4010 (count step), N-4012 (holds), N-4013 (add character report, M8), N-4016 (scripts build for a device),
  N-4017 (a test refused before it is read).
- Handed to Agent 2 (Planner 23:09): N-4011 (a mod's animation file in an archive with no model), N-4014 (a
  character's look never attached) and N-4015 (its actions and voice never replace the game's). Their test is
  docs/modding/examples/a-new-character/tests/look-and-moves/test.json.owed; the gated tests/play checks the tuning.
- Files of others touched: platform/android/build_apk.ps1 (the names set in test-bin), tools/lumacore/mod_tests.py
  and luma_test.py (holds, logs per test, the folder's result, the examples origin); board 23:16.
