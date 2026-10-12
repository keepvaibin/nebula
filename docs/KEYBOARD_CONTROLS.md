# Controls

The game sees one Wii Remote with a Nunchuk on channel 0. Every Wii input can
be bound to keys and mouse buttons, Xbox (XInput) controls, Switch Pro
Controller and Switch 2 Pro Controller controls.

## Default maps

| Wii input | Keyboard and mouse | Xbox | Switch Pro / Pro Controller 2 |
|---|---|---|---|
| A | `Space`, `Enter`, left mouse button | A | A |
| B | `Left Shift`, `Right Shift`, right mouse button | B, RT | B, ZR |
| 1 | `1`, Numpad `1` | - | - |
| 2 | `2`, Numpad `2` | - | - |
| Plus | `=`, `P` | Menu | Plus |
| Minus | `-`, `M` | View | Minus |
| Home | `H` | - | Home |
| D-pad | Arrow keys | D-pad | D-pad |
| Nunchuk stick | `W` `A` `S` `D` | Left stick | Left stick |
| C | `C` | Left stick click | Left stick click |
| Z | `Z`, `Left Ctrl`, `Right Ctrl` | LB, LT | L, ZL |
| Shake (Wii Remote, spin) | `Q`, `E` | X, Y, RB | X, Y, R, motion |
| Shake (Nunchuk) | - | - | - |
| Pointer | `I` `J` `K` `L`, mouse position | Right stick | Right stick, gyro (off by default) |
| Pointer recenter | `R` | Right stick click | Right stick click |

Switch buttons are by label (Nintendo A is Wii A); Xbox buttons by position.
Capture, GL, GR and C (Pro Controller 2) are unbound by default. The mouse
position drives the pointer while the cursor is inside the game picture, also
when another window has focus; keys and mouse buttons need the game window
focused.

## Remapping

In game: `F1`, View+Menu (Xbox) or Minus+Plus (Switch), page CONTROLS.

| Row | Keyboard | Controller |
|---|---|---|
| INPUT: keyboard+mouse or controller | Left/Right | D-pad/stick |
| EDIT: scheme shown below | Left/Right | D-pad/stick |
| Binding row: add the next pressed input | `Enter` or Right | A |
| Binding row: clear | `Backspace` | X |
| Cancel waiting for an input | `Esc` | - |
| Apply the page | `Space` | Y |

Each Wii input holds up to four inputs; adding a fifth replaces the last.
Stick and pointer directions accept stick directions and buttons; the mouse
buttons bind only to button rows. Controller input is selected only while a
controller is attached. Nebula settings (NebulaSettings.exe, first launch)
has the same choices on its Controls tab.

Settings are stored in `runtime_settings.ini` next to `config.ini`:

```
input_mode=controller
bind.keyboard.a=Space, Enter, MouseLeft
bind.xbox.b=B, RT
bind.switch_pro.shake=X, Y, R
tune.switch_pro.motion=remote
```

Schemes: `keyboard`, `xbox`, `switch_pro`, `switch_pro2`. Targets: `a b one
two plus minus home dpad_up dpad_down dpad_left dpad_right stick_up
stick_down stick_left stick_right c z shake nunchuk_shake pointer_up
pointer_down pointer_left pointer_right pointer_recenter`. Input names: key
names (`Space`, `LShift`, `W`, `Num1`, `F5`, `Equals`, `VK2E`), `MouseLeft`
`MouseRight` `MouseMiddle` `Mouse4` `Mouse5`; Xbox `A B X Y LB RB LT RT LS RS
Menu View DpadUp...`; Switch `A B X Y L R ZL ZR LS RS Plus Minus Home Capture
GL GR C DpadUp...`; sticks `LX+ LX- LY+ LY- RX+ RX- RY+ RY-`.

