# Diagnostics

Environment variables for testing and debugging the runtime. None of them is needed to play.

## Test sessions

| Variable | Effect |
|---|---|
| `GALAXY_HIDE_WINDOW=1` | Runs without showing the game window; controllers never vibrate |
| `GALAXY_TEST_NO_RAW_MOUSE=1` | Leaves raw mouse motion (WM_INPUT) unregistered, so only the cursor position moves the pointer |
| `GALAXY_RENDER_CONFIG_PATH`, `GALAXY_RUNTIME_SETTINGS_PATH` | Use these settings files instead of the ones in `%LOCALAPPDATA%\Nebula` |
| `GALAXY_AUDIO_WAV_DUMP=<path>`, `GALAXY_AUDIO_WAV_DUMP_ONLY=1` | Write the mixed audio to a WAV file, optionally without opening the sound device |
| `GALAXY_PRESENT_SNAPSHOT_DIR`, `GALAXY_PRESENT_SNAPSHOT_EVERY`, `_FIRST`, `_LAST` | Save every Nth presented frame as a PPM image |
| `GALAXY_TRACE_PRESENT_STATS=1` | Log presented and new frames per second |
| `GALAXY_D3D_DEBUG=1` | Enable the Direct3D 12 debug layer |

## The start and the game thread

Every start logs its steps as `boot:` lines (milliseconds from the process's creation: the game's data, the module, the
renderer, the game, its first frame, the title screen) and the renderer's as `[boot] renderer` lines (milliseconds a
step).

| Variable | Effect |
|---|---|
| `GALAXY_BOOT_FAST=0` | Game frames at 60 a second from the start (by default the black frames before the first 3D scene run ahead) |
| `GALAXY_PROFILE_GAME=<seconds>` | Sample the game thread each millisecond from its start, with its callers, into `game-profile.txt` beside the crash dumps; `tools/perf/game_profile.py` names them from the game module's link map |
| `GALAXY_TRACE_THREAD_CPU=1` | Log each thread's CPU time each second |

## Scripted input

Times are in video frames (60 per second) from the start of the session.

| Variable | Format |
|---|---|
| `GALAXY_INPUT_AUTOPRESS_SCRIPT` | `start:duration:buttons;...`, for example `5400:120:A+B` |
| `GALAXY_INPUT_STICK_SCRIPT` | `start:duration:x:y;...`, axes from -1 to 1 |
| `GALAXY_INPUT_SHAKE_SCRIPT` | `start:duration;...`, Wii Remote shakes |
| `GALAXY_INPUT_POINTER_X`, `GALAXY_INPUT_POINTER_Y` | Fixed pointer position, -1 to 1 |
| `GALAXY_INPUT_REPLAY_LOG` | A recorded input log (TSV) replayed as host input |

## Audio

| Variable | Effect |
|---|---|
| `GALAXY_AUDIO_MUSIC_GAIN`, `GALAXY_AUDIO_EFFECTS_GAIN`, `GALAXY_AUDIO_SFX_GAIN`, `GALAXY_AUDIO_VOICE_GAIN`, `GALAXY_AUDIO_AMBIENCE_GAIN` | Gain per category, 0.0 to 4.0 (default 1.0) |

## Tools for a landing (`tools/verify`, each says its arguments)

- `all_stops.sh OLD NEW TAG`: two builds, deterministic, through all 102 stops; 386 pictures compared (`compare_stops.py`, `sheet.py`).
- `monkey.py BIN TAG SHARDS FRAMES SEED`: random input on every stop, looking for crashes and hangs.
- `boots.sh BIN TAG COUNT PARALLEL`: many boots at once; a boot that never reaches the title stays with its log.
- `extended.sh BIN TAG`: the ending, Luigi, a slow drive. `luigi2.sh BIN TAG`: a change of player both ways.
- `gpulost.sh BIN TAG TIMES SECONDS`: the graphics device lost. `validate.sh BIN TAG NAME=value`: the tour under the API's validation.
- `sizes.sh BIN TAG`: eight window sizes and shapes. `tools/perf/threadprof.py`: one thread's hot functions, from outside, with its pdb.
- A fatal logs its stack, and a thread dump each thread's stack at its last wait, as module offsets; a link map of the module names them.
