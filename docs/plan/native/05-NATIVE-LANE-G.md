# Nebula native plan: Lane G, the game and the display, and the end

Step by step. The order and the reasons are in `docs/plan/native/02-NATIVE-OVERVIEW.md`. Each step is a card: what changes, where, the rows it closes, what it needs first, the old path, the proof, what a player gets, what to read first. "FM" is the design file `docs/engine/FRAME-MODEL.md` on main; "INV" is the inventory `docs/engine/CONSOLE-FREE.md` (branch `docs/console-free` until Stage 0 lands it). The full ledger text of each row is in `docs/plan/native/rows/<package>.tsv`.

## Contents

- **G1 The input record and its reads**
  - G1.1 The record: the device's own motion, a pointer in the picture's terms, one fixed space for speed
  - G1.2 Connection, language, rumble and battery, behaviour by input
  - G1.3 Sticks: one radial dead zone, the Nunchuk's centre
  - G1.4 Presses latched between reads
  - G1.5 Windows: the controller worker and the pointer buttons; the parts of P-244 left
- **G2 How a picture reaches the display**
  - G2.1 Landing 1: a sequence starts at its arrival; the step keeps its due times
  - G2.2 Landing 2: the lead at whole multiples of 60 Hz
  - G2.3 Landing 3: a display model, one platform at a time
  - G2.4 Landing 4: no closing spin where a display time is asked
  - G2.5 Landing 5: one arbiter, the clock held, the heat input
  - G2.6 Landing 6: the pointer and the menu cursor at the picture's moment
  - G2.7 Landing 7: every camera cut and warp stated; `camera_epoch` read by the host
  - G2.8 Landing 8: the cost of a picture
  - G2.9 Landing 9: the default rate
- **G3 The game step on the cores**
  - G3.1 The pool and the fork-and-join call; the run lists made safe
  - G3.2 Visibility's judge
  - G3.3 Collision queries with caller buffers; shadows on workers
  - G3.4 Animation without callbacks
  - G3.5 Particles without callbacks
  - G3.6 The frame list's recorders
  - G3.7 Loads read ahead; the stage load on the load worker
- **G4 The step's speed**
  - G4.0 Take the step-speed commits already written
  - G4.1 Collision
  - G4.2 Gravity, sensors, the judge
  - G4.3 Arenas, pools, emitters
  - G4.4 Small walks
- **G5 Every actor runs, the same on every screen**
  - G5.1 Landing 1: the "seen" judge on the reference volume
  - G5.2 Landing 2: running split from the judge
  - G5.3 Landing 3: drawing culled per picture
  - G5.4 Landing 4: the actors' own skips
  - G5.5 Landing 5: the measure and the choice
