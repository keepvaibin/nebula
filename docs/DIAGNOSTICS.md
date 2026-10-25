# Diagnostics

Environment variables for testing and debugging the runtime. None of them is needed to play.

## Test sessions

| Variable | Effect |
|---|---|
| `GALAXY_HIDE_WINDOW=1` | Runs without showing the game window; controllers never vibrate |
| `GALAXY_RENDER_CONFIG_PATH`, `GALAXY_RUNTIME_SETTINGS_PATH` | Use these settings files instead of the ones in `%LOCALAPPDATA%\Nebula` |
| `GALAXY_NAND_ROOT` | Save folder |
| `GALAXY_INSTANCE_LEASE` | Private single-instance lock name, so test sessions can run side by side |
| `GALAXY_AUDIO_WAV_DUMP=<path>`, `GALAXY_AUDIO_WAV_DUMP_ONLY=1` | Write the mixed audio to a WAV file, optionally without opening the sound device |
| `GALAXY_PRESENT_SNAPSHOT_DIR`, `GALAXY_PRESENT_SNAPSHOT_EVERY`, `_FIRST`, `_LAST` | Save every Nth presented frame as a PPM image |
| `GALAXY_TRACE_PRESENT_STATS=1` | Log presented and new frames per second |
| `GALAXY_D3D_DEBUG=1` | Enable the Direct3D 12 debug layer |

## The start and the game thread

Every start logs its steps as `boot:` lines (milliseconds from the process's creation: the disc, the module, the
renderer, the game, its first frame, the title screen) and the renderer's as `[boot] renderer` lines (milliseconds a
step).

| Variable | Effect |
|---|---|
| `GALAXY_BOOT_FAST=0` | Retraces at 60 a second from the start (by default the black frames before the first 3D scene run ahead) |
| `GALAXY_PROFILE_GAME=<seconds>` | Sample the game thread each millisecond from its start, with its callers, into `game-profile.txt` beside the crash dumps; `tools/perf/game_profile.py` names them from the game module's link map |
| `GALAXY_TRACE_THREAD_CPU=1` | Log each thread's CPU time each second |

## Scripted input

Times are in video frames (60 per second) from the start of the session.

| Variable | Format |
|---|---|
| `GALAXY_INPUT_AUTOPRESS_SCRIPT` | `start:duration:buttons;...`, for example `5400:120:A+B` |
| `GALAXY_INPUT_AUTOPRESS_SCRIPT_STOP_AT_MARIO_CONTROL` | `1` stops the button script when the player first controls Mario |
| `GALAXY_INPUT_STICK_SCRIPT` | `start:duration:x:y;...`, axes from -1 to 1 |
| `GALAXY_INPUT_STICK_SCRIPT_RELATIVE_MARIO_CONTROL` | `1` counts stick script times from the first frame the player controls Mario |
| `GALAXY_INPUT_SHAKE_SCRIPT` | `start:duration;...`, Wii Remote shakes |
| `GALAXY_INPUT_POINTER_X`, `GALAXY_INPUT_POINTER_Y` | Fixed pointer position, -1 to 1 |
| `GALAXY_INPUT_REPLAY_LOG` | A recorded input log (TSV) replayed as host input |

## Input and vibration traces

| Variable | Effect |
|---|---|
| `GALAXY_RUMBLE_TRACE=1` | Log the game's motor changes (`[rumble]`) and what the controller in use would receive (`[rumble-out]`); nothing is sent to a controller |
| `GALAXY_TEST_RUMBLE_TARGET` | `xinput:N`, `switch:N`, `playstation:N` or `steam:N`: the controller `[rumble-out]` reports for, in trace sessions |
| `GALAXY_TRACE_TILT=1` | Log tilt sections and the accelerometer values sent (`[tilt]`) |
| `GALAXY_TRACE_NATIVE_MOUSE_POINTER=1` | Log pointer depth answers (`[mouse-pointer-frame]`) and world points (`[pointer-world]`) |

## Audio

| Variable | Effect |
|---|---|
| `GALAXY_AUDIO_MUSIC_GAIN`, `GALAXY_AUDIO_EFFECTS_GAIN`, `GALAXY_AUDIO_SFX_GAIN`, `GALAXY_AUDIO_VOICE_GAIN`, `GALAXY_AUDIO_AMBIENCE_GAIN` | Gain per category, 0.0 to 4.0 (default 1.0) |
| `GALAXY_TRACE_AUDIO_STEMS=1` | Log the JAudio category of each voice; `_START_SYNC`, `_END_SYNC` and `_LANE` narrow it |
