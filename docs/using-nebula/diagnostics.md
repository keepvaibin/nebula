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
| `constants-watch` | A test, on Direct3D 11: once a frame a small compute job reads a numbered piece of the constant ring and writes back what it saw; a wrong read prints one line starting `[constants] watch` with what was written and what was read, and the test runner fails the run. The gate turns it on for its Direct3D 11 runs (ledger P-332). |
| `device-faults` | For a lost graphics device: the Direct3D 12 breadcrumbs and page faults (DRED), or the Vulkan vendor's fault data in `logs/device_fault_<vendor>_<device>.bin` beside the settings. |
| `file-reads` | Each read and map of the game's files, and the sound clock, with the frame it happened in. |
| `frame-tails` | A histogram of frame intervals every 2 seconds. |
| `frame-times` | Each frame's time. |
| `gpu-passes` | GPU time for each pass, and shader invocation counts (`gpu-pass-ms` and `gpu-stats` in the present statistics). |
| `hdr-highlights` | With HDR output, screenshots and test captures show where the picture goes past the white level: the picture in grey, and each brighter pixel yellow up to 2 times the white level, orange up to 4, red up to 8, magenta above. |
| `heaps` | Everything `memory` records, plus the process heaps by block size. |
| `memory` | What the renderer holds every 600 frames: rings, texture tables and a `[vram] breakdown` line. |
| `pipelines` | The pipelines the run created, written at exit to `pipelines-PROCESS-ID.bin`. `tools/dev/site_pipelines.py` reads it. |
| `pointer-latency` | The time from the pointer selecting something to the picture that shows it. |
| `present-stats` | Presented frames, new frames, and GPU and CPU times, each second. |
| `frame-plans` | Every 60th frame list's shape (each pass's kind, target and what it reads, each run of like draws' pipeline state) and the plans the renderer made of it, in `frame-plans-<process>.txt`. No game content. A tour's file is a fixture of the walk plan test (`runtime/src/render/core/testdata/frame-plans`).
| `programs-drawn` | Each pair of shader programs a draw asks for, once. A stage compiles only those. |
| `profile` | The game thread and the render thread, each sampled each millisecond with its callers, until the run ends (10 minutes at most), in `game-profile-PROCESS-ID.txt` and `render-profile-PROCESS-ID.txt`. Each sample carries the timeline's clock. `tools/perf/game_profile.py` names the game module's functions from its link map; `--ranges` keeps the samples inside given stretches of the timeline. |
| `ring-stress` | A test: the upload rings are made again every 5 frames, so a test run proves that old rings outlive every frame that reads them. |
| `resolution-steps` | A test: dynamic resolution is on and walks every step from the whole render size to the smallest the upscaler allows and back, 120 pictures each, whatever the GPU's time. Each change is logged. |
| `rt-inputs` | The world's lights in each frame list, checked against their rules and counted every 300 lists (`[rt-inputs] lights` lines). |
| `scene-loads` | The phases, presses and placed objects of each scene change. |
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

## Pacing

Every ten seconds of play the log has a `[pace]` line, on every platform and renderer, from what the game knows of its steps and the pictures it shows:

| Field | What |
|---|---|
| `steps_per_second` | The game's speed: steps over the window's own time. |
| `pictures`, `displayed`, `generated` | Pictures presented, those the renderer reported reaching the display, and in-between ones. |
| `on_time`, `missed`, `doubled` | Pictures shown the number of refreshes after the one before that was asked for; refreshes a picture stayed on longer; pictures never seen. |
| `animation_within_1ms`, `animation_p99_us` | The game time each picture shows against the moment it reached the display, against the window's usual gap. |
| `delay_avg_us`, `delay_p99_us` | From the start of the step a picture shows to its moment on the display. |
| `render_*`, `generated_render_*` | The render thread's time on each picture. |
| `step_work_*` | Each step's own work, from its start until the game waits for the next. |

The display's moments come from the swap chain's frame statistics on Direct3D 12 and 11, from present timing (or display timing on Android) on Vulkan, and from each drawable's presented time on Metal. Where a renderer cannot tell, `displayed` stays below `pictures`. `tools/gate/pace.py run` runs the pacing tests (`tests/pace`) in real time and writes each measure against its target.