- **G6 Sound**
  - G6.1 Sound sequences in host order (P-186)
  - G6.2 Sends and sound settings as typed records (P-221, P-222)
  - G6.3 Sound lookups by map (P-223)
  - G6.4 Checked sound data and handles (P-224, P-227, P-228)
  - G6.5 Streams read ahead; a stream's end by the step clock (P-225, P-226)
  - G6.6 Movie sound rate and volume (P-271, P-219's movie half)
  - G6.7 The surround law (P-193)
  - G6.8 The audio device (P-037, P-071, P-072, P-286)
- **G7 Text**
  - G7.1 Landing 1: `NMSG` version 2 beside version 1
  - G7.2 Landing 2: the tag processors on runs (with P-201)
  - G7.3 Landing 3: the screens, the host and the mod surface on runs
  - G7.4 Landing 4: code points through the font (P-187)
  - G7.5 Landing 5: version 1 and the in-text format deleted
- **G8 Save, tables, limits and waits**
  - G8.1 The save as named, sized records with a version (P-205, P-232)
  - G8.2 Bounded waits on the game thread (P-272, P-235)
  - G8.3 Table fields by index, a resource's size by its handle (P-189, P-233)
  - G8.4 Limits that grow, names without 16-bit hashes (P-192, P-148, P-204)
- **G9 Timing and small items**
  - G9.1 Movie cues fire when the movie's place crosses them (P-203)
  - G9.2 The staff roll placed from the music's position (P-206)
  - G9.3 The Mii library's own identity (P-268)
  - G9.4 Fixed counts in the 2D screens, and a picture size cap (P-208, P-211)
  - G9.5 The host's menus: stability (P-306)
- **Z The end**
  - Z.1 Profiles taken again
  - Z.2 The end check
  - Z.3 The statement and the two builds

---

## G1 The input record and its reads

The game's input record still has the Wii Remote's shape: tilt is gravity in the remote's axes, the pointer carries a roll and a distance as a sensor bar would show them, and every other device is rewritten into that form. The host also gives the game a pad's connection as the remote kit's callbacks, the language as the console's ten slots, and rumble through the console's motor registry. Sticks pass two dead zones that differ by platform, short presses can be lost, and the Windows controller worker and pointer buttons have stalls and a way to end the game. This package gives the game a record in its own terms (tilt as a lean, a pointer in the picture's shape, one fixed space for the pointer's speed), reports devices as state, makes sticks and presses behave the same on every platform, and fixes the Windows input worker. A player gets the same pad, pointer and touch behaviour on every platform and screen shape, a round stick, and no lost taps.

- **Design:** INV section 2, bullet "Input" (corrected 11-19: the record is not modern), and INV section 3's lines for P-282, P-234, P-236 and P-244 (also P-243/P-244's shared line on rules that read the screen). The rest is the rows' notes, which hold the owner's answers of 11-19: P-282 (a tilt record in the game's terms, no remote emulation, the game's dead zones kept), P-084 (a modern stick's full circle mapped onto the Nunchuk's reach; replay changes once) and P-244 (the best form for everyone; decided: one fixed space). Cut-short rows (P-073, P-074, P-075, P-077, P-083, P-084) are completed from audit file B (`docs/audit-2026-11-17/B-findings-port.md`, section R4.3 and the "Round 4 leftovers" part; line numbers in those findings are as of 11-17 and have moved). FM 2.16 target 6 (fresh input) reads the record this package makes; it is G2.6's work.
- **Needs first:** nothing stated in the skeleton. G1.1 must be on main before G2.6. G1 stands at places 30 to 34 of `steps.tsv`, after G2.1 to G2.5 and the surround steps (the owner's order of 11-23); G2.1 to G2.5 need none of it, and G1.4 and G1.5 read `host_window_sdl.cpp` and `host_main.cpp` as G2.3 leaves them (G2.3 changes the event pump and the surface hand-over, G1.4 the latches beside them).
- **Rows:** 13 (P-282, P-279, P-244, P-234, P-236, P-220, P-073, P-084, P-074, P-075, P-077, P-083, P-288)

### G1.1 The record: the device's own motion, a pointer in the picture's terms, one fixed space for speed

- **What changes.** `NebulaInputSample` (`nebula_game_api.h:304-316`) is the record. Each fault of the rows is a bullet; P-282 and P-279 close here, P-244 closes at G1.5 (see "Parts closed here" there).
  - P-282, tilt: every native device is turned into a remote's gravity: a pad's tilt is rewritten as a remote's gravity by the section the game names (`tilt.cpp:67-112`, `gTiltSection` in `host_main.cpp`), upright with `calcXY`'s 10-degree lean for the Star Ball and flat for the ray, and a stick stands in where a pad has no motion. New: a tilt record in the game's terms (lean right and forward from the player's neutral), filled by each driver and read by the game's tilt code (Tamakoro's `calcTiltXY`, SurfRay through `MR::getMainTilt`) with `calcTiltXY`'s dead zones and ranges kept. The remote-frame conversion lives only in the real Wii Remote's driver.
  - P-282, roll and distance: the pointer's roll as the sensor bar would show it (`StarPointerLayout.cpp:688`) and its distance (`PlayerInputUtil.cpp:64`) are a virtual remote's (`kVirtualPointerDistance`, `native_input.h:50-52`). New: roll, distance and facing are optional, filled only by a real remote; the game uses its own defaults without them.
  - P-282, the pointer switched off: `apply_tilt_accel` writes the tilt section's gravity and keeps `pointer_on` only while that virtual remote would face the bar (`remote_faces_screen`, `native_input.h:80-82`, `controls_engine.cpp:10-23`), so a mouse, gyro or stick pointer vanishes in a tilt section because of the console camera's limit. New: the pointer stays on (the game reads `pointer_on`; `PlayerInputs.cpp:123-131`).
  - P-279: the Android, Linux and Apple hosts give the gyro pointer a fixed 16:9 (`kDesignContentAspect`) while the Windows controller path and the Wii Remote pointer take the picture's latched shape. New: both hosts take `galaxy::latched_content_aspect()`; one source for every pointer.
  - P-244, position and speed: the pointer's position is scaled by `MR::getScreenWidth` (the display's width when wide) and its velocity is the difference of two positions (`StarPointerController.cpp:68`, `:96-99`; `ScreenUtil.cpp:88-93`). Rules compare that velocity with fixed numbers: a fireball reflects over 30 (`FireBall.cpp:225-228`), `MagicBell` rings past a threshold (`:148-151`), the space cocoon reacts above 5 (`SpaceCocoon.cpp:617-618`). New (Agent 1's decision after the owner's answer of 11-19): position and velocity for the game's rules are measured in one fixed space, the layout's reference space with equal units across and down (the console's height units, so the tuned numbers keep their meaning).
- **Where.** `native/include/nebula_game_api.h` (`NebulaInputSample`); `runtime/src/tilt.cpp`; `runtime/include/galaxy/native_input.h`; `runtime/src/native_input.cpp`; `runtime/src/input/controls_engine.cpp` (`apply_tilt_accel`); `native/host/host_main.cpp` (`gTiltSection`); `native/petari/src/Game/System/PlayerInputs.cpp`; `native/petari/src/Game/Screen/StarPointerLayout.cpp`; `native/petari/src/Game/Util/PlayerInputUtil.cpp`; `native/petari/src/Game/Screen/StarPointerController.cpp`; `native/petari/src/Game/Util/ScreenUtil.cpp`; `native/petari/src/Game/Enemy/FireBall.cpp`; `native/petari/src/Game/MapObj/MagicBell.cpp`; `native/petari/src/Game/Ride/SpaceCocoon.cpp`; `platform/sdl/host_window_sdl.cpp` (`:556`); `platform/apple/host_window_apple.mm` (`:314`); `runtime/include/galaxy/content_viewport.h` (`:21`); `runtime/src/controller_hid.cpp` (`:2076`); `runtime/src/input/wiimote_motion_pointer.cpp` (`:39`); `runtime/include/galaxy/display_aspect.h` (`latched_content_aspect`). All lines as cited at 241395b49.
- **Rows.** P-282 whole; P-279 whole; P-244 advanced (the pointer's position and speed in the fixed space and the rules above; the rest is listed at G1.5).
- **Needs first.** nothing.
- **Old path.** none named by the sources: the remote-shaped conversion (`tilt.cpp:67-112`, `kVirtualPointerDistance`, `remote_faces_screen`) is removed from the native drivers in this step. The sources give no switch.
- **Proof.**
  1. P-282: the lean values the reworked game side gives are the same as today's bit for bit, shown by a test on the tilt sections; or the replay change is accepted and recorded once (the row says "proven or accepted").
  2. P-282: the game's dead zones and ranges are unchanged (`calcTiltXY`), and a mouse, gyro or stick pointer stays on in a tilt section.
  3. P-279: replays unchanged (they hold the pointer sample the host hands the game); the same gyro turn reaches the picture's edge on Windows, Android, Linux and Apple at 4:3, 21:9 and phone-tall.
  4. P-244: the replay changes on screens other than the one recorded; after the step the same recorded input gives the same fireball reflection, bell ring and cocoon touch at 4:3, 16:9 and 21:9 (derived from the row's fault; no named test).
  5. A search for `MR::getScreenWidth` in `StarPointerController.cpp` returns nothing (derived from the row, not a test the sources name).
- **For a player.** The gyro pointer reaches the picture's edge at the same turn on every platform; the same hand motion gives the same fireball and bell result on every screen shape; a pointer no longer disappears in a tilt section.
- **Read first.** INV's "Input" bullet already says "not modern" after the 11-19 correction; set its class to modern, and its section 3 row to done, when this step lands.

### G1.2 Connection, language, rumble and battery, behaviour by input

- **What changes.** Each fault is a bullet; the three rows close here.
  - P-234, connection: a pad's connection reaches the game as the remote kit's connect and extension callbacks, a Nunchuk's arrival told as `HAND_UNKNOWN` then `HAND_SUB` (`PlayerInputReader::updateConnection`, `PlayerInputs.cpp:61-87`). New: the host reports the device and its parts as a state the input reads, with no callback order.
  - P-234, language: the language is the console's ten system-language slots, the last three falling back to English, mapped by region to the game's ten (`Language.cpp:14-71`; `decideLanguage` clamps the host's number). New: the host names the game language (a language id or a locale tag), so a language that content adds needs no slot.
  - P-234, the host's list: the languages are the seven of `GameLanguage` (`runtime_settings.h:52-65`) and a folder of any other name is Auto (`folder_language`, `runtime_settings.cpp:330-340`), so a release or added content with another language cannot be chosen. New: the list is built at load from the store's language folders, each named by a tag the setting saves.
  - P-236, the registry: rumble runs through a per-channel registry made for the console's motor callback (`InputRumble::sInstanceForCallback`, `InputRumble.cpp:10`, `:62-67`, `:81-88`; switched between a pad's game and menu rumble at `GameSystemFunction.cpp:153-188`). Each pad's two `InputRumble` objects both call update every step (`PlayerInput.cpp:37-38`) and each update advances the registered one (`:116-125`), so the registered rumble's channels step twice a step (`mStep++`, `:51`) and the other never steps. New: each `PlayerInput` steps the rumble in use once a step (game or menu as a state, no registry).
  - P-236, the battery: it is copied from the device record at connect and then every 300 seconds of steps (`InputInfoChecker.cpp:7`, `:18-22`, `:29-39`), the interval of the console's info request. New: read from the device record when shown.
  - P-236, the host side: the game already hands a strength 0-255 (`InputRumble::updateRumble`, `:129-143`; `PlayerInputs.cpp:218-221`; `nebula_game_api.h:325`), quantised to 32 levels with a motor bit (`host_input.cpp:1224-1227`), while the comments in `rumble.h:3-6` and `controller_hid.h:65-71` still describe a virtual remote's on/off bit. New: the comments are rewritten and the 32 steps are checked against each device's motor resolution.
  - P-220, five places branch on `__ANDROID__`: the title starts on A or B only on Android and on any button elsewhere (`TitleSequenceProduct.cpp:234-238`), its "press A or tap" prompt (`:72-93`); the pointer guidance never asks for 2P (`StarPointerGuidance.cpp:84-88`), the 1P frame never asks for the pointer (`:150-154`) and the prompt is not set (`:61-66`) on Android. New: each follows the input in use (touch, pointer, pad) and the device's capabilities, one behaviour everywhere for the same input.
- **Where.** `native/petari/src/Game/System/PlayerInputs.cpp` (`:61-87`, `:218-221`); `native/petari/src/Game/System/Language.cpp`; `native/petari/src/Game/System/InputRumble.cpp`; `native/petari/src/Game/System/PlayerInput.cpp`; `native/petari/src/Game/System/GameSystemFunction.cpp`; `native/petari/src/Game/System/InputInfoChecker.cpp`; `runtime/include/galaxy/runtime_settings.h`; `runtime/src/runtime_settings.cpp`; `runtime/src/input/host_input.cpp` (`:1224-1227`); `runtime/include/galaxy/rumble.h`; `runtime/include/galaxy/controller_hid.h`; `native/include/nebula_game_api.h` (`:325`); `native/petari/src/Game/Screen/TitleSequenceProduct.cpp`; `native/petari/src/Game/Screen/StarPointerGuidance.cpp`. All lines as cited at 241395b49.
- **Rows.** P-234 whole; P-236 whole; P-220 whole.
- **Needs first.** nothing.
- **Old path.** none: nothing draws differently. The callback shapes, the language slots, the registry and the `__ANDROID__` branches are replaced in this step.
- **Proof.**
  1. Replay identical for P-234 and P-236 (no step reads either).
  2. P-236: before the fix, check whether the disc's `InputRumble::update` also advanced the registered instance from both objects; if so the pattern's double rate is as made, is kept, and is written as the pattern's own rate.
  3. P-236: the comments in `rumble.h` and `controller_hid.h` no longer describe an on/off bit; the 32 strength steps are checked against each device's motor resolution (from the row; the sources name no test).
  4. P-220: `git grep -n -E 'defined\(__ANDROID__\)|__APPLE__|_WIN32|TARGET_OS' <main> -- native/petari` leaves only the 25 hits that are ABI and linkage in `LumaClasses`, `LumaGuard`, `LumaHook`, `LumaNumbers`, `LumaTypes` and `NebulaModule.c` (the row: 30 hits, 25 of them those).
  5. P-220: the same input gives the same title start and the same pointer guidance on Windows with a touch screen and on Android with a pad (derived from the row's fault; no named test).
  6. P-234: a language folder of another name, in a release or added content, can be chosen in the settings (INV 5.H.3, as the row cites it).
- **For a player.** A touch screen on Windows and a pad on Android behave as the other platform does for the same input; a language added as a folder can be picked.
- **Read first.** The check in proof item 2 has no stated method beyond reading the disc's `InputRumble::update`; read the row and the code first.

### G1.3 Sticks: one radial dead zone, the Nunchuk's centre

- **What changes.** Each fault is a bullet.
  - P-073 (Windows): the left stick has a per-axis (square) dead zone for XInput and for HID pads: `normalize_xinput_axis` once per axis (`host_input.cpp:622-623`; `native_input.cpp:8`; XInput default 7849/32767, about 24%, `input_bindings.cpp:468`) and `apply_deadzone` per axis for HID pads (`switch_controller.cpp:234-235`, called from `host_input.cpp:692-694`), while SDL uses a radial one (`native_game_host_sdl.cpp:519`, `apply_radial_deadzone` in `pointer_control.cpp:63-74`). A push of 40% at 20 degrees off the x axis gives y = 0.137, inside the y dead zone, so Mario walks along x. New (audit's fix): the radial dead zone on the left stick in all three paths, the per-axis value passed as 0, the per-scheme defaults kept.
  - P-084 (every platform): every stick source passes a second per-axis dead zone and per-axis clamp before the game sees it: `nunchuk_stick_counts` applies 15 counts of dead zone per axis and full deflection at 71 counts per axis and normalises only when the length is over 1 (`native_input.cpp:84-107`), so full speed comes at about 70% on each axis. The right count suits a real Nunchuk but not pads and keys, which already had a dead zone. Then the game itself multiplies the stick by 1.5 and clamps each axis before taking its length (`Mario.cpp:1204-1213`): a stick reaching 1.0 saturates at two thirds of its travel and diagonals flatten toward a square. Owner 11-19: a modern stick's full circle is mapped onto the Nunchuk's reach; replay changes once. Every native stick (pad, keyboard, script, on-screen) goes through `nunchuk_stick` (`live_controls_engine.cpp:204-206`, `native_input.h:90`), so there is one radial dead zone and one response on every platform.
  - P-074 (Wii Remote): the Nunchuk stick takes its centre from the first report after connecting (`stick_origin_set_`, `wiimote_input.cpp:333-339`; `wiimote_input.h:76-78`), so a stick off centre at that moment stays offset until the next connection, while the calibration blocks are read, checksums and all, only for the accelerometer's zero and one g (`:238-250`). New (audit's fix): the stick centre, and the minimum and maximum for scaling, come from the calibration block when its checksum passes; the first report is the fallback only when it does not, with a re-centre after a period at rest.
- **Where.** `runtime/src/input/host_input.cpp`; `runtime/src/native_input.cpp`; `runtime/include/galaxy/native_input.h`; `runtime/src/input/live_controls_engine.cpp`; `runtime/src/switch_controller.cpp`; `runtime/src/pointer_control.cpp`; `platform/sdl/native_game_host_sdl.cpp`; `runtime/src/input_bindings.cpp`; `native/petari/src/Game/Player/Mario.cpp` (`:1204-1213`); `runtime/src/input/wiimote_input.cpp`; `runtime/include/galaxy/input/wiimote_input.h`. Lines as cited at 241395b49 or in the audit of 11-17 (moved since).
- **Rows.** P-073 whole; P-084 whole; P-074 whole.
- **Needs first.** nothing (the skeleton lists it after G1.2; it states no dependency).
- **Old path.** none: the per-axis dead zones and the game's own 1.5 gain and clamp are replaced in this step (the sources give no switch).
- **Proof.**
  1. P-084: the replay changes once, recorded as one new reference (the owner's answer); a stick at 1.0 in any direction reaches the Nunchuk's full reach; a scripted stick at 30% magnitude and 20 degrees, logging Mario's walk direction (the audit's run).
  2. P-073: the same stick position gives the same output on XInput, HID and SDL pads; a push of 40% at 20 degrees keeps its direction.
  3. P-074: a Nunchuk whose resting stick is off its calibrated centre gives zero at rest (derived from the row's fault; no named test).
- **For a player.** A stick behaves the same on every platform and its diagonals are round; Mario's speed for a given stick changes once, with the full stick circle in use.
- **Read first.** (1) Whether the game's 1.5 gain and per-axis clamp (`Mario.cpp:1204-1213`) are changed in the game or the host maps the circle so the game's code stays; the row says only "a gain of 1.0 changes Mario's speed for a given stick" and the owner's answer. Decide and write it in the row. (2) P-074: the byte offsets of the stick centre in the Nunchuk's calibration block (the audit names WiiBrew's "Nunchuk" page: x max, min, centre, then y max, min, centre, after the accelerometer values) are checked against the reference before the fix.

### G1.4 Presses latched between reads

- **What changes.** P-075: buttons are carried as levels, the newest sample replaces the last, and nothing latches a press that came and went between two game reads. Each path is a bullet.
  - SDL hosts: `sample_gamepads` reads `SDL_GetGamepadButton` levels on the main thread once per pump; the mouse buttons (`host_window_sdl.cpp:1324-1332`) and the menu keys (`set_key`, `:1452-1458`) are levels too. `take_input_state` (`:1506-1517`) copies them and latches only the on-screen buttons (`g_touch_pressed`, `:1509`). The game reads the stored levels at its step.
  - Apple: the same (`host_window_apple.mm:1089-1092` in the audit).
  - Windows HID: each report replaces `state.decoded` (`controller_hid.cpp:2116`) and the game copies the newest (`:2517-2536`).
  - A real Wii Remote: each report replaces `newest_` (`Motion::add`, `wiimote_motion.cpp:69-72`) and a read hands over the newest report's levels (`:155-171`), once per game read (`host_main.cpp:3846`).
  - New (audit's fix): accumulate "pressed since the last take" bits next to the levels, from `SDL_EVENT_GAMEPAD_BUTTON_DOWN` events, the HID worker's per-report edges, the Apple `valueChangedHandler` and the Wii Remote's reports; clear them in `take_input_state` and `read_hid_pads(consume)`. The game then sees a button down as the level or pressed-since-take, for one step.
  - Who is at risk (audit): a press shorter than about 8 ms on SDL, or that ends before the next game step on any host; fast taps and turbo inputs (spin, Star Bit shooting). The Wii read every remote report into a buffer.
- **Where.** `platform/sdl/host_window_sdl.cpp`; `platform/apple/host_window_apple.mm`; `runtime/src/controller_hid.cpp` (`read_hid_pads`); `runtime/src/input/wiimote_motion.cpp`; `native/host/host_main.cpp`. Lines as cited at 241395b49 (the host_main line has moved at main).
- **Rows.** P-075 whole.
- **Needs first.** nothing (the skeleton states no dependency).
- **Old path.** none: nothing draws differently.
- **Proof.**
  1. A scripted press of 5 ms (a macro or a test pad) counted at the game's input over 100 presses, on each platform: all 100 seen once (the audit's run).
  2. Replays unchanged (the recorded samples already hold the levels the game saw).
- **For a player.** A quick tap on a pad, key or remote is no longer lost.
- **Read first.** Nothing the sources leave open.

### G1.5 Windows: the controller worker and the pointer buttons; the parts of P-244 left

- **What changes.** P-077, P-083 and P-288 are on the Windows input path; each fault is a bullet.
  - P-077: the HID worker registers a new thread-pool wait for every report: `UnregisterWait`, then `RegisterWaitForSingleObject` with `WT_EXECUTEONLYONCE` (`disarm_io`/`arm_io`, `controller_hid.cpp:171-185`; armed after each pending read, `:1856-1859`, `:1880-1885` in the audit). Each report costs a pool-thread wake, a `SetEvent` and a worker wake (two context switches), at hundreds of reports a second over USB. New (audit's fix): one I/O completion port for all pad handles (`CreateIoCompletionPort` per handle, `GetQueuedCompletionStatusEx` in the worker loop): one wake per batch, no per-report registration; up to 60 pads also fit a direct `WaitForMultipleObjects`.
  - P-083: the worker's own comment says it never stalls on a busy radio (`controller_hid.cpp:1193-1196`) but several paths on it still do synchronous device I/O with timeouts of 30 to 200 ms, and while one runs no controller's reports are read. They are: `handle_report` calls `read_playstation_features` for a Bluetooth DualShock 4 or DualSense still sending short reports, up to 5 times, once a second (each call up to three feature-report waits of 200 ms); `send_light_bar` does one or two `write_output` calls (each `write_report` with a 30 ms wait); `stop_device_rumble` and `close_device` do `write_rumble_off` with a 30 ms wait and `finish_post(wait=true)`. A second controller's input freezes up to 600 ms while a Bluetooth DualSense connects next to it. New (audit's fix): move the feature reads to the setup thread before the device is handed over; send the light bar through `queue_output` (asynchronous); do the off report at close through the same queue, with one bounded wait at process exit only.
  - P-288, throws: `consume_host_pointer_button_transitions` throws `std::runtime_error` on a focus epoch that goes back, an invalid result, a sequence gap or a drain that does not end (`host_input.cpp:482-484`, `:510-520`, `:555-558`), while an overflow is resynchronised in place; it is called from the window's live-controls host (`:964`) and nothing catches on that path (the one catch is the device list's, `:169`), so a journal fault can end the game. New: each failure is handled as the overflow is (logged, the cursor and buttons resynchronised to the pointer's state now) and asserted in debug builds only.
  - P-288, the shake line: every shake press writes a line to unbuffered `std::cerr` inside the sample, in release builds too (`live_controls_engine.cpp:533-537`). New: it goes through the buffered log or a trace build only.
- **Where.** `runtime/src/controller_hid.cpp`; `runtime/src/input/host_input.cpp` (`:169`, `:482-484`, `:510-520`, `:555-558`, `:964`); `runtime/src/input/live_controls_engine.cpp` (`:533-537`). Lines as cited at 241395b49 or in the audit of 11-17.
- **Rows.** P-077 whole; P-083 whole; P-288 whole; P-244 closes here, with the parts below.
- **Needs first.** nothing.
- **Old path.** none: nothing draws differently.
- **Proof.**
  1. P-288: each of the four invariant failures is handled without throwing; a debug build asserts; replay identical.
  2. P-288: a search for `std::cerr` in `live_controls_engine.cpp` returns nothing in a release build (derived from the row, not a named test).
  3. P-077: ETW context-switch counts for `nebula-controller-hid` and the thread pool's wait thread, with a DualSense on USB, before and after (the audit's run).
  4. P-083: two pads; connect a DualSense over Bluetooth and log the gap between the other pad's report times on the worker: no gap near 600 ms, none at a light-bar write or a disconnect (the audit's run).
- **For a player.** A controller's input on Windows does not stall on a busy radio and a pointer-button fault no longer ends the game.
- **Parts closed here (plan decision).** The parts of P-244 that no other step of this package places:
  - [ ] the rules named by the row's report, read whole and moved to the fixed space of G1.1: `FireBubble` (over 10), `StringSpider` (at 20), `Karikari`'s flick and release;
  - [ ] `MarioLauncher.cpp:502-516` (shoots along the world ray through the pointer's screen position in the current view) and `BigBubble.cpp:907-921` (`calcStarPointerWorldPointingPos`: the same pointer position pushes the bubble another way at another aspect): read whole; each takes the fixed space or S0.2.2's scene query, whichever the rule needs, and the choice is written in the row;
  - [ ] `StarPieceDirector.cpp:519-546` (the star piece shot's goal comes from the picture's depth under the pointer; N-1068): placed with S0.2.2 and S0.2.3 (P-294); here only the check that no screen-size number is left in it.
- **Read first.** Whether a caller further up catches the P-288 throw is read with the fix (the row says so).

### Rows of this package

| Row | Today | After | Closes at |
| --- | --- | --- | --- |
| P-282 | The input record keeps the Wii Remote's shape: tilt as the remote's gravity, a sensor-bar roll, a virtual remote's distance; every device is rewritten into it, and a pointer goes off where a remote would not face the bar. | A tilt record in the game's terms (lean from the player's neutral), roll, distance and facing optional and only from a real remote; the game's dead zones kept; no remote emulation (owner 11-19). | G1.1 |
| P-279 | Android, Linux and Apple give the gyro pointer a fixed 16:9; Windows and the Wii Remote pointer take the picture's shape, so the same turn reaches the edge at different points. | Both hosts take `galaxy::latched_content_aspect()`: one source for every pointer. Replay identical. | G1.1 |
| P-244 | The pointer's position scales by the display's width and its speed is the difference of two positions; fireball, bell and cocoon rules compare it with fixed numbers, so the same motion plays differently by screen. | Position and speed measured in one fixed space, the layout's reference space with equal units (console height units, tuned numbers keep their meaning). Replay changes on other screens. | G1.5 |
| P-234 | A pad's connection arrives as the remote kit's callbacks; the language is the console's ten slots with the last three English; the host knows only seven languages, other folders are Auto. | The host reports devices and parts as state; the host names the language by id or tag; the list is built at load from the store's language folders. Replay identical. | G1.2 |
| P-236 | Rumble runs through a motor-callback registry; the registered rumble steps twice a step and the other never; the battery is copied every 300 seconds of steps. | Each pad steps the rumble in use once a step, no registry; battery read when shown; 32 strength steps checked against each device; comments rewritten. | G1.2 |
| P-220 | Five places branch on `__ANDROID__`: title start, its prompt, and three pointer-guidance prompts, so a touch screen on Windows or a pad on Android gets the other platform's behaviour. | Behaviour by the input in use (touch, pointer, pad) and the device's capabilities: one behaviour everywhere for the same input. | G1.2 |
| P-073 | On Windows the left stick has a square per-axis dead zone for XInput and HID pads; the SDL hosts use a radial one. A small diagonal push snaps to a straight line. | The radial dead zone on the left stick in all three paths; per-scheme defaults kept. | G1.3 |
| P-084 | Every stick passes a second per-axis dead zone and clamp, and the game multiplies by 1.5 and clamps each axis, so full travel is reached at two thirds and diagonals flatten. | Every native stick goes through `nunchuk_stick`; the full circle maps onto the Nunchuk's reach (owner 11-19). Replay changes once. | G1.3 |
| P-074 | A Nunchuk's stick centre is the first report's, though the calibration blocks are read for the accelerometer; an off-centre stick stays offset until the next connection. | The centre (and scaling) come from the Nunchuk's calibration when its checksum passes; the first report is the fallback, with a re-centre at rest. | G1.3 |
| P-075 | Buttons are levels; the newest sample replaces the last, so a short press can be lost (SDL pads, mouse and menu keys, Apple, Windows HID, a real Wii Remote). | "Pressed since the last take" bits beside the levels, cleared at each take; the game sees level or pressed-since-take for one step. | G1.4 |
| P-077 | The Windows HID worker registers a new thread-pool wait for every report (unregister, then register): two context switches a report. | One I/O completion port for all pad handles: one wake per batch of reports. | G1.5 |
| P-083 | Input can stall on Windows: synchronous device I/O of 30 to 200 ms on the HID worker (feature reads, light bar, rumble off) freezes every pad's reports, up to 600 ms. | Feature reads on the setup thread; the light bar and the off report through the asynchronous queue; one bounded wait at exit. | G1.5 |
| P-288 | The Windows pointer-button journal throws on four invariant failures, uncaught on that path, and every shake press writes to unbuffered `std::cerr` in release builds. | Each failure handled as an overflow is (logged, resynchronised), asserted in debug only; the shake line through the buffered log or a trace build only. | G1.5 |


---

## G2 How a picture reaches the display

Today the in-between pictures (frame interpolation) start a step late, the real picture of a step is almost never shown on a refresh, the renderers do not ask the display when a picture will show, Linux has no display clock, two controllers act on the same GPU time without knowing of each other, the pointer and the menu cursor are as old as their step, camera cuts come from two places, and the default frame rate is 60. This package makes the schedule start each sequence at its arrival, puts the real picture on a refresh at whole multiples of 60 Hz, gives every platform a display model, removes the closing spin, replaces the governors by one arbiter that acts only by resolution, draws the pointer at the picture's moment, states every cut, and keeps the default frame rate at 60 on every system, a player choosing a higher rate (the owner, 11-22: "should be 60fps"). A player gets less delay between a press and the picture, even pacing on every display at any rate the player picks, a pointer that follows the hand, and no blend across a door or a pipe.

- **Design:** FM 2.16 (landings 1 to 9, its "Rows" table, "Proof"), with FM 1.1, 1.3 and 1.5 (the threads, the render loop, what the game thread waits for) and FM 2.1 rules 1, 2 and 5. The rows' notes (read 11-18, placed 11-19 by FM 2.16) hold each row's own design and proof; the measures are file 00 4.3 (`NEBULA-V2-PLAN/00-READ-FIRST.md`) and audit files E and F (`docs/audit-2026-11-17/E-pacing-interpolation-as-built.md`, `F-interpolation-and-frame-unlock.md`). Rows whose ledger text is cut short are completed from audit file B (`B-findings-port.md`: findings P-019, P-034, P-042, P-078, P-163) and audit file E (the "L. Findings" part: I-002 to I-014). The owner's decisions in FM 2.16: a device that cannot hold 120 is never switched to 60 by itself; the 59.94 lock at 0.2%; the default stays 60 (11-22, landing 9).
- **Needs first:** nothing for G2.1 to G2.5 and G2.7 beyond the order of the landings; G1.1 before G2.6; R5 and R7.2 before G2.8; G2.9 last, after the measures hold. G2.1 to G2.5 stand at places 23 to 27 of `steps.tsv`, directly after R1 and R2; G2.6 and G2.7 stand at 35 and 36, after G1.
- **Order (the owner, 11-23: "move all android pacing issues and any dependencies on native it might have to before audio").** R1 and R2 stand first because a pipeline made inside a draw (R1.2) and a sampler table that starts again and waits for the whole graphics card (R2.2) would each be counted by the pacing measures as late moments and mended under them; G1 stands before G2.6 only (G2.6 samples the pointer from G1.1's record), so G2.1 to G2.5 name no need of it and the surround steps (G6.2, G6.7) follow them at 28 and 29. Nothing a later step does writes landings 1 to 5 again: R7.7 takes one wait out of `svc_frame_begun` and gives the output's shape to the frame, G8.2 reads the pacer's interval, G2.8 adds the unseen walk's removal at the rates landing 2 leaves with lead 0.
- **Rows:** 27 (I-002, I-004, I-005, I-006, I-007, I-010, I-012, I-013, I-014, N-1084, P-019, P-034, P-042, P-078, P-163, P-274, P-281, P-311, P-312, P-313, P-314, P-315, P-316, P-317, P-318, P-363, P-364). I-003 closes at G2.8; its row is in R5's rows file, not here.
- **Any refresh rate, whole or not (plan decision, the owner 11-20; again 11-23: "any arbitrary refresh rate so 75 or 82 even would work perfectly, along with a perfected variable refresh rate implementation"):** in-between pictures must be right at every refresh rate, not only at multiples of 60. Each picture is drawn for the moment it will be shown (landing 3), so any rate and variable refresh are covered by design; the code holds no list of rates and no rate has a branch of its own. Landing 2 only adds one thing at exact multiples of 60 (120, 180, 240, 360, 480 Hz): one of the pictures shown is the real game frame. The proof of landings 1 and 3 therefore includes, in `frame_schedule_tests.cpp`, a test over many rates from 40 to 500 Hz: pictures evenly spaced, each at the moment it is shown, none shown twice, none skipped. The test's rates are sample points, not a list the game knows: 40, 48, 50, 59.94, 60, 72, 75, 90, 100, 119.88, 120, 144, 165, 180, 200, 240, 360, 480 and 500 Hz; a few hundred random rates between 40 and 500 Hz, most not whole numbers (82.37 Hz, say), from a fixed seed; and a variable-refresh range (the sequence of intervals a variable display gives between its lowest and highest rate). The measures on a real display are the PC's monitor at the modes its system display settings list (the tour's rates 60, 120, 144, 165 and 240 Hz where it lists them; the game does not set a display's rate, its REFRESH RATE row says "set in the system display settings") and the tablet at 120 and 60 Hz.
- **Which clock paces the step and which the pictures, at any rate (read at `step_lock.h`, `frame_phase.cpp`, `vsync_grid.h`).** The step follows the display only where a whole number of refreshes makes one step within 0.02% with in-between pictures on (0.2% with them off): 60, 120, 180, 240, 360 and 480 Hz, and 59.94 and 119.88 Hz with them off (`step_lock_refreshes` is then 1, 2, 3, 4, 6 or 8). At every other rate, whole or not (72, 75, 90, 100, 144, 165, 200, 500, 82.37 Hz), `step_lock_refreshes` is 0 and the step stays on the host's own clock at exactly 60 steps a second: Windows `FramePhase::next_start` returns the step's period, the SDL hosts' `wait_display_frame` (through `VsyncGrid::next_slot`) and Apple's return false and the host's `PreciseWait` runs the step. The display clock then paces only the pictures: each is drawn for the refresh moment it will be shown and each step's own frame is walked and not shown until G2.8 (one unseen walk a step; it costs speed, not evenness). Below 60 Hz (40, 48, 50) the step is the same and the display shows pictures at its own rate.

### G2.1 Landing 1: a sequence starts at its arrival; the step keeps its due times

- **What changes.** Each row is a bullet (plan decision; the step closes when the last is done).
  - I-004: today a list that shows is held (`interp_list_waiting_`) until the next step asks for its present (`svc_frame_begun` calls `submit_frame(true, black)`), and only then does its sequence start (`run_pending_interp_start`, `interp_start_pending_`), with the present's asked moment as its arrival. The only reason to wait is black, which `NebulaFrameClock::show` sets only around a restart. New: `svc_submit_frame` marks the list's chunk shown when the frame clock shows a picture and the start is not skipping frames (`gBootFast`); such a chunk starts its sequence when its walk is done, its reference being its own step's moment plus the step clock's tracked delay; the next frame's present supersedes it only when it is black; the held list and the present-only gate go and the next present request only feeds the cadence count.
  - N-1084: a slot between a frame's present and its arrival gets `t` below 0, clamped to 0, so it shows the frame before, early (about one frame in seven at 120 Hz, found by `frame_schedule_tests.cpp`). New: a slot before the reference shows the sequence before at its own `t`, not the frame before, early.
  - P-274: when the step's wait returns late, `next_frame_start` sets the next step a whole period from now, so each late wake drops its lateness; the step clock keeps its due times and runs missed steps back to back up to a bound (a quarter second, say), then drops the rest with a log line.
  - I-013: `next_frame_start` takes a step's work as `g_last_present_ns` minus the previous step's start, and the render thread stamps that only for a real picture, so with in-between pictures on the phase aims at the arrival delay, not the step's own work; the render thread records, per frame index, when that step's list was taken and its picture rendered (beside `SubmitEnd` and `RenderSubmitted`), and `next_frame_start` reads the entry of the step that started at `previous_ns`.
- **Where.** `runtime/src/render/render_core.cpp` (`start_interp_sequence`, `interp_list_waiting_`, `interp_start_pending_`, `run_pending_interp_start`, `note_picture_shown`, `g_last_present_ns` at `:527`); `runtime/src/render/frame_schedule.cpp`, `runtime/include/galaxy/render/frame_schedule.h` (`FrameSchedule`, `ScheduleDisplay`); `runtime/src/render/frame_schedule_tests.cpp`; `native/host/host_main.cpp` (`svc_submit_frame`, `svc_frame_begun`, `svc_frame_wait`, `next_frame_start` at `:1091`); `native/petari/include/Game/System/NebulaFrameClock.hpp`; `runtime/include/galaxy/frame_timeline.h`. `FrameSchedule` and its tests are on main; FM cites the lines of the branch `rows/i016` (697535a86), so take lines from main.
- **Rows.** I-004 whole; N-1084 whole; P-274 whole; I-013 whole.
- **Needs first.** nothing.
- **Old path.** The landing's own switch (its name is not given in the sources) keeps the held list and the present-only gate beside the new start; this step deletes them once its measures hold.
- **Proof.**
  1. `frame_schedule_tests.cpp` on simulated displays at every rate of file E A.5 and at every sample rate of the head's "Any refresh rate" paragraph (the named rates, the random ones that are not whole numbers, the variable-refresh range), under steady, varying and spiking load: a sequence starts at its arrival; N-1084's early count becomes zero.
  2. A unit test of the I-013 measure: a step rendered at +5 ms whose real picture is shown at +30 ms gives work 5 ms (before: 30 ms or 0).
  3. `tools/gate/pace.py` at 240 Hz with in-between pictures on: `delay_avg_us` before and after (file F: 42.4 ms to about 26); the timeline's `RenderBegin` to first present; at 120 Hz the timeline's `StepWaitEnd` against `VBlank`.
  4. Pictures and replays the same in fixed-step tests; a late wake gives the same steps with different wall times (P-274: replay identical).
  5. `git grep -n -E "interp_start_pending_|interp_list_waiting_|run_pending_interp_start" runtime` returns 0 (FM's proof after landing 1).
  6. The game step's time and the walk's, on the PC and the tablet, before and after: never slower.
  7. The hosts' step-wait decision as a unit at every sample rate of the head (the pure parts: `step_lock_refreshes`, `FramePhase::next_start` in `frame_phase_tests.cpp`, `VsyncGrid::next_slot`, the decision inside `wait_display_frame`, in `vsync_grid_tests.cpp`), with in-between pictures on and off: at 60, 120, 180, 240, 360 and 480 Hz (59.94 and 119.88 Hz with them off) the step follows the grid at 1, 2, 3, 4, 6 and 8 refreshes and its slot lands on a vblank; at every other rate, whole or not, the decision is the host's own clock at exactly 60 steps a second and the pictures' slots are still the display's vblanks. The test fails if a rate outside the multiples locks the step, or a multiple does not.
- **For a player.** With in-between pictures on, the picture shows the game about a step sooner (measured 17 to 23 ms late today). At a restart the last list before black can be shown for up to a step; the console never showed it.
- **Read first.** (1) The bound for missed steps is "a quarter second, say" in P-274's row; fix the number here and record it. (2) The sources disagree on a chunk that is not marked: I-004's row says it "waits as today"; FM 2.16 target 1, later, says the held list and the present-only gate go. This card follows FM; record the choice in the row.

### G2.2 Landing 2: the lead at whole multiples of 60 Hz

- **What changes.** Today a lead that puts `t = 1` on a slot is given only off vblanks (`locked = !on_vblanks && rate a multiple of 60`); on vblanks the lead is 0 and the slots follow the display, so `t = 1` falls between refreshes and the next sequence's start walks the real frame without showing it: 5 to 13 of 2,640 steps are shown and every step costs one extra unseen walk (audit file E, finding I-002: at 120 Hz with VSync on a step costs 3 walks for 2 presents, at 144 Hz about 3.4 walks for 2.4; every renderer with VSync on is affected). The step is already phased to the display on every host (Windows `FramePhase` in `svc_frame_wait`; the SDL hosts `wait_display_frame`). New: at a refresh of k times 60 within 0.2% (`vblanks_per_slot` 1 and rate / 60 whole) with the step locked to the display (`step_lock.h`), the sequence takes lead 1/k with its slot clock anchored at the vblank nearest the arrival, so slots show `t = 1/k .. 1`, the last the real picture, and no walk is thrown away at 120, 240 and 360 Hz. Other rates keep lead 0 until E4's named last real frame removes the unseen walk (FM 2.15; the skeleton's R7.2 carries the names).
- **Where.** `runtime/src/render/frame_schedule.cpp` (`locked`, the lead and slot clock in `next`); `runtime/src/render/render_core.cpp` (the next sequence's start); `runtime/include/galaxy/step_lock.h`; `native/host/host_main.cpp` (`svc_frame_wait`); `platform/sdl/display_clock.cpp` (`wait_display_frame`); `runtime/src/render/frame_schedule_tests.cpp`.
- **Rows.** I-002 whole.
- **Needs first.** G2.1 (the row: I-013 first, so the phase holds under in-between pictures).
- **Old path.** The landing's own switch keeps lead 0 on vblanks; this step deletes it once its measures hold.
- **Proof.**
  1. `frame_schedule_tests.cpp` at 120, 180, 240, 360 and 480 Hz with the step locked: a refresh lands on `t = 1`; at the head's other sample rates, whole or not, no slot is placed to land on `t = 1` and the lead stays 0 (the test fails if a lead is given at a rate that is not a whole multiple of 60).
  2. Real pictures shown, counted as file F section 1A did, at 120 and 240 Hz near every step (today 5 to 13 of 2,640); walks per step k (today k + 1).
  3. `pace.py`'s `animation_within_1ms` unchanged.
  4. The game step's time and the walk's, on the PC and the tablet, before and after: never slower (FM 2.16's proof, each landing).
- **For a player.** At 120, 180, 240, 360 and 480 Hz every step's real picture is shown on a refresh; the game's own frames are no longer skipped. At any other rate, whole or not (75, 100, 144, 165 or 82 Hz), the pictures are evenly spaced once landing 3 is in, and one unseen walk of the list per game step stays until landing 8: that costs speed, not evenness.
- **Read first.** Nothing the sources leave open.

### G2.3 Landing 3: a display model, one platform at a time

- **What changes.** `ScheduleDisplay` answers two more questions through each platform: when a picture presented now will be shown, and when the last one was shown. The schedule then picks each picture's `t` from its predicted display time and corrects its lead from the actual one. Each platform is its own piece, with the `[pace]` measures before and after (the pacing recorder writes them every ten seconds, `pacing_recorder.h`):
  - Windows (P-312, P-311): the frame statistics already read after every present (`DisplayStats`, `dxgi_pacing.h`) feed the model, and the lead adapts as Android's does (`present_slots`), so a picture starts as late as it safely can. M2 is traced first with those statistics (the latency object's wait, the flip queue or the compositor): frame latency 1 and a wait on its object before each present (`renderer_d3d12.cpp` `:3356`, `:4405-4413`; `runtime/src/render/d3d11/renderer_d3d11.cpp` `:308`); the change shows a picture at the first refresh after its slot, as on Vulkan. P-311's code is aba64e331 (`present_sync_interval`, `renderer_d3d12.cpp:4305-4331`, `renderer_d3d11.cpp:622`); its after-measure at 240 Hz is the `[pace]` measure of file 00 4.3.
  - Vulkan and Android (P-317, P-281, and the present-at-a-requested-time half of I-012): Vulkan present timing asks each picture's time where the surface offers it (the check at `renderer_vk.cpp:1034-1058`, today for reports only), else present wait as now (`:2276-2299`). Android adds present timing beside the display timing extension (`:2361-2398`), each picked at start by what the device offers; its lead at 60 Hz (three refreshes, 50 ms, P-317) comes down to what the device holds once its GPU clock is held (G2.5): file 00 4.3 asks two. P-281 has four faults, each its own item here and the row closes with the last (plan decision):
    - the event pump sleeps a fixed 8 ms with no event waiting (`pump_events(8)`), so touch, pad and window events and queued requests wait up to that long; new: a block that an event or a queued request wakes (an SDL event watch signalling a condition);
    - the display clock's sleep retries on any error of `clock_nanosleep`, not only `EINTR` (`display_clock.cpp:155-157`), so an `EINVAL` spins its thread for good; new: retry only on `EINTR` and otherwise hand back to the host's clock;
    - the surface hand-over goes on after 2 s with the renderer still holding the surface (`host_window_sdl.cpp:1670-1679`; the renderers check `surface_withdrawn` before each acquire, `renderer_vk.cpp:2991`, `:2996`); new: past 2 s the device-lost path runs rather than the surface going under a present;
    - the Wii Remote link reads its JNI table unlocked (`wiimote_link_android.cpp:73`, `:80`, `:117-118`, `:130`, `:133`) while `nativeInit` writes it under `s.lock` (`:175-188`); new: the table is published once through an atomic pointer, release on write, acquire on read.
  - Linux (I-005, and P-281's event pump and surface hand-over, which sit in `host_window_sdl.cpp`, the Android and Linux host, so Linux takes the pump's block with Android): the display clock comes from the presents. The Vulkan renderer already makes its swap chain with present ids and present wait; a waiter thread stamps each present's completion, the stamps are fitted to the mode's period (`SDL_GetCurrentDisplayMode`, refined by the median spacing) into the vblank grid `display_clock.cpp` hands the slots, as the Choreographer's are on Android; where `VK_EXT_present_timing` is offered its times take that place; no grid keeps today's clock. Until one runs, the settings mark in-between pictures on Linux as not synchronised.
  - Apple (I-006, P-034): the newer Metal display link, which gives each frame its target presentation time, replaces the display clock and paces the render thread; the drawable is presented at that time and the rate is asked as a range. The drop on a drawable wait goes: today a frame whose `nextDrawable` took over `kDrawableWaitMs` (1.5 ms) is not handed over (`held_last_`); instead drawables handed over and not yet presented are counted (up at `presentDrawable`, down in the presented handler, a presented time of 0 included) and a picture is held only when the count is 2 or more as the drawable comes. A dropped in-between picture today leaves a refresh showing the previous one (audit file E, I-006: macOS and iOS with in-between pictures). The names and versions are taken from Apple's documentation in the landing.
    - P-034: the display link's hold (17ad0a4cd, `set_display_clock_held`: macOS stops the `CVDisplayLink`, iOS pauses the `CADisplayLink`, on the main queue) is on main; its proof is owed. The audit's finding adds that on iOS the link asks the display's fastest rate with minimum, preferred and maximum all equal, so a ProMotion display is held at 120 Hz while the game makes 60 pictures; new: 120 only when in-between pictures will fill it, else 60 with a range up to 120 (this is the "rate asked as a range" above; audit file B, finding P-034).
  - Variable refresh (I-007): told by the system where it says so (Vulkan present timing or display timing; Apple the display link's minimum and maximum rates), and otherwise proved from the presents on the display clock's own vblank waits with no in-between picture in flight. With presents held at a rate between the mode and half of it (`variable_refresh_cap`) a fixed display shows each picture for whole mode periods (DXGI `PresentRefreshCount` steps of 1 and 2), a variable one for the presents' own interval. The test runs on the frame statistics in the first seconds after a display change and its answer holds until the next. When variable, the slots follow the arrival clock at the cap (the present pacer's QPC clock), presents spaced evenly by the precise wait, VSync on with tearing allowed above the range. It replaces `next_variable_refresh`'s 0.8 test on Direct3D 12, Direct3D 11 and Vulkan.
  P-313 (M3): on Vulkan, at a scene restart, one picture reaches the display 50 to 60 ms late and one step runs long (18.4 and 24.6 ms); traced with the timeline at a restart, then fixed where it lies.
- **Where.** `runtime/include/galaxy/render/frame_schedule.h` (`ScheduleDisplay`); `runtime/src/render/frame_schedule.cpp`; `runtime/src/render/dxgi_pacing.h`; `runtime/src/render/renderer_d3d12.cpp`; `runtime/src/render/d3d11/renderer_d3d11.cpp`; `runtime/src/render/vk/renderer_vk.cpp`; `runtime/src/render/mtl/renderer_mtl.mm` (`kDrawableWaitMs`, `held_last_`); `platform/apple/display_clock_apple.mm`; `platform/sdl/display_clock.cpp`; `platform/sdl/host_window_sdl.cpp` (`pump_events`, the surface hand-over); `platform/sdl/wiimote_link_android.cpp`; `native/host/host_main.cpp` (the `pump_events(8)` call); `runtime/include/galaxy/render/renderer_common.h` (`next_variable_refresh`, `variable_refresh_cap`); `runtime/include/galaxy/frame_timeline.h`; `runtime/src/frame_timeline_tests.cpp`. Lines as cited at 241395b49 (host_main's have moved at main).
- **Rows.** I-005 whole; I-006 whole; I-007 whole; I-012 (the present-at-a-requested-time half; the timeline half is on main, see Read first); P-281 whole; P-312 whole; P-317 (the Android lead after the clock is held, re-measured at G2.5); P-311 whole (the after-measure); P-034 whole (the Mac job's start check); P-313 whole; P-363 (part): proofs 5 and 8 are the simulated and the decision's half, the row closes only with a run on a real display (owed, `PLAN owed`).
- **Needs first.** nothing beyond the order of the landings; the timeline on every platform (I-012's first half) is on main.
- **Old path.** One switch per platform piece, the old path beside it until that piece's measures hold: the 0.8 variable-refresh test, the drawable-wait drop, the Linux host clock, Direct3D's present with no asked time. This step deletes them platform by platform.
- **Proof.**
  1. `frame_schedule_tests.cpp`: every slot's `t` is its predicted display time's, at every sample rate of the head's "Any refresh rate" paragraph (the named rates, the random rates that are not whole numbers, the variable-refresh range); the variable-refresh decision as a unit (statistics in, answer out) with fixed and variable sequences (I-007, I-016's scheduler tests).
  2. `[pace]` of file 00 4.3, recorded by the game: the PC with Direct3D 12, Direct3D 11 and Vulkan at 60, 120, 144, 165 and 240 Hz, VSync on and off; the tablet at 60 and 120 Hz in every tour stop (it holds in all 102 at 120 Hz). Direct3D at 240 Hz shows a picture at the first refresh after its slot (P-312, P-311). The PC's rates are the modes its monitor lists in the system's display settings (the game sets none: its REFRESH RATE row says "set in the system display settings"); the step reads that list first and records it with the run, and a mode other than the monitor's current one is the owner's to set (`PLAN owed`): a rate not run is said in the log as not measured, never as held. A rate the monitor does not list is proven by the simulated display of proof 1, as proof 8 says for Android.
  3. Linux (I-005) at 144 Hz with present-stats: the vblank-steps histogram filled, no two pictures in one refresh over 3000 pictures; a unit test of the fit fed stamps with jitter and a missed refresh, at the head's sample rates.
  4. Metal (I-006): a Mac run with `--diagnostics timeline` and a test hook that delays the render thread by 2 ms every 30th frame: `pictures_held_` is 0 before the count says the line is full, and the start's queued pictures are still drained. P-034: the Mac job's start check (Metal) extended so that after 300 frames it holds the clock for 1 s and counts `on_vsync` callbacks (0 expected), then lets go and sees them again; it fails before 17ad0a4cd. Linux and Apple are built by the CI jobs; their measures are the owner's runs.
  5. Variable refresh, on both displays we have, each named, with the timeline (`VBlank` against `PresentEnd`), the variable state, and even spacing of new pictures at 144 Hz with two in-between pictures: (a) the PC's monitor (an Odyssey G8 by its own name, 3840 by 2160 at 240 Hz; whether its adaptive sync is switched on in the driver is not read anywhere in the files, so the step reads the state first, records it, and if it is off asks the owner through `PLAN owed` and says the proof was not made); (b) the tablet's 120 Hz panel on Vulkan (it lists 120, 60 and 30 Hz modes: whether it varies continuously or only switches between modes is read first and recorded, and the proof says which it was). The same decision as a unit (proof 1) stands for Direct3D 12, Direct3D 11, Vulkan, Linux, Android and Apple. No variable-refresh display of ours exists for Linux or the Mac (P-363): the decision's unit test is their proof until one does.
  6. P-281: replay identical. P-313: no hitch over one refresh at a scene's start from the second run on (file 00 4.3).
  7. The game step's time and the walk's, on the PC and the tablet, before and after: never slower (FM 2.16's proof, each landing).
  8. An Android display at a rate that is not a multiple of 60: the tablet lists 120, 60 and 30 Hz and no other mode, so no device of ours shows one, and it is proven by our own code fed that rate, as proof 3 does for Linux. The Android display clock's grid (`VsyncGrid` in `vsync_grid.h`, fed by `display_clock.cpp`'s Choreographer callbacks and `on_refresh_rate`) is fed vsync stamps at every non-multiple sample rate of the head (75, 82.37, 100, 144 Hz and the random ones), with jitter and a missed refresh, in `vsync_grid_tests.cpp`: its interval settles on the rate's period, `next_slot` answers the host's clock (the step is not locked) and the slots it gives stay on the grid; then `frame_schedule_tests.cpp` takes that grid and every slot's `t` is its predicted display time's. What this does not show is the system's side (Choreographer, display timing and the preferred-rate call at that rate): P-363 stays open until a run on a real Android display at such a rate exists.
- **For a player.** A picture reaches the display at the first refresh after its moment on Direct3D, Vulkan and Android; Linux and Mac get even pacing; variable-refresh monitors and ProMotion panels show evenly spaced pictures; touch and pad events on Android are no longer held up to 8 ms.
- **Read first.** (1) I-012's timeline half is already on main (`frame_timeline.h` states every platform, with `frame_timeline_tests.cpp`, commit 71e31e86e): read the row against it and close that half; only presenting at a requested time is left (plan decision). (2) P-317's own note names landings 3 and 5; the skeleton and FM's table place it at landing 3, so it closes here and its measure is taken again at G2.5 (plan decision). (3) M2's cause is read from the statistics before anything changes; the sources do not say which of the three it is. (4) R1 and R2 stand before this step (places 15 and 19): every pacing measure on the tablet is recorded with R1.2's count of pipelines made by draws after a stage's first picture ("made by draws first", a counted timeline event, R1.2's proof 1) and R2.2's log line `[samplers] ... starts again` (R2.2's proof 4); a tour stop where either is above 0 is measured again, since a pipeline built in a draw or a sampler table restarted is a late moment that is not the display model's. If R1.2 or R2.2 is not on main when the measure is taken, the measure says so.

### G2.4 Landing 4: no closing spin where a display time is asked

- **What changes.** Today a wait to a slot sleeps and then spins its last margin, which follows the timer's lateness between 100 us and 2 ms (`precise_wait.h:38-50`, `:72-86`); M4 measured the render thread at 163 ms of CPU a second at 120 pictures a second on the PC (99 ms at 240, where it sleeps in the present), and nothing was measured on a phone, where it costs battery and heat. New: where a display time is asked (Vulkan present timing, Android display timing, Metal's display link) the render thread starts a picture its lead ahead and the display holds it to its time; the closing spin of `PreciseWait` stays only where no time can be asked. With VSync off and a cap, `PresentPacer` still sleeps up to 2 ms (`renderer_common.h:181-202`); the sources do not change it.
- **Where.** `runtime/include/galaxy/precise_wait.h` (`PreciseWait`); `runtime/src/render/frame_schedule.cpp`; `runtime/src/render/vk/renderer_vk.cpp`; `runtime/src/render/mtl/renderer_mtl.mm`; `platform/apple/display_clock_apple.mm`; `runtime/include/galaxy/render/renderer_common.h`. Lines as cited at 241395b49.
- **Rows.** P-314 whole.
- **Needs first.** G2.3 (the display time must be asked first).
- **Old path.** The landing's own switch keeps the spin beside it; the spin is deleted where a time is asked once the measures hold, and stays elsewhere.
- **Proof.**
  1. The render thread's CPU time a second on the tablet before and after (FM 2.16's proof).
  2. `[pace]` measures unchanged on the PC and the tablet (file 00 4.3).
  3. The game step's time and the walk's, on the PC and the tablet, before and after: never slower (FM 2.16's proof, each landing).
- **For a player.** A phone's battery and heat: the render thread no longer spins for each picture. Nothing else visible.
- **Read first.** The tablet's figure is not yet measured; take the "before" first.

### G2.5 Landing 5: one arbiter, the clock held, the heat input

- **What changes.** Each row is a bullet, then the design that closes them all (plan decision).
  - I-010: today the GPU governor (`FrameSchedule::gpu_governor`) fits the in-between pictures into nine tenths of a step by a GPU reading, the cadence governor drops them when they cost the game its steps and tries again with back-off, and dynamic resolution aims the scene's GPU time at `resolution_budget_ms(present_frame_rate(cap, VSync, refresh))` (`resolution_governor.h`). None reads the other (I-010): with k capped the pictures come at 60 x (k + 1) while resolution budgets 1/refresh, so it falls too far and then k rises, a slow loop.
  - P-318: nothing acts on what is shown (in the heaviest tour stop at 120 Hz, 72 uneven pictures a second, 42% on time, 318 late moments).
  - P-315: the GPU reading follows the clock the governor's own decision left (in-between pictures switched off on a reading of 10 to 13 ms against 5 and did not come back).
  - P-019: the performance hint's target is the game step (`host_main.cpp:1191`, `perf_hint.h:4-9`) and nothing in play reads the device's heat: no `AThermal_*` call, no thermal listener (audit file B, finding P-019; the hint's own faults, wall time instead of CPU time and its threads, are P-069's, outside this package). The audit's input on Android: register `AThermal_registerThermalStatusListener` (API 30 and up, so available at the app's minimum of 31), or poll `AThermal_getThermalHeadroom` (API 31) once a second off the frame threads, and feed it to the arbiter.
  - P-316: when the cadence guard cuts the in-between pictures, the 60 pictures a second drift off every second refresh.
  - New, for all five rows: one arbiter in the schedule, pure and under its tests, owns k and the resolution against one budget, the slot period in use: the display's rate is filled first (k up to refresh / 60 - 1) at no less than the resolution floor, then resolution rises in the time left; over budget, resolution falls to the floor before k falls. Its inputs are the share of pictures on time and the late moments, the wall time from picture to picture against the slot, the GPU time, and the device's thermal headroom. Its order is resolution first, then nothing else; the one reduction of pictures that stays is the cadence guard that protects the 60 steps, and when it acts its pictures are placed on every n-th refresh by the display clock (P-316). The performance hint holds the clock (its target the slot, told the wall time of a picture's CPU and GPU work with the newest calls the system has), no decision reads a GPU time taken under a clock its own decision left, and a return is judged by trying it, with back-off. A target missed for a sustained time is said in the settings with what to change; nothing switches by itself (the owner's decision), and the first start's measure picks a target the device holds.
- **Where.** `runtime/src/render/frame_schedule.cpp` (`gpu_governor` at `:160`, the cadence governor from `:214`, as FM cites them on `rows/i016`, 697535a86); `runtime/include/galaxy/render/resolution_governor.h`; `runtime/src/render/renderer_d3d12.cpp` (dynamic resolution, once per scene picture); `native/host/host_main.cpp` (`:1191`, the hint's target); `runtime/include/galaxy/perf_hint.h`; `runtime/include/galaxy/pacing_recorder.h`; `runtime/src/render/frame_schedule_tests.cpp`. Other lines as cited at 241395b49. The row text of I-010 names `interp_gpu_governor` in `render_core.cpp`; at main it is `FrameSchedule::gpu_governor`.
- **Rows.** I-010 whole; P-315 whole; P-316 whole; P-318 whole; P-019 whole (the heat input; its own note says the hint's fault is P-069, outside this package).
- **Needs first.** G2.3 (the hint's clock and the Android lead are named with the display model in FM 2.16 target 5).
- **Old path.** The landing's own switch keeps the two governors beside the arbiter; this step deletes them once its measures hold.
- **Proof.**
  1. `frame_schedule_tests.cpp`: the arbiter lowers resolution before anything else; never reads its own clock's effect (a simulated GPU whose time rises when it idles); places capped pictures evenly; returns by trying.
  2. I-010: a controller test fed measured costs (a heavy scene at 144 Hz gives one stable (k, sixteenths), no alternation over 3000 pictures); then the run the audit asks for, `[dynres]` and `[interp-stats]` `gpu-generated-cap` on a heavy galaxy at 144 Hz.
  3. P-316: on the tablet at 120 Hz with the cap, the 60 pictures a second keep every second refresh (today 120 of 120 at first, then 24 and 24 of 120 after twenty seconds).
  4. P-315: in-between pictures do not switch off on a loading screen's low clock, and come back by trial.
  5. P-318 and file 00 4.3: the tablet at 120 Hz holds in all 102 tour stops, dynamic resolution acting to hold it; the PC at every offered rate.
  6. P-317's measure again: two refreshes from step to display at 60 Hz on the tablet.
  7. P-019: the thermal state is read on the tablet and is an input of the arbiter (derived from the row; the sources name no test).
  8. The game step's time and the walk's, on the PC and the tablet, before and after: never slower (FM 2.16's proof, each landing).
- **For a player.** A heavy scene lowers its resolution, not its picture rate, and keeps even pictures; nothing switches to 60 by itself; a missed target is said in the settings.
- **Read first.** The heat input's source is named for Android only (the audit's two calls above); for Apple, Linux and Windows the sources name none, so read the row and FM 2.16 item 7 first (`git grep` shows only the benchmark's own heat measure at 241395b49). Run the audit's check on the tablet once the clock is held: `adb shell dumpsys android.hint_manager` and a trace over 5 minutes in one galaxy, comparing missed frames. (Same as G2.3's Read first (4).) Every pacing measure on the tablet is recorded with R1.2's count of pipelines made by draws after a stage's first picture ("made by draws first", R1.2's proof 1) and R2.2's log line `[samplers] ... starts again` (R2.2's proof 4); a tour stop where either is above 0 is measured again; if R1.2 or R2.2 is not on main when the measure is taken, the measure says so.
- **A rule against a jam (the owner's order of 11-23: nothing is called done that is not).** P-318's note says the GPU's time was inside its budget while the wall time was not, and "the rest is speed" (G2.8). If proof 5 fails only in tour stops where, with the clock held and the resolution at its floor, the timeline's `Walk` events show the render thread's time for a picture above the slot, the step is not called done there: the stops and numbers are recorded; the landing changes this card's Rows line to say P-318 advanced (closes at G2.8), gives G2.8's Rows line "P-318 whole" and records both with `PLAN decide`; P-318 is advanced at G2.5 (pictures even, resolution first) and closes at G2.8 (the unseen walk and the walk's cost), as P-244 (G1.1 and G1.5) and I-003 (R5 and G2.8) already close across two steps; the rows tables of G2 and R5 say so. A proof 5 that fails in any other way is not covered by this rule.
- **Android at any rate (proof 5 and 6).** The tablet lists 120, 60 and 30 Hz; the arbiter's inputs at any other display rate are proven by `frame_schedule_tests.cpp` fed that rate (the head's sample rates), not by a device of ours: P-363 holds what is left.

### G2.6 Landing 6: the pointer and the menu cursor at the picture's moment

- **What changes.** Today the star pointer and the HUD are frame-list items blended between steps, so above 60 Hz the pointer moves on the step's positions with the interpolation delay added (P-078); the settings menu reads the mouse, touch and wheel once per system step, so hover, drag and the cursor are as old as their step (P-163). New: the pointer's layout item is marked present-time, and the render thread places it from the newest host pointer state at the picture's walk (the same projection the step uses, `project_client_mouse_pointer`, and the newest HID gyro state), while the game keeps the step's position for its hit tests. The settings menu stays a game layout on the game step for every decision (a click, a row chosen, a value applied); `readInput` keeps the step's pointer for hit tests, and the pointer item, a slider knob being dragged and a list being dragged are marked present-time, placed by the render thread from the newest host pointer and finger state (`NebulaHost::menuFinger`'s host side), clamped to the drag's own range, which the step writes into the item. The value a drag sets is still the step's. The free-look camera part of FM's target 6 is parked by the sources and is not in this plan.
- **Where.** `runtime/src/render/render_core.cpp` (the pointer-latency stamps, `:472-506` as cited); `runtime/src/input/host_input.cpp` (`project_client_mouse_pointer`); `native/petari/src/Game/Screen/NebulaSettingsMenu.cpp` (`readInput`, `:3530-3580`); `native/petari/include/Game/System/NebulaHost.hpp` (`menuFinger`); `native/petari/src/Game/Screen/StarPointerLayout.cpp`; `native/petari/src/Game/Render/FrameList.cpp`; `native/include/nebula_frame_list.h`.
- **Rows.** P-078 whole; P-163 whole.
- **Needs first.** G1.1 (the record's pointer in the picture's terms).
- **Old path.** The landing's own switch keeps the step-time item beside the present-time one; this step deletes the old path once its measures hold.
- **Proof.**
  1. Pointer-latency diagnostics at 60 and 144 Hz before and after (P-078); the tour's pictures the same (tours take pictures of real frames).
  2. A 240 Hz run with the pointer-latency diagnostics and a scripted drag (the test extension's pointer path) before and after: the knob's latency from the input stamp falls from about a step plus the interpolation delay to under one picture; the menu tour's pictures the same (P-163).
  3. Replay identical: the game's hit tests keep the step's position.
  4. The game step's time and the walk's, on the PC and the tablet, before and after: never slower (FM 2.16's proof, each landing).
- **For a player.** The pointer, a dragged slider knob and a dragged list follow the hand at the picture's moment instead of a step and the interpolation delay behind.
- **Read first.** Nothing the sources leave open.

### G2.7 Landing 7: every camera cut and warp stated; `camera_epoch` read by the host

- **What changes.** I-014 has several faults, each a bullet; the row closes with the last (plan decision).
  - Two places only state a camera cut (`CameraAnim.cpp:132` and `CameraViewInterpolator.cpp:209`, through `MotionSignal::cameraCut`, `MotionSignal.cpp:136-137`), carried as `NEBULA_FRAME_CAMERA_CUT` (`FrameList.cpp:502-504`); a turn is never a cut (`frame_interpolation.h:35-45`) and a jump of the eye past the limits is.
  - An event camera that snaps sends no cut: `CameraDirector::isInterpolationOff` also counts a camera man that declares its own interpolation off (an event camera that snaps): the switch is instant but sends no cut, so the host's heuristic (60 degrees, or 1500 units and 6 times the last move) decides, and a snap to a near angle is blended for one step (a smear across the cut).
  - The list's `camera_epoch` is written (`sCuts`) and read by nothing.
  - Warps and respawns: the stated teleports today are a new life, a death, the player placed, and a draw slot's epoch; other warps rely on the heuristic.
  - New: the cut is sent where the game decides a camera does not interpolate: `CameraDirector::switchAntiOscillation` when `isInterpolationOff()` (both causes), `CameraAnim`'s cut frames as now, and the director's restart paths after a warp or a scene start. Every place the camera code changes view without blending states the cut, and every warp or respawn of an object states it (the landing reads `Game/Camera` and the position setters whole and lists them). The thresholds stay as a net. The host compares `camera_epoch` between the two lists of a pair instead of the flag, so a cut in a list never shown still parts the pair.
- **Where.** `native/petari/src/Game/Camera/CameraDirector.cpp` (`switchAntiOscillation`, `isInterpolationOff`); `native/petari/src/Game/Camera/CameraAnim.cpp` (`:132`); `native/petari/src/Game/Camera/CameraViewInterpolator.cpp` (`:209`); `native/petari/src/Game/Camera/CameraManEvent.cpp`; `native/petari/src/Game/Util/MotionSignal.cpp`; `native/petari/src/Game/Render/FrameList.cpp`; `native/include/nebula_frame_list.h` (`camera_epoch`, `NEBULA_FRAME_CAMERA_CUT`); `runtime/src/render/core/pairing.cpp`; `runtime/src/render/render_core.cpp`; `runtime/include/galaxy/render/frame_interpolation.h`; the stated teleports: `native/petari/src/Game/LiveActor/LiveActor.cpp:88`, `native/petari/src/Game/Player/MarioActorGameOver.cpp:66`, `native/petari/src/Game/Player/MarioAccess.cpp` (`:264`, `:552`), `native/petari/src/Game/Util/ActorMovementUtil.cpp:511`, `native/petari/src/Game/NameObj/NameObjCategoryList.cpp:25`. Lines as cited at 241395b49.
- **Rows.** I-014 whole.
- **Needs first.** nothing beyond the order of the landings.
- **Old path.** The flag `NEBULA_FRAME_CAMERA_CUT` goes when the host reads `camera_epoch`; the landing's own switch keeps both until its measures hold, and this step deletes the flag.
- **Proof.**
  1. The tour's present-stats with in-between pictures on: game-cuts against camera-cuts at pipes, doors and event cameras (every heuristic cut there is also a game cut).
  2. Generated pictures at a door show no blend.
  3. The step-hash replay the same (the signal is not game state).
  4. The game step's time and the walk's, on the PC and the tablet, before and after: never slower (FM 2.16's proof, each landing).
- **For a player.** No blend across a door, a pipe or a snapping event camera, and none after a warp or a respawn.
- **Read first.** The sources disagree on the signal that stays: I-014's row says `camera_epoch` leaves the frame list (the flag is the one signal); FM 2.16 target 7, later, says the host compares `camera_epoch` and the flag goes. This card follows FM; record the choice in the row.

### G2.8 Landing 8: the cost of a picture

- **What changes.** Today each in-between picture is a full CPU walk of the list and a full GPU frame, and the walk repeats the matrix rows for the picture (I-003). New (FM 2.16 target 8 and I-003's note): no unseen walk (landing 2 and E4's names); blending on the GPU (E3 step 5, FM 2.13); each draw pass is recorded once per list at its first picture (`DrawRecorder` ops, `draw_record.h`), with every row read through the rows base as the one patched value, and an in-between picture writes its blended rows into a new rows slice and replays the recorded ops with that base: no walk of the items and no material or pipeline lookups. An item culled at both ends of the pair stays culled (the bound's union over the pair, tested at record time); items culled at one end are recorded and drawn. A pass none of whose read versions changes with `t` (E4's versions say this) is kept from the real frame, not drawn again; passes that do not depend on `t` (2-D layers not marked present-time, the frame copy of a step picture) replay as recorded. Each API's own rule for re-executing recorded commands is read in its documentation first; else the draws are recorded with no per-draw arithmetic. The GPU work stays a full frame, its cost P-156's, measured per pass on the tablet first. I-003 closes here.
- **Where.** `runtime/src/render/core/draw_record.h` (`DrawRecorder`); `runtime/src/render/core/draw_walker.cpp`; `runtime/src/render/render_core.cpp`; `runtime/src/render/frame_schedule.cpp`. The encoder call `set_rows` and `walk_.rows_address` the row names exist at main (`draw_record.h:102`, `draw_walker.cpp:370`, `:1123`).
- **Rows.** I-003 (R5's row; it closes here). No row of this package's rows file.
- **Needs first.** R5 (poses and matrices on the GPU, E3 step 5) and R7.2 (the planner, E4 landing 2).
- **Old path.** The landing's own switch keeps the per-picture walk; this step deletes it once the measures hold.
- **Proof.**
  1. The timeline's `Walk` events (b=1 for an in-between picture) on the PC at 240 Hz and the tablet at 120 Hz: the render-thread time of an in-between picture before and after; it does not grow with the objects drawn (file 00 4.3).
  2. The interpolation tours' pictures the same.
  3. The game step's time and the walk's, PC and tablet, before and after: never slower.
- **For a player.** More pictures a second on a weak processor, and no loss of pictures on a heavy scene.
- **Read first.** FM 2.16 target 8's recorded-pass replay is "where the API allows it, by each API's own rule read in its documentation"; read each API's rule before writing the code, and measure each pass on the tablet first. If G2.5's rule against a jam left P-318 advanced, this step's Rows line says "P-318 whole" and its proof 1 records, in the tour stops the rule named, that the render thread's time for a picture is inside the slot (the numbers G2.5 recorded, before and after).

### G2.9 Landing 9: the default rate

- **What changes.** Today the default frame-rate setting is 60, the game step, on every platform (the setting `max_fps`, `max_fps = kGameStepsPerSecond` in `render_config.h`; audit file B, finding P-042). A new install on a 120, 144 or 165 Hz display therefore starts at 60 with in-between pictures unused until the player finds the setting. "0, the display's own" already exists (`GameActivity.java:178`); the audit's form is a default of 0 on every platform where in-between pictures are on by default and 60 where the device's budget says so. On Android, with `config.ini` missing or unreadable the saved rate falls back to 60 (`GameActivity.java:179-192`), rates up to 61 are taken as the game's own and leave the display mode to the system (`:203`), and the surface vote takes every setting above 0 and up to the literal 61 as the game's own frames (`FIXED_SOURCE`, `host_window_sdl.cpp:360-363`), not from `NEBULA_STEPS_PER_SECOND` or the pacing state. Decided (the owner, 11-22: "should be 60fps."; the earlier recommendation of 120 is withdrawn): the default stays 60 on every system and a player chooses a higher rate. New: the default is not changed; the Android fall-back and the surface vote read the step rate (`NEBULA_STEPS_PER_SECOND`, the pacing state) and not the literal 61, so they hold at any step rate; every setting on these pages shows only where it acts; and the first start on an integrated-graphics Mac tells the player once what it chose and where to change it (P-364, below). How the Android fall-back and the vote follow the step rate is not given by the sources.
- **Where.** The setting `max_fps` and its default (`runtime/include/galaxy/render/render_config.h`, as cited in the audit at line 53; it is line 52 at main); `platform/android/app/src/main/java/dev/nebula/galaxy/GameActivity.java`; `platform/sdl/host_window_sdl.cpp` (`vote_frame_rate`). Lines as cited at 241395b49.
- **Rows.** P-042 whole (decided: the default stays 60); P-364 whole.
- **Needs first.** G2.1 to G2.8: it is last, after the measures hold.
- **Old path.** none: the default is one value.
- **Proof.**
  1. Every measure of file 00 4.3 holds on the PC (Direct3D 12, Direct3D 11, Vulkan) and the tablet at each offered rate, with the default in place; the tablet at 120 Hz holds in all 102 stops.
  2. A first start on a 120 Hz display runs at 60, and with `config.ini` missing the Android host does the same; the rate a player picks (any rate, whole or not) is the one that runs.
  3. The game step's time and the walk's, on the PC and the tablet, before and after: never slower (FM 2.16's proof).
  4. P-364: the UPSCALING row's help line names INTERNAL RESOLUTION and says that UPSCALING OFF brings it back (a test of the row's text in `settings_graphics_page_tests.cpp`, failing with the old line); the first start's choice on an integrated-graphics Mac (`choose_first_start_scale`) is told to the player once, on the first picture after it was made and never again (a test of the once-only state with a fake host); each new text has its 14 cells (`python tools/nebula_text.py --check`, `tools/settings_table.py --check` where a row is touched). The Mac itself is built by the Mac job; what a Mac shows is the owner's to look at (`PLAN owed`).
- **For a player.** A first start runs at 60 pictures a second on every display, and a player who wants more picks it in the settings. A Mac that chose a smaller 3D resolution for itself says so once, and the settings say how to change it.
- **Parts closed here (plan decision).** What FM 2.16 target 9 and the row's notes name and no earlier step places:
  - [ ] the Android fallback: with `config.ini` missing or unreadable `savedFrameRate` returns 60 (`GameActivity.java:179-192`) and `applyDisplayRate` leaves rates up to 61 to the system (`:203`); both read the step rate, not the literal 61 (the default stays 60);
  - [ ] the surface vote: `FIXED_SOURCE` takes every setting above 0 and up to 61 as the game's own frames (`host_window_sdl.cpp:360-363`), not from `NEBULA_STEPS_PER_SECOND` or the pacing state; it reads the step rate and not the literal 61;
  - [ ] P-364 (the owner's test of the Mac app, 11-23): see proof 4. The row UPSCALING shows the choice and, with it on, INTERNAL RESOLUTION is not shown (`settings_graphics_page.cpp`: the row is offered only while upscaling is OFF, one row sets the 3D resolution); `choose_first_start_scale` (`renderer_mtl.mm`) sets FSR 1 at PERFORMANCE on the first start of an Intel Mac drawing with integrated graphics on a display with more pixels than points and says so only on stderr;
  - [ ] "every setting on these pages shows only where it acts" (FM target 9): the sources give no list of the settings; read the frame-rate, in-between-picture and resolution rows of the settings and show each only on the platform and renderer where it acts.
- **Read first.** (1) The audit names the benchmark's first-start choice (`choose_first_start_scale`, from P-035's note) as the place to decide the default; the default is decided (60), so it is read for what it sets besides and for P-364: it is `RendererMtl::choose_first_start_scale` in `renderer_mtl.mm` (declared at `renderer_mtl.h:746`), Mac only. (2) How the Android fallback and the vote follow the default is not given by the sources; read the row and the code first.

### Rows of this package

| Row | Today | After | Closes at |
| --- | --- | --- | --- |
| I-002 | On vblanks the real picture of a step is almost never shown (5 to 13 of 2,640) and every step costs one unseen walk. | At whole multiples of 60 Hz the sequence takes lead 1/k so the last slot shows the real picture; no walk is thrown away at 120, 240, 360 Hz. | G2.2 |
| I-004 | In-between pictures start a step late: the list is held until the next step asks for its present. | The sequence starts when the list arrives, its reference its step's moment plus the tracked delay. | G2.1 |
| I-005 | Linux has no display clock. | A clock from present-wait stamps fitted to the mode's period, or present timing where offered; no grid keeps today's clock. | G2.3 |
| I-006 | Metal drops a picture when `nextDrawable` waited over 1.5 ms. | Count drawables handed over and not yet presented; hold a picture only at 2 or more; the wait time is not read. | G2.3 |
| I-007 | Variable refresh is inferred from the vblank rate, which in-between pictures near the full rate hide. | Told by the system, else proved from the presents; slots then follow the arrival clock at the cap. | G2.3 |
| I-010 | Dynamic resolution and the interpolation governor act on the same GPU time without knowing of each other. | One arbiter owns k and the resolution against one budget; resolution first. | G2.5 |
| I-012 | The timeline was Windows only; only Android Vulkan presents at a requested time. | The timeline on every platform (on main); present at a requested time on Vulkan, Android, Linux and Apple. | G2.3 |
| I-013 | The step's phase measures the arrival delay or 0 under in-between pictures, not its own work. | Work measured per step from the render thread's slot for that frame index. | G2.1 |
| I-014 | Camera cuts come from two places; a snapping event camera sends none; `camera_epoch` is read by nothing. | Cuts stated at every no-interpolation switch, restart and warp; the host compares `camera_epoch`. | G2.7 |
| N-1084 | A slot between a frame's present and its arrival shows the frame before, early (about 1 frame in 7 at 120 Hz). | Such a slot shows the sequence before at its own `t`; the early count becomes zero. | G2.1 |
| P-019 | The app never reads the device's thermal state (the hint exists; its fault is P-069). | The device's thermal headroom is an input of the arbiter (Android: a thermal listener, or a headroom poll once a second). | G2.5 |
| P-034 | The Apple display link is never stopped (code in 17ad0a4cd; proof owed). | The Mac job's start check holds the clock for 1 s and counts 0 callbacks, then sees them again. | G2.3 |
| P-042 | The default frame rate is 60, the game step, on every platform. | Decided (the owner, 11-22: "should be 60fps."): the default stays 60 on every system and a player chooses a higher rate; the Android fall-back and the surface vote read the step rate, not the literal 61. | G2.9 |
| P-078 | The star pointer is sampled once a step (the free look is parked); above 60 Hz the pointer lags by the interpolation delay. | The pointer's item is present-time, placed from the newest host pointer state; the game keeps the step's value for hit tests. | G2.6 |
| P-163 | The menu reads mouse, touch and wheel once per system step, so its pointer, drag and knob lag. | The pointer, a dragged knob and a dragged list are present-time items; decisions stay on the step. | G2.6 |
| P-274 | A late step wake drops the time it was late by, so game time slips against the clock. | The step clock keeps its due times and runs missed steps back to back up to a bound, then drops the rest with a log line. | G2.1 |
| P-281 | Android's host: an 8 ms fixed event sleep, a clock sleep that retries on any error, a surface handed on after 2 s, an unlocked JNI table. | A woken block, retry only on `EINTR`, the device-lost path past 2 s, an atomic published table. | G2.3 |
| P-311 | At 240 Hz with VSync on, Direct3D presented a game frame with a whole step's sync interval; aba64e331 changed it; no after-measure. | The after-measure at 240 Hz is made with the `[pace]` measure of file 00 4.3. | G2.3 |
| P-312 | At 240 Hz Direct3D shows a picture two refreshes after its moment, Vulkan one; the render thread blocks about 4 ms a picture. | The cause traced with the frame statistics; a picture at the first refresh after its slot. | G2.3 |
| P-313 | On Vulkan at a scene restart one picture reaches the display 50 to 60 ms late and one step runs long. | Traced with the timeline at a restart and fixed where it lies; no hitch over one refresh. | G2.3 |
| P-314 | The render thread spins the last part of each wait: 163 ms of CPU a second at 120 pictures on the PC. | Where a display time is asked, no spin; the spin stays only where no time can be asked. | G2.4 |
| P-315 | In-between pictures switch off on a GPU time read at a clock lowered during loading and may not return. | The hint holds the clock; no decision reads a time taken under its own clock effect; a return is tried. | G2.5 |
| P-316 | With in-between pictures capped off on a 120 Hz display, the 60 pictures a second drift from every second refresh. | When the cadence guard acts, its pictures are placed on every n-th refresh by the display clock. | G2.5 |
| P-317 | At 60 Hz Android asks each picture's time three refreshes after its step. | The lead comes down to two refreshes once the clock is held. | G2.3 |
| P-318 | Nothing acts on what is shown: 72 uneven pictures a second in the heaviest tour stop at 120 Hz. | One arbiter judges by pictures on time and wall time against the slot and acts by resolution; no switch to 60. | G2.5 (if G2.5's jam rule applies: advanced there, closes at G2.8) |
| P-363 | No display of ours shows what a player may have: an Android display at a rate that is not a multiple of 60 (the tablet lists 120, 60 and 30 Hz), and a real variable-refresh display on Linux and on the Mac (none ours); on Windows and Android the PC's monitor and the tablet's panel are read first (G2.3 proof 5). | The decisions are proven by our own code on simulated displays (G2.3, proofs 1, 5 and 8); the row closes with a run on each real display, the log showing the display's own rate (`refresh_ms` of `[pace]`) and even pictures. | G2.3 (part); closes with a real run |
| P-364 | A Mac that chose FSR 1 at PERFORMANCE for itself on its first start shows no INTERNAL RESOLUTION row (it is shown only while UPSCALING is OFF) and no text says that the game made the choice or that OFF brings the row back. | The UPSCALING row's help line names INTERNAL RESOLUTION; the first start's choice is told once, in all 14 languages. | G2.9 |


---

## G3 The game step on the cores

The game step runs on one thread (`git grep -n -E "std::thread|parallel_for" 241395b49 -- native/petari/src` finds nothing), and a second thread would collide with state that the game shares between callers: the skeleton's globals, the visibility judge's moves between lists, the particle library's one work record, each collision keeper's one result array, the shadow request list, the frame list writer's file-scope variables, and every load copied from the mapped store on the game thread. This package gives the game one worker pool with a fork-and-join call, makes the run lists' iteration safe, and then moves visibility, collision queries and shadows, animation, particles and the frame list's recorders onto workers in seven landings, each behind a switch, with the order of effects always the game's. The loads are read ahead by the loader's thread. A player gets a step that costs less on a phone and a PC with several cores, and no stall on a drive read inside a step.

- **Design:** FM 2.17 (today's shared state, the target, the seven landings, the proof; file 08 5.D.6: it lands only with the replay identical, the thread sanitizer clean and the `soak` check passes), FM 2.1 rules 3 and 4, FM 1.1 and 1.5 (the game thread and what it waits for, the asset read among them), and the rows' notes (P-146's list of file-scope state, P-241, P-265, P-283, N-1083). N-3013 and N-3014 are `app/work`'s ledger rows (FM 2.6, 2.17); they are not in this package's rows file.
- **Needs first:** G3.4 after R5; G3.5 after R4.4; G3.6 after R6 and S0.2.1; G3.7 after S0.2.1 (P-260's first half). FM 2.17 says F7's other changes (P-129 to P-141) are faster single-thread work and are not this section's (they are G4, after G3.3). The cost of P-191's "every actor runs" (G5) lands on these workers.
- **Rows:** 6 (P-040, P-146, N-1083, P-283, P-265, P-241). P-260's second half is placed at G3.7; its row is in S0.2's rows file.

### G3.1 The pool and the fork-and-join call; the run lists made safe

- **What changes.** P-265 first, then the pool. P-265 has several faults, each a bullet; the row closes with the last (plan decision).
  - The run's cursor: `NameObjCategoryList::execute` walks a category with a `NameObj**` cursor and reads `end()` again on each pass so that a call may add or remove; but `add` pushes onto an `MR::Vector` whose storage `grow()` moves to a new block when full, leaving the cursor in the old block.
  - A removal skips an object: `remove` moves the last object into the removed place, so a removal at or before the cursor skips that object for the pass.
  - Who calls during a run: the game's own connects and disconnects are requests run between the lists' runs; the other path is `MR::connectNow`, which `LumaUi` calls when added content makes a layout, from whatever it was running in.
  - New: the run iterates by index against the live size, and adds and removes made during a category's run are applied at its end (or `connectNow` refuses inside a run and queues).
  - The pool (FM target 1): one worker pool for the host and the game, sized from the processors the process may use (N-3014), at the game thread's priority, with a fork-and-join call the game makes inside its step: the game thread takes a share and returns when every share is done, and no work crosses a step. N-3013 and N-3014 are the faults GitHub found in `app/work`'s frozen commits: the headless host's include, and the pool counting processors the process may not use. The landing carries a switch (workers on or off, and a worker count) with the one-thread path beside it.
- **Where.** `native/petari/src/Game/NameObj/NameObjCategoryList.cpp` (`:22-32`, `:47-71`); `native/petari/include/Game/Util/Array.hpp` (`:111-119`, `:399-402`); `native/petari/src/Game/Scene/SceneExecutor.cpp` (`:38-40`, `:81`); `native/petari/src/Game/NameObj/NameObjExecuteHolder.cpp` (`:153-196`, `:312-318`, `:386-387`); `native/petari/src/Game/System/LumaUi.cpp` (`:671-674`); the game thread's entry in `native/petari/src/Game/System/GameSystem.cpp` (`nebula_game_main`) and its start in `native/host/host_main.cpp`. The existing record workers' fork-and-join is `RecordPool` (`runtime/src/render/core/draw_record.h:274`). `app/work`'s pool is a branch the sources name without a file; no pool file of that kind is on main (not found at 790626cb5). Lines as cited at 241395b49.
- **Rows.** P-265 whole; P-040 and P-146 advanced (P-040: the pool; both close at G3.7).
- **Needs first.** nothing.
- **Old path.** The switch (workers off, one worker count) keeps the one-thread path beside the pool; the sources do not say a step deletes it (the sources give no further detail; read FM 2.17 first).
- **Proof.**
  1. P-265: replay identical for the disc's content (its changes go through the requests); a run inside a category that adds or removes gets a defined order.
  2. The replay's step hash identical with workers off, on, and at one, two and every core, over the tour and the replay suite. The step hash is the audit's: with the host's deterministic step, after every step a hash of every live actor's position, velocity, gravity, nerve and step and of the binder's ground, wall and roof planes (audit file B, R8.3); the frame-list hash is the same with each list's items, constants and blocks.
  3. The frame list's hash identical the same way.
  4. The thread sanitizer clean over the tour (the SDL host on Linux); `.github/workflows/check.yml`'s `linux-sanitizers` job runs the unit tests under it already, and the tour under the sanitizers runs in the local gate.
  5. The `soak` check passes (`tools/gate/soak.py`).
  6. The step's time on the tablet in the five heaviest stops before and after, and on the PC never slower (a step of 1.4 ms at most there: the pool's cost must not show).
- **For a player.** Nothing visible: it is how the code is built.
- **Read first.** N-3013 and N-3014 are not in the rows file; read them in `app/work`'s ledger. FM 2.17 landing 1 lists them with the pool.

### G3.2 Visibility's judge

- **What changes.** Today `ClippingActorHolder::movement` moves actors between lists as it judges them (`startClipped` and `endClipped` connect and disconnect actors from the scene and draw lists, `ClippingActorHolder.cpp:44-52`, `LiveActor.cpp:294-349` in the audit), and `LiveActor::sClipScope` is one global mode set in `ClippingActorInfo.cpp`. `isGroundShown` caches `mGround` on its first use (`ClippingActorInfo.cpp:58-67` in the audit). New: `judgeClipping` is split into a pure judge (`isJudgedToClip` and `isDrawnWhileClipped` read only the camera's planes and the actor's sphere) that writes one result byte per actor on the workers; the moves between lists and the draw-list changes are applied afterwards on the game thread, in order; the ground cache a judge reads is filled first. The step's effects are the same as one thread's whatever the worker count. FM 2.18 later builds the "seen" judge for the reference view volume on this one (its landing 1 says the judge keeps today's numbers with the switch off, so this landing alone changes nothing).
- **Where.** `native/petari/src/Game/LiveActor/ClippingActorHolder.cpp` (`:36`); `native/petari/src/Game/LiveActor/LiveActor.cpp` (`:292`); `native/petari/src/Game/LiveActor/ClippingActorInfo.cpp` (`:78-114`); `native/petari/src/Game/LiveActor/ClippingJudge.cpp`. Lines as cited at 241395b49.
- **Rows.** P-040 and P-146 advanced (the visibility's shared state).
- **Needs first.** G3.1.
- **Old path.** The landing's switch keeps the one-thread judge beside the worker judge; deletion is not stated.
- **Proof.**
  1. The replay's step hash identical with workers off, on, and at one, two and every core, over the tour and the replay suite; the frame list's hash the same.
  2. The thread sanitizer clean over the tour; the `soak` check passes.
  3. The step's time on the tablet in the five heaviest stops before and after; the PC never slower.
- **For a player.** Nothing visible: it is how the code is built.
- **Read first.** Nothing the sources leave open.

### G3.3 Collision queries with caller buffers; shadows on workers

- **What changes.** Each fault is a bullet.
  - P-241, the memory fault: each keeper has one result array (`CollisionCategorizedKeeper.cpp:14`); `getStrikeInfo` returns `&mHitInfoArray[i]` (`:557-559`), and a query that grows the array (`growHitInfo`, `:515-519`) frees the array that `HitInfo` pointers from an earlier query still point into.
  - Shared collision state: the result buffer per keeper (read back by `Collision::getStrikeInfoMap`), `sAreaPrisms` (`CollisionParts.cpp:527-530`), the binder's `sLocalPlanes` (`Binder.cpp:78`), and the binder's add and remove of its extra part in the global zone (`Binder.cpp:114-116`, `:174-176` in the audit).
  - Shadows: the request list (`ShadowController.cpp:43`) and the collision and gravity queries above; `ShadowController::update` writes only its own fields and the gravity search is const (audit file B, R8.2).
  - New: a collision query takes its caller's result buffer (the keeper's buffer stays for the old call sites) and an extra part as a candidate, not inserted in the shared zone (P-146's form: query results owned by the caller); queries are then read-only over parts. The requested shadow updates run on workers, which read collision after all movement ran, so no ordering changes. Actor movement stays on the game thread in order, because an actor's bind reads parts moved by actors before it in the same step (FM target 3).
- **Where.** `native/petari/src/Game/Map/CollisionCategorizedKeeper.cpp` (`:14`, `:515-519`, `:557-559`); `native/petari/src/Game/Map/CollisionParts.cpp` (`:527-530`); `native/petari/src/Game/LiveActor/Binder.cpp` (`:78`); `native/petari/src/Game/LiveActor/ShadowController.cpp` (`:43`). Lines as cited at 241395b49.
- **Rows.** P-241's collision part (a query that grows the array no longer frees what an earlier result points into); P-241 closes at G3.7 with its halo part; P-040 and P-146 advanced (collision results, shadows).
- **Needs first.** G3.2.
- **Old path.** The landing's switch keeps the shared array and the serial shadow update beside the new path; deletion is not stated.
- **Proof.**
  1. The replay's step hash identical with workers off, on, and at one, two and every core; the frame list's hash the same.
  2. The thread sanitizer clean over the tour; the `soak` check passes.
  3. The step's time on the tablet in the five heaviest stops before and after; the PC never slower.
- **For a player.** Nothing visible: it is how the code is built (and the memory fault in collision queries is gone).
- **Read first.** The halo half of P-241 (a halo whose object name is not in the two-entry table dereferences null at `Halo::init`) is closed at G3.7 (plan decision: the ledger's placement note says "B1", which is not a step of the skeleton). G4 starts after this step.

### G3.4 Animation without callbacks

- **What changes.** Today the `Skeleton` globals (`Model.cpp:13-19`) and their one stack (`:136`) are shared, `MtxCalc::calc` reads `Skeleton::mJoint` and `mModel`, and joint callbacks call actor code (`:175`, `:189-191` in the audit) that can touch anything. `ModelManager::update` and `calcAnim` (`native/petari/src/Game/LiveActor/ModelManager.cpp:25-94`, `:404-418` in the audit) also run the material and visibility players, which write the shared `Material` when it is not owned. New: models with no joint callback and no calc callback (`mCalcCallBack`, `Model.cpp:491-493`) pose on workers, each with its own skeleton state (a struct passed by pointer, replacing the globals); the rest pose on the game thread in list order. The callback rule must be exact, since a callback can read another actor's matrices (the audit's risk).
- **Where.** `native/petari/src/Game/Render/Model.cpp` (`:13-19`, `:136`, `:175`). Lines as cited at 241395b49. After R5 the game writes a block's joints once a step from the skeleton's evaluation (R5.1, FM 2.13); read what R5 left of `Model.cpp` first.
- **Rows.** P-040 and P-146 advanced (skeleton evaluation).
- **Needs first.** R5; G3.1.
- **Old path.** The landing's switch keeps the serial posing beside the worker posing; deletion is not stated.
- **Proof.**
  1. The replay's step hash identical with workers off, on, and at one, two and every core; the frame list's hash the same.
  2. The thread sanitizer clean over the tour; the `soak` check passes.
  3. The step's time on the tablet in the five heaviest stops before and after; the PC never slower.
- **For a player.** Nothing visible: it is how the code is built.
- **Read first.** Nothing the sources leave open.

### G3.5 Particles without callbacks

- **What changes.** Today the particle library has one `EmitterWork` (`pWd`) that every emitter's calc and draw fill and read in turn (`EmitterManager.cpp:65`, `EmitterManager.hpp:54`); `ParticleCore::getCalcCurrentPosition*` reads its public scale as the last emitter left it (`ParticleCore.cpp:314-325`); the draw batch, camera, current particle and texture matrix are file-scope (`ParticleDraw.cpp:20-49`). The free lists of emitters and particle chunks (`EmitterManager.cpp:20-40`) and the per-resource made counts that seed each emitter (`:100-101`) are shared too (audit file B, R8.2). New: emitters with no callback run on workers (those with a callback, such as `MultiEmitterCallBack`, which calls game code, stay serial), each worker with its own work record and a chunk cache refilled from the shared list; finished emitters are freed on the game thread in order; seeds stay per emitter (they come from a mix per creation).
- **Where.** `native/petari/src/Game/Particle/EmitterManager.cpp` (`:65`); `native/petari/include/Game/Particle/EmitterManager.hpp` (`:54`); `native/petari/src/Game/Particle/ParticleCore.cpp` (`:314-325`); `native/petari/src/Game/Particle/ParticleDraw.cpp` (`:20-49`). Lines as cited at 241395b49. R4.4 changes the particle draw sites first; read what it left.
- **Rows.** P-040 and P-146 advanced (the particle library's shared state).
- **Needs first.** R4.4; G3.1.
- **Old path.** The landing's switch keeps the serial emitters beside the worker ones; deletion is not stated.
- **Proof.**
  1. The replay's step hash identical with workers off, on, and at one, two and every core; the frame list's hash the same.
  2. The thread sanitizer clean over the tour; the `soak` check passes.
  3. The step's time on the tablet in the five heaviest stops before and after; the PC never slower.
- **For a player.** Nothing visible: it is how the code is built.
- **Read first.** Nothing the sources leave open.

### G3.6 The frame list's recorders

- **What changes.** Today the frame list's writer keeps 13 file-scope variables (`FrameList.cpp:250-320`). Also shared (audit file B, R8.2): the light state (`sLights`, `sLightOwner`), the site pipeline table (`:418-446`), and the `Model` and `Material` caches written during drawing (`Material.cpp:719-766`, `Model.cpp:581-617`). New: one recorder per worker holds today's file-scope state, one store each, joined in category order with indices rebased; the material and pose caches written in drawing get a per-step first-writer rule (a "claimed by" mark or a lock-free rule). Draw order inside a category stays as today, so the split is per category list, not per object. The game writes both poses of each object once a step into the list (FM 2.2), so the recorders write what R5 and R6 left in it.
- **Where.** `native/petari/src/Game/Render/FrameList.cpp` (`:250-320`; the `store()` at `:343-361`); `native/include/nebula_frame_list.h`. Lines as cited at 241395b49 (FM 1.2 and 2.17).
- **Rows.** P-040 and P-146 advanced (the frame list writer's state).
- **Needs first.** R6; S0.2.1 (drawing stops changing the game); G3.1.
- **Old path.** The landing's switch keeps the single writer beside the recorders; deletion is not stated.
- **Proof.**
  1. The frame list's hash identical with workers off, on, and at one, two and every core, over the tour and the replay suite; the replay's step hash the same.
  2. The thread sanitizer clean over the tour; the `soak` check passes.
  3. The step's time on the tablet in the five heaviest stops before and after; the PC never slower.
- **For a player.** Nothing visible: it is how the code is built.
- **Read first.** P-146's other file-scope state is not placed by the skeleton or FM: the text writer's material, glyph run and matrix (`native/petari/src/Game/Ui/CharWriter.cpp:13-20`), the pending pane draw (`native/petari/src/Game/Ui/UiCommon.cpp:21-29`), the five matrices of `MtxUtil` (`native/petari/src/Game/Util/MtxUtil.cpp:6-14`), the fur's one `SiteMaterial` rewritten per shell (`native/petari/src/Game/Util/FurDrawer.cpp:17`), the pivot shuffle in `MR::sort` (`native/petari/include/Game/Util/SortUtil.hpp:67-78`, a function-local static shared by every sort, so a sort's order among equal keys follows the sorts before it) and the static buffer of `AstroMapObjFunction::getModelName` (`native/petari/src/Game/MapObj/AstroMapObjFunction.cpp:206-213`). Each is closed in G3.7's "Parts closed here" (plan decision); read each whole at the landing whose worker part reaches it and say which.

### G3.7 Loads read ahead; the stage load on the load worker

- **What changes.** Each row is a bullet; P-040, P-146 and P-241 close here.
  - N-1083: the game thread copies every load from the mapped store (`svc_asset_read`, a copy from the store's mapped view), so a page not in memory is a drive read inside a step. New (FM 2.1 rule 3): what a step will read is read ahead by the loader's thread; the game thread copies only what is in memory.
  - P-283: the host's achievements, stats and Mii library load on their first call, on the caller's thread with their lock held: `achievements.ini` is read and an import merged and written, `stats.ini` the same, and the Mii library is read or seeded from the console database and written, so a first unlock, counter or Mii read from a step does file work in it. New: each store is loaded at start on the loader thread (or its writer thread) and the game's calls read memory only; the calls that come from the step are listed with the fix (the report names `mii_library_count` and `mii_library_mii`).
  - P-260's second half: the stage's load after the star is chosen, which S0.2.1 made run in slices of a fixed count, moves onto the load worker, and no frame steps game state that the stage keeps while it loads.
  - P-040 and P-146 close last: the table of FM 2.17's shared state and the list in P-146's note are checked line by line, each part gone or per worker (see "Parts closed here").
- **Where.** `native/host/host_main.cpp` (`svc_asset_read`; FM cites `:3144`, the row N-1083 `:3152`); `runtime/include/galaxy/asset_store.h` and `runtime/src/asset_store.cpp` (the store); `runtime/src/achievements.cpp` (`:125-148`); `runtime/src/stats.cpp` (`:80-106`); `runtime/src/mii_import.cpp` (`:196-222`, `:540-555`); for P-260: `native/petari/src/Game/Scene/GameScene.cpp` (`initAfterScenarioChosen`, `:165-217`), `native/petari/src/Game/Scene/StageDataHolder.cpp` (`:168`), `native/petari/src/Game/Scene/PlacementInfoOrdered.cpp` (`:139`), `native/petari/src/Game/Scene/ScenarioSelectScene.cpp` (`:277`, `:312`, `:320`), `native/petari/src/Game/System/GameSystemSceneController.cpp` (`:151-163`, `isReadyToStartScene`). Lines as cited at 241395b49.
- **Rows.** N-1083 whole; P-283 whole; P-260's second half (closes P-260; its row is in S0.2's rows file); P-040 whole; P-146 whole; P-241 whole (its halo part is closed here).
- **Needs first.** S0.2.1 (P-260's first half); G3.1; G3.2 to G3.6 for the closing check. R1.2's load gate (`isReadyToStartScene`) is on the same path; read what it left.
- **Old path.** The landing's switch keeps the synchronous reads and first-call loads beside the read-ahead; deletion is not stated.
- **Proof.**
  1. The replay's step hash identical with workers off, on, and at one, two and every core, over the tour and the replay suite; the frame list's hash the same; N-1083 and P-283 replay identical (the row).
  2. P-260: the step hash at play's first step equal on a fast and a slowed machine (S0.2.1's proof, kept).
  3. The thread sanitizer clean over the tour; the `soak/long` check passes.
  4. The step's time on the tablet in the five heaviest stops before and after; the PC never slower.
  5. No file read on the game thread inside a step (FM 2.1 rule 3; the sources name no test; the existing `--diagnostics file-reads` trace lists each read with its frame).
  6. P-241: an unknown halo name is refused at load with a message (derived from the row; no named test).
- **For a player.** No stall inside a step when a page of the game's data is not in memory, and no hitch on a first achievement, counter or Mii read.
- **Parts closed here (plan decision).** Parts of P-040, P-146 and P-241 that a row's note names and no earlier step places. Each is read whole, then made per worker or per caller, or shown not to be shared, and the choice is written in the row:
  - [ ] P-241's halo half: a halo whose object name is not in the two-entry table (`ZoneHalo`, `PowerStarHalo`) dereferences null at `Halo::init` (`native/petari/src/Game/Map/Halo.cpp:45`; `getParam` returns `nullptr`, `:24-34`); fix: a null check, so the unknown name is refused at load with a message (plan decision: the halo half closes here, in G3.7; the row's other option, parameters from the placement, is not taken);
  - [ ] the text writer's material, glyph run and matrix (`native/petari/src/Game/Ui/CharWriter.cpp:13-20`) and the pending pane draw (`native/petari/src/Game/Ui/UiCommon.cpp:21-29`): one drawer of layouts at a time;
  - [ ] the five file-scope matrices of `MtxUtil` (`native/petari/src/Game/Util/MtxUtil.cpp:6-14`);
  - [ ] the fur's one `SiteMaterial` rewritten per shell (`native/petari/src/Game/Util/FurDrawer.cpp:17`);
  - [ ] `MR::sort`'s pivot shuffle, a function-local static shared by every sort (`native/petari/include/Game/Util/SortUtil.hpp:67-78`), so a sort's order among equal keys follows the sorts before it;
  - [ ] the static buffer returned by `AstroMapObjFunction::getModelName` (`native/petari/src/Game/MapObj/AstroMapObjFunction.cpp:206-213`): one caller at a time;
  - [ ] state that the audit's P-040 finding names beyond FM 2.17's table: the global view matrix `ModelSys::mViewMtx` (`native/petari/include/Game/Render/Model.hpp:190`, written at `SceneExecutor.cpp:118-123`), the `MotionSignal::drawObject` slots (`NameObjCategoryList.cpp:25`), the arena allocator, which has no lock (P-012), and `calcViewAndEntry` per active executer (`DrawBufferGroup.cpp:72-78`, `DrawBufferExecuter.cpp:81-84`);
  - [ ] every row of FM 2.17's "Today" table (animation, visibility, particles, collision, shadows, frame list, loads) is gone or per worker.
- **Read first.** (1) P-283: which calls come from the step is listed with the fix. (2) The sources give no landing for the items in "Parts closed here" (they are not in FM 2.17's table): read each whole and say which landing's worker part reaches it; a part that no worker reaches stays as it is and is recorded as not shared.

### Rows of this package

| Row | Today | After | Closes at |
| --- | --- | --- | --- |
| P-040 | The step has no worker parallelism, and the shared state that blocks it is named so a plan can start from it. | One pool and a fork-and-join call; visibility, collision and shadows, animation, particles and the frame list's recorders on workers; effects applied in the game's order. | G3.7 |
| P-146 | Frame-list building, skeleton evaluation and collision results live in file-scope or member state that only one caller at a time can use; the text writer, matrices, fur, sort pivot and particle work are the same. | Per-worker or per-caller state in each; query results owned by the caller. | G3.7 |
| N-1083 | The game thread copies every load from the mapped store: a page not in memory is a drive read inside a step. | What a step loads is read ahead by the loader's thread. | G3.7 |
| P-283 | Achievements, stats and the Mii library load on their first call, on the caller's thread with their lock held, so file work can happen inside a step. | Each store loaded at start on the loader thread; the game's calls read memory only. | G3.7 |
| P-265 | A category's run keeps a cursor into a vector that may move, and a removal at or before the cursor skips an object for the pass. | The run iterates by index against the live size; adds and removes during a run are applied at its end. | G3.1 |
| P-241 | A collision query that grows the keeper's array frees the array earlier results point into (and an unknown halo name dereferences null at init). | Query results owned by the caller (G3.3); an unknown halo name refused at load with a message (a null check, G3.7). | G3.7 |


---

## G4 The step's speed

This package makes the game step cheaper on one thread: collision queries, sensor and gravity lookups, arenas and pools, and a few walks that repeat work. Every change keeps the step's result exactly the same. A player gets a steadier step time, most of all on the tablet and in crowded scenes.

- **Design:** each row's own note and its finding in the audit of 2026-11-17 (audit file B, round 8, "R8.1 Game step: findings", "R8.3 Top ten", and the area 4 findings for P-013 and P-039; the finding id is the row id). FM 1.10 and 2.6 place the rows as "after the foundation: the step's speed"; FM 2.17 calls P-129 to P-141 "collision and sensors found by position", faster single-threaded work that is not the worker pool's. The old commits are on `app/land1`.
- **Needs first:** G3.3 (the skeleton: collision queries with caller buffers and shadows on workers). G4.1 to G4.4 also need G4.0.
- **Rows:** 21 (N-1076, N-1077, P-013, P-039, P-088, P-129, P-130, P-131, P-132, P-133, P-134, P-135, P-136, P-137, P-138, P-139, P-141, P-165, P-238, P-240, P-247)

Common to every landing of G4 (the skeleton): the replay is identical, and the step's time is measured on the tablet's five heaviest tour stops and on the PC before and after, never slower. Line numbers below are as cited at 241395b49 (the rows' own commit) unless a card says "as cited in audit file B" (the audit of 2026-11-17, whose lines may have moved). The audit's proof method, used by every card here: record one input file per galaxy, run the old and the new build with the deterministic step, and after every step hash every live actor's position, velocity, gravity, nerve and step and the binder's ground, wall and roof planes; the two builds give the same hash at every step.

### G4.0 Take the step-speed commits already written

- **What changes.** The step-speed commits written earlier sit on the frozen branch `app/land1`. They are taken one row a landing and each is checked again against what G3 changed, because G3.3 changes the same collision code. Two open rows about those commits are checked with them. N-1077: tour stop 082-ReverseKingdomGalaxy-5's arrive picture differed by 199 pixels on every renderer and every disc (a crystal's sparkles, the same in every run), seen by the main gate of `app/land1`. The row's own plan is to find the cause by the step hash when the commits land in small groups. N-1076: Direct3D 12 tour stop 059-KoopaBattleVs2Galaxy-1 ended with 0xC0000374 (heap corruption) near frame 98, the faulting stack a pipeline worker in `Pipelines::build` into D3D12Core and the NVIDIA driver. It is the first such end in any gate. It was not seen on main in 50 runs, so it is looked for with these commits.
- **Where.** Branch `app/land1` (exists locally and at origin/app/land1). The gate run `D:/NebulaWork/build/gate-runs/20261118-132111-37fcfab5e-main` (the run that found both). `D:/NebulaWork/plan/a1/queue.py` runs the stop in a loop; its results go to `D:/NebulaWork/build/crash059-<commit>/results.tsv` (folders `crash059-697535a86` and `crash059-cce47fc21` exist).
- **Rows.** N-1077: whole, closed when no group of commits changes that picture, or the cause is named and fixed. N-1076: whole, closed when the stop runs clean with the commits on, or the cause is named and fixed.
- **Needs first.** G3.3.
- **Old path.** None: nothing draws differently; the commits replace the code they touch.
- **Proof.**
  1. Each group of commits: the replay's step hash is identical to main's.
  2. Tour stop 082's arrive picture compared on every renderer and every disc after each group; a difference is traced to its commit by the step hash.
  3. Tour stop 059 on Direct3D 12 run again in the loop with the commits on, with the sanitizer build mended first (N-1062, the row's own condition). The row's earlier count is 50 runs on main with no crash.
  4. The common measure above, once per row landed.
- **For a player.** Nothing visible, unless the sparkle difference or the crash is found and fixed.
- **Read first.** The sources give no list of the commits on `app/land1`. Read its `git log` first. The planner's file `D:/NebulaWork/plan/07-BASE-FIRST.md` (block "E7 as it is coded") names P-013, P-038, P-039, P-088, P-127 to P-141 and P-152 as that package's 20 rows, about 22 commits, one row each. P-165, P-238, P-240 and P-247 are not in that list (they were found on 11-18, after it), so they are written new in their group. P-038, P-127, P-128 and P-152 belong to R4.8 and R6.5, not here.

### G4.1 Collision

Row P-138's own fix stays here (plan decision, checker decision 4): R6.5 takes the binder's still test as it stands on main after this landing.

- **What changes.** Today the collision server finds parts and prisms by walking everything. The queries must find parts by position and keep the order of their hits. Each fault is its own item (plan decision: the row closes when its last fault is done).
  - P-129: every collision query walks every part in the global zone and in each overlapping zone; zone 0 has no bounds, so a part placed there is tested by every query. Cost is actors times parts per step; the binder issues these queries for every bound actor every step. The audit's fix: a bounding-volume tree or a uniform grid over part spheres, rebuilt from the parts that moved this step (`mStillCount == 0`), candidates sorted by their index in the zone before testing, so the per-part tests, the hit lists and their order are unchanged.
  - P-130: a moving part whose sphere touches its zone's box edge recomputes the whole zone's bounds every step (two passes over all parts, one square root each). The audit's fix: recompute once per zone per step after the loop, which keeps the bounds bit-identical; growing the box by union is allowed too, because the bounds only gate queries.
  - P-131: once any ball or line query has filled 64 hits, every later query allocates three heap arrays per part tested, for the rest of the scene (the room for hits only grows). The audit's fix: cap the per-part working list at the stack room and loop (the visit order is fixed, so a cap and a second pass give the same list), or keep the three working arrays as members of the keeper sized to the hit room.
  - P-132: a ball query against a moving part inverts the part's previous matrix and takes the inverse matrix's scale (three square roots) on every query. The audit's fix: cache the inverse previous matrix and the average inverse scale when the part's matrix changes (`updateMtx`, `resetAllMtxPrivate`); the same operations on the same inputs, so bit-identical.
  - P-133: prism lists remove duplicates by a linear search over the hits found so far, and a prism that misses is retested in every block it appears in. The audit's fix: a per-query visited stamp per prism (a 32-bit array per server stamped with a query counter); the hit order is unchanged.
  - P-134: each prism stores one vertex plus normals, and the other two vertices are rebuilt at every use with a cross product and a divide (cooked prism record: the height, one position, the face normal, three edge normals and the attribute row). The audit's fix: the cook adds the two derived vertices, computed with the same function, so the values are bit-identical and the read is a load.
  - P-138: a bound actor that does not move runs the same map query twice a step (the loop in `findBindedPos` runs at index 0 and 1 at one position). The fix: skip the second when the step is zero and the first ran. The audit adds that `Binder::bind` inserts and removes its extra collision part in the global zone on every call, the removal a linear search; G3.3 passes the extra part to a query as a candidate, so read whether it is gone before touching it.
- **Where.** `native/petari/src/Game/Map/CollisionCategorizedKeeper.cpp` (query loops; FM 2.17 cites `:14`; as cited in audit file B: `:94-480` the queries, `:587-634` the zone bounds, `:515-519` `growHitInfo`, `:668-680` removal from the global zone), `native/petari/src/Game/Map/CollisionParts.cpp` (as cited in audit file B: `StrikeLists` `:296-318`, `:338`, `updateMtx` `:147-184`; FM 2.17 cites `sAreaPrisms`, `:527-530`), `native/petari/src/Game/Map/KCollision.cpp` (`:266`, `:324`, `:331`, `getPos` `:794-827`, as cited in audit file B), `native/petari/src/Game/LiveActor/Binder.cpp` (`sLocalPlanes`, `:78`; `findBindedPos` `:198-236` as cited in audit file B; P-138's still test is in the binder, FM 2.14). P-134 also changes the cook: `crates/nebula-import/src/cook/kcl.rs:8-9`. The build with hook points (not the plain module) has a second path in the zone-bounds code (`:61-64`); change it too.
- **Rows.** P-129, P-130, P-131, P-132, P-133, P-134, P-138: each whole.
- **Needs first.** G4.0.
- **Old path.** None: the code is replaced in place. P-134 changes a cooked record, so the three discs' game data are cooked again (the sources give no version number; read the cook first).
- **Proof.**
  1. The replay's step hash is identical (the order of query results is kept).
  2. P-129: a debug build runs both searches per query and compares the hit lists (audit file B, R8.3 item 1); step-hash replay in a galaxy with many moving parts (the audit names Bowser's battle galaxies).
  3. P-131: step-hash replay in a galaxy that makes the hit room grow (log `growHitInfo` first, audit file B).
  4. Step time on the tablet's five heaviest stops and on the PC, before and after, never slower.
  5. P-134: the cook's new prism record agrees with the old one for every prism of the three discs (the sources name no test for this row; the claim is that nothing a query reads changes).
- **For a player.** Nothing visible: it is how the code is built; heavy scenes step faster.
- **Read first.** The audit gives a fix for each fault above; where it offers two, choose by reading the code. Whether the hit room passes 64 in play, and the number of parts tested per query, were never measured (audit file B: "needs a run"); measure first and write the figure on the row.

### G4.2 Gravity, sensors, the judge

- **What changes.** FM 2.17 states the form for the first two: found by position. Each fault is its own item (plan decision: a row closes when its last fault is done).
  - P-139: gravity is found by testing every registered gravity for every actor and every shadow each step, with no spatial filter (cost: actors plus shadows, times gravities; each shape's range test comes after its own vector work). The audit's fix: a bounding sphere per gravity (shape extent plus range plus distance; an infinite range is always kept) in a grid or sorted list, the rest tested in the same priority order, so the sum and the gravity chosen are unchanged. The bound must be conservative for every shape (cube, cone, torus, segment, wire).
  - P-039: sensor hits are found by testing every pair between groups each step (13 group pairs plus character against character; the products of counts reach the hundreds in a busy scene). The audit's fix: a uniform grid or sort-and-sweep per group pair yields candidate pairs, sorted by (i, j) so the hits keep the original order, because gameplay reads the order of `addHitSensor`. An entry with a NaN position is a candidate of every pair (the current test counts NaN as a hit). The build with hook points calls `isSensorActive` per pair and keeps doing so; it is a second path that needs the same change.
  - P-136: looking up a sensor by name hashes the string at every call. The audit's fix: a compile-time hash at the call site (a constexpr overload taking a literal). The row's sites: the stage name is compared as a string every step (`WaterBazooka.cpp:651`; the same test runs once at init); sensors are found by name in 23 enemy files (Hanachan 24 a step); the star ball asks `getSensor("Body")` for every hit pair each step (`Tamakoro.cpp:345`); a clip set by name hashes it and scans every single-clip entry (`getSingleIndex`, `XanimeResource.cpp:188-196`), then `findResMotion` scans the swap table with `strcasecmp` and asks the motion table twice (`:206-210`, `:224-236`); Mario does this up to four times when his mud or bee wall walk starts (`MarioActorParts.cpp:265-285`).
  - P-135: the clipping pass calls `MR::getClippingJudge`, a scene-object lookup, once per target actor per step, and for every clipped actor tests the drawn-while-clipped frustum again each step. The audit's fix: pass the judge into `judgeClipping` once per step, bit-identical (the audit calls it small and lists it for completeness).
- **Where.** Under `native/petari/src/Game/`: `native/petari/src/Game/Gravity/GravityFollower.cpp:13-24`, `native/petari/src/Game/Gravity/PlanetGravityManager.cpp:30-33` (as cited in audit file B the gravity loop is `:16-71`), `native/petari/src/Game/LiveActor/SensorHitChecker.cpp` (`:13`, `:65-72`; as cited in audit file B `:118-207` the pair loops, `:21-41` the sphere test), `native/petari/src/Game/LiveActor/HitSensorKeeper.cpp` (`:40-54`, `:103-117`, as cited in audit file B), `native/petari/src/Game/LiveActor/LiveActor.cpp` (`getSensor`), `native/petari/src/Game/Enemy/WaterBazooka.cpp`, `native/petari/src/Game/Ride/Tamakoro.cpp`, `native/petari/src/Game/Animation/XanimeResource.cpp`, `native/petari/src/Game/Player/MarioActorParts.cpp`, `native/petari/src/Game/LiveActor/ClippingJudge.cpp`, `native/petari/src/Game/LiveActor/ClippingActorHolder.cpp` (`:36-42`), `native/petari/src/Game/LiveActor/ClippingActorInfo.cpp` (`:39-56`, `:100-115`), `native/petari/src/Game/LiveActor/ClippingDirector.cpp` (`:94-97`; the last three as cited in audit file B).
- **Rows.** P-139, P-039, P-136, P-135: each whole.
- **Needs first.** G4.1.
- **Old path.** None: the code is replaced in place.
- **Proof.**
  1. The replay's step hash is identical, including each sensor's list contents (P-039).
  2. P-139: a debug build computes both paths and compares every query, then the step-hash replay (audit file B, R8.3 item 3).
  3. Step time on the tablet's five heaviest stops and on the PC, before and after, never slower.
  4. P-136: a search finds no name hash or name compare left in the per-step code the row lists (the files above); the sources give no count to match.
- **For a player.** Nothing visible: it is how the code is built; crowded scenes step faster.
- **Read first.** G5 changes the same judge and the gravity follower's skip (FM 2.18, G5.2). G5 comes after G4, so P-135 is done on today's judge. The gain of P-039 and P-139 was never measured: the audit asks for the count of pairs and gravities tested per step in a busy galaxy; take it before and after.

### G4.3 Arenas, pools, emitters

- **What changes.** Each fault is its own item (plan decision).
  - P-013: every object construction (`ArenaOwned::ArenaOwned` calls `NebulaArena::findFromRoot`) and every `delete` (`free` calls `findFromRoot`) scans all arenas linearly, so the cost is the number of live arenas (a scene's, each layout's, each added content's). The audit's fix: the arena spans kept sorted and binary searched, or the owning arena's pointer put in the block header (8 more bytes). The engine's heaps are gone and the game uses `NebulaArena`; new and delete belong to one thread (P-012, fixed). Allocation addresses may change; nothing should depend on them, and the step-hash replay is the check.
  - P-137: picking a free actor from a pool (`LiveActorGroup::getDeadActor`, used by coins, star pieces, effect actors) scans the pool from its start every time. The audit's fix: a free list, or a "first maybe dead" index, that takes the lowest dead index, so the same actor is chosen.
  - P-247: pools that are empty make actors inside the step: `CoinHolder` makes 32 coins and runs their after-placement init in the step that asks (`takeDeadCoin`, `CoinHolder.cpp:131-152`), and by the report the air bubble, big bubble, star piece and pressure bullet pools do the same. After the change pools are made at load to the scene's count and grown off the step when added content asks for more. And a dynamic object's collision is one octree leaf holding every prism (`DynamicCollisionObj::createCollision`, `DynamicCollisionObj.cpp:146-170`), so each query tests every triangle; it becomes a BVH (bounding-volume tree) refitted when its vertices move. The query order is kept.
  - P-141: the emitter pool is walked in full, empty slots included, up to three times a step: `ParticleEmitterHolder::update` walks every slot of every chunk, and `movementNormal`, `movementIgnorePause3D` and `movementIgnorePause2D` each call it (the 2D one repeats the 3D one's walk, because `update(true)` acts only on group 1). The pool starts at 512 emitters. The audit's fix: walk a list of live emitters in slot order (`EmitterManager` already keeps live lists per group) and drop the duplicate `update(true)`.
- **Where.** Under `native/petari/src/Game/`: `native/petari/src/Game/System/NebulaArena.cpp` (as cited in audit file B: `ArenaOwned` `:109-113`, `free` `:309-316`, `findFromRoot` `:318-326`; `:115-118` is P-232's unchecked growth, G8.1), `native/petari/src/Game/LiveActor/LiveActorGroup.cpp` (`:18-26`, `getDeadActor`, as cited in audit file B), `native/petari/src/Game/MapObj/CoinHolder.cpp`, `native/petari/src/Game/MapObj/StarPieceDirector.cpp:257-263`, `native/petari/src/Game/MapObj/AirBubbleHolder.cpp:14-26`, `native/petari/src/Game/MapObj/BigBubbleHolder.cpp:65-76`, `native/petari/src/Game/MapObj/DynamicCollisionObj.cpp`, `native/petari/src/Game/Effect/ParticleEmitterHolder.cpp` (`:23-46`), `native/petari/src/Game/Effect/ParticleCalcExecutor.cpp` (`:30-53`), and `native/petari/src/Game/Particle/EmitterManager.cpp` (FM 2.17 cites `:65` for its `EmitterWork`; the live lists are at `:113-121`, as cited in audit file B).
- **Rows.** P-013, P-137, P-247, P-141: each whole.
- **Needs first.** G4.2.
- **Old path.** None: the code is replaced in place.
- **Proof.**
  1. The replay's step hash is identical (query order kept, as the P-247 note says).
  2. P-013: the arena tests (allocate, free, grow, destroy, `findFromRoot` over many arenas); the load-time logs (`loads.py`) before and after (audit file B).
  3. Step time on the tablet's five heaviest stops and on the PC, before and after, never slower.
  4. The hitch rows: the step that first needs a coin, bubble or star piece makes no actors (the sources name no test; the row's claim is the check).
  5. The sanitizer build still sees the arenas: `sanitizer/tour` with `unit/game-asan` passes, and the arena tests that ask the sanitizer which bytes are poisoned (`native/tests/arena_tests.cpp`) are kept in step with the new header.
- **For a player.** Fewer small hitches when many coins or bubbles appear; nothing else visible.
- **Read first.** G8.4 (P-192) classes the same pools; this landing makes them at load and G8.4 lets them grow. The number of live arenas in play was never logged (audit file B: log it once a second in a busy galaxy first).

### G4.4 Small walks

- **What changes.** Each fault is its own item (plan decision).
  - P-238: hiding or showing a joint and its children visits each joint once for every joint above it on its first-child chain, so deep skeletons cost exponentially (`JointUtil.cpp:95-104`, `:128-137`). Fix: one walk over the subtree; the result is identical because the hide is idempotent.
  - P-238, second fault: the text-box recursion looks every child pane up again by name from the root (`LayoutUtil.cpp:164-180`), so a child whose name another pane shares gets that pane's operation. Fix: the recursion passes the pane itself.
  - P-240: every local stage-switch read or write finds its zone's switch set by a linear scan, and `isOnAllSwitchAfterB` repeats it for each switch of its range (`StageSwitch.cpp:77-87`, `:147-160`). A zone's bits are reallocated inside the step when a higher switch is first set (`ZoneSwitch::set`, `:21-36`). The ocean ring's nearest point scans every segment, several times a step (`OceanRing.cpp:187-200`). The form: a `SwitchIdInfo` resolves its zone's switch set once at creation, the bits are sized at placement from the highest switch named, and the ring's search starts from the last segment found.
  - P-165: every `NebulaText` lookup is a linear scan of the 1,292-row table (`find` and `english` loop over `cNebulaTextRows` with a string compare); the settings menu translates every changed row's label and value through it, a miss is split into pieces and each looked up again, a page change refills every row, and the achievement board does the same for each card. And `NebulaText::get` with a growing buffer expands each text twice (measure, then get). The audit's fix: the table emitted sorted for a binary search, or as a perfect hash (the table is generated by `tools/nebula_text.py`), and `get` returning the pointer and length from one pass.
  - P-088: the open settings menu builds its page twice a frame on the game thread: once for the count and once for the rows. For CONTROLS every row goes through `controls_row`, which copies the whole `controls_rows()` vector, and that calls `joycon_status()` and the mod-action list each time; a key builds `controls_row` up to six times in one test (`native_settings.cpp:1257-1261`); about 2N+2 full rebuilds a frame for N rows. A slider rebuilds its page per drag step (`:1794-1820`) and the per-frame view build is `:6187-6235`. The audit's fix: build the page once a frame, pass its size and rows together, and compute `controls_rows()` once per build.
- **Where.** Under `native/petari/src/Game/`: `native/petari/src/Game/Util/JointUtil.cpp:95-104`, `:128-137`, `native/petari/src/Game/Util/LayoutUtil.cpp:164-180`, `native/petari/src/Game/Map/StageSwitch.cpp`, `native/petari/src/Game/Map/OceanRing.cpp:187-200`, `native/petari/src/Game/System/NebulaText.cpp` (with `NebulaText.hpp` and `NebulaTextTable.inc`; its generator is `tools/nebula_text.py`); and `runtime/src/ui/native_settings.cpp:1257-1261`, `:1794-1820`, `:6187-6235`. The page builds are in `build_native_settings_view` (as cited in audit file B).
- **Rows.** P-238, P-240, P-165, P-088: each whole.
- **Needs first.** G4.3.
- **Old path.** None: the code is replaced in place.
- **Proof.**
  1. The replay's step hash is identical (the row notes for P-238 and P-240 say so).
  2. Step time on the tablet's five heaviest stops and on the PC, before and after, never slower.
  3. P-238: a deep skeleton's hide and show visits each joint once (the sources name no test).
  4. P-088: the settings menu builds a page once for a frame, and a CONTROLS key builds its row list once (the sources name no test). The audit's own run: open CONTROLS with a controller and several added actions, and log the step time against the menu closed.
  5. P-165: the audit's own run: time `fillTexts` on a page change of the graphics page in Japanese with the frame-phase timer, before and after.
- **For a player.** The settings menu answers faster on slow devices; nothing else visible.
- **Read first.** G7.3 moves `NebulaText.txt`'s text onto the text builder (`localization/NebulaText.tsv` is the file at 790626cb5; `NebulaText.txt` is not found at 790626cb5): whichever lands second reads the other's form. The settings menu's `native_settings.cpp` runs on the game thread, which is why it is in this package.

### Rows of this package

| Row | Today | After | Closes at |
| --- | --- | --- | --- |
| N-1076 | A Direct3D 12 tour stop (059-KoopaBattleVs2Galaxy-1) ended with heap corruption near frame 98, in a pipeline worker; first such end in any gate; not seen on main in 50 runs. | Checked when the step-speed commits are taken: the stop runs clean with them, or the cause is named and fixed; sanitizer build mended first (N-1062). | G4.0 |
| N-1077 | Tour stop 082-ReverseKingdomGalaxy-5's arrive picture differs by 199 pixels on every renderer and disc: a crystal's sparkles, the same every run. | Found by the step hash as the commits land in small groups; the picture matches at every group. | G4.0 |
| P-013 | Every object construction and every `delete` searches all arenas linearly. | Arena spans sorted and binary searched, or the owning arena's pointer in the block header; step hash identical. | G4.3 |
| P-039 | Sensor hits are found by testing every pair between groups, each step. | Found by position (grid or sweep per group pair), candidates sorted so the order of hits is unchanged; NaN positions stay candidates. | G4.2 |
| P-088 | The open settings menu builds its page twice a frame on the game thread; the CONTROLS page rebuilds its row list several times per key; sliders rebuild per drag step. | A page built once a frame with its size and rows together; `controls_rows()` computed once per build. | G4.4 |
| P-129 | Every collision query walks every part in the global zone and each overlapping zone; no spatial index over parts. | Parts found by position, candidates tested in zone order so hits and their order are unchanged. | G4.1 |
| P-130 | A moving part whose sphere touches its zone's box edge recomputes the whole zone's bounds every step. | Bounds recomputed once per zone per step (bit-identical), or grown by union. | G4.1 |
| P-131 | After any ball or line query fills 64 hits, every later query allocates three heap arrays per part tested. | The working list capped at the stack room with a second pass, or the arrays kept as keeper members. | G4.1 |
| P-132 | Ball queries against a moving part invert the part's previous matrix and take the inverse's scale (three square roots) each time. | Inverse previous matrix and average scale cached when the part's matrix changes; bit-identical. | G4.1 |
| P-133 | Prism lists remove duplicates by linear search; a prism that misses is retested in every block it appears in. | A visited stamp per prism per query; hit order unchanged. | G4.1 |
| P-134 | Each collision prism stores one vertex plus normals; the other two vertices are rebuilt at every use. | The cook adds the two derived vertices, computed by the same function; the read is a load. | G4.1 |
| P-135 | The clipping pass calls `MR::getClippingJudge`, a scene-object lookup, once per target actor per step, and re-tests the drawn-while-clipped frustum of each clipped actor. | The judge passed into `judgeClipping` once per step; bit-identical. | G4.2 |
| P-136 | Sensors, clips and the stage name are looked up by name (hashed or compared) in per-step code, in 23 enemy files and Mario's animation starts. | A compile-time hash at the call site; no name hash or compare left in the listed per-step code. | G4.2 |
| P-137 | Picking a free actor from a pool scans the pool from the start every time. | A free list or first-maybe-dead index that takes the lowest dead index; the same actor is chosen. | G4.3 |
| P-138 | A bound actor that does not move runs the same map query twice per step. | The second query skipped when the step is zero and the first ran. | G4.1 |
| P-139 | Gravity is found by testing every registered gravity for every actor and every shadow each step; no spatial filter. | A bounding sphere per gravity in a grid or sorted list, tested in the same priority order; bound conservative for every shape. | G4.2 |
| P-141 | The emitter pool is walked in full, empty slots included, up to three times a step. | A list of live emitters walked in slot order; the duplicate `update(true)` dropped. | G4.3 |
| P-165 | Every `NebulaText` lookup is a linear scan of the 1,292-row table; `NebulaText::get` with a growing buffer expands each text twice. | The table sorted for binary search or a perfect hash; one pass returns the pointer and length. | G4.4 |
| P-238 | Hiding or showing a joint visits each joint once for every joint above it on its first-child chain; text-box recursion looks each child up by name from the root. | One walk over the subtree; the recursion passes the pane itself. Result identical. | G4.4 |
| P-240 | Stage-switch reads find the zone's set by linear scan; a zone's bits are reallocated in the step; the ocean ring scans every segment. | `SwitchIdInfo` resolves the set once; bits sized at placement; ring search starts from the last segment found. | G4.4 |
| P-247 | Pools make actors inside the step when empty (32 coins and more); a dynamic collision object is one octree leaf of every prism. | Pools made at load to the scene's count, grown off the step; a BVH refitted when vertices move. Query order kept. | G4.3 |


---

## G5 Every actor runs, the same on every screen

This package stops the game from playing differently on different screens. Today an actor is stopped when it is "clipped", and the volume that decides this is built from the screen's aspect, so a wide screen runs more actors than a narrow one (N-1034: the tablet and the PC differed in steps at 100 of 101 tour stops). After it, every actor of the loaded scene runs every step, and every rule that asks whether something is seen asks one reference view volume, the same on every screen. A player gets the same game on a tablet, a phone, a 4:3 monitor and a 48:9 monitor.

- **Design:** FM 2.18 (the owner's decisions of file 08 5.H.2 and the rows' "Owner 11-19" answers; landings 1 to 5; the table of the 42 reads), FM 2.17 rule 5 and rule 3 (actor movement stays on the game thread in list order), INV section 3 (the rows "What the game runs follows the screen's shape" and "Game rules that read the screen's shape or size"), and each row's note.
- **Needs first:** G3.2 (visibility's judge) and G4 (the skeleton). G5.3 also needs R4 (the draw records of E3 step 4).
- **Rows:** 4 (P-191, P-243, P-253, P-256). Also P-248 (part): three of its parts are placed in G5.3 (plan decision); the row itself is in R4's file (its punch box part is R4.3) and closes at G5.3.

File names below are under `native/petari/src/Game/` (folders `Enemy`, `Boss`, `MapObj`, `NPC`, `LiveActor`, `Camera`, `Util`, `Demo`, `Map`, `Effect`, `Ride`, `System`), each found at 790626cb5.

Common to every landing: behind a switch ("run all" or "run by the judge"), the old path beside it; with the switch off the replay is identical to today's. Line numbers are as cited at 241395b49 (the rows) and 697535a86 (FM 2.18's table). Every one of the 42 reads of the clipped state (the table after the last card) is named in the landing that changes it and read whole there.

### G5.1 Landing 1: the "seen" judge on the reference volume

- **What changes.** Today `ClippingJudge` builds its volumes with `MR::getAspect()`. The landing adds a second fact beside "runs": "seen", an answer the step computes once for each actor against one reference view volume and stores per actor. The volume is the game camera's position and direction with a size at least as wide and as tall as the widest and tallest picture offered (48:9 and the tallest phone shape), the game's far distances kept. Its numbers are data in one place and are never read from the window, so a screen added later widens no rule unless the data says so. The four reads that ask "is it seen" take this answer (`CrystalCage.cpp:190`, `MogucchiHill.cpp:130`, `Candlestand.cpp:94`, `PhantomTorch.cpp:75`). The rules P-243 names that test the frustum or the screen position take it too (see Read first). "Seen" is asked of one volume made from the game camera; a second camera asks its own, and a rule may say "seen by any". With the switch off the judge's numbers are today's, so this landing alone changes nothing. The cost lands on the worker pool: the "seen" judge is visibility's judge of G3.2 (FM 2.18 target 4; the actors run in list order on the game thread, FM 2.17 rule 3).
- **Where.** `native/petari/src/Game/LiveActor/ClippingJudge.cpp:20-21`, `:33`, `:42`; `ClippingActorHolder.cpp` (`movement`, `:36` as FM 2.17 cites it); `ClippingActorInfo.cpp:78-114` and `LiveActor.cpp:292` (`sClipScope`, one global mode, FM 2.17); the four reads above. The 16 machinery reads are listed in the table after the last card.
- **Rows.** P-191: advances (the judge). P-243: advances (the reads that ask "is it seen").
- **Needs first.** G3.2, G4.
- **Old path.** The switch (run all, or run by the judge). With it off the judge is today's. The sources do not say which landing deletes the old judge (see G5.5).
- **Proof.**
  1. With the switch off, the replay's step hash is identical to today's over the tour and the replay suite.
  2. The four reads, and each frustum rule of Read first, read whole and checked under the new rule, and named in this landing (FM 2.18's proof).
- **For a player.** Nothing visible: with the switch off nothing changes.
- **Read first.** P-243's frustum rules do not all appear in the 42 reads, because those were found by `git grep -n -E "isClipped|isInvalidClipping|isClippingInvalid"`. The row's decision (Agent 1, 11-19): every rule that asks whether an actor is seen asks the reference volume. The sources give no count of such calls; search for the frustum-test calls first. Each rule is its own item (plan decision: P-243 closes when the last is done), read whole and changed in this landing:
  - `BasaBasa.cpp:658-685`: goes home after `cClipResetSteps` out of view and away from the player (`isJudgedToClipFrustum` at `:664`).
  - `Gesso.cpp:417-433`: goes home after 5 s out of view or past 10,000 units.
  - `Petari.cpp:458-466`: jumps out only while on screen.
  - `BreakableCage.cpp:342-350`: its 6-step scene stop runs only in view.
  - `MeteorStrike.cpp:302-307`: a meteor moves on by the frustum.
  - `SpaceShipStep.cpp:94-122`: resets after 4 s unseen.
  - `StarPiece.cpp:304-312`: a clipped star piece that is not floating or on a rail dies.
  - `PowerStar.cpp:574-583`: the comet's power star hides by the frustum.
  - `StrayTico.cpp:283-285`: a following Luma is warped back only when it and its warp point are both out of view.
  - `TicoFat.cpp:567-574`: the fat Luma's guidance, near state and feeding by pointer start only while it projects onto the screen.
  - `TripodBoss.cpp:986-996` (through `TripodBossAccesser.cpp:47-53`): before Megaleg's fight every boss part is made dead or alive by a frustum test of one sphere.
  - `SkeletalFishGuard.cpp:548-551`: a shark's guard breaks away only while it projects onto the screen.
  - `BigBubble.cpp:169-178` (and `:1086`): tests the frustum in its step and, outside it, does not move its control vertices, from which another bubble's merge is placed.
  The split of these rules between landing 1 (they ask "seen") and landing 2 (their only trigger is `startClipped`, below) is made by the row's wording: the rules above ask the frustum, so they are this landing's.

### G5.2 Landing 2: running split from the judge

- **What changes.** When the switch is on, the holder stops calling `startClipped`; it judges each actor once a step against the reference volume and stores "seen". The eight reads that stop work while an actor is stopped are then never true: the actor runs, so its gravity follows, its sensors hit and its timers count. The six forced-running calls (`invalidateClipping` and its readers) have no effect on running, which is always, and stay, because they mean "must run" with the switch off. Actors run in list order on the game thread (FM 2.17 rule 3). Rules whose only trigger is `startClipped` or `endClipped` are moved to ask "seen" in the reference volume before the holder stops calling it. Each is its own item (plan decision):
  - an escaping `BombBird` ends only when it becomes clipped (`BombBird.cpp:126-132`): the row says it keeps its rule on the reference volume;
  - a `Kameck` sleeps only from `startClipped` (`Kameck.cpp:166-169`): the same;
  - a `CocoNut` returns to its spawn and is put back only through `startClipped` (`CocoNut.cpp:96-122`);
  - `Dodoryu`'s clip hooks change its rabbit's clipping and its state's (`Dodoryu.cpp:333-343`);
  - the bird lice are held and released by calling `startClipped` and `endClipped` (`SkeletalFishBossBattleDirector.cpp:124-134`): a hold state of their own keeps the replay.
  The row names no further form for `CocoNut` and `Dodoryu` than "same questions": read each whole.
- **Where.** The eight "stopped" reads: `CameraTargetObj.cpp:30`, `:97`; `GravityFollower.cpp:16`; `SensorHitChecker.cpp:13`; `OceanHomeMapCtrl.cpp:52`; `FlashingCtrl.cpp:24`; `MirrorReflectionModel.cpp:55`; `PartsModel.cpp:93`. The six forced-running calls: `DemoCtrlBase.cpp:64`, `DemoExecutor.cpp:103`, `Butterfly.cpp:339`, `:349`, `MorphItemObjNeo.cpp:649`, `TalkDirector.cpp:300`. The machinery's reads and `PlanetGravityManager.cpp:30-33` (a gravity whose follower is not valid is skipped).
- **Rows.** P-191: advances (running split). P-243: advances (the startClipped rules). P-253: advances (always running covers the view groups; done in G5.4).
- **Needs first.** G5.1.
- **Old path.** The same switch: with it off the holder calls `startClipped` as today.
- **Proof.**
  1. Switch off: replay's step hash identical.
  2. Switch on: the eight "stopped" reads are never true (checked one by one, each read whole); the six forced-running calls change nothing.
  3. The tour stops stay green and the `soak` check passes with the switch on (the sources name no figure for this landing alone; the measure is G5.5's).
- **For a player.** With the switch on, things far from the camera keep moving: gravity pulls, sensors hit, timers count. With it off, nothing changes.
- **Read first.** Under (c), the last two rules of P-243's first group "lose their trigger" (the row); the row's decision keeps them on the reference volume, as written above. FM 2.18 names no landing for them; they cannot wait past this one because `startClipped` stops being called here.

### G5.3 Landing 3: drawing culled per picture

- **What changes.** Eight reads use the clipped state to decide whether to draw. Their picture is now culled by each picture's own real view, apart from the step, on E3's draw records. Under "run all" an actor that runs far from the camera is still not drawn when no picture sees it. Each of the eight is read whole and changed to the picture's own culling. Three choices that P-248 names are made in the step for one camera, and they come here as well (plan decision; the punch box part of P-248 stays in R4.3; the row closes here):
  - a boss part hides its model by a frustum test in the step (`TripodBossFixPartsBase.cpp:102-106`);
  - the shark's flat shadows are switched by the one camera's direction in `calcAnim` (`SkeletalFishBoss.cpp:255-271`);
  - the bubble picks its surface's subdivision, 1 to 6, from its projected radius and tessellates in `calcAnim` (`BigBubble.cpp:188-208`).
  Each is decided per picture, from that picture's own view, not in the step (the sources give no further form for these three; read each whole first).
- **Where.** `FlashingCtrl.cpp:47`; `MirrorActor.cpp:35`; `MirrorReflectionModel.cpp:34`; `PartsModel.cpp:67`; `FurCtrl.cpp:37`; `WoodBox.cpp:119`; `LumaContentFx.cpp:733`, `:908`; and for P-248's parts `TripodBossFixPartsBase.cpp:102-106`, `SkeletalFishBoss.cpp:255-271`, `BigBubble.cpp:188-208`. The draw records are R4's (FM 2.12).
- **Rows.** P-191: advances (drawing). P-248 (part): a boss part hidden by a view test inside the step. P-248 (part): the shark's flat shadows switched by the camera's direction. P-248 (part): the bubble's subdivision from its size on screen. The row closes here, after its punch box part (R4.3).
- **Needs first.** G5.2, R4.
- **Old path.** The same switch: with it off the drawing reads ask the clipped state as today.
- **Proof.**
  1. Switch off: replay and pictures unchanged.
  2. Switch on: every tour stop's pictures green on every renderer; the eight reads and P-248's three choices each named here and checked (the sources name no other test).
- **For a player.** Nothing visible: what is drawn is what a picture sees, as before.
- **Read first.** FM 2.18 says this landing is "on E3's records". Read R4's records first (R4.0: a block names its camera by role) to see how a draw names its view, then change the eight reads.

### G5.4 Landing 4: the actors' own skips

- **What changes.** Actors skip by themselves, apart from the clipped state, and each skip follows the screen or the player. Each is its own item (plan decision: P-191, P-243 and P-253 close when their last item is done).
  - The ocean bowl skips its step when the player is farther than 1000 plus its radius or the camera is in other water; its wave phases stop, and `calcWaterInfo`'s height, which gameplay reads, follows them (`OceanBowl.cpp:160-163`). Running it every step changes the replay; the owner's answer puts it with "run all" (P-191).
  - View groups stop and start every actor of a group by the cube the player stands in, and the port added a 30-step hold (`sInsideHoldSteps`, `ViewGroupCtrl.cpp:15`, `:60-99`), added against a player on a cube's edge stopping and starting a whole group every few steps. The hold goes, and the cubes stay only for what the game does on entering them (P-253).
  - Always-on effect objects were given the far maximum so torches stay lit (`SimpleEffectObj.cpp:75-82`), where the game used the level's distance (400 for `RandomEffectObj`, `:74-76`): the far distance of always-on effects goes (P-253).
  - The distance sleeps (Kuribo past 3000 units, PackunPetit past 5000 with sensors, animation and shadow off, and by the report Karikari, KoteBug, Poihana, TakoHei, Metbo and Begoman) follow "every actor always runs": the work is kept running every step (wake timing and far contacts change) (P-243).
  - The camera's wall-collision ball is sized inside the step from the render field of view and the screen's aspect (`CameraViewInterpolator.cpp:192-197`); it is sized from the reference volume instead (P-256, the owner's answer), and the renderer keeps the picture out of walls by its own near plane, nearer for wider pictures (the reversed depth already in use).
- **Where.** `OceanBowl.cpp:160-163`; `ViewGroupCtrl.cpp:15`, `:60-99`; `SimpleEffectObj.cpp:75-82`; `RandomEffectObj.cpp:74-76`; `Kuribo.cpp:469-487`; `PackunPetit.cpp:89-99`, `:489-496`; `CameraViewInterpolator.cpp:192-197`.
- **Rows.** P-191: advances (the ocean bowl). P-243: advances (distance sleeps). P-253: whole (the hold, the effects' far distance). P-256: whole (the ball).
- **Needs first.** G5.2.
- **Old path.** The same switch. With it off, each skip is today's.
- **Proof.**
  1. Switch off: replay's step hash identical.
  2. Switch on: the same inputs at 4:3, 16:9, 21:9, 48:9, the tablet's and a tall phone's shape give the same step hash at every step. The row P-256 notes that its read alone fails this test even with every actor running, so the ball is in before the test is run.
  3. Each changed skip read whole and named in this landing.
- **For a player.** The ocean's waves keep moving when you are far away; groups of actors do not stop half a second after you leave their cube; the camera is pushed out of walls the same on every screen.
- **Read first.** FM 2.18's five landings name P-256 in none. It is placed here, behind the same switch, because the proof's four-screen hash needs it before G5.5. The replay's reference changes where the camera stops against walls on every screen but the reference shape (the row). Two of P-243's questions stay with the row's decision (Agent 1): distance sleeps follow "always runs"; the rest keep their rule on the volume.

### G5.5 Landing 5: the measure and the choice

- **What changes.** With the switch on, the measure of file 00 4.3 is taken on the three Windows renderers and the tablet at each offered rate, and the step budget is checked. If every measure holds and no step passes its budget, (c) is kept: every actor runs every step, drawing is culled by the real view. If not, (b) is built: one run volume the same on every screen, with the same "seen" volume as its run volume. Either way it is taken on every device, with no list of devices. The replay's reference changes once; it is said on the board in this landing, and recorded again once.
- **Where.** `ClippingJudge.cpp:20-21`, `:33`, `:42`; the measure's scripts (the sources name them only as file 00 4.3 and the play route measure); the gate's replay and tour.
- **Rows.** P-191: whole. P-243: whole. P-253 and P-256: confirmed by the same test.
- **Needs first.** G5.1 to G5.4.
- **Old path.** FM 2.18 keeps "the old path beside it" through the landings and does not say when the switch-off judge is deleted. The switch and the old "run by the judge" path are deleted at the end of G5.5, whichever choice is kept (plan decision). If every actor runs, the judge only answers "seen". If one run volume is kept, the judge is that volume, built from the reference volume. Either way `ClippingJudge` no longer reads `MR::getAspect()` (plan decision). The final search below must find only the machinery and the forced-running calls.
- **Proof.**
  1. One test plays the same inputs at 4:3, 16:9, 21:9, 48:9, the tablet's shape and a tall phone's shape and gets the same step hash at every step.
  2. Every tour stop green and the `soak/long` check passes.
  3. The 42 reads each checked under the new rule and named in the landing that changed it.
  4. The search for the clipped state run again, `git grep -n -E "isClipped|isInvalidClipping|isClippingInvalid" -- 'native/petari/src/*.cpp'`: its hits only the machinery and the forced-running calls.
  5. The measure's figures, before and after, written on the row (file 08 5.H.2: "the row holds the figures before and after"): file 00 4.3's measures on the three Windows renderers and the tablet at each offered rate; no step of the play route or the tours over the step's budget.
- **For a player.** The same game on every screen shape, and a scene's far actors keep living.
- **Read first.** (b) and (c) are the owner's order ("B is ok but C if performance doesn't suffer", file 08 5.H.2). Which was kept is recorded on the board and in FM 2.18. Whether the switch stays and whether `MR::getAspect()` stays in `ClippingJudge` are decided: see Old path (plan decision).

### Rows of this package

| Row | Today | After | Closes at |
| --- | --- | --- | --- |
| P-191 | What the game runs follows the screen's shape: `ClippingJudge` builds its run volumes with `MR::getAspect()`; the tablet and the PC differed in steps at 100 of 101 stops; the ocean bowl and gravity follow the run volume too. | Every actor of the loaded scene runs every step, drawing culled by the real view; if a measure fails, one run volume the same on every screen. Replay reference changes once. | G5.5 |
| P-243 | Game rules read the camera frustum, whose width follows the screen's aspect (BasaBasa, Gesso, Petari, BombBird, Kameck, cages, bosses, NPCs); some actors sleep by distance from the player. | Every rule that asks "is it seen" asks one reference view volume, the same on every screen; the distance sleeps follow "every actor always runs". | G5.5 |
| P-253 | View groups stop and start actors by the player's cube, and the port added a 30-step hold; always-on effects were given the far maximum. | The hold goes with "every actor always runs"; no rule follows one player's place; the effects' far distance goes. | G5.4 |
| P-256 | The camera's wall-collision ball is sized from the render field of view and the screen's aspect, so the camera is pushed out of walls differently by window shape. | The ball is sized from the reference view volume (the widest and tallest picture), the same on every screen; the renderer's near plane keeps the picture out of walls. | G5.4 |

P-248 (part) is placed in G5.3 and is not in this package's rows file: its row stays in R4's table and closes at G5.3.


---

## G6 Sound

This package takes the console's forms out of the sound code: sequence data in the console's byte order, packed mix words and Q15 numbers, linear lookups, unchecked tables and handles, streams played from the mapped file, a music end told by the audio device, a fixed movie rate, and a surround output with no written law. The sound itself is kept as it was made (the owner's answer, file 08 5.H.5): the sound driver's tick, the filters made for 32 kHz, the volume ramp, the speaker's 6 kHz waves, the two-channel output. A player hears the same game; on a surround device the sound is placed by a stated, tested law, and a stuck or missing audio device no longer holds the game.

- **Design:** INV section 2 ("Sound" bullet), INV section 3 (the lines for P-186, P-193, P-219, P-221 to P-228), INV section 4 (the owner's answers; "What P-193 starts from" and the law's nine points, written 11-19 before the code), and each row's note. FM 1.10 places P-186, P-193 and the mixer's rows (P-037, P-071, P-072) after the foundation.
- **Needs first:** nothing in the skeleton. Inside the package: G6.2 before G6.7 (the law needs the buses named, P-221).
- **Order (the owner, 11-22, moved 11-23):** G6.2 and G6.7 are done first of the package: in `steps.tsv` they stand at orders 28 and 29, directly after the tablet's pacing steps G2.1 to G2.5 (orders 23 to 27, the owner's order of 11-23) and before G1, so the surround output can be tried early. The step tool makes the package's other steps wait for them (a step waits for its package-mates that stand before it in the file): G6.1 (order 115), G6.3 to G6.6 (116 to 119) and G6.8 (120) come after both. They need nothing of S0.2, S0.3, R1, R2 or G2.1 to G2.5 (their cards need nothing in the skeleton, and the table's needs column says S0.1, landed); G2.3 and G2.5 add texts to the settings before G6.7's row (5 of `02-NATIVE-OVERVIEW.md`), so G6.7 reads the settings files as they stand.
- **Rows:** 16 (P-037, P-071, P-072, P-186, P-193, P-219, P-221, P-222, P-223, P-224, P-225, P-226, P-227, P-228, P-271, P-286)

Common to the package (the owner, file 08 5.H.5): sound as made; no step is claimed "checked by ear". Line numbers are as cited at 241395b49.

### G6.1 Sound sequences in host order (P-186)

- **What changes.** The cooked sound sequences keep their data section in the console's byte order. The cook already decodes the operations (`mOps`); it now writes every table an operation reads in host order and typed, and the run time reads records. Each fault is its own item (plan decision: the row closes when the last is done):
  - `SeqTrack` swaps the data section at every read on the sound thread through `be16`, `be24`, `be32` (`SeqTrack.cpp:29-37`): envelope tables (`setOscTable`, `:706-724`), filter weights (`setFIR`, `:739-745`) and the table-reading operations (`SEQ_REG_TABLE`, `:1505-1525`).
  - The Me phrases' operands are built big-endian byte by byte at every read (`MeTrack::read16`, `read24`, `MeTrack.cpp:628-638`); they become operands in host order.
  - The interrupt, jump and call tables are 24-bit byte offsets of the disc's program (`SeqTrack.cpp:828`, `:1401`, `:1404`) turned into operations by a binary search at each jump (`opAt`, `:961-977`); they become tables of operation indices.
  - The rhythm parser's look-ahead decodes up to 1000 operations of the raw bytes with its own table of opcode shapes at every beat note (`RhythmSystem.cpp:247-287`) and moves the track when it fails (`:285`); it becomes a look-ahead over the cooked operations.
- **Where.** `native/petari/src/Game/Sound/SeqTrack.cpp`, `MeTrack.cpp`, `RhythmSystem.cpp`; the cook `crates/nebula-import/src/cook/sound.rs` (the sequence decode).
- **Rows.** P-186: whole.
- **Needs first.** Nothing.
- **Old path.** None named: the cook's output and the run-time reader change together (the sources give no switch; read the cook first).
- **Proof.**
  1. A test cooks each game sequence both ways and compares every value the operations read (the row).
  2. A search for `be16`, `be24`, `be32` in the sound code finds nothing (the inventory's byte-swap line lists the sequences' data as the only console byte order the game reads while it runs; the sources name no such search).
  3. The rhythm look-ahead gives the same decisions over the cooked operations as over the raw bytes (the sources name no test; read the row).
- **For a player.** Nothing visible or audible: it is how the code is built.
- **Read first.** G6.2 and G6.7 land before this step (they stand at orders 28 and 29 in `steps.tsv`). The cook's `sound.rs` is read as G6.2 left it (its sends and settings writer), and the line numbers of this card are as cited at 241395b49.

### G6.2 Sends and sound settings as typed records (P-221, P-222)

- **What changes.** P-221: a note's six sends are kept as the driver's packed mix-config words (bus << 8 | curve << 4 | curve; 0xffff first for the panner mode; defaults 0x150, 0x210, 0x352, 0x412; `SoundVoices.hpp:123`, `:110-112`), decoded at the note's start with the console's bus numbers remapped by output mode (`SoundVoices.cpp:564-579`), and again at every update (`mixerVolumes`, `:704-786`). Streams hold the same six words (`StreamPlayer.hpp:101`). The cook writes each send as a typed record (bus, two curve kinds, panner flag); each output mode's bus map is a table made once; the update only evaluates the curves. P-222: sound settings sit in the DSP's and the sound table's encodings and are decoded at each use; each is an item (plan decision: the row closes when the last is done):
  - an effect line's two sends and eight FIR weights as Q15 integers divided by 32768 at each set (`SoundEngine.cpp:596-606`);
  - a voice's filter as a mode word (0x20 IIR on, five bits of taps capped at 0x14 or 0x18) and Q15 coefficients (`SoundVoices.hpp:214-234`);
  - a sound's switches as the table's packed 16 bits unpacked by shifts at each use, two known only as BIT11 and BIT8 (`SoundAudibleParam.hpp:11-40`; `mAudibleSw`, `mSoundSw`, `mChannelCtrl`, `SoundData.hpp:284-286`);
  - a wave's disc format code deciding whether it plays and whether it is music (`SoundVoices.hpp:102-104`, `SoundVoices.cpp:128-132`; the store's samples are all PCM16).
  The cook writes floats of the same values, the tap count and an IIR flag, a typed record of named switches per sound, and a playable flag and category per wave. The values stay as made, the IIR cutoff table's numbers too; only their form changes.
- **Where.** `native/petari/src/Game/Sound/SoundVoices.cpp`, `StreamPlayer.hpp`, `SoundEngine.cpp`, `native/petari/include/Game/Sound/SoundVoices.hpp`, `SoundAudibleParam.hpp`, `SoundData.hpp`; the cook `crates/nebula-import/src/cook/sound.rs`.
- **Rows.** P-221: whole. P-222: whole.
- **Needs first.** Nothing (G6.1 is not needed: it changes the sequence decode, this step the sends and settings). This step stands at order 28 of `steps.tsv`, directly after G2.5, so G6.7 can follow it at once.
- **Old path.** None named (the sources give no switch).
- **Proof.**
  1. Gains unchanged: for every note and stream of the disc data, the typed record gives the same bus, curve kinds and gain as the packed word, in every output mode (the row's claim; no test is named in the sources).
  2. The step's replay is identical (rows).
  3. A search finds no packed-word decode or Q15 division in the sound code (the sources name no pattern).
- **For a player.** Nothing audible: the values are the same.
- **Read first.** P-193's law needs these buses named; G6.7 builds on this record.

### G6.3 Sound lookups by map (P-223)

- **What changes.** Lookups on the sound paths are linear searches: every sequence start walks every static sequence (`SoundArchive.cpp:131-137`); switching a wave group off searches every live group's waves for each id, on the audio thread (`SoundBank.cpp:70-86`); `WaveBank::find` walks the groups at every note start (`:96-98`). Statics are mapped by id at load; the cook writes each wave's group; the bank keeps, per id, the live groups that hold it. The disc's tables are small enough to walk; added content's are not.
- **Where.** `native/petari/src/Game/Sound/SoundArchive.cpp`, `SoundBank.cpp`; the cook `sound.rs`.
- **Rows.** P-223: whole.
- **Needs first.** Nothing.
- **Old path.** None named.
- **Proof.**
  1. Replay identical.
  2. A sequence start and a wave-group switch-off with a table far larger than the disc's give the same result as today's on the disc's table (the sources name no test; the row's reason is scale).
- **For a player.** Nothing visible.

### G6.4 Checked sound data and handles (P-224, P-227, P-228)

- **What changes.** Cooked or program values are trusted, handles are left pointed to, and music tables are read with no bound. Each fault is its own item (plan decision: a row closes when its last fault is done).
  - P-224, stream channels: a stream's channel count from the mapped header indexes six-entry arrays with no run-time check (`StreamPlayer.cpp:33-36`, `:177-188`, `:191-213`, `:216-221`; `StreamSound.cpp:109-113` checks the tag only); the importer refuses more than 6 (`audio_data.rs:105`), but a store from another cook is not refused.
  - P-224, sequence ports: a sequence's port operations index `mPorts[16]` and shift `1 << a[0]` with neither the run time nor the cook bounding the port (`SeqTrack.cpp:1412-1426`; `sound.rs:204-211`); port numbers are to be bounded by the cook.
  - P-224, effect categories: a sound's group of 16 or more indexes the 16 effect categories (`EffectManager.cpp:162-168` from `:211`); the categories' table is sized from the data in G8.4 (P-192).
  - P-224, manager count: `PlayerLink::setMgrCount` raises the count past the room made at create (`PlayerLink.cpp:14-25`, `:128-133`); it is checked against its room.
  - P-224, `EffectSound` allows no sequence source at `:134` and uses it at `:152`.
  - P-224's general form: every count, index and offset of the cooked sound data is checked at load in 64 bits and a bad part refused with a message.
  - P-227, takeover: a handle taken over by a new sound stays pointed to by the old one. `AudMeMgr::startMe` stops the handle's old phrase only when its stop is timed and never detaches it (`AudMePlayer.cpp:235-244`); `SpkSoundHolder`'s start stops or kills the old speaker sound without releasing the handle (`SpkSound.cpp:151-166`). `attachHandle` then points the handle at the new sound (`AudMePlayer.cpp:76-82`, `SpkSound.cpp:74-81`), and the old sound's `releaseHandle`, at its end, clears the handle and so drops the new sound (`AudMePlayer.cpp:84-91`, `:186-194`; `SpkSound.cpp:34-35`, `:83-90`); a handle freed first is written after its free. A handle and its sound detach as a pair: the takeover releases the old owner.
  - P-227, ids: a Me phrase's id indexes the parameter table unbounded (`AudMePlayer.hpp:133-135`) and a speaker sound's wave id is checked only for < 0 (`SpkSound.cpp:147`, then `:103-107`). Ids are checked against their tables at start and an unknown one refused.
  - P-228, scenario music: a stage's scenario music is indexed by `scenarioNo - 1` in rows of 6 (`AudStageBgmWrap.cpp:110-112`, `:137-139`; the rows at `:66`); a scenario past 6 on a disc galaxy (added content) or a sequence music past the disc's list reads past.
  - P-228, BGM settings: by the id's wave number in a 105-entry table and the multi BGM's set in 62 (`AudBgmSetting.cpp:6-15`, `:217-280`, read at `:283-299`); they become records keyed by sound id (G8.4, P-192).
  - P-228, track controller: a child by its track number, dereferenced unchecked at every update (`AudTrackController.cpp:78`).
  Each number is checked against its row (no music, or the stage's own, when outside).
- **Where.** `native/petari/src/Game/Sound/` (`StreamPlayer.cpp`, `StreamSound.cpp`, `SeqTrack.cpp`, `EffectManager.cpp`, `PlayerLink.cpp`, `EffectSound.cpp`), `native/petari/src/Game/RhythmLib/AudMePlayer.cpp`, `native/petari/src/Game/Speaker/SpkSound.cpp`, `native/petari/src/Game/GameAudio/AudStageBgmWrap.cpp`, `native/petari/src/Game/AudioLib/AudBgmSetting.cpp`, `AudTrackController.cpp`; the cook `crates/nebula-import/src/cook/sound.rs`, `audio_data.rs`.
- **Rows.** P-224, P-227, P-228: each whole.
- **Needs first.** Nothing.
- **Old path.** None: checks are added; good content behaves as before.
- **Proof.**
  1. Replay identical for good content (P-224's row).
  2. Bad content is refused with a message, not read past: a stream of more than 6 channels, a port past 15, a group of 16 or more, a scenario past 6, an unknown Me or speaker id, a track past its table (the sources name no test; these are the row's faults).
  3. A takeover test: a new sound taking a handle is not dropped when the old sound ends (the sources name no test).
- **For a player.** Nothing visible; a bad content file is refused instead of crashing or playing the wrong sound.
- **Read first.** The effect categories' table size comes from the data in G8.4 (P-192); here the index is checked against the room that exists. P-228's BGM settings as records keyed by sound id are G8.4's too: those two parts are closed there (they are named in G8.4's parts closed there).

### G6.5 Streams read ahead; a stream's end by the step clock (P-225, P-226)

- **What changes.** P-225: a stream's samples are played straight from the mapped store; the voices' wave pointers are the mapping (`StreamPlayer.cpp:207-208`, from `StreamSound.cpp:105`), so a page not in memory is a drive read inside the mix on the audio thread (the audio thread's counterpart of N-1083). A reader thread fills a ring ahead of the play position (or the range is read in before play); the mix never touches the drive. P-226: the step learns that a music stream ended from the audio device. The audio thread counts a stream's finishes as the device takes samples (`StreamPlayer.hpp:26-28`); `StreamSound::finished` compares the count on the game thread (`StreamSound.cpp:122-124`), `tryDie_` kills the sound on it (`:263-266`), and `AudBgm::isStopping` answers true (`AudBgm.hpp:76-82`), which `MR::isPlayingStageBgm` and its family read (`SoundUtil.cpp:378-440`; 32 lines match `git grep -c -E 'MR::is(Playing|Stage)[A-Za-z]*(Bgm|BGM)' -- native/petari/src`). So the step on which a non-looping music ends follows the device's buffer and latency, and the ticks dropped after a stall (`SoundEngine.cpp:151-160`). After: a stream's end is known by the step clock (its length in samples from the step it started, the audio following); the device never decides a step. The beat itself is read only by the audio thread's sequence clock (`TempoAdjuster::sequenceSeconds`, `RhythmSystem.cpp:100-110`); no step reads it.
- **Where.** `native/petari/src/Game/Sound/StreamPlayer.cpp`, `StreamPlayer.hpp`, `StreamSound.cpp`, `SoundEngine.cpp`, `native/petari/include/Game/AudioLib/AudBgm.hpp`, `native/petari/src/Game/Util/SoundUtil.cpp`.
- **Rows.** P-225: whole. P-226: whole.
- **Needs first.** Nothing.
- **Old path.** None named. The device's finish count is no longer read by a step.
- **Proof.**
  1. P-225: no drive read happens in the mix (a stream whose pages are not in memory plays without a gap; the sources name no test).
  2. P-226: the same inputs give the same step on which a music ends on every device, with the audio buffer and latency changed, on the PC and the tablet (the row says the result is the same on every device afterwards).
  3. The 32-line search above, each hit checked to read the step clock's answer.
- **For a player.** Music cuts and scene changes that wait for a music's end happen at the same moment on every device.
- **Read first.** The row says the replay's reference after this is the same on every device but not the same as each device's run today. A question for the owner arises only if today's runs must be matched. G9.2 (P-206, the staff roll) comes after this step and reads the stream's position it defines.

### G6.6 Movie sound rate and volume (P-271, P-219's movie half)

- **What changes.** A movie's audio record carries no rate and the mixer takes it at 32 kHz (`MovieAudio::kRate`, `runtime/include/galaxy/movie_audio.h:20`; `native_audio.cpp:302-311`), so a movie with sound at another rate (added content) plays at the wrong speed; the interface's comment on it is two versions run together (`nebula_game_api.h:929-931`). A movie's volume is a float that goes to the host as the THP player's 0..127 integer (`MoviePlayerSimple.cpp:141`, `MovieStream.cpp:235-242`, `NEBULA_MOVIE_AUDIO_VOLUME`, `nebula_game_api.h:426`), so its fades move in 128 steps and the gain is the square of it. After: NMOV names its audio rate in its header, the record carries it and the mixer resamples from it (the disc's movies stay 32 kHz, sound as made); the volume is a linear f32 gain and its ramp handed to the mixer, converted only where the game's data gives 0-127; the comment is rewritten.
- **Where.** `runtime/include/galaxy/movie_audio.h:20`, `runtime/src/native_audio.cpp:302-311`, `native/include/nebula_game_api.h:426`, `:929-931`, `native/petari/src/Game/Screen/MoviePlayerSimple.cpp:141`, `MovieStream.cpp:235-242`; the movie cook (the sources do not name the file that writes NMOV's header).
- **Rows.** P-271: whole. P-219: its movie-volume half (the pointer half closes at S0.2.3).
- **Needs first.** Nothing.
- **Old path.** None named: the header and the reader change together.
- **Proof.**
  1. Replay identical (the row).
  2. A movie at 44.1 kHz and one at 48 kHz play at the right speed; the disc's movies (32 kHz) give the same samples as today (the sources name no test).
  3. A fade of a movie's volume moves in finer steps than 128 (the sources name no test).
- **For a player.** The disc's movies sound the same; fades are smoother.
- **Read first.** R9.2 also changes NMOV's header (a frame table). Whichever lands second reads the first's layout.

### G6.7 The surround law (P-193)

- **What changes.** The two-channel output stays as made. The surround output has no written law, and its path for real speakers has faults (INV section 4, read at a1f423dd8 and 697535a86; the rows' 11-18 lines). The game's send gains already apply a matrix rear curve, `sin((x * 0.34776 + 0.32612) * pi / 2)`, for every send whose second curve is 3 or 7, in every output mode, before the device's layout is known (`SoundVoices.cpp:772` in `mixerVolumes`, `:704-786`); the law takes the rear amount before that curve, in the game's sends, with the buses named (G6.2). The bus set includes the matrix encoding's inverted right (`SoundEngine.cpp:16-21`): kept for two channels, never on a speaker of its own. On speakers of their own the surround buses still come 2.5 ms late and the inverted right is still subtracted from the right speaker (`audio_mixer.cpp:244-261`); the panner's rear effect sends are matrixed into effect lines 2 and 3 with a cross term in both modes (`:804-818`); `place()` splits a rear bus equally over side and back speakers whatever their angles, sends the centre at -3 dB to left and right when there is no centre speaker, and clamps (`native_audio.cpp:95-118`); the two-channel path writes two values a frame for any device of two channels or fewer (`:153`, `:158`); the same sound is 0.65 dB louder on its own speaker than on two channels (0.8602 of its power on two). The speaker list knows seven positions with LFE among the silent Others (`audio_device.h:12`); WASAPI opens the device's mask only when surround is asked and the mask has side or back bits (`audio_device_wasapi.cpp:207-227`), with `popcount(mask)` channels (`:179`) against an 18-entry speaker list (`:400`); the SDL hosts open two channels always and read the device's format only for its rate (`audio_device_sdl.cpp:17-20`, `:33`), so Android and Linux cannot reach the discrete path until the stream takes the device's channel count and order. After: the law below, written in INV section 4 before this code, with each property a test.
- **The player's choice (the owner, 11-22: "3.1/5.1/7.1 surround also in the game options").** The law's point 2 stands: the game never plays more speakers than the device reports. One setting, "Speakers", takes the place of today's "Sound output" (`SoundOutput` Mono, Stereo, Surround, `runtime_settings.h:28`, `:40`; the row at `native_settings.cpp:1474-1475`, `:5472-5477`; the texts S.MONO, S.STEREO, S.SURROUND, S.SOUND_OUTPUT of `localization/NebulaText.tsv`).
  - **One set of choices, why.** Today's "Surround" already means two things by the device (matrix-encoded on two channels, rear speakers where the device has them: the comment at `runtime_settings.h:27`). Left as it is beside 3.1, 5.1 and 7.1 it would be two answers to one question ("Surround" and "5.1"). So the single row names the speakers the sound is played on, and each of today's three values keeps its sound: Mono is Mono; Stereo is Stereo; today's Surround on two channels is the value "Encoded stereo" (the matrix for a receiver that decodes it, named for what it is). Nothing else of today's three is lost, and the two-channel output stays bit for bit (point 1).
  - **The values:** Auto (the default: the device's report), Mono, Stereo, Encoded stereo, 3.1, 5.1, 7.1.
  - **What each plays.** Mono, Stereo and Encoded stereo are the two-channel paths of point 1, on the device's front pair, every other channel silent, on any device. Auto: the device's report (point 2): on a device of two channels it is Stereo, bit for bit; on one with more, every channel the game has content for (the device's other layouts, 4.0, 5.1 back and 7.1.4, play under Auto; height and low-frequency channels silent). 3.1: left, right and centre, the low-frequency channel silent (point 2: the game has no content for it). 5.1: 3.1 and one rear pair (the device's side pair, else its back pair). 7.1: 5.1's speakers, then both pairs. A smaller layout than the device reports is played by the law's fold (point 4 and the addition 11 written in INV section 4), the other channels silent.
  - **Offered.** A layout is offered when the device has all its speakers (a stereo headset offers Auto, Mono, Stereo, Encoded stereo; a 4.0 device has no centre, so it offers no layout with one). A stored choice the device cannot play stays stored and is played as Auto until a device that can play it returns (the row keeps showing the stored choice).
  - **The game's own mode.** The game reads its output mode every frame (`AudSystem.cpp:194`, `:333-345`, `:431`) through `svc_system_setting` (`host_main.cpp:3576-3588`), now answered from the resolved choice and the device's last report, not from the stored value: Mono 0; Stereo 1; Encoded stereo 2 on two channels; Auto, 3.1, 5.1 and 7.1 give 2 when the device has more than two channels (a note's rear amount exists only in mode 2) and 1 on two channels.
  - **Stored.** A new key `audio_speakers` (`auto|mono|stereo|encoded|3.1|5.1|7.1`; `RuntimeAudioSettings::speakers`; written with the other `audio_` keys, `runtime_settings.cpp:550`) and its row in `runtime/src/ui/settings_table.tsv` (page Audio, the Android launcher and the game's menu and the Mac column as `audio_sound_output` has them, label "Speakers"). The old key `audio_sound_output` is read and never written again: `mono` gives Mono, `surround` gives Encoded stereo, `stereo` and no key give Auto (every file wrote `stereo` as the default, so it carries no choice; this is the choice taken here: a player who never touched the row gets the device's speakers, and anyone who wants Stereo picks it).
  - **The device layer.** `AudioDevice::set_surround(bool)` (`audio_device.h:31`, called at `native_audio.cpp:181`, `:223`) becomes a request for a layout; the device also reports the speakers it has, apart from the speakers it opened (`speakers()`, `audio_device.h:26`), and the row's offered list is made from that report. WASAPI opens the mask when the request has more than two channels; the SDL hosts open the device's channel count (the law's point 2).
  - **A device change while the game runs** (a headset plugged in or out: WASAPI's endpoint watch, `audio_device_wasapi.cpp:81`; on the SDL hosts what the stream does when the default device changes is read first, the sources do not say) resolves the choice again from the new report, and the gains ramp as point 7 says (10 ms), so nothing clicks; the stored choice is not changed.
  - **The texts, in all 14 languages.** Existing in all 14: Auto (`S.AUTO`), Mono, Stereo. New in all 14 (the tool's `--check` refuses an empty or foreign cell): the row's name "Speakers", the value "Encoded stereo", and one help line for the row ("Which speakers the game plays on. Auto uses what the device reports."). "3.1", "5.1" and "7.1" are digits and a point, shown as they are in every language, not looked up. `S.SURROUND` and `S.SOUND_OUTPUT` go when a search finds nothing reading them. The glyphs come from each language pack's font (`tools/nebula_text.py --fonts`).
- **Where.** `runtime/src/audio_mixer.cpp:244-269`, `:704-729`, `:804-818`; `runtime/include/galaxy/audio_mixer.h:90-95`; `runtime/src/native_audio.cpp:95-131`, `:153`, `:158`; `runtime/include/galaxy/audio_device.h:12`; `runtime/src/audio_device_wasapi.cpp:135-155`, `:179`, `:207-230`, `:400`; `platform/sdl/audio_device_sdl.cpp:17-20`, `:33`; `native/petari/src/Game/Sound/SoundVoices.cpp:665-786`, `SoundEngine.cpp:16-21`.
- **Rows.** P-193: whole.
- **Needs first.** G6.2. This step stands at order 29 of `steps.tsv`, directly after G6.2, and needs nothing of S0.2, S0.3, R1, R2 or G2.1 to G2.5 (the cards of those name no sound file and no row of the Audio page; G2.3 and G2.5 add to the settings' texts and this step reads those files as they stand; the files this step changes are named in Where).
- **Old path.** The two-channel path is kept bit for bit. The discrete path's gains, delays and matrix are replaced in this step (the sources name no switch). The setting `audio_sound_output` is replaced by `audio_speakers`; the old key is read for what it holds (see the player's choice) and never written.
- **Proof.** The law's nine points, then three tests of the player's choice (10 to 12), each a test (INV section 4; constants `kFold`, `kUnit`, `kMatrix` as in `audio_mixer.cpp`):
  1. Two channels: today's matrix, bit for bit; the game's mono mode (0) stays as it is.
  2. The layout is the device's report, never assumed: Windows the mix format's channel mask; Android the output's channel mask where the system reports one (else two channels); Linux and Mac what their device reports. Speakers the game has no content for (low frequency, height, front wide) stay silent.
  3. A note in surround form, and every note when the output has rear speakers, is placed by its pan p and rear r with the panner's constant-power law (front left `cos(pi p/2) cos(pi r/2)` and so on); the six-bus path with its matrix curve and the inverted right are not used there. An effect line's return to the surround buses becomes a rear return of the same power.
  4. Rear gain (with the row's "a defined fold for fewer speakers"): a sound's power on rear speakers equals its power on two channels. Rear amounts are scaled by `kFold sqrt(kUnit^2 + kMatrix^2)` = 0.92747; with sides and backs both present each gets 0.92747 / sqrt 2 = 0.65582 of it. Front and centre stay at 1; a centre with no speaker of its own goes to left and right at 1 / sqrt 2.
  5. No delay, no inversion and no matrix on a speaker of its own: the surround buses' 2.5 ms and the rear effect sends' matrix belong to two channels only; with rear speakers the rear effect sends feed rear echo lines unmatrixed.
  6. Left and right mirror: swapping p for 1 - p swaps every left speaker's gain with its right twin's, at every r.
  7. No jump: every gain is continuous in p and r over [0, 1]; a change of p, r or layout ramps as volumes do (10 ms).
  8. Channel order is the device's (the mask's bit order on Windows, the reported order elsewhere); each channel is named from its bit, never by position.
  9. The properties over p and r on a 101 by 101 grid for each layout (2.0, 3.0, 4.0, 5.1 side, 5.1 back, 7.1, 7.1.4), and a render of known sounds through the real mixer to a file for each layout, each channel's energy and phase checked against 1 to 8. Windows and Android in the gate; Linux and Mac built with it. The owner's listening comes later and is not a gate.
  10. The player's choice against the device, in the form of point 9 (the real mixer rendered to a file, each channel's energy checked): every value of "Speakers" (Auto, Mono, Stereo, Encoded stereo, 3.1, 5.1, 7.1) against every device layout (2.0, 3.0, 4.0, 5.1 side, 5.1 back, 7.1, 7.1.4). For each pair: (a) the channels that carry sound are exactly the ones the card's "what each plays" names, every other channel is all zero samples (the low-frequency and height channels included); (b) for Auto, 3.1, 5.1 and 7.1 each sound's total power equals its power on two channels at every point of the grid (point 4); (c) for Mono, Stereo and Encoded stereo the output equals the two-channel render made before this step, bit for bit, on the front pair; (d) a value the device cannot play is not offered, and when stored it plays as Auto; (e) mirror and no jump (points 6 and 7) hold for every pair that plays more than two channels, over the 101 by 101 grid.
  11. The row and its storage: the offered values for each device layout (the cycle skips the others, in both directions); a stored choice written and read back; an old file with `audio_sound_output=mono`, `=surround`, `=stereo` and with no key gives Mono, Encoded stereo, Auto and Auto, and the file then written has `audio_speakers` and no old key; `python tools/settings_table.py --check` and `python tools/nebula_text.py --check` pass; each new text has its 14 cells.
  12. A device change while the game runs: a stand-in device that goes 5.1, then 2.0, then 7.1 between two render calls changes the played layout at once, and in no channel does a sample step by more than the 10 ms ramp of point 7 allows; the stored choice is the same after. The game's mode (0, 1, 2) answered to `svc_system_setting` is checked for each value on two channels and on 5.1.
- **For a player.** Two-channel players hear no change. On a device with more speakers, sound is placed in a stated, matched-power way, with no echoes or matrix tones on a speaker of its own, and an Android or Linux device with more channels uses them. The sound settings have one row, "Speakers": Auto, Mono, Stereo, Encoded stereo, 3.1, 5.1 and 7.1, in every language of the game; a layout the device does not have is not offered; Auto is the default for a player who never chose. Plugging a headset in or out changes the speakers at once and without a click.
- **Read first.** Before the code: (a) which sounds and effect lines send to the inverted right or the surround buses (law point 3); (b) what the game's mono mode (0) does in the mixer (INV: "not checked here"); (c) the Android channel mask report and what Linux and Mac report (law point 2). The game's sound code and the mixer are read first; nothing is written from memory (file 08 5.H.5). Also read before the code, for the player's choice: (d) where the game's mode is read and set (`AudSystem.cpp:194`, `:333-345`, `:431`) and what Mono does after the game's mode 0 (`native_audio.cpp:277-283`: the mean of the two channels, kept as it is); (e) how the settings screen and the Android launcher list a row from `settings_table.tsv` (`tools/settings_table.py` says which readers must agree) and how the row's English text reaches the played language (`NebulaSettingsMenu.cpp:649`); (f) what each device reports for its speakers (Windows' mask, the SDL hosts' format, Apple's `audio_device_apple.mm`). The law as the card first stated it names the fold only for rear speakers; for a layout with no rear speaker (3.0, and 3.1 chosen on a larger device) INV section 4 gets the written fold (addition 11) before the code.

### G6.8 The audio device (P-037, P-071, P-072, P-286)

- **What changes.** Each fault is its own item (plan decision: a row closes when its last fault is done).
  - P-037: the SDL audio callback can allocate on the audio thread: `out_.resize(frames * 2)` inside `pull` (`audio_device_sdl.cpp:54-62`), which allocates when a callback asks for more frames than any before it (after a device change to a larger buffer, or on the first callbacks). The audit's fix: reserve the device's largest buffer at `start` (`SDL_GetAudioDeviceFormat`'s device frames, times 4 for safety) and render in chunks of that size when SDL asks for more.
  - P-071: on SDL hosts (Linux, Android) and Apple a movie's picture runs ahead of its sound by the audio the device and SDL's stream still hold. The picture's clock is the mixer's unplayed count plus `device->queued_frames()` (`native_audio.cpp:159-162`); only `WasapiDevice` overrides `queued_frames` (`audio_device_wasapi.cpp:112`), the base returns 0 (`audio_device.h:33`), and `SdlAudioDevice` and `audio_device_apple.mm` do not override it. The audit's fix: SDL returns the stream's queued bytes (`SDL_GetAudioStreamQueued`, over 8) plus the device's buffer; Apple adds the output's presentation latency (iOS: output latency plus buffer duration; macOS: the device latency property plus the buffer frame size). The size of the offset was never measured.
  - P-072: the Windows output starts at one engine period and lowers the fill again after 10 quiet seconds (`audio_device_wasapi.cpp:36-37`, `:253`, `:309-315`, `:338-341`). A machine that needs two periods glitches, raises, lowers 10 s later and glitches again; the raise comes only after the late wake, so the first gap is always heard; no under-run is counted; `raise_fill` writes to `std::cout` on this audio thread (`:264`). The game's whole sound frame is computed inside this callback, so the headroom needed is the slowest sound frame. The audit's fix: start at two periods; lower only after a long quiet window and never below the highest level needed this session; count real under-runs (padding 0 at a wake, or position jumps) and show them in the audio stats line; move the log line off the audio thread.
  - P-286, thread: the WASAPI device's thread is detached with no stop and its endpoint watch is never unregistered (`audio_device_wasapi.cpp:102-108`, `:358-388`); it becomes a joinable thread with a stop flag joined by the device's owner and the watch unregistered.
  - P-286, no device: with no endpoint the game's sound is ticked by 10 ms waits, not a clock (`:346-356`); it is paced by the host's clock.
  - P-286, scratch: `run_fir` copies reach plus count samples into a fixed 1024-float array with no check (`audio_mixer.cpp:75-93`); the scratch is sized from the largest kernel at `set_rate` (the comment's 31 taps enforced).
  - P-286, loops: a voice whose loop wraps eight times in one read is silenced and ended with no note (`:479-486`); a short loop is rendered whole, or its end noted in the loss report.
- **Where.** `platform/sdl/audio_device_sdl.cpp:54-62`; `runtime/src/audio_device_wasapi.cpp`; `runtime/src/audio_mixer.cpp:75-93`, `:479-486`.
- **Rows.** P-037, P-071, P-072, P-286: each whole.
- **Needs first.** Nothing in the skeleton. It follows G6.7, and both change the device files; G6.7 now stands at order 29 and this step at order 120, so the device files are read as G6.7 leaves them (its device layer reports the speakers the device has and takes the layout asked for; the lines cited below are as cited at 241395b49).
- **Old path.** None named.
- **Proof.**
  1. P-286: the samples the game asks for are unchanged (sound as made); the replay is identical (sound does not feed the step); the no-device tick's time is checked (the row).
  2. P-037: no allocation on the audio thread (the sources name no test).
  3. P-071: a movie's picture and sound line up on SDL hosts and Apple; the audit's own run: a movie with a clap frame, recorded with a phone camera, on Android and Linux (no other test is named).
  4. P-072: the audit's own run on a loaded machine: log fill changes with timestamps and count wakes with padding 0, before and after.
- **For a player.** A closing game or a vanished sound device no longer leaves a thread behind; movies stay in step with their sound on Android, Linux and Apple.
- **Read first.** P-071 and P-072's ledger rows are cut short; their full findings are in audit file B (R4.2, "Round 4 findings, item 2") and are used above.

### Rows of this package

| Row | Today | After | Closes at |
| --- | --- | --- | --- |
| P-037 | The SDL audio callback can allocate on the audio thread (`out_.resize(frames * 2)`). | The device's largest buffer reserved at start; the callback renders in chunks and allocates nothing. | G6.8 |
| P-071 | On SDL hosts and Apple a movie's picture runs ahead of its sound by the audio the device and SDL's stream still hold (`queued_frames` is overridden only on WASAPI). | `queued_frames` on SDL (stream queue plus device buffer) and on Apple (output latency plus buffer); picture and sound line up. | G6.8 |
| P-072 | The Windows output starts at one engine period and lowers the fill after 10 quiet seconds, so a machine needing two periods glitches, raises, lowers and glitches again; no under-run is counted. | Start at two periods; lower only after a long quiet window and never below the session's highest need; count under-runs; log off the audio thread. | G6.8 |
| P-186 | The cooked sound sequences keep their data in the console's byte order; `SeqTrack` swaps it at every read; Me operands, jump tables and the rhythm look-ahead read raw bytes. | The cook writes every table in host order, typed; tables of operation indices; Me operands in host order; look-ahead over cooked operations; a test compares every value. | G6.1 |
| P-193 | Surround output has no written law; the speakers-of-their-own path keeps delays, an inverted bus, matrixed echo sends and a louder sound; SDL hosts open two channels. | The nine-point law tested at every layout, a render-to-file test through the real mixer; the device's channel count and order used. | G6.7 |
| P-219 | A movie's f32 volume goes to the host as 0..127 and fades in 128 steps; the pointer is unprojected through the console's projection and viewport forms. | An f32 gain and ramp to the mixer (here); the pointer's half is the inverse view-projection in the host's terms (S0.2.3). | G6.6 (movie half; pointer half S0.2.3) |
| P-221 | A note's six sends are the driver's packed mix-config words, decoded at the note's start and at every update, with the console's bus numbers remapped by mode. | Typed send records from the cook; a bus map per output mode made once; the update only evaluates curves; gains unchanged. | G6.2 |
| P-222 | Sound settings are in the DSP's and table's encodings (Q15 sends and FIR weights, a filter mode word, packed switch bits, a format code), decoded at each use. | The cook writes floats of the same values, a tap count and IIR flag, named switches, a playable flag and category per wave; values as made. | G6.2 |
| P-223 | Sequence starts, wave-group switch-off and wave lookups search linearly on the sound paths, one on the audio thread. | Statics mapped by id; the cook writes each wave's group; the bank keeps per id the live groups that hold it. | G6.3 |
| P-224 | Stream channel counts, sequence ports, effect categories, the manager count and the sequence source are indexed with no check. | Every count, index and offset checked at load in 64 bits and a bad part refused with a message; ports bounded by the cook. | G6.4 |
| P-225 | A stream's samples play straight from the mapped store, so a page fault is a drive read inside the mix on the audio thread. | A reader thread fills a ring ahead of the play position; the mix never touches the drive. | G6.5 |
| P-226 | The step learns a music stream ended from the audio device, so the step of its end follows the device's buffer and latency. | The stream's end known by the step clock (its length in samples from the step it started); the device never decides a step. | G6.5 |
| P-227 | A handle taken over by a new sound stays pointed to by the old one, whose release then drops the new sound; Me and speaker ids are unchecked. | A handle and its sound detach as a pair; ids checked against their tables at start and an unknown one refused. | G6.4 |
| P-228 | Scenario music, BGM settings and a track controller's child are indexed with no bound. | Each number checked against its row (no music, or the stage's own, outside); settings as records keyed by sound id (G8.4). | G6.4 |
| P-271 | A movie's audio record carries no rate; the mixer takes 32 kHz, so a movie at another rate plays at the wrong speed; the volume is the 0-127 SDK form. | NMOV names its audio rate; the mixer resamples from it; the volume is a linear gain with a ramp; the disc's movies stay 32 kHz. | G6.6 |
| P-286 | The WASAPI thread is detached with no stop and its watch never unregistered; no-device ticks are 10 ms waits; `run_fir` has an unchecked 1024-float array; a loop that wraps eight times in one read is silenced. | A joinable thread joined by the device's owner; the host's clock; scratch sized from the largest kernel; a short loop rendered whole or its end noted. | G6.8 |


---

## G7 Text

This package changes how the game holds text. Today a message is a run of 16-bit characters with the message editor's binary tag records inside it (a 0x1A unit, a length, 16-bit parameters, string arguments as 4-byte tokens), and every reader scans for them and trusts their length. After it, each message is parsed once into text runs of 32-bit code points and typed tag records, each holding its own string, and the font reads code points. A player sees the same text on every screen; added content can use more than eight sound tags in a message and any character, and a damaged message can no longer make the game read past it.

- **Design:** INV section 4, "What P-202 and P-187 start from" (the form, the five landings and the proof), INV section 2 ("Text" bullet) and section 3's lines for P-187, P-201, P-202, P-215, P-237, P-275, and each row's note.
- **Needs first:** nothing in the skeleton. The one UTF-8 decoder (`MR::utf8ToText16`, P-209, with P-320's two other decoders moved onto it) is on main at 790626cb5.
- **Rows:** 6 (P-202, P-215, P-237, P-275, P-187, P-201)

The five steps are INV section 4's five landings. P-201 goes with landing 2, as the skeleton says. The mod surface's tag writer moves to the same builder in landing 3, because the old format cannot be deleted while it writes it. Line numbers are as cited at 241395b49 and (INV section 4's search) 697535a86.

### G7.1 Landing 1: `NMSG` version 2 beside version 1

- **What changes.** The cooked message file `NMSG` keeps the disc's text as it was: UTF-16 units (`Nmsg.text`, `message.rs:40-43`) with the message editor's tags inside the text: a 0x1A unit, then the length-and-group unit in the disc's byte order, then the tag's units (`message.rs:114-135`). The cook now parses each message once into runs: text runs of code points (32-bit) and typed tag records (kind, parameters, and a string of its own where the tag names one), with the message's run list and its plain text apart. Version 2 is cooked beside version 1. The reader (`MessageData`, `MessageHolder.hpp:8-62`) gets a run API. Nothing yet reads it in the game.
- **Where.** `crates/nebula-import/src/cook/message.rs`; `native/petari/include/Game/System/MessageHolder.hpp`, `native/petari/src/Game/System/MessageHolder.cpp`.
- **Rows.** P-202: advances (the form is made). P-187: advances (code points in the cooked text).
- **Needs first.** Nothing.
- **Old path.** `NMSG` version 1 and its in-text format stay until G7.5, which deletes them.
- **Proof.**
  1. A test that every message of the three discs (RMGE01, RMGP01, RMGJ01) in every language gives the same characters and the same tags from both versions.
  2. A fuzz test of malformed tags against the cook's parser (INV: part of the proof; the sources name no tool).
- **For a player.** Nothing visible: nothing reads version 2 yet.

### G7.2 Landing 2: the tag processors on runs (with P-201)

- **What changes.** The tag processors read the in-text records by scanning for 0x1A. They now walk runs; each tag holds its own string; there is no token table and no byte arithmetic; nothing scans for 0x1A or reads a length from inside the text. Each fault is its own item (plan decision: a row closes when its last fault is done):
  - P-202: a string parameter is a 4-byte token naming an entry in a table that only grows (`MessageTagSkipTagProcessor.cpp:5-31`), the records are parsed at `:40-77`, and `ReplaceTagProcessor` rewrites the records in place byte by byte (`ReplaceTagProcessor.cpp:49-72`, `:139-161`, `:239-321`).
  - P-201: a message's sound tags are tracked in a u8 mask, so the ninth and later sound tags of a message play nothing (`CustomTagProcessor.cpp:483-499`, `mSoundIndex >= 8`); the mask or list is sized to the message's tags, as a field of the parsed message.
  - P-215, the ruby tag: it copies its base text, up to 255 units by a byte of the tag, with no check against the text left (`CustomTagProcessor.cpp:644-654`, the copy at `:653`); with typed records the cause is gone. (`NebulaUi`'s half is G7.3.)
  - P-237, the line copy of `MessageUtil` copies a tag's bytes without counting them against the room (`MessageUtil.cpp:89-113`, the tag loop at `:94-98`).
  - P-237, `addNumberFontTag` assumes 256 units of room and cuts the tag's size past 255 (`StringUtil.cpp:215-233`, `:223`, `:229`).
  - P-237, `convertUTF16ToASCII` bounds by `num - 1` against an unsigned count, so 0 writes without end (`StringUtil.cpp:252-266`).
- **Where.** `native/petari/src/Game/Screen/CustomTagProcessor.cpp` (the 0x1A hits at `:408`, `:429`), `MessageTagSkipTagProcessor.cpp` (`:95`, `:103`), `ReplaceTagProcessor.cpp` (`:53`, `:87`, `:117`, `:158`, `:308`), `native/petari/include/Game/Ui/TagProcessorBase.hpp:37-38`, `native/petari/src/Game/Util/MessageUtil.cpp`, `StringUtil.cpp`.
- **Rows.** P-201: whole. P-202: advances (processors). P-215: advances (the ruby copy). P-237: advances (the message helpers).
- **Needs first.** G7.1.
- **Old path.** Version 1 and the in-text readers remain beside the run readers until G7.5.
- **Proof.**
  1. The text-layout test (every message's glyph sequence and pen positions, old path against new, every language and region) is identical.
  2. The gate's pictures are unchanged.
  3. A message with nine or more sound tags plays every one of them (the row's fault; the sources name no test).
- **For a player.** Nothing visible with the disc's text; messages with more sound tags now play them all.
- **Read first.** INV's landing 2 is only "the tag processors on runs"; P-237's three message helpers are placed here because they copy or size tags (the row's placement note says P-237 closes with P-202, at G7.5).

### G7.3 Landing 3: the screens, the host and the mod surface on runs

- **What changes.** The remaining readers and writers of the in-text records move to runs: `NebulaUi` (its tag walk, `NebulaUi.cpp:131-139`; `wrapWords` steps over a tag by its claimed length and reads `pSource[end]` past the terminator before its clamp, `:154-160`: P-215), `NebulaSettingsMenu` (`:889-901`), `NebulaAchievementToast` (it writes the records, `:54-64`), `StaffRoll` (`:646-657`), `TitleSequenceProduct` (builds a record into a prompt, `:76-93`), and the mod surface's tag writer (`nebula_text_tags.h`, which writes the same records; its decoder is on the one decoder, P-320). The host's `message_utf8` skips a tag by the size in the next unit's high byte with no check against the string's end, so a tag whose size runs past the text reads up to the loop's 256 units beyond it (`host_main.cpp:2162-2168`, P-275); it reads the tags from the typed records. Nebula's own text (`NebulaText`) and added content build runs through the same builder, decoded by the one UTF-8 decoder (P-209, P-320).
- **Where.** `native/petari/src/Game/Screen/NebulaUi.cpp`, `NebulaSettingsMenu.cpp`, `NebulaAchievementToast.cpp`, `StaffRoll.cpp`, `TitleSequenceProduct.cpp`; `native/include/nebula_text_tags.h`; `native/host/host_main.cpp`; `native/petari/src/Game/System/NebulaText.cpp` and `localization/NebulaText.tsv` (INV names `NebulaText.txt`: not found at 790626cb5).
- **Rows.** P-202: advances (all readers). P-215: advances (`NebulaUi`'s `wrapWords`). P-275: advances (the host's skip).
- **Needs first.** G7.2.
- **Old path.** Version 1 and the in-text format remain, unread, until G7.5.
- **Proof.**
  1. The text-layout test identical (as G7.2).
  2. The gate's pictures unchanged; the settings menu, the achievement toast, the staff roll and the title's prompt compared.
  3. The host's reading of a message with a tag whose size runs past the text stops at the text's end (the sources name no test; the row's fault).
- **For a player.** Nothing visible.
- **Read first.** The skeleton and INV agree that the mod surface's writer moves here. INV says the mod surface "writes the same records" and gives no other detail of that surface; read `nebula_text_tags.h` first. INV names Nebula's own text (`NebulaText.txt`) among the users of the builder but lists it in no landing; it is placed here, with the other builders of text. G4.4 (P-165) reads `NebulaText`'s table: whichever lands second reads the other's form.

### G7.4 Landing 4: code points through the font (P-187)

- **What changes.** The game's text and font path holds characters as 16-bit codes, the console font format's range; a character past U+FFFF cannot be drawn, though the host's own reader of the same text joins surrogate pairs. Nothing shown today is past U+FFFF. After: code points as 32-bit values through the readers and the tag processors, and `NFNT` version 2 with a code table by range. Each fault is its own item (plan decision: the row closes when the last is done):
  - the reader and the glyph pages: `CharStrmReader` reads UTF-8 of at most three bytes and UTF-16 units (`CharStrmReader.cpp:10-33`), the tag processors take a `u16` code (`TagProcessorBase.hpp:37-38`), and the cooked font finds a glyph through two 256-entry pages by the code's high and low byte (`native_data.rs:244-257`);
  - the text formatter's `%c` keeps the low 16 bits of its argument (`TextString.cpp:108-112`);
  - the update notice is handed to the game as UTF-16 units, one per call under a lock (`launcher_text`, `launcher.cpp:162-166`), a code past U+FFFF split into a surrogate pair (`:65-74`);
  - a mod font's metrics are cooked into byte fields: ascent, descent, cell and baseline `u8`, line feed `i8`, each glyph's left and widths a byte (`font_cook.rs:234-250`; the glyph record, `native_data.rs:253`). The row's modern-form line gives no form for the metrics; decide their width with the cook's font code in front of you.
- **Where.** `native/petari/src/Game/Ui/CharStrmReader.cpp`, `native/petari/include/Game/Ui/TagProcessorBase.hpp`, `native/petari/src/Game/Util/TextString.cpp`, `runtime/src/ui/launcher.cpp`, `crates/nebula-import/src/cook/native_data.rs`, `font_cook.rs`.
- **Rows.** P-187: whole.
- **Needs first.** G7.3.
- **Old path.** The sources do not say whether `NFNT` version 1 is kept beside version 2; read first.
- **Proof.**
  1. A test draws a character past U+FFFF from a test font (the row).
  2. The text-layout test (every message's glyph sequence and pen positions, old path against new, every language and region) identical through landing 4.
  3. `%c` keeps all 32 bits; the update notice reaches the game as whole code points (the sources name no test).
- **For a player.** Nothing visible with today's languages; a character past U+FFFF can now be shown.
- **Read first.** The sources give no form for the update notice's and `%c`'s change beyond "code points as 32-bit values"; read the row and the code.

### G7.5 Landing 5: version 1 and the in-text format deleted

- **What changes.** `NMSG` version 1 and the in-text format are deleted from the cook and the reader. The search for 0x1A then gives only the 9 other hits, which are other things (a sound id, effect floors, a timer, a colour, filter tables).
- **Parts closed here** (plan decision: parts of P-237 that the row names and no earlier step places; each is bounded by its room and checked at its input, as the row says):
  - [ ] `emitEffectWithEmitterCallBack` uses the emitter unchecked, and a name the keeper lacks gives a null (`EffectUtil.cpp:72-73`);
  - [ ] `tryCreateCollisionMoveLimit` writes the parts before its null check (`LiveActorUtil.cpp:2219-2222`);
  - [ ] the effect-name cache's `heap_realloc` is unchecked (`MotionSignal.cpp:44-49`);
  - [ ] pointer touch ids are addresses and sums of two addresses, which can be equal (`StarPointerUtil.cpp:127`, `:254`, `:294`, `:502`): ids come from a counter;
  - [ ] `BezierSurface::setDivideLevel` neither checks the level against the table of 7 nor resizes the cache made for the first (`BezierSurface.cpp:594-604`, `:634-636`; latent: the only run-time level change, `BigBubble.cpp:207`, uses a surface with no cache and levels up to 4);
  - [ ] the footprint ring's fade window reads past a ring of 8 or fewer (`FootPrint.cpp:81-96`, window of 10 at `:17`; every ring the game makes is 32 or more).
- **Where.** `crates/nebula-import/src/cook/message.rs`; `native/petari/include/Game/System/MessageHolder.hpp`; the files above under `native/petari/src/Game/Util/` and `native/petari/src/Game/Ride/BigBubble.cpp`.
- **Rows.** P-202, P-215, P-275: whole. P-237: whole (the message parts at G7.2, the rest here).
- **Needs first.** G7.4.
- **Old path.** This is the step that deletes it.
- **Proof.**
  1. `git grep -n -E "0x1A|0x001A" <commit> -- native/petari/src` gives only the 9 other hits (it gave 26, 17 of them message tags, at 697535a86).
  2. The text-layout test and the gate's pictures still identical.
  3. The fuzz test of malformed tags against the cook's parser passes.
  4. Every box of "Parts closed here" ticked, each read at its input (the row names no test for them).
- **For a player.** Nothing visible: it is how the code is built.
- **Read first.** The placement notes say P-215, P-237 and P-275 close with P-202, so their last parts are in this step. The row for P-237 gives no test for its non-text parts.

### Rows of this package

| Row | Today | After | Closes at |
| --- | --- | --- | --- |
| P-187 | Characters are 16-bit codes (the console font's range): the reader, the tag processors and the font's two 256-entry pages; a character past U+FFFF cannot be drawn; `%c`, the launcher text and font metrics are narrow too. | 32-bit code points through readers and tag processors; `NFNT` version 2 with a code table by range; a test draws a character past U+FFFF. | G7.4 |
| P-201 | A message's sound tags are tracked in a u8 mask, so the ninth and later sound tags play nothing. | A mask or list sized to the message's tags, a field of the parsed message. | G7.2 |
| P-202 | Message tags are the console's binary records inside the character stream, a 4-byte token for each string, rewritten in place byte by byte. | A message parsed once into text runs and typed tag records, each with its own string; no token table, no byte arithmetic; version 1 deleted. | G7.5 |
| P-215 | A tag's claimed length is trusted: the ruby copy and `NebulaUi`'s `wrapWords` read past the text. | Both bounded by the text's length; the typed records remove the cause. | G7.5 |
| P-237 | Shared helpers copy tags past their room, assume 256 units, write without end at 0, use an emitter unchecked, and more (touch ids as addresses, an unchecked divide level, an unchecked realloc). | Each bounded by its room and checked at its input; touch ids from a counter. | G7.5 |
| P-275 | The host's `message_utf8` skips a tag by its claimed size with no check against the string's end. | The skip checked against the units that remain; tags read from the typed records. | G7.5 |


---

## G8 Save, tables, limits and waits

This package changes the save, the tables and the limits that were fixed for the console, and two waits on the game thread. The save stops being the console's `GameData.bin` layout (16-bit name hashes, byte counts, 6-bit ids) and becomes named, sized records with a version; tables are read by index, not by column name; shared counts grow where the code can ask for one more and gets none in silence; and a stuck save can no longer freeze the game. A player keeps their old save (converted once, the old file kept) and loses nothing to a cap; added content is not capped by a shared table.

- **Design:** each row's note; INV section 2 ("Tables" and "Limits" bullets), INV section 3's lines for P-189, P-192, P-204, P-205, P-232, P-233, P-235, and INV section 4 (the owner's answers of file 08 5.H.3: "it grows where the code can ask for one more and silently gets none; it stays where the count is what the actor is made of"; the classing of the 138 tables and the pools of 11-19).
- **Needs first:** nothing in the skeleton.
- **Rows:** 9 (P-148, P-189, P-192, P-204, P-205, P-232, P-233, P-235, P-272)

Line numbers are as cited at 241395b49 (the rows' own commit) unless a card says otherwise. A row with several faults has each fault as its own item (plan decision): the row closes when its last item is done.

### G8.1 The save as named, sized records with a version (P-205, P-232)

- **What changes.** Nebula's save is the console's `GameData.bin` layout in host order, and the game still reads a console file at load. After: the save is named, sized records with a version; a galaxy, a value or a spin driver of added content needs no hash space, offset width or id mask. P-232 is closed by the new form, and its own faults are fixed. Each fault is an item:
  - P-205, names and offsets: attribute names as 16-bit hashes with u16 offsets (`BinaryDataContentAccessor.cpp:13-20`, `:55-64`).
  - P-205, galaxy records keyed by a 16-bit hash (`GameDataGalaxyStorage.cpp:184`).
  - P-205, chunk counts in a byte (`BinaryDataChunkHolder.cpp:46-49`).
  - P-205, spin-driver ids masked to 6 bits (`SpinDriverPathStorage.cpp:55`).
  - P-205, event values as u16 (`GameEventValueChecker.cpp:48-53`).
  - P-205 (with P-232's collision), event flags under a 15-bit name hash with the top bit their state (`GameEventFlagTable.cpp:586`, `GameEventFlagStorage.cpp:33-34`).
  - P-205, the galaxy chunk is written with no bound against its room (`GameDataAllGalaxyStorage::serialize` ignores `dataSize`, `GameDataGalaxyStorage.cpp:367-388`).
  - P-205, the console file at load: the game still reads a console `GameData.bin` from the NAND layout's folder (`save_files.h:37-39`, `/title/00010000/<the code in hex>/data`; `host_main.cpp:3521`, `:6187`; `SaveDataHandler.cpp:21-23`), the Wii's big-endian and the Switch release's little-endian version 2 told apart by `SaveFileFormat.hpp:35-39`; Android's save import writes a console file there as it is (`SaveTransfer.kt:103`, `:177-194`) and keeps `title` and `shared2` as save top folders (`:278-279`). After: the import turns a console file once into `Nebula.sav` in the release's folder, the NAND layout known only to the importer, on every platform (`save_transfer.cpp` too); the old file is kept as a backup (plan decision).
  - P-232, size: `GameEventValueChecker::deserialize` reads `size / 2` entries of four bytes (`GameEventValueChecker.cpp:75-91`), twice what the chunk holds; `SaveReader` gives zeros past the end (`SaveBytes.hpp:67-73`), hash 0 is not found, so every load marks the chunk failed. Fix: entries counted by their size (size / 4).
  - P-232, failure status: the holder takes status 1 as success as it does 0 (`BinaryDataChunkHolder.cpp:56-66`), so no chunk's failure of that kind is seen. Fix: a chunk's failure reported as one.
  - P-232, flag collisions: a flag added at run time is checked free only by its 32-bit name hash (`nameIsFree`, `GameEventFlagTable.cpp:336-349`) while saved under 15 bits, so two flags with one save hash are saved twice and a load sets the first (`getIndexFromHashCode`, `:589-591`). Fix: flags saved by name or by an id checked unique across the table and every added flag at load.
  - P-232, resource names: `ResourceHolderManager::find` takes the 32-bit lower-case hash for the name with no compare (`ResourceHolderManager.cpp:144-154`), so two archives with one hash are one. Fix: names compared after a hash match.
  - P-232, allocation: the growth of the mapped-asset and arena lists does not check `realloc` (`GameAssets.cpp:110-113`, `NebulaArena.cpp:115-118`). Fix: allocation failures checked.
- **Where.** `native/petari/src/Game/System/` (`BinaryDataContentAccessor.cpp`, `BinaryDataChunkHolder.cpp`, `GameDataGalaxyStorage.cpp`, `SpinDriverPathStorage.cpp`, `GameEventValueChecker.cpp`, `GameEventFlagTable.cpp`, `GameEventFlagStorage.cpp`, `ResourceHolderManager.cpp`, `GameAssets.cpp`, `NebulaArena.cpp`, `SaveDataHandler.cpp`), `native/petari/include/Game/System/SaveBytes.hpp`, `SaveFileFormat.hpp`; `runtime/include/galaxy/save_files.h`, `runtime/src/save_transfer.cpp`; `native/host/host_main.cpp`; `platform/android/app/src/main/kotlin/dev/nebula/galaxy/SaveTransfer.kt`.
- **Rows.** P-205: whole. P-232: whole.
- **Needs first.** Nothing.
- **Old path.** The console-layout reader in the game goes when the import converts a file once (the row's modern form). The sources give no switch; read the load path first.
- **Proof.** The sources name no test for these rows; the rows state what must be shown:
  1. A Wii (big-endian) and a Switch (little-endian version 2) console file each convert once to `Nebula.sav` with every value equal, on every platform, and the old file is kept.
  2. The event value chunk reads `size / 4` entries and no good chunk is marked failed; a bad chunk is reported as failed.
  3. Two event flags with one 15-bit hash are saved and loaded separately; two archives with one 32-bit hash are not one.
  4. The galaxy chunk is bounded by its room.
- **For a player.** The save carries over from every platform; the game does not mark a good save chunk failed.
- **Read first.** Whether the old file's backup keeps its name and folder is a plan decision: record it with the import's code.

### G8.2 Bounded waits on the game thread (P-272, P-235)

- **What changes.** Two waits on the game thread are fixed, and each fault is an item:
  - P-272, the wait for the last save: the game's wait for its last save request sleeps until the worker ends it (`svc_save_wait`, `host_main.cpp:3490-3495`; `StatusBoard::wait`, `save_files.cpp:179-186`, a condition wait with no time limit), on the game thread. A write stuck on a vanished network folder or a removed card freezes the step for good. After: a bounded wait; the game's own save flow turns a stuck write into its save-failed message.
  - P-272, the exit: every way out waits a second and then for as long as the save takes (`host_main.cpp:4739-4748`), while the comment over the exits says the wait is bounded so that a dead drive does not hold the game (`:4402`). After: a bounded wait; past it the player is told a save is being written and chooses to wait or quit; the comments are made to agree.
  - P-235, the cutscene skip: it runs its unseen steps for up to 9 ms of wall time and 40 steps a frame (`NebulaCutsceneSkip.cpp:46-47`, `tryExtraStep`, `:384-399`), a fixed share that fits a 16.7 ms frame and is more than a whole frame above about 110 Hz. After: the skip's budget is a share of the measured frame interval (the frame pacer's). The skip's steps are the same; only how many run each frame changes.
  - P-235, save and load requests: a request waits on the game thread for the one before it (`SaveDataHandler.cpp:120-122`, from `:129` and `:534`). After: a request is queued behind the running one and the step never waits.
- **Where.** `native/host/host_main.cpp:3490-3495`, `:4739-4748`, `:4402`; `runtime/src/save_files.cpp:179-186`; `native/petari/src/Game/System/NebulaCutsceneSkip.cpp`, `native/petari/src/Game/System/SaveDataHandler.cpp`.
- **Rows.** P-272: whole. P-235: whole.
- **Needs first.** Nothing.
- **Old path.** None named: the waits are replaced.
- **Proof.**
  1. Replay identical (both rows).
  2. A save stuck on a vanished folder or a removed card freezes neither the step nor the exit; the player is told and chooses (the sources name no test).
  3. A save or load request made while one runs queues and the step does not wait.
  4. The cutscene skip at about 60, 120 and 240 Hz uses a share of the frame interval and never more than a frame (the sources name no figure for the share).
- **For a player.** A stuck save no longer freezes the game for good; skipping a cutscene on a high-refresh screen does not stall a frame.
- **Read first.** The sources name the "frame pacer's measured frame interval" but no function; read FM 2.16 and the pacing code first. S0.2.1 leaves the skip's batching by time and names this step for its budget.

### G8.3 Table fields by index, a resource's size by its handle (P-189, P-233)

- **What changes.** P-189: table fields are found by their column name's hash when read (`Table.cpp:33-48`); 241 reads in 56 files, all at load by the inventory's first count. The cook writes each table kind as a typed record with its columns resolved, and the game reads fields by index. The row's correction says reads by name also run in steps, and names are resolved to ids at load with an unknown name refused there. Each read is an item (the row closes when the last is done):
  - a message's index by a binary search on its name at each fetch (`MessageData::findMessageIndex`, `MessageHolder.cpp:149-159`, from `:28`, `:41`, `:49`, `:77`);
  - a story event's progress by scanning the map table by name at each ask (`GameDataHolder::isPassedStoryEvent`, `GameDataHolder.cpp:282-288`; from `GameEventFlagChecker.cpp:43`, `EventUtil.cpp:157`, `:161`, `StorySequenceExecutor.cpp:686-760`), a name not in the table reading as passed;
  - rail point arguments by name (`MR::getRailPointArg*`, `RailUtil.cpp:666-700`; from `GreenCaterpillarBig.cpp:137`, `HoneyBee.cpp:186`, `ClipAreaDropLaser.cpp:103`, `Syati.cpp:493`, `Pukupuku.cpp:338`);
  - `findElementBinary` reading the key column by name at every probe (`Table.cpp:56-89`);
  - collision codes by column name every step (`CollisionCode.cpp:100-121`, `'Floor_code'` at `:123-125`, reached by `MarioModule::getFloorCode`, `MarioModule.cpp:568-570`, `FloorCode::getCode`, `MarioMapCode.cpp:8-20`, `MR::getFloorCodeIndex`, `MapUtil.cpp:578-580`; read from `MarioJump.cpp:146`, `:203`, `:266`, `:278` and `MarioMove.cpp:834`); each prism group's codes become a typed record indexed by the prism's attribute;
  - the stage-effect tables (32, 29 and 155 rows) searched by object name at each ask (`StageEffectDataTable.cpp:269-295`; from `AnmModelObj.cpp:75-76`, `:88`, `:102`, `HeavensDoorDemoObj.cpp:59`);
  - a rail rider's current point's id by column name at every move (`RailRider.cpp:250-253`);
  - a camera shake's name compared with seven strings each step (`ActorPadAndCameraCtrl.cpp:107-127`).
  P-233: a resource's size is found by walking every entry of its archive's folder tree comparing data pointers (`AssetFolder::findSize`, `AssetFolder.cpp:115-129`, for `getResSize`, `:131-133`); the layout's resource accessor takes the BRLYT and BRLAN kinds by four-byte tags, leaves out fonts by a `.brfnt` substring and reads a size as the cooked record's second word (`LayoutHolder.cpp:12-35`). After: an asset's handle carries its size and kind beside its pointer, from the cooked index, and the accessor asks by kind.
- **Where.** `native/petari/src/Game/Util/Table.cpp`; `native/petari/src/Game/System/MessageHolder.cpp`, `GameDataHolder.cpp`, `AssetFolder.cpp`, `LayoutHolder.cpp`; `native/petari/src/Game/Map/CollisionCode.cpp`; `native/petari/src/Game/Util/RailUtil.cpp`; `native/petari/src/Game/LiveActor/RailRider.cpp`, `ActorPadAndCameraCtrl.cpp`; `native/petari/src/Game/MapObj/StageEffectDataTable.cpp`; the cook for tables and prisms (the sources name `crates/nebula-import/src/cook/table.rs` and `kcl.rs`).
- **Rows.** P-189: whole. P-233: whole.
- **Needs first.** Nothing.
- **Old path.** None named: the readers and the cook change together; the search for the name lookup ends at zero.
- **Proof.**
  1. The replay identical (both rows; the file-08 test for P-189).
  2. The name search gone, the search recorded in the inventory (the sources give no pattern; the row's search is the one that finds `getCsvData*`, `getValue<T>(key)`, `findElement*`, `searchItemInfo`, `getColumnKind`, INV section 2 "Tables").
  3. An unknown message, story-event or rail name is refused at load (the row's form).
- **For a player.** Nothing visible: it is how the code is built; less per-step work.
- **Read first.** R10 changes the layout readers; `LayoutHolder.cpp:12-35` may be in a new form when this step starts (the sources give no further detail; read it in its form then). R10 and this step both touch the layout resource accessor: whichever lands second reads the other's form.

### G8.4 Limits that grow, names without 16-bit hashes (P-192, P-148, P-204)

- **What changes.** P-192: shared counts and id widths can cap what is added. The owner's rule (file 08 5.H.3): it grows where the code can ask for one more and gets none in silence; it stays where the count is what the actor is made of; every shared holder grows and every id or index is 32 bits wide or more (INV section 3). The tables and pools were classed 11-19 by reading (149 hits of the literal-size pattern, 11 not tables; 33 working arrays, 14 record fields, 36 the game's values, 38 an actor's parts, 17 the sound driver's state; 28 dead-actor takes and 40 free-slot loops read): nothing new grows beyond the row's list. Each item below is one fault and is closed here (the row closes when the last is done).
- **Parts closed here** (plan decision: the last step of the package closes every part the rows name):
  - Parts that belong to G6.4 and are closed here: [ ] the sound effect categories' table sized from the data (P-224); [ ] the BGM settings as records keyed by sound id (P-228).
  - Sound ids and tables:
    - [ ] `SoundID` packs section and group in 8 bits each and the wave in 16 (`Sound.hpp:26-27`, `SoundVoices.cpp:70`);
    - [ ] banks by a byte number, 256, a higher one a panic (`SoundEngine.hpp:207-208`, `:294-296`; `SoundEngine.cpp:119-121`);
    - [ ] 16 effect categories by the sound's group (`EffectManager.hpp:17-21`; table sized from the data);
    - [ ] wave groups switched by a 32-bit mask (`SoundArchive.cpp:123-129`);
    - [ ] 16-bit resource numbers with 0xffff for none (`SoundData.hpp:282-283`, `StdSoundInfo.cpp:22-30`);
    - [ ] tracks told apart inside the 16-bit wave field (`StdSoundInfo.cpp:85-88`);
    - [ ] the host's voices a fixed 4096, full: stolen by priority or dropped in silence (`SoundEngine.hpp:206`, `SoundEngine.cpp:92-105`; replay identical below today's 4096);
    - [ ] talk voices by a byte into a table of 162, so an added character has no voice of its own (`AudTalkSoundData.cpp:183-188`, from `SoundUtil.cpp:272`);
    - [ ] the sound-name index's byte counters (`AudSoundNameConverter.cpp:54-70`);
    - [ ] BGM settings in fixed tables keyed by the wave number, to become records keyed by sound id (`AudBgmSetting.cpp:6-15`, `:217-280`; P-228);
    - [ ] two limited-sound slots, a third filed nowhere (`AudSystem.cpp:857-872`);
    - [ ] speaker waves by a 16-bit id (`SpkTable.hpp:6-8`);
    - [ ] chord tables by an s16 id (`AudChordInfo.cpp:24-25`).
  - Game data:
    - [ ] a message's sound, area and already-read ids are s8 (`MessageRecord`, `MessageHolder.hpp:23-32`);
    - [ ] story progress is a u8 (`GameDataPlayerStatus.hpp:26`) given the table's u32 in silence (`GameDataHolder.cpp:290-296`);
    - [ ] the flag store is sized once and a set past its size is dropped in silence (`MR::BitArray::set`, `BitArray.cpp:16-19`; `isOn` reads off, `:8-14`).
  - Scene pools that answer none when full (each grows from data already resident, no file read in a step):
    - [ ] Kabokuri fires, pool of 16 (`KabokuriFireHolder.cpp:6-16`, `Kabokuri.cpp:339`); Kameck beams, fire balls and turtles, 16 (`KameckBeamHolder.cpp:8-10`); TakoHei ink, 16 (`TakoHeiInkHolder.cpp:6-14`);
    - [ ] clip-area drops, 32 (`ClipAreaDropHolder.cpp:5-14`, `:26-31`; `ClipAreaDropLaser.cpp:107`);
    - [ ] stand-in models, a closed table of 15 by placement id (`DummyDisplayModel.cpp:18-34`, `:54-56`);
    - [ ] caps of 1024 notes (`NoteFairy.cpp:154-157`) and 1024 rocks (`RockCreator.cpp:43-48`): RockCreator's count of live rocks is the creator's design, so it stays the game's number for the game's creators and becomes data a creator carries, with no shared storage cap (the decision recorded in the row; its no-spawn while every rock lives, `:108-115`, is the game's rule);
    - [ ] Mario's message queue of 32 (`MarioMessenger.cpp:6`, `:24-27`; its two arrays never freed, `:8-10`, `:34-35`);
    - [ ] four short talk balloons shared by every talking actor, a fifth taking the oldest with no check that it is free (`TalkBalloon.cpp:94-96`, `:580-591`);
    - [ ] guard points 0 to 15 (`SkeletalFishBossFunc.cpp:9-20`);
    - [ ] the touch grab targets in a `std::array` of 32 (`native_game_host_sdl.cpp:124-127`, `:194-202`).
  - Id widths:
    - [ ] camera group codes in a 256-entry array (`GameCameraCreator.cpp:24-30`);
    - [ ] a camera chunk's zone narrowed to s8 (`CameraParamChunkID.hpp:26`, `CameraParamChunkID.cpp:64-65`);
    - [ ] a talk animation's start frame to s16 (`DemoTalkAnimCtrl.cpp:68-73`);
    - [ ] particle group and manager ids u8 and resource ids u16 (`EmitterManager.cpp:78-101`, tables sized by `gidMax` and `ridMax`, `:45-62`, indexed with no bound and the manager read with no null check, `:80`, `:90`, `:160`; P-188's seed packing is R4.4's);
    - [ ] texture-index animation entries u8 (`ParticleShape.hpp:193`, one effect resource names at most 256 textures);
    - [ ] a class tag u16 with bit 15 the own-class flag (`NameObj.hpp:46-47`, at most 32,767 classes);
    - [ ] a cube camera's id narrowed to u16 (`CubeCamera.cpp:66-68`).
  - Materials and the cook:
    - [ ] material slots 16 (`NEBULA_MATERIAL_SLOTS`, `nebula_limits.hlsli:6-7`) and a draw names at most 16 maps (`program.hlsli:84-100`); without descriptor indexing Vulkan writes a draw's table as 16 maps and samplers (`shader_compile_vk.h:48`, `renderer_vk_frame.cpp:2373-2387`, `:2661-2663`), Direct3D 11 and Metal the same: the form past it is refused at load with a message; whether the cook refuses or drops a 17th is read in `crates/nebula-import/src/cook/limits.rs`;
    - [ ] a whole number in a table cell is masked to the column's width in silence (`table.rs:342`; widths `:210-216`);
    - [ ] the sound item record keeps the disc's 8- and 16-bit fields (`sound.rs:1266-1281`);
    - [ ] a layout of 65,535 panes or materials is refused at cook, loudly (`ui_layout.rs:636-640`; already loud, recorded as kept).
  - Stays as made: the effect categories' active and inactive limits (the sound data's mix), an actor's own sound handles (`SoundObject.cpp:10`), the remix's 16 note ids (`AudRemixSequencer.cpp:76-89`, `AudRemixMgr.cpp:12`) and the music's own polyphony (5.H.5), and every actor's own count (a boss's 64 fires, a Kameck's eight summoned actors).
  - P-148: a sensor's list of touching sensors is fixed at its group size and drops any more without a word (`HitSensor.cpp` `addHitSensor`, filled from `SensorHitChecker.cpp`); the owner: let it grow, like every other console limit.
  - P-204: effect and sound names are matched by 16-bit hashes: `PaneEffectKeeper`'s find (`PaneEffectKeeper.cpp:107-116`) uses `MultiEmitter::isEqualName(u16)` (`MultiEmitter.cpp:243-245`, `MultiEmitter.hpp:40-44`), so two effect names in one layout's list with the same low 16 bits resolve to the first; `AudSeKeeper` keeps joint boxes by a u16 hash and finds them by it (`AudSeKeeper.cpp:58`, `:81`, `AudSeKeeper.hpp:22`). After: by name (or the full hash and the name), as `MultiSceneEffectKeeper` does (`MultiSceneEffectKeeper.cpp:77-80`). The same match in P-192's Effect line (`MultiEmitter.cpp:243-245`) is closed by P-204.
- **Where.** The files named in the lists, under `native/petari/src/Game/` (`Sound`, `GameAudio`, `AudioLib`, `Speaker`, `System`, `Util`, `Enemy`, `MapObj`, `Player`, `NPC`, `Boss`, `Camera`, `Demo`, `Particle`, `NameObj`, `AreaObj`, `Effect`, `Screen`, `Scene`, `LiveActor`), `platform/sdl/native_game_host_sdl.cpp`, `runtime/src/render/materials/` and `runtime/src/render/vk/`, and the cook's `limits.rs`, `table.rs`, `sound.rs`, `ui_layout.rs`.
- **Rows.** P-192: whole. P-148: whole. P-204: whole.
- **Needs first.** Nothing. G4.3's pools and G6.4's checked sound tables come before or after; the categories' table size and BGM settings records named in G6.4 are done here.
- **Old path.** None: each limit is replaced where it stands.
- **Proof.**
  1. The row's test: a test adds actors and classes far past the largest scene's numbers and the scene runs, with no entry dropped. No added-content work starts by this.
  2. The replay identical (voices: identical below today's 4096).
  3. Each of the 138 tables and each pool has its line (grows, or stays with the reason); the 11-19 classing is the record.
  4. P-204: two effect names with the same low 16 bits resolve to their own entries (the sources name no test).
  5. Every box of "Parts closed here" ticked.
- **For a player.** Nothing visible with the disc's content; no sound, effect or attack is dropped in silence when more is added.
- **Read first.** The owner's 11-19 answer allows a non-breaking modern form. The row asks a question of the owner only for RockCreator's no-spawn while every rock lives (`RockCreator.cpp:108-115`, the game's rule); it is decided as written in the list above.

### Rows of this package

| Row | Today | After | Closes at |
| --- | --- | --- | --- |
| P-148 | A sensor's list of touching sensors is fixed at its group size and drops any more without a word. | It grows, like every other console limit (the owner). | G8.4 |
| P-189 | Table fields are found by their column name's hash; reads by name also run in steps (messages, story events, rail points, collision codes, stage effects, shakes). | The cook writes each table kind as a typed record with columns resolved; fields read by index; names resolved to ids at load. | G8.3 |
| P-192 | Shared counts and id widths cap what is added: sound ids, banks, categories, voices, pools of 16 and 32, narrow ids, 16-slot material and bound tables. | It grows where the code can ask for one more and gets none in silence; ids 32 bits or wider; an actor's own count stays; tested far past the largest scene. | G8.4 |
| P-204 | Effect and sound names are matched by 16-bit hashes, so names with the same low 16 bits resolve to the first. | Matched by name (or the full hash and the name), as `MultiSceneEffectKeeper` does. | G8.4 |
| P-205 | The save is the console's `GameData.bin` layout in host order (16-bit hashes, byte counts, 6-bit ids, u16 values), and the game reads a console file from the NAND layout's folder. | Named, sized records with a version; a console file converted once into `Nebula.sav` by the importer on every platform; the old file kept. | G8.1 |
| P-232 | The event value chunk reads twice its size, so every load marks it failed; flags saved under 15-bit hashes can collide; resource names compared by hash alone; unchecked reallocs. | Entries counted by size; a failure reported as one; flags by name or a unique id; names compared after a hash match; allocation checked. | G8.1 |
| P-233 | A resource's size is found by walking its archive's folder tree; the layout accessor reads kinds by four-byte tags and sizes from a record word. | An asset's handle carries its size and kind beside its pointer; the accessor asks by kind. | G8.3 |
| P-235 | The cutscene skip runs up to 9 ms and 40 steps a frame, more than a frame above about 110 Hz; a save or load request waits for the one before it. | The skip's budget is a share of the measured frame interval; a request queues behind the running one; the step never waits. | G8.2 |
| P-272 | The game's wait for its last save has no time limit on the game thread; a stuck write freezes the step or the exit for good. | A bounded wait in both places; the player is told and chooses to wait or quit; the save-failed message; comments agree. | G8.2 |


---

## G9 Timing and small items

This package fixes six small things that still follow the console or a fixed size: movie cues that a hitch can lose, the staff roll timed by counting steps, the Mii rules of the console's format, fixed counts in the 2D screens, a picture size cap, and the host's menu stability. A player gets subtitles and rumble that never get lost, an end roll that stays with its music, special Miis, no silent caps, and a more stable settings menu.

- **Design:** each row's note; INV section 3's lines for P-203, P-206, P-208, P-211 and INV section 2 ("Screen" bullet); the owner's answer on P-268 (11-19).
- **Needs first:** G9.2 needs G6.5 (the skeleton: P-206 reads the stream's position that G6.5 defines). Nothing else in the skeleton.
- **Rows:** 6 (P-203, P-206, P-208, P-211, P-268, P-306)

A row with several faults has each fault as its own item (plan decision): the row closes when its last item is done. Line numbers are as cited at 6577392b5 (P-203, P-206, P-208, P-211: the inventory's read) and 241395b49 (the rows' own recheck) unless a card says otherwise.

### G9.1 Movie cues fire when the movie's place crosses them (P-203)

- **What changes.** A movie's subtitle line starts only when the shown frame equals its start frame (`MoviePlayingSequence.cpp:335-350`), and a movie rumble starts the same way (`DemoPadRumbler.cpp:23-30`). The shown frame is picked from the sound's place and frames are passed over when the game is a whole frame late (`MovieStream.cpp:194-217`, `MovieStream.hpp:50-54`), so a hitch at that moment loses the line or the rumble for good. The frame is also picked inside `MoviePlayerSimple::draw` (`:72-74`), so what the step reads depends on when the draw ran. After: each entry fires when the movie's place crosses its start (last < start <= now), read from the movie's position and not from the picked picture. The pick moves to the step (or the host) and the draw only shows it.
- **Where.** `native/petari/src/Game/Screen/MoviePlayingSequence.cpp`, `MovieStream.cpp`, `MoviePlayerSimple.cpp`, `native/petari/src/Game/Demo/DemoPadRumbler.cpp`, `native/petari/include/Game/Screen/MovieStream.hpp`.
- **Rows.** P-203: whole.
- **Needs first.** Nothing.
- **Old path.** None named: the comparison on equality is replaced.
- **Proof.**
  1. A forced late step over a start frame still shows the line and rumbles (the row's test).
  2. The movies of the disc play with the same subtitles and rumble when no step is late.
- **For a player.** A subtitle or a rumble in a movie is never skipped because the game stalled for a moment.
- **Read first.** S0.2.1 (landing A, P-214) already moves the movie's frame pick into the step ("the movie's frame from the stream's position"). Read what is left of `MoviePlayerSimple.cpp:72-74` and `:128` after it before changing the cue test.

### G9.2 The staff roll placed from the music's position (P-206)

- **What changes.** The staff roll is timed by counting game steps beside the music: pictures at fixed steps (`StaffRoll.cpp:49-56`, `:178-187`), `mBgmFrame` counted per step while the music plays (`:552-554`), and the end logo when that count reaches 14211 less the scroll's steps (`:387-395`). A late audio buffer or a hitch moves the pictures and the end off the music. After: the scroll and the pictures are placed from the stream's play position (in samples), as the movies follow the sound's place.
- **Where.** `native/petari/src/Game/Screen/StaffRoll.cpp`.
- **Rows.** P-206: whole.
- **Needs first.** G6.5.
- **Old path.** None named: the step counting is replaced.
- **Proof.**
  1. A late audio buffer or a hitch no longer moves the pictures or the end off the music (the sources name no test; the row names the fault).
  2. The roll's pictures and end logo land at the same moments as today on a steady run.
- **For a player.** The end roll stays with its music on any device, even after a stall.
- **Read first.** The row says "the stream's play position (samples)"; G6.5 (P-226) makes the step know a stream's position by the step clock, with the audio following, and says no step reads the device's play position. Use G6.5's position. The 64 line layouts and 20 pictures in the same file are P-208's part (G9.4).

### G9.3 The Mii library's own identity (P-268)

- **What changes.** A Mii is known by the console format's 8-byte create ID and its flag bits (`MiiCharacters.cpp:22-24`, `:156-182`): an ID of zeros is none, a temporary ID (bit 0x20) never matches another, and a special Mii (bit 0x80 clear) is refused unless it is marked local-only (`:202`); `nebula_game_mii_valid` applies the same rules for the host (`:221-224`). After: the host's library gives each Mii a stable identity of its own and checks the record once at import; the create ID stays as the key old console saves carry. The owner (11-19): drop the refusal; special Miis can be imported. The game's steps are the same; which Miis are offered may change.
- **Where.** `native/petari/src/Game/Mii/MiiCharacters.cpp` (`nebula_game_mii_valid`), `native/petari/include/Game/Mii/Mii.hpp`, `runtime/include/galaxy/mii_import.h` (the library list of any length, `:51-57`, `:61`).
- **Rows.** P-268: whole.
- **Needs first.** Nothing.
- **Old path.** None named.
- **Proof.**
  1. A special Mii that is not marked local-only is offered by the picker and imports (the owner's answer; the sources name no test).
  2. The game's step hash is identical (the row).
- **For a player.** Special Miis can be imported and chosen.
- **Read first.** The owner's answer (11-19) drops the special and local-only refusal. The sources do not say whether the "none" for an ID of zeros and the temporary-ID rule stay; decide them with the library's identity, in the code.

### G9.4 Fixed counts in the 2D screens, and a picture size cap (P-208, P-211)

- **What changes.** The fixed counts are replaced by sizes taken from the content. The modern form of P-208 is: probe to the first missing name; lists sized to the content; counters laid out from the number's width; a single last point for the pointer. Each fault is an item:
  - P-208, picture book: pages and lines are found by probing message names up to 100 (`PictureBookLayout.cpp:61-73`, `:425-443`).
  - P-208, star list: it shows five races (`PowerStarList.cpp:113-123`, `:183-189`) and holds a coin score to 999 (`:573-579`).
  - P-208, star-piece counter: slots for three (corner) or four (centre) digits (`StarPieceCounter.cpp:138-156`).
  - P-208, wipe holders: arrays of the size passed with no bound check (`WipeHolderBase.cpp:7-10`, `:64-73`; 8 scene, 4 system). No path adds a wipe today (`git grep addWipeLayout` at 6577392b5 found `SceneWipeHolder.cpp` and `SystemWipeHolder.cpp` only).
  - P-208 (also named in P-206's note), staff roll: 64 line layouts with the rest of a line dropped (`StaffRoll.cpp:283-288`, `:691-695`) and 20 pictures (`:27-28`, `:193-204`).
  - P-208, the pointer's past points: a fixed 16 (`StarPointerController.cpp:20`, `:56`), of which one is ever used (`InputPointer::getEnablePastCount` returns 0 or 1, `InputPointer.cpp:47`).
  - P-211: a picture of the ABOUT page (and the benchmark's graph) wider or taller than `cModPictureSide`, 1024 (`NebulaSettingsMenu.cpp:321`), is not shown at all and the log has no line (`:4996`); the copy is made into a buffer of 1024 by 1024 RGBA8 whatever the picture's size (`:4999-5001`). After: the texture is made at the picture's size, or the picture is scaled to the largest texture the device takes and the log says so; it is never dropped.
- **Where.** Under `native/petari/src/Game/Screen/`: `PictureBookLayout.cpp`, `PowerStarList.cpp`, `StarPieceCounter.cpp`, `WipeHolderBase.cpp`, `StaffRoll.cpp`, `StarPointerController.cpp`, `NebulaSettingsMenu.cpp`; `native/petari/src/Game/System/InputPointer.cpp`.
- **Rows.** P-208: whole. P-211: whole.
- **Needs first.** Nothing in the skeleton. G9.2 changes the same file (`StaffRoll.cpp`): land this after it.
- **Old path.** None named: the fixed sizes are replaced.
- **Proof.**
  1. P-208: a picture book with more than 100 messages lists them all, and a coin score past 999 and a star-piece count of five digits are laid out (the sources name no test).
  2. P-211: a picture wider or taller than 1024 is shown, scaled and logged if it exceeds the device's largest texture.
  3. The pictures of every 2D screen unchanged for the disc's content (the gate's pictures).
- **For a player.** Nothing visible with the disc's content; larger content and counts are not cut off.
- **Read first.** The sources leave nothing open for this step. Read the code that fills each count before replacing it.

### G9.5 The host's menus: stability (P-306)

- **What changes.** The host's settings and benchmark menus have six faults. Each is an item, with the row's fix:
  - The benchmark menu reads a result file into a buffer of the most it allows, 16 MiB + 1 for frame lists, zero-filled whatever the file's size (`file_text`, `benchmark_menu.cpp:74-84`), its results listed, read and drawn under its lock. Fix: the file read by its size on a worker when the page opens, the finished pages handed over under the lock.
  - The render thread's `poll_dlss_download` reads `s_vendor_offer` and `s_license_pages` with no lock and then takes the settings lock that a whole page build holds (`native_settings.cpp:5697-5708`; `s_vendor_offer` is a plain enum, `:5486`). Fix: the download's state published as atomics; the render thread takes no settings lock.
  - `save_now` waits at most 5 s for the save worker and then writes itself, so two writers can overlap on the settings files (`settings_save_model.cpp:93-98`). Fix: every write through the model's one thread, `save_now` posting a request and waiting for its completion with a bounded, reported timeout, files replaced atomically.
  - The save worker and the dropped-file Mii import run on detached threads, the worker capturing `this` and never joined (`settings_save_model.cpp:35-36`; `native_settings.cpp:6529-6538`, no `try`). Fix: workers owned and joined at shutdown, each task catching its own exceptions.
  - The YOUR MIIS row shows "%d OF 100" (`settings_general_page.cpp:208-210`), the Wii database's 100 slots, though the library is a list of any length (`mii_import.h:51-57`, `:61`) and the game's picker takes any number (`Mii.hpp:3`, `MiiCharacters.cpp:111`). Fix: the row shows the count alone.
  The added-content import's detached threads (`native_settings.cpp:3401-3474`) and the ABOUT window's wait (`:5953-5966`) are outside this step, as the row says.
- **Where.** `runtime/src/ui/benchmark_menu.cpp`, `runtime/src/ui/native_settings.cpp`, `runtime/src/ui/settings_save_model.cpp`, `runtime/src/ui/settings_general_page.cpp`, `runtime/include/galaxy/mii_import.h`.
- **Rows.** P-306: whole.
- **Needs first.** Nothing.
- **Old path.** None: the code is replaced in place.
- **Proof.**
  1. The game's replay is identical (the row).
  2. The sources name no test. What must be shown: the benchmark page opens a large result file without a 16 MiB zero-fill on the render thread; the render thread takes no settings lock for the download poll; two saves never overlap on the settings files; closing the game joins the save worker and the Mii import; the Mii row shows a count over 100 as its own number.
- **For a player.** The settings menu does not hitch or lose a setting when a save or a download overlaps; the Mii row shows the real count.
- **Read first.** The benchmark and the settings pages are host code, not the game's step; the rows' fixes name no figures to meet.

### Rows of this package

| Row | Today | After | Closes at |
| --- | --- | --- | --- |
| P-203 | A movie's subtitle line and rumble start only when the shown frame equals their start frame; a late game loses them for good; the frame is picked inside `draw`. | Each entry fires when the movie's place crosses its start, read from the movie position; the pick moves to the step and the draw only shows it. | G9.1 |
| P-206 | The staff roll is timed by counting steps beside the music (pictures at fixed steps, the end at 14211 less the scroll's steps); a stall moves them off the music. | The scroll and pictures are placed from the stream's play position (samples), as the movies follow the sound's place. | G9.2 |
| P-208 | Fixed counts in the 2D screens: picture-book probe to 100, five races, scores to 999, three or four digits, unbounded wipe arrays, 64 staff-roll lines, 20 pictures, 16 past pointer points. | Probe to the first missing name; lists sized to the content; counters laid out from the number's width; a single last point. | G9.4 |
| P-211 | An ABOUT picture (or the benchmark's graph) larger than 1024 is not shown, with no log line; the copy is into a fixed 1024 by 1024 buffer. | The texture is made at the picture's size, or the picture is scaled to the largest texture the device takes and the log says so; never dropped. | G9.4 |
| P-268 | A Mii is known by the console format's create ID and flags: zeros are none, temporary IDs never match, a special Mii is refused unless local-only. | The host library gives each Mii a stable identity and checks the record once at import; the create ID stays as the key for old saves; special Miis are allowed (owner, 11-19). | G9.3 |
| P-306 | The benchmark menu zero-fills a 16 MiB buffer; the download poll takes no lock on shared state; two save writers can overlap; detached threads never joined; the Mii row says "OF 100". | A sized read on a worker; atomics for the download state; one writer thread and a bounded, reported `save_now`; owned and joined workers; the Mii row shows the count alone. | G9.5 |


---

## Z The end

This package is the last work: the game's performance profiles are taken again on the finished code, the end check is run on one commit of main, and, only if every check holds, the statement is made and the two builds (the Windows exe and the Android apk) are produced. Nothing is released, tagged or uploaded. A player gets a Windows build and an Android build that use a profile of the finished game, and the Android, Linux and Apple builds, which today get only part of the profile's gain, get a profile of their own.

- **Design:** the rows' notes (P-014, P-269); FM 1.10 (P-014: "a profile taken before the foundation is stale after it: retaken after F7"); `native/CMakeLists.txt:379-401` and `tools/gate/pgo.py` for the profile; `D:/NebulaWork/plan/08-CONSOLE-FREE-ORDER.md` section 6 for the statement and the two builds; the end check is the planner's five checks (plan decision, in Z.2). P-014's finding is in audit file B (area 4: game step performance structure, finding P-014).
- **Needs first:** every other package of this plan (the skeleton: the profile is retaken at the end, and Z.2 is run on the finished commit).
- **Rows:** 2 (P-014, P-269)

### Z.1 Profiles taken again

- **What changes.** The Windows release links a stored profile of the game's hot paths, `native/pgo/game.profdata`, made by `tools/gate/pgo.py make` from the tour's 102 stops played on Direct3D 12 in fixed steps, and it makes the game's CPU fall about 17 %. A profile older than the code still applies to what has not changed. Two items (plan decision: each is an item, and each row closes when its items are done):
  - P-014, confirm and close: the row says the stored profile's staleness warnings are switched off (audit file B, finding P-014: `-Wno-profile-instr-unprofiled -Wno-profile-instr-out-of-date`, the profile dated 2026-11-14, `tools/verify/pgo_profile.sh` (not found on main) a manual step run before a release). That fault may already be fixed on main: `tools/gate/pgo.py check BUILD_LOG` exists and fails a build with more than 5 % of its functions out of date or a file the profile does not know, and the build keeps the two warnings on, not as errors (`native/CMakeLists.txt:379-401`). Confirm this at the commit being finished, by the check's test (`tools/gate/tests/pgo_test.py`) and by a build log; if it holds, close P-014 with that evidence. The profile is still retaken on the finished code (the earlier changes of this plan make today's profile stale; the check will say how much).
  - P-269: the profile names functions as the Windows build mangles them (the Microsoft ABI), so the Android, Linux and Apple builds turn `NEBULA_PGO` into ThinLTO alone (`:384-393`): about 7 % of the game's CPU by itself, against about 17 % with the profile (the comment's own figures, still to be measured on the tablet). After: (1) the Windows profile is taken again on the finished code; (2) a second profile is trained on an Itanium-ABI build (the same tour, `tools/gate/pgo.py make` on Linux or Android) and linked into the Android, Linux and Apple modules; (3) P-014's staleness check runs on both profiles; (4) `-ffp-contract=off` keeps the results bit for bit.
- **Where.** `native/pgo/game.profdata` (its note file `native/pgo/game.profdata.txt` is not found at 790626cb5); `native/CMakeLists.txt:379-401`; `tools/gate/pgo.py` and `tools/gate/tests/pgo_test.py`; `tools/gate/gate_tour.py` (the tour played in fixed steps); `tools/gate/build_windows.ps1`; `.github/workflows/release.yml` (the check runs on the players' build, per `pgo.py`'s own text).
- **Rows.** P-014: whole (fault confirmed fixed and closed, the profile retaken at the end, the staleness check on). P-269: whole (the second profile and its use by the other platforms' builds).
- **Needs first.** Every other package (the skeleton).
- **Old path.** The ThinLTO-only fallback for non-Windows builds (`NEBULA_PGO` turned into `NEBULA_LTO`, `native/CMakeLists.txt:389-393`) is replaced by the second profile.
- **Proof.**
  1. `pgo.py check` passes on the Windows profile build's log (at most 5 % of functions out of date, no unknown file), and on the second profile's build.
  2. The replay's step hash is identical with each profile (the row: "replay identical").
  3. The tablet's step time is measured with and without the second profile; the comment's 7 % and 17 % are the figures to be checked, not assumed.
- **For a player.** The game steps faster on phones, tablets, Linux and Mac, as it does on Windows; no behaviour changes.
- **Read first.** P-014's text says the staleness warnings are switched off. At 790626cb5 the build keeps both on (`native/CMakeLists.txt:396-398`) and `pgo.py check` exists (plan decision: confirm and close the fault, and still retake the profiles). The sources do not name the second profile's file; choose its name with the build change.

### Z.2 The end check

- **What changes.** Nothing in the code. The check is exactly these five, run in this order on one commit of main, each run then and not remembered (plan decision). If one cannot be met, you are not at the end: go back to the item that fails it. If the only things left need the owner (file 08 section 8), post `NOT 100%` with exactly those items, make no builds and stop. A false 100% is the one result worse than no result (file 08 section 6).
- **Where.** `docs/engine/CONSOLE-FREE.md` (the inventory; the pass-one searches are run by the patterns in its section 1; their script `tools/verify/console_free_search.py` is not found at 790626cb5); `docs/engine/FRAME-MODEL.md`; the measures table `docs/plan/native/measures-done.md`; the gate and the replay suite.
- **Rows.** None closed here: this step checks the others.
- **Needs first.** Z.1, and every other package.
- **Old path.** None: nothing draws differently.
- **Proof.** The five checks, in this order, on one commit:
  1. Every row of the plan is closed with its proof; every inventory line is closed; and the inventory's whole-tree searches are run again on this commit, finding only the listed stays.
  2. `FRAME-MODEL.md` section 1.9 is empty.
  3. The same recorded play gives the same game on Direct3D 12, Direct3D 11, Vulkan, the tablet and a software device, at 4:3, 16:9, 21:9, 48:9 and the tablet's and a tall phone's shape.
  4. Pacing holds on the three Windows renderers and the tablet at every offered rate, and no figure is worse than the baseline.
  5. The full test set is green on that commit.
- **For a player.** Nothing visible: it is the proof that the rest is done.
- **Read first.** File 08 section 6 lists five different checks (the inventory; FM 1.9, the "done when" of E3, E4 and E6b and F7; the main gate and the long run; the play route measure; no open row that crashes, hangs or loses data); the planner's five replace them (plan decision), and the skeleton's "overview's section 2" is not among the sources. The two lists differ: the planner's third check is the cross-renderer, cross-shape replay, the fifth the full test set.

### Z.3 The statement and the two builds

- **What changes.** Only if Z.2 passed on one commit. The statement is made on the board and as the first lines of `CONSOLE-FREE.md`: "100%: every part of Nebula that was console era, or that the audit and the frame model named for rewriting, has been overhauled, reworked or rewritten. Nothing of the console's implementation runs in the game. Commit <hash>; the list and its proofs are in this file." It is never posted with an item open, a check not run, or a reason "good enough". Then the two builds are made from that commit: the Windows build (the exe as the release workflow makes it) and the Android build (the apk), into `D:/NebulaWork/final/<hash>/` with their SHA-256 sums and build logs. They are signed exactly as the gate's own Windows and tablet builds are signed today; nothing about signing changes. The apk is installed over the tablet's game (never an uninstall, never a wipe), and the start check and the play route run on it. The exe is installed and started on the PC, and a tour runs on it. No release, no tag, no upload: those are the owner's. Then the stop: post `DONE` with the commit, the two files and their sums, delete the timer, and start no other row.
- **Where.** `D:/NebulaWork/final/<hash>/` (to be created; not present now); `tools/gate/build_windows.ps1`; `installer/build-native-setup.ps1`; `platform/android/build_apk.ps1`; `.github/workflows/release.yml` (how the exe is made; it is not run to release anything); the board.
- **Rows.** None.
- **Needs first.** Z.2.
- **Old path.** None.
- **Proof.**
  1. The two files exist in `D:/NebulaWork/final/<hash>/` with their SHA-256 sums and build logs, and the hash is the one the statement names.
  2. The apk's start check and the play route pass on the tablet, installed over the existing game.
  3. A tour passes on the PC with the installed exe.
  4. Nothing was released, tagged or uploaded.
- **For a player.** The finished Windows and Android builds, ready for the owner to release.
- **Read first.** File 08 section 6 speaks of the board and of a timer; they belong to that order's way of working, and the plan's own reporting place is not among the sources. Section 8 of that file lists what is the owner's.

### Rows of this package

| Row | Today | After | Closes at |
| --- | --- | --- | --- |
| P-014 | The Windows release links a stored profile whose staleness warnings were switched off (at 790626cb5 they are on and `pgo.py check` fails a stale build). | The profile is taken again on the finished code, with the staleness check on. | Z.1 |
| P-269 | The game module's profile is named in the Windows ABI, so Android, Linux and Apple builds get ThinLTO alone, about 7 % of the game's CPU against about 17 % with the profile. | A second profile trained on an Itanium-ABI build, linked into those modules, with the staleness check on both; `-ffp-contract=off` keeps results bit for bit; measured on the tablet. | Z.1 |
