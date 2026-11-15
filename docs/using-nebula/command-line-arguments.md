---
title: Command-line arguments
shortTitle: Command-line arguments
intro: Start Nebula from a terminal or a shortcut with arguments that choose its files, its renderer and what it records.
contentType: reference
---

## About command-line arguments

Every Nebula executable takes the same arguments: `Nebula.exe` (Direct3D 12), `NebulaD11.exe` (Direct3D 11), `NebulaVk.exe` (Vulkan) and the SDL builds for Linux and macOS. Each argument is a name followed by a value.

The Start menu shortcut on Windows and `nebula-launch` on Linux pass `--pak` and `--saves` for you. The Android app also passes `--data` and `--module`.

## Files and folders

| Argument | Value | Default |
|---|---|---|
| `--pak FILE` | The game content, `game.pak`. Required. | None |
| `--saves FOLDER` | The saves folder. Required, except with `--benchmark 1`. | None |
| `--module FILE` | The game module. | `smg_game.dll` (`libsmg_game.so` on Linux and Android, `libsmg_game.dylib` on macOS) beside the executable |
| `--data FOLDER` | Nebula's data folder: shader caches, crash dumps, downloaded libraries in `vendor`, and the settings unless `--settings` names another folder. | `%LOCALAPPDATA%\Nebula` on Windows; `$XDG_DATA_HOME/Nebula`, else `~/.local/share/Nebula`, elsewhere |
| `--settings FOLDER` | The folder that holds `config.ini` and `runtime_settings.ini`, with `Movies`, `TextureDumps` and `mods` beside them. | The data folder |
| `--crash-dumps FOLDER` | Crash dumps and crash reports. | `crashes` in the data folder |
| `--shipped-mods FOLDER` | The `.luma` files that come with the game. Nebula installs each one at start. The Android app passes the folder it unpacks them to. | `luma/mods` in the program folder (in the Mac app, `Contents/Resources/luma/mods`) |

## Starting and renderer

| Argument | Value |
|---|---|
| `--renderer d3d12`, `--renderer vulkan` or `--renderer d3d11` | Windows, `Nebula.exe` only. Overrides the renderer chosen in **Settings** for this start. `vulkan` runs `NebulaVk.exe` and `d3d11` runs `NebulaD11.exe` (the build for the same CPU level) in its place, without starting Direct3D 12. A renderer given here never falls back to another, and a restart from the game keeps it. While it is in use, **Renderer** in **Settings** shows it and cannot be changed. |
| `--home 1` or `--home 0` | `1` shows the home screen (**Play**, **Settings**, **Benchmark**, **Quit** and an update notice) before the title screen. `0`, the default, goes straight into the game. |
| `--safe-start 1` | Turns every mod off for this start only. Your choices on the **Mods** page stay as they are for the next start. Holding <kbd>Shift</kbd> while Nebula starts does the same. |
| `--seconds N` | Ends the game after N seconds. |
| `--hidden 1` | Linux and macOS. Starts without a window and never reads your keyboard, mouse or controllers. Test tools use it. On Windows, a start with a hidden window does the same. |

To choose the graphics card, use **Settings** in Nebula, or set `gpu=` in `config.ini`. When `gpu=` is empty, Nebula uses the high-performance graphics card.

To choose the renderer on Windows, use **Renderer** on the **System** page of **Settings**, or set `renderer=` in `config.ini` to `d3d12`, `vulkan` or `d3d11`. When `renderer=` is `auto` or empty, Nebula uses the newest renderer the graphics card runs: Direct3D 12, then Vulkan, then Direct3D 11. Choosing another graphics card sets the renderer to the newest one that card runs. When no graphics card runs the renderer a start uses, Nebula starts again with the next renderer in that order, and a renderer chosen in **Settings** goes back to `auto`.

### About safe starts

After a crash with mods turned on, the next start is a safe start. If only mod code was on, that start turns off only the code. A safe start loads the game without the parts that mod code needs, and the log shows `game module: plain (safe start: REASON)`.

## Mods

| Argument | Value |
|---|---|
| `--import FILE-OR-FOLDER` | Imports a mod the same way as dropping it on the window, then shows the result: the validator's findings. Nothing is added when the validator finds an error. Repeat the argument for each mod. **Open with** and the `.luma` file type use this argument. If Nebula is already running with the same settings folder, the running game takes the files and comes to the front. |
| `--import-mod FOLDER-OR-ARCHIVE` | Imports one or more mods into the `mods` folder of the settings folder. The validator checks each package first, and nothing is added when it finds an error. Prints the result as JSON: for each mod, `checked`, `refused` and `findings` (each with `code`, `error`, `file` and `text`). |
| `--remove-mod MOD-ID` | Removes an imported mod. Replace MOD-ID with the mod's `id` from its `luma.json`. |
| `--list-mods` | Prints the imported mods as JSON. |

## Diagnostics