| Tuning (`tune.<scheme>.`) | Values | Default |
|---|---|---|
| `pointer_speed` | 0.2-6.0 pointer widths per second | keyboard 1.6, controllers 2.4 |
| `invert_pointer_y` | 0/1 | 0 |
| `stick_deadzone`, `pointer_deadzone` | 0-0.5 of full deflection | Xbox: XInput defaults (0.24, 0.27); Switch 0.12 |
| `motion` (Switch) | `off`, `remote`, `nunchuk` | `remote` |
| `shake_sensitivity` (Switch) | 1 (hard shake) - 5 (light) | 3 |
| `gyro_pointer` (Switch) | 0/1 | 0 |

`GALAXY_XINPUT_LEFT_DEADZONE`, `GALAXY_XINPUT_POINTER_DEADZONE`,
`GALAXY_XINPUT_POINTER_SPEED`, `GALAXY_KEYBOARD_POINTER_SPEED` and
`GALAXY_XINPUT_TRIGGER_THRESHOLD` (default 96 of 255) override these.

## Controllers

- Input source: `input_mode=keyboard_mouse` (default) or `controller`; one
  source is live at a time. Controller mode reads every attached XInput pad
  and Switch controller together. `GALAXY_INPUT_MODE` overrides the file.
- Xbox: XInput slots 0-3. `GALAXY_DISABLE_XINPUT=1` disables it,
  `GALAXY_XINPUT_REQUIRE_FOCUS=1` reads it only while the window has focus.
- Switch Pro Controller: USB or Bluetooth (HID). Nebula reads the stick
  calibration from the controller, switches it to full reports with the IMU
  on, rumble off and player LED 1.
- Switch 2 Pro Controller: USB only. It sends input only after a vendor
  command on its second USB interface, which needs the WinUSB driver (for
  example installed for "Interface 1" with Zadig); without it the CONTROLS
  page shows NEEDS WINUSB. Over Bluetooth it is a Bluetooth LE device that
  Windows does not expose as HID; not supported.
- Motion (Switch): a shake is the acceleration magnitude minus gravity over
  about 20 ms crossing the sensitivity threshold, then held 60 ms with a
  280 ms cooldown. The gyro pointer moves the pointer by the controller's
  yaw/pitch rate; the recenter binding returns it to the middle.

Switch protocol details follow SDL's zlib-licensed `SDL_hidapi_switch.c` /
`SDL_hidapi_switch2.c` and dekuNukem's Nintendo_Switch_Reverse_Engineering.

## Native Settings Overlay

`F1`, View+Menu (Xbox) or Minus+Plus (Switch) opens or closes the settings
overlay; a one-line hint names them for the first seconds of a session, and
the file-select screen shows a PC SETTINGS button. Autorepeat never toggles
it. While it is open the game sees no keyboard or controller input, and
buttons held when it closes stay hidden from the game until released.

| Keyboard | Controller | Settings action |
|---|---|---|
| `F1` | View+Menu, Minus+Plus | Open or close |
| `Tab` / Page Down, Page Up | RB/R, LB/L | Next, previous page |
| Up / Down | D-pad or left stick up/down | Select row |
| Left / Right | D-pad or left stick left/right, A | Change the selected row |
| `Enter` | Y | Apply (CONTROLS: `Space`/Y) |
| `Backspace` | X | Undo the draft (CONTROLS binding row: clear) |
| `Esc` | B or Menu/Plus | Close; an unapplied draft is discarded |

On Switch controllers the overlay uses the console's meaning: A changes, B
closes. Pages: DISPLAY, GRAPHICS, CONTROLS, AUDIO (changes apply live), CAPS
(read-only). Settings files are written through flushed sibling temporaries
and atomically replaced; a failed save stays dirty and is retried on the next
close or at shutdown.

## Headless Diagnostics

Automated runtime probes can script input by VI retrace:

