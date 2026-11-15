---
title: Diagnostics
shortTitle: Diagnostics
intro: Record what a run of Nebula does, from frame times to file reads, to find the cause of a slowdown or a fault.
contentType: reference
---

## About diagnostics

You never need diagnostics to play. To record a run, start Nebula with `--diagnostics` and one or more names from the tables below, separated by commas. For example, `--diagnostics frames,gpu`.

Files go to the `diagnostics` folder in Nebula's data folder, named with the process ID. Lines go to the log. For more information about the data folder, see [Command-line arguments](command-line-arguments.md).

## Recordings

| Name | Records |
|---|---|
| `device-faults` | For a lost graphics device: the Direct3D 12 breadcrumbs and page faults (DRED), or the Vulkan vendor's fault data in `logs/device_fault_VENDOR_DEVICE.bin` beside the settings. |
| `file-reads` | Each read and map of the game's files, and the sound clock, with the frame it happened in. |
| `frame-tails` | A histogram of frame intervals every 2 seconds. |
| `frame-times` | Each frame's time, and where a scripted press begins (`[latency]` lines). `tools/verify/gaps.py` reads them. |
| `gpu-passes` | GPU time for each pass, and shader invocation counts (`gpu-pass-ms` and `gpu-stats` in the present statistics). |
| `hdr-highlights` | With HDR output, screenshots and test captures show where the picture goes past the white level: the picture in grey, and each brighter pixel yellow up to 2 times the white level, orange up to 4, red up to 8, magenta above. |
| `heaps` | Everything `memory` records, plus the process heaps by block size. |
| `memory` | What the renderer holds every 600 frames: rings, texture tables and a `[vram] breakdown` line. |
| `pipelines` | The pipelines the run created, written at exit to `pipelines-PROCESS-ID.bin`. `tools/dev/site_pipelines.py` reads it. |
| `pointer-latency` | The time from the pointer selecting something to the picture that shows it. |
| `present-stats` | Presented frames, new frames, and GPU and CPU times, each second. |
| `programs-drawn` | Each pair of shader programs a draw asks for, once. A stage compiles only those. |
| `profile` | The game thread and the render thread, each sampled each millisecond with its callers, until the run ends (10 minutes at most), in `game-profile-PROCESS-ID.txt` and `render-profile-PROCESS-ID.txt`. Each sample carries the timeline's clock. `tools/perf/game_profile.py` names the game module's functions from its link map; `--ranges` keeps the samples inside given stretches of the timeline. |
| `ring-stress` | A test: the upload rings are made again every 5 frames, so a test run proves that old rings outlive every frame that reads them. |
| `resolution-steps` | A test: dynamic resolution is on and walks every step from the whole render size to the smallest the upscaler allows and back, 120 pictures each, whatever the GPU's time. Each change is logged. |
| `rt-inputs` | The world's lights in each frame list, checked against their rules and counted every 300 lists (`[rt-inputs] lights` lines). |
| `scene-loads` | The phases, presses and placed objects of each scene change. `tools/verify/loads.py` reads them. |
| `telemetry` | Frame counts in shared memory each second, for `tools/telemetry`. |
| `thread-cpu` | Each thread's CPU time and context switches, each second. |
| `timeline` | The frame pipeline's events, written at exit to `timeline-PROCESS-ID.bin`, or to the file `--timeline` names. `tools/perf/timeline_digest.py` reads it. |

## Sets

A set turns on several recordings at once.

| Set | Recordings |
|---|---|
| `0` | None |
| `1` | `timeline`, `frame-tails`, `pointer-latency`, `present-stats` and `gpu-passes`. This is what **Record** on the **SYSTEM** page of the settings records. |
| `frames` | `timeline`, `frame-tails`, `pointer-latency`, `present-stats` and `frame-times` |
| `gpu` | `gpu-passes` |
| `loads` | `scene-loads` and `file-reads` |
| `threads` | `thread-cpu` and `profile` |

To dump the game's textures, turn on **Dump textures** in the **Graphics** settings. Nebula writes them to `TextureDumps` beside `config.ini`.

## Start-up lines in the log

Every start logs its steps as `boot:` lines, in milliseconds since the process was created: the game's data, the module, the renderer, the game, its first frame and the title screen. The renderer logs its own steps as `[boot] renderer` lines, in milliseconds for each step.

