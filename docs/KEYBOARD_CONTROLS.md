# Keyboard Controls

Keyboard keys and mouse buttons require the game window to be focused. Mouse
position continues to drive the Wii Remote IR pointer while the cursor is
inside the game window, including when another window has focus. The native
runtime presents one connected Wii Remote with a Nunchuk on channel 0.

## Main Controls

| Keyboard | Wii input | Super Mario Galaxy use |
|---|---|---|
| `W`, `A`, `S`, `D` | Nunchuk stick | Move Mario / menu analog movement |
| `Space` or `Enter` | Wii Remote A | Confirm, jump, interact |
| `Left Shift` or `Right Shift` | Wii Remote B | B trigger |
| `Q` or `E` | Wii Remote shake acceleration | Spin |
| `Z` or `Ctrl` | Nunchuk Z | Crouch; combine with movement/jump |
| `C` | Nunchuk C | Camera reset / first-person action |
| `I`, `J`, `K`, `L` | Wii Remote IR pointer | Point up, left, down, right |
| `R` | Recenter pointer | Return the pointer to screen center |

## Wii Remote Buttons

| Keyboard | Wii input |
|---|---|
| Arrow keys | D-pad |
| `=` / `+` or `P` | Plus |
| `-` or `M` | Minus |
| `1` or Numpad `1` | Wii Remote 1 |
| `2` or Numpad `2` | Wii Remote 2 |
| `H` | Home |

The Nunchuk stick returns neutral input when the game window is not focused.
The pointer remains active while the mouse cursor is inside the game client.
Spin input is encoded as a short Wii Remote accelerometer movement in
the Bluetooth/HID report stream; the translated WPAD/KPAD code decides how that
movement becomes gameplay.

## Controller Controls

XInput controllers are supported natively on Windows. The runtime scans slots
0-3 and maps the first connected controller to the same channel-0 Wii Remote
with Nunchuk.

| Controller | Wii input | Super Mario Galaxy use |
|---|---|---|
| Left stick | Nunchuk stick | Move Mario / menu analog movement |
| Right stick | Wii Remote IR pointer | Point at the screen |
| `A` | Wii Remote A | Confirm, jump, interact |
| `B` or right trigger | Wii Remote B | B trigger |
| `X`, `Y`, or right shoulder | Wii Remote shake acceleration | Spin |
| Right-stick click | Recenter pointer | Return the pointer to screen center |
| Left shoulder or left trigger | Nunchuk Z | Crouch; long-jump setup |
| Left-stick click | Nunchuk C | Camera reset / first-person action |
| D-pad | Wii Remote D-pad | Menu navigation |
| Start | Plus | Pause / plus |
| Back | Minus | Minus |

By default, Release play mode uses one live input source at a time and starts
in keyboard/mouse mode. `GALAXY_INPUT_MODE` accepts only explicit source modes.
Input source is read-only in the in-game overlay: select it in the launcher so
controller availability can be checked before the runtime starts. An explicit
physical Wii Remote selection remains fail-closed when the required HID device
is absent.

| Env var | Value | Behavior |
|---|---|---|
| `GALAXY_INPUT_MODE` | `controller` | Use XInput for gameplay buttons/sticks; mouse position and XInput right stick both drive the IR pointer |
| `GALAXY_INPUT_MODE` | `keyboard_mouse` | Use keyboard/mouse gameplay input; XInput right stick still drives the IR pointer |
| `GALAXY_DISABLE_XINPUT` | `1` | Disable controller polling entirely |
| `GALAXY_XINPUT_REQUIRE_FOCUS` | `1` | Require the runtime window to be foreground before reading XInput |

Keyboard keys and mouse buttons are ignored while the game window is not focused;
mouse position still drives IR while the cursor is inside the game client.
XInput is read even when the diagnostic terminal has foreground focus, so an
Xbox controller can advance boot and safety screens immediately after launch.
Mouse IR is published while the cursor is inside the game client, with a short
loss hold for transient Win32/RDP sampling gaps; stale keyboard/mouse state does
not keep the IR camera visible after the live mouse source drops out.

Controller tuning hooks:

| Env var | Default | Behavior |
|---|---:|---|
| `GALAXY_XINPUT_POINTER_SPEED` | `2.4` | Right-stick pointer speed |
| `GALAXY_XINPUT_POINTER_DEADZONE` | XInput right-stick default | Right-stick pointer deadzone |
| `GALAXY_XINPUT_LEFT_DEADZONE` | XInput left-stick default | Left-stick movement deadzone |
| `GALAXY_XINPUT_TRIGGER_THRESHOLD` | `96` | Trigger press threshold for B/Z-style mappings |
| `GALAXY_KEYBOARD_POINTER_SPEED` | `1.6` | Keyboard pointer speed for `I`, `J`, `K`, `L` |

## Native Settings Overlay

While the native game window owns focus, `F1` globally opens or closes the
renderer-owned settings overlay. This is an explicit host UI policy; it is not
conditioned on a guessed guest save-select address. Keyboard autorepeat is
ignored for `F1`, so holding the key cannot flap the overlay.

| Key | Settings action |
|---|---|
| `F1` | Open or close settings |
| `Tab` | Next page |
| Up / Down | Select row |
| Left / Right | Adjust an editable row |
| `Esc` | Close and persist settings |

Internal resolution supports 1x through 4x and is staged for the next process
start because EFB/depth/XFB resources cannot be resized safely in place. The
active input source and fixed VI-locked 60 Hz Release presentation profile are
display-only. Settings files are written through flushed sibling temporaries
and atomically replaced; a failed save remains dirty and is retried on a later
close or shutdown. Overlay geometry has separate compact 427x240, 854x480, and
large/4K layouts so the advertised low-resolution window remains usable.

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