| Env var | Format |
|---|---|
| `GALAXY_INPUT_AUTOPRESS_SCRIPT` | `start:duration:buttons;...`, e.g. `5400:120:A+B` |
| `GALAXY_INPUT_AUTOPRESS_SCRIPT_STOP_AT_MARIO_CONTROL` | `1` to stop the long-form button script at the first translated Mario-control boundary; successful diagnostic runs emit one `[input-script-stop]` record and hard-fail if that source contributes A/B afterward |
| `GALAXY_INPUT_AUTOPRESS_SCRIPT_STOP_FALLBACK_VI` | Absolute HID-timeline VI used only before a live Mario-control marker is published; the live marker is authoritative afterward |
| `GALAXY_INPUT_STICK_SCRIPT` | `start:duration:x:y;...`, with stick axes clamped to `-1..1` |
| `GALAXY_INPUT_STICK_SCRIPT_RELATIVE_MARIO_CONTROL` | `1` to treat stick script times as relative to first Mario-control VI |
| `GALAXY_INPUT_STICK_SCRIPT_RELATIVE_FALLBACK_VI` | Absolute VI fallback anchor for relative stick scripts when the Mario-control marker is absent |
| `GALAXY_INPUT_SHAKE_SCRIPT` | `start:duration;...`, stages report-level Wii Remote shake acceleration |
| `GALAXY_INPUT_POINTER_X`, `GALAXY_INPUT_POINTER_Y` | Pointer coordinates clamped to `-1..1`, active for the whole run |
| `GALAXY_INPUT_POINTER2_X`..`GALAXY_INPUT_POINTER6_Y` plus matching `*_VI` | Pointer waypoints that become active at the given VI |
| `GALAXY_INPUT_POINTER_SWEEP_VI`, `GALAXY_INPUT_POINTER_SWEEP_DURATION` | Horizontal pointer sweep over a VI window |
| `GALAXY_INPUT_REPLAY_LOG` | Path to a `tools/measure_inputs.ps1` TSV; replayed as virtual host input before HID report formatting |

These hooks are for deterministic native-runtime diagnostics and do not affect
normal interactive input unless the environment variables are set. Treat them as
proof inputs only when `[input-trace]` shows the expected nonzero report-level
button, stick, pointer, or shake samples on the native Bluetooth/HID boundary.

`tools/run_release_diagnostics.ps1 -TraceInput -InputMode controller` records
`[input-trace]` lines with `source=`, `ptrsrc=`, controller slot, stick values,
pointer coordinates, `shake=`, and the native input boundary (`boundary=bt-hid`
for the virtual Wii Remote path or `boundary=bt-hid-real` for real-device
pass-through).
The diagnostics analyzer treats traced input as proven only when those native
boundary tokens are present and well-formed.
The diagnostics runner always launches the visible Release runtime and clears
`GALAXY_HIDE_WINDOW`.

## Audio Diagnostics

The native DSP mixer exposes persisted gain controls used by the native
settings UI and release launcher:

| Env var | Lane |
|---|---|
| `GALAXY_AUDIO_MUSIC_GAIN` | JAudio sequence/stream music |
| `GALAXY_AUDIO_EFFECTS_GAIN` | shared non-music effects bus |
| `GALAXY_AUDIO_SFX_GAIN` | sound-effect owner categories |
| `GALAXY_AUDIO_VOICE_GAIN` | voice owner categories |
| `GALAXY_AUDIO_AMBIENCE_GAIN` | atmosphere/ambience owner categories |

Each value is a scalar from `0.0` to `4.0`; unset means `1.0`.

Audio stem tracing can be narrowed for live voice/SFX probes:

| Env var | Format |
|---|---|
| `GALAXY_TRACE_AUDIO_STEMS` | set to `1` to log JAudio owner lanes |
| `GALAXY_TRACE_AUDIO_STEMS_START_SYNC` | first DSP sync frame to trace |
| `GALAXY_TRACE_AUDIO_STEMS_END_SYNC` | last DSP sync frame to trace |
| `GALAXY_TRACE_AUDIO_STEMS_LANE` | comma-separated `music`, `sfx`, `voice`, `ambience`, `unknown`, or `all` |
