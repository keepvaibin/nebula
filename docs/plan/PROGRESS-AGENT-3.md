# Progress: Agent 3 (audio, input, menus, text; Android; Windows, Linux, Mac; console limits; game step)

One dated entry per package step. Newest last.

## 2026-11-17 Start

- Took E10, E11, E12, E8, E7 and my part of E14 from Agent 1 (file 05): 71 rows, agent column 3 (4f46aabe7).
- Tree D:/NebulaWork/MarioGalaxyRecomp/Nebula-A3, branch app/work from engine/n1011 bbeeab141.

## 2026-11-17 E10 input and audio sweep (app/work)

- 66818a950 audio: P-037, P-071, P-072 fixed.
  - SDL renders in chunks from one buffer made with the device; no allocation on the audio thread.
  - SDL and Apple report the sound they still hold, so a movie's picture follows what is heard.
  - The Windows fill rises on a late wake or render and never lowers; the log's sound line gives fill and under-runs.
  - Test: galaxy_audio_fill_tests.
- 874754e3b input: P-073, P-084, P-074, P-075, P-076 fixed; P-077 and P-083 in code, open for their proof.
  - One radial dead zone per stick on every host; the game gets the direction kept (game_stick). The Nunchuk
    curve only for Nunchuk counts and the tour script. The stick test fails on the code before (family stick 0.6).
  - Nunchuk stick centre from its calibration; three checks of galaxy_native_input_tests fail before.
  - Short presses latched on SDL, Apple, Windows HID and XInput (4 ms sampler while the game reads).
  - Absolute raw mouse input gives motion (Remote Desktop, virtual machines, pen tablets); macOS uses GCMouse
    counts. galaxy_raw_mouse_win32_tests fails before.
  - The Windows HID worker waits on one completion port; light bar, motor off and the PlayStation mode request do
    not wait; stuck requests are cancelled by the timer. No controller is attached to this PC, so the device proof
    of P-077 and P-083 is owed (a pad run or a pipe-backed worker test).
- Unit tests: 87 of 88 pass in my build; galaxy_player_input_tests needs the gate's settings and dumps folders.
- Not run here: the Apple and SDL code builds in CI on app/work (no Mac SDK on this machine, file 04 decision).