| Argument | Value |
|---|---|
| `--diagnostics WHAT` | What this run records, in the log and in the `diagnostics` folder of the data folder. Separate several values with commas. `1` records what **Record** on the **SYSTEM** page of the settings records: the frame timeline, the frame and pointer monitors, present statistics, and GPU time for each pass. For more information, see [Diagnostics](diagnostics.md). |
| `--timeline FILE` | Where the recorded timeline goes. The default is `diagnostics/timeline-PROCESS-ID.bin` in the data folder. |
| `--console-log 1` | Writes the log to the terminal Nebula was started from, or to a console window of its own, instead of `logs/latest.log` in the settings folder. When the output already goes to a pipe or a file, the log stays there without this argument. |
| `--gpu-validation 1` or `--gpu-validation 2` | Turns on the graphics API's validation layer. `1` is the Direct3D 12 debug layer, or the Khronos validation layer when it is installed. `2` also turns on Direct3D 12 GPU-based validation. After a lost Direct3D 12 device, the log lists the objects that were still alive (`[D3D12-LIVE-OBJECT]`). The game runs much slower with validation on. |

## Benchmark

| Argument | Value |
|---|---|
| `--benchmark 1` | Runs the benchmark and exits. The benchmark plays real stages (the list in `benchmark.tsv` beside `game.pak`, or the built-in list), then measures each internal resolution and optional pass, on a blank save kept in memory. It reads your settings but never writes them. It reads and writes no save, Mii, achievement or statistic, and loads no mod. |
| `--benchmark-out FILE` | With `--benchmark 1`, writes the result to FILE. The default is `diagnostics/benchmarks/benchmark-UTC-TIME.json` in the data folder, with the frame times in a `.frames` file beside it. Nebula keeps the newest 5 results there. |

The result holds the measurements, the recommended settings and a summary. A finished run exits with code `0` and its result has `"status":"complete"`.

In the game, the benchmark is in **Settings** > **System** > **Benchmark**:

- **Run benchmark** takes the game back to the title screen and runs the benchmark there, on a blank save kept in memory. Anything not saved in the file you are playing is lost. While it runs, a panel at the top left of the screen shows the stage, the step and the frame rate. To stop the run, press Esc, or B on a controller. On Android, press Back. When the run ends, the game goes back to the title screen with your saves and the results open.
- **Benchmark** on the home screen (`--home 1`) runs it the same way.
- **Last result** opens the results: a summary with a frame rate graph, each stage, where the time goes and the hardware, and the earlier runs.
- **Apply recommended** adds the recommended settings as changes to apply.
- **Send to developer** sends the last result once, when the build can send reports.

## Diagnostics reports

| Argument | Value |
|---|---|
| `--report-enabled 1` | Exits with code `0` when this build can send reports, and `2` when it cannot. The apps hide the option when it cannot. |
| `--send-report FILE` | Seals a benchmark result to the developer's key and sends it once. Add `--with-log 1` to include the end of the last log, with paths and names removed. |
| `--send-report FILE --preview 1` | Prints the exact report that would be sealed, and sends nothing. |
| `--send-report FILE --save-sealed OUT` | Writes the sealed report to OUT instead of sending it. Add `--public-key HEX` to seal it to another key. |

`--send-report` exits with one of these codes:

| Code | Meaning |
|---|---|
| `0` | Sent. |
| `2` | This build cannot send reports. |
| `3` | The file was refused. |
| `4` | No internet connection. |
| `5` | The server refused the report. |
| `8` | This report was already sent. |

## Arguments Nebula sets for itself

Nebula passes these arguments when it starts itself again. Do not pass them yourself.

| Argument | Value |
|---|---|
| `--gpu-start-retry N` | The number of tries so far when the game starts again because no graphics memory was free. Nebula tries at most 5 times. |
| `--started-again N` | The number of times the game has started itself again. |
| `--start-again-of ID-TIME` | Windows only. The process that the new start takes over from. |
| `--imports-taken 1` | The files given with `--import` were imported before the game started itself again. They are not imported again. |
| `--crash-report FILE` | Shows the report of an instance that stopped. |
| `--check-renderers` | Windows only. Checks which renderers each graphics card runs, for **Renderer** in the settings, prints the result and ends. |

## Arguments for building Nebula

The build uses these arguments. Each must be the first argument, and neither needs `--pak`.

| Argument | Value |
|---|---|
| `--luma-describe FILE` | Loads the game module, writes its type rows as JSON to FILE and exits. Add `--module FILE` to load another module. Exits with code `0`, or `2` when FILE cannot be written. `tools/lumacore/compare_types.py` checks the rows against `luma_types.json`. |
| `--luma-services FILE` | Loads the game module, writes the services a mod can import (each with its name, version and the permission it needs) as JSON to FILE and exits. Add `--module FILE` to load another module. Exits with code `0`, or `2` when FILE cannot be written. The build writes this file as `luma_services.json` beside `luma_types.json`, where the validator and LumaLink read it. `tools/lumacore/check_services.py` checks it against the headers. |
| `--report-keygen FOLDER` | Writes a new private key file for diagnostics reports to FOLDER and prints the public key for `diag_endpoint.h`. |
| `--open-report FILE --key KEY-FILE` | Prints an opened report. Exits with code `7` when FILE is not a report, was sealed to another key, or was changed. |

## Further reading

* [Diagnostics](diagnostics.md)
* [Error codes](error-codes.md)
