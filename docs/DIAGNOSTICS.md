# Diagnostics

What a run records with `--diagnostics <what>[,<what>]` (every build), then the environment variables for testing the
runtime. None of them is needed to play. The command line's other arguments are in COMMAND_LINE.md.

## --diagnostics

Files go to `diagnostics` in the data folder (COMMAND_LINE.md `--data`), named with the process id; lines go to the log.

| Name | Records |
|---|---|
| `timeline` | The frame pipeline's events, written at exit to `timeline-<pid>.bin` (or `--timeline`'s file); `tools/perf/timeline_digest.py` reads it |
| `frame-tails` | The frame intervals' histogram every 2 s |
| `pointer-latency` | The pointer's selection to the picture showing it |
| `present-stats` | Presented and new frames, GPU and CPU times, each second |
| `frame-times` | Each frame's time and where a scripted press begins (`[latency]` lines; `tools/verify/gaps.py`) |
| `gpu-passes` | GPU time by pass and the shader invocation counts (present-stats' `gpu-pass-ms`, `gpu-stats`) |
| `memory` | What the renderer holds every 600 frames: rings, texture tables, the `[vram] breakdown` line |
| `heaps` | `memory`, and the process's heaps by block size |
| `thread-cpu` | Each thread's CPU time and context switches each second |
| `profile` | The game thread sampled each millisecond with its callers, to the run's end (10 minutes at most), into `game-profile-<pid>.txt`; `tools/perf/game_profile.py` names them from the game module's link map |
| `scene-loads` | Each scene change's phases, presses and placed objects (`tools/verify/loads.py`) |
| `file-reads` | Each read and map of the game's files, and the sound clock, with its frame |
| `pipelines` | The pipelines the run created, at exit, into `pipelines-<pid>.bin` (`tools/dev/site_pipelines.py`) |
| `rt-inputs` | What ray tracing reads of each frame list, checked against its rules and counted every 300 lists: the world's lights (`[rt-inputs] lights` lines) and, with ray tracing on, the traced scene (`[rt-inputs] scene` lines) and each frame's builds (`[rt]` lines) |
| `rt-view` | With ray tracing on: the traced scene drawn over the game's opaque scene, each instance in its own colour (a scene without multisampling) |
| `rt-check` | With ray tracing on: a camera ray a pixel against the depth the game drew, coloured by class (green: the same surface; blue: drawn but not traced; orange: traced behind; red: traced in front; magenta: traced where nothing was drawn) and counted every 300 frames (`[rt-check]` lines) |

Sets: `frames` (timeline, frame-tails, pointer-latency, present-stats, frame-times), `gpu` (gpu-passes), `threads`
(thread-cpu, profile), `loads` (scene-loads, file-reads), `1` (the launcher's recording: timeline, frame-tails,
pointer-latency, present-stats, gpu-passes), `0` (nothing). Texture dumps are a setting (Graphics, DUMP TEXTURES) into
`TextureDumps` beside `config.ini`.

## Test sessions

| Variable | Effect |
|---|---|
| `GALAXY_AUDIO_WAV_DUMP=<path>`, `GALAXY_AUDIO_WAV_DUMP_ONLY=1` | Write the mixed audio to a WAV file, optionally without opening the sound device |
| `GALAXY_PRESENT_SNAPSHOT_DIR`, `GALAXY_PRESENT_SNAPSHOT_EVERY`, `_FIRST`, `_LAST` | Save every Nth presented frame as a PPM image |

## The start and the game thread

Every start logs its steps as `boot:` lines (milliseconds from the process's creation: the game's data, the module, the
renderer, the game, its first frame, the title screen) and the renderer's as `[boot] renderer` lines (milliseconds a
step). The black frames before the first 3D scene run ahead.

## Scripted input

Times are in video frames (60 per second) from the start of the session. `GALAXY_TEST_SCRIPT_CLOCK` picks the clock of the `GALAXY_INPUT_*_SCRIPT` variables (and the waypoints): unset, game frames from boot; `frames`, scene frames from boot (the same for builds with different renderer speeds); `stop`, scene frames since the current tour stop began, with no scripted input before the first stop. `GALAXY_TEST_EVENTS`, `GALAXY_TEST_TELEPORT`, `GALAXY_TEST_WALK` and the by-stop present snapshots always count scene frames from the current tour stop (from boot without a tour), so `stop` puts a script and the events on one clock.

| Variable | Format |
|---|---|
| `GALAXY_INPUT_AUTOPRESS_SCRIPT` | `start:duration:buttons;...`, for example `5400:120:A+B` |
| `GALAXY_INPUT_STICK_SCRIPT` | `start:duration:x:y;...`, axes from -1 to 1 |
| `GALAXY_INPUT_SHAKE_SCRIPT` | `start:duration;...`, Wii Remote shakes |
| `GALAXY_INPUT_POINTER_X`, `GALAXY_INPUT_POINTER_Y` | Fixed pointer position, -1 to 1 |

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