## Environment variables for testing

Test tools set these variables to control a run. None of them is needed to play.

### Audio

| Variable | Effect |
|---|---|
| `GALAXY_AUDIO_AMBIENCE_GAIN`, `GALAXY_AUDIO_EFFECTS_GAIN`, `GALAXY_AUDIO_MUSIC_GAIN`, `GALAXY_AUDIO_SFX_GAIN`, `GALAXY_AUDIO_VOICE_GAIN` | The gain for one sound category, from `0.0` to `4.0`. The default is `1.0`. |
| `GALAXY_AUDIO_WAV_DUMP=PATH` | Writes the mixed audio to a WAV file. |
| `GALAXY_AUDIO_WAV_DUMP_ONLY=1` | With `GALAXY_AUDIO_WAV_DUMP`, writes the WAV file without opening the sound device. |

### Pictures

| Variable | Effect |
|---|---|
| `GALAXY_PRESENT_SNAPSHOT_DIR` | The folder for saved frames, as PPM images. |
| `GALAXY_PRESENT_SNAPSHOT_EVERY` | Saves every Nth presented frame. |
| `GALAXY_PRESENT_SNAPSHOT_FIRST`, `GALAXY_PRESENT_SNAPSHOT_LAST` | The first and last frame to save. |
| `GALAXY_PRESENT_SNAPSHOT_GAME_FRAMES=<first>-<last>` | Saves every picture shown of those game frames, interpolated ones included. |

### Scripted input

Times are in video frames (60 each second).

| Variable | Format |
|---|---|
| `GALAXY_INPUT_AUTOPRESS_SCRIPT` | `START:DURATION:BUTTONS;...`, for example `5400:120:A+B`. |
| `GALAXY_INPUT_POINTER_X`, `GALAXY_INPUT_POINTER_Y` | A fixed pointer position, from `-1` to `1`. |
| `GALAXY_INPUT_SHAKE_SCRIPT` | `START:DURATION;...`, each a Wii Remote shake. |
| `GALAXY_INPUT_STICK_SCRIPT` | `START:DURATION:X:Y;...`, each axis from `-1` to `1`. |

`GALAXY_TEST_SCRIPT_CLOCK` chooses the clock for the `GALAXY_INPUT_*_SCRIPT` variables and the waypoints:

| Value | Clock |
|---|---|
| Not set | Game frames since the start. |
| `frames` | Scene frames since the start. Builds with different renderer speeds count the same. |
| `stop` | Scene frames since the current tour stop began. No scripted input runs before the first stop. |

`GALAXY_TEST_EVENTS`, `GALAXY_TEST_TELEPORT`, `GALAXY_TEST_WALK` and snapshots taken by tour stop always count scene frames since the current tour stop, or since the start without a tour. Use `stop` to put a script and these events on one clock.

## Test tools

The scripts in `tools/verify` and `tools/perf` each print their arguments when started without them.

| Tool | Use |
|---|---|
| `tools/verify/all_stops.sh OLD NEW TAG` | Runs two builds through every tour stop and compares the pictures, with `compare_stops.py` and `sheet.py`. |
| `tools/verify/boots.sh BIN TAG COUNT PARALLEL` | Starts the game many times at once. A start that never reaches the title screen keeps its log. |
| `tools/verify/extended.sh BIN TAG` | Plays the ending, Luigi and a slow drive. |
| `tools/verify/gpulost.sh BIN TAG TIMES SECONDS` | Loses the graphics device on purpose. |
| `tools/verify/luigi2.sh BIN TAG` | Changes the player to Luigi and back. |
| `tools/verify/monkey.py BIN TAG SHARDS FRAMES SEED` | Sends random input on every stop, to find crashes and hangs. |
| `tools/verify/sizes.sh BIN TAG` | Runs eight window sizes and shapes. |
| `tools/verify/validate.sh BIN TAG NAME=VALUE` | Runs the tour with the graphics API's validation on. |
| `tools/perf/threadprof.py` | Samples one thread's busiest functions from outside the process, with its PDB file. |

A fatal error logs its stack, and a thread dump logs each thread's stack at its last wait, as module offsets. Use the game module's link map to name them.

## Further reading

* [Command-line arguments](command-line-arguments.md)
* [Error codes](error-codes.md)