## Testing

Every check that starts the game is a harness test: a `test.json` that `tools/lumacore/luma_test.py` runs with the test extension (`--extension`), hidden and without sound. The steps give the input, menu keys, pictures, window sizes and device losses; the game's facts and log lines are checked against the test's `expect`. `tools/gate/gate.py` runs them all. None of this is needed to play.

| Where | What |
|---|---|
| `tests/engine` | The engine's own tests: the start, device loss, window shapes and live resizing, pictures on frames in a row, stage loads as Luigi, save files. |
| `tools/gate/gate_tour.py` | The galaxy tour (`tools/verify/tours/tour_enterable.txt`) as harness tests. |
| `tests/mods` | The mod engine's tests (`tools/lumacore/mod_tests.py`). |
| `tools/gate/pictures.py` | The approved pictures of each test and renderer, checked exactly, and approved after review. |

On Android the same tests run on a device with `luma_test.py run ... --device <adb serial>`, in a separate test app (`dev.nebula.galaxy.test`, from `platform/android/build_apk.ps1 -Debuggable -Harness ...`) that `tools/lumacore/luma_test_android.py prepare` installs with a game.pak and `remove` uninstalls. The installed game is never touched.

A test's `settings` names the game's settings the way `tools/verify/settings_args.py` lists them, for example `GALAXY_MAX_FPS=30`. Four test hooks go to the game's environment instead, because the host reads them when it starts:

| Hook | Effect |
|---|---|
| `GALAXY_TEST_VRAM_BUDGET_MB` | The graphics memory the renderer takes as its budget, in MB, whatever the card reports. |
| `GALAXY_TEST_DISC_DELAY_US` | Each read of the game's files waits this long first, in microseconds: a slow drive. |
| `GALAXY_TEST_MODS_SCREEN` | A hidden start still shows the mods' preparing screen, and starts again after it, as a player's start does. |
| `GALAXY_TEST_MODS_CHANGED` | The game takes its mods as changed and prepares them again, as the Mods page makes it. |

Any other name is an error. The run's sound goes to `audio.wav` in its folder (`GALAXY_AUDIO_WAV_DUMP`, without opening the sound device).

## Test tools

These scripts work on files a run or the importer left; each prints its arguments when started without them.

| Tool | Use |
|---|---|
| `tools/verify/compare_snapshots.py` | Compares two folders of pictures, picture by picture. |
| `tools/verify/audio_compare.py`, `audio_bands.py`, `wavdiff.py`, `wavdrift.py`, `wavlag.py`, `wavshift.py` | Compare two runs' sound. |
| `tools/verify/store_check.py`, `pakfiles.py`, `pak_compressed.py`, `audiocheck.py`, `moviecheck.py`, `gstcheck.py` | Check a game.pak against another or against its source. |
| `tools/verify/objdiff.py`, `pe_sections.py` | Compare two builds' objects or DLLs. |
| `tools/verify/step_hash.py` | Plays the tour on two builds with the step hash on and names the first step and actor where they differ (`replay`), or compares two runs' hashes (`compare`). A test turns the hash on with a step `"hash": "on"`: after every step the fact `hash.step` hashes every live actor's name, position, rotation, scale, velocity, gravity and state step. `"hash": "detail"` writes each actor's values at that step (`hash.actor`). The gate plays its tours with the hash on: Direct3D 12's 20 recorded stops must match `tests/replays`, and every other renderer's and the tablet's stops must match Direct3D 12's (`check`). A change meant to change what the game does is recorded again from a gate's Direct3D 12 tour (`record`). |
| `tools/perf/threadprof.py` | Samples one thread's busiest functions from outside the process, with its PDB file. |

A fatal error logs its stack, and a thread dump logs each thread's stack at its last wait, as module offsets. Use the game module's link map to name them.

## Further reading

* [Command-line arguments](command-line-arguments.md)
* [Error codes](error-codes.md)
