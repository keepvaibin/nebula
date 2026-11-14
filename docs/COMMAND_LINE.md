# Command line

`NebulaNative.exe` (Direct3D 12), `NebulaNativeVk.exe` (Vulkan) and the SDL hosts take the same arguments, each a name
and a value. The launcher passes `--pak` and `--saves`; the Android app also passes `--data` and `--module`.

| Argument | Value |
|---|---|
| `--pak <file>` | The game content (`game.pak`). Required. |
| `--saves <folder>` | The saves folder. Required, except with `--benchmark 1`. |
| `--module <file>` | The game module; default `smg_game.dll` (`libsmg_game.so`, `libsmg_game.dylib`) beside the executable. |
| `--data <folder>` | Nebula's data folder (shader caches, crash dumps, downloaded libraries under `vendor`, settings unless `--settings` names others); default `%LOCALAPPDATA%\Nebula`, else `$XDG_DATA_HOME/Nebula`, else `~/.local/share/Nebula`. |
| `--settings <folder>` | The folder of `config.ini` and `runtime_settings.ini` (and `Movies`, `TextureDumps` and `mods` beside them); default the data folder. |
| `--crash-dumps <folder>` | Crash dumps and reports; default `crashes` in the data folder. |
| `--gpu-validation 1\|2` | The graphics API's validation layer: 1 the Direct3D 12 debug layer or the Khronos validation layer (when installed), 2 also Direct3D 12's GPU-based validation; after a lost Direct3D 12 device, the objects still alive (`[D3D12-LIVE-OBJECT]`). Slow. |
| `--console-log 1` | The log stays on the console instead of `logs/latest.log` in the settings folder (a start with its output in a pipe or a file keeps it there anyway). |
| `--diagnostics <what>[,<what>]` | What this run records, into the log and `diagnostics` in the data folder (DIAGNOSTICS.md). `1` is the launcher's recording: the frame timeline, the frame and pointer monitors, present statistics and GPU time by pass. |
| `--luma-describe <file> [--module <file>]` | First argument only, for the build, not for players: loads the game module (default as `--module`), writes its type rows as JSON ([modding/language/types.md](modding/language/types.md)) to the file and exits: 0, or 2 when the file cannot be written. tools/lumacore/compare_types.py checks them against `luma_types.json`. Needs no `--pak`. |
| `--luma-services <file> [--module <file>]` | First argument only, for the build, not for players: loads the game module (default as `--module`), writes the services a mod can import (name, version, the permission each needs) as JSON to the file and exits: 0, or 2 when the file cannot be written. The build writes it as `luma_services.json` beside `luma_types.json`, where the validator and LumaLink read it; tools/lumacore/check_services.py checks it against the SDK's headers. Needs no `--pak`. |
| `--timeline <file>` | Where the recorded timeline goes; default `diagnostics/timeline-<process id>.bin` in the data folder. |
| `--seconds <n>` | End the game after n seconds. |
| `--import <file or folder>` | Import this mod as a drop on the window does, then show the result (the validator's findings; nothing is added when it finds an error). Given again for each. "Open with" and the `.luma` association start the game with it; a game already running for the same settings folder takes the files and comes forward. |
| `--safe-start 1` | Every mod is off for this start; your choices on the Mods page stay as they are for the next. Holding Shift as Nebula starts does the same. After a crash with mods on, the next start does this by itself (or, when mods' code was on, leaves only the code off). A safe start loads the game without the parts mods' code needs (`game module: plain (safe start: ...)` in the log). |
| `--home 0\|1` | 1: the home screen (Play, Settings, Benchmark, Quit, an update's notice) before the title, in the game's menus; 0: straight into the game. Default 1 on macOS and Linux, 0 on Windows and Android (their launchers are the home screen) and in a hidden or `--seconds` run. |
| `--benchmark 1` | Runs the benchmark and exits: real stages (the list in `benchmark.tsv` beside `game.pak`, else the built-in one), then a probe of internal heights and optional passes, on a blank save kept in memory. The settings are read, never written; no save, Mii, achievement or statistic is read or written, and no mod loads. The result is `diagnostics/benchmark-<utc>.json` in the data folder: the measurements, the recommended settings and a summary (exit 0 and `"status":"complete"` when it finished). |
| `--benchmark-out <file>` | With `--benchmark 1`: the result goes to this file instead. |
| `--benchmark-return 1` | Set by the home screen's Benchmark (with `--benchmark 1`): when the run ends, the game starts again on the home screen with `--benchmark-result`. |
| `--benchmark-result <file>` | Set by that start: the result's recommended settings open in the settings menu as changes to apply. |
| `--gpu-start-retry <n>` | Set by the game itself when it starts again because no graphics memory was free: the tries so far (5 at most). |
| `--renderer d3d12\|d3d11` | Windows, `NebulaNative`: `d3d11` runs `NebulaNativeD11` (the build of the same CPU level) in its place without starting Direct3D 12; `d3d12` never falls back. Not given: Direct3D 11 runs when no adapter can run Direct3D 12. One process lifetime and exit code either way. |

The GPU is a setting: `gpu=` in `config.ini`, chosen in Nebula Settings (empty: the high-performance GPU).

## Mod tools (the launcher's Mods page)

| Command | |
|---|---|
| `--import-mod <folder or archive>...` | Import mods into the settings folder's `mods`, each package through the validator first (nothing is added when it finds an error); prints the result as JSON, each with `checked`, `refused` and `findings` (`code`, `error`, `file`, `text`). |
| `--remove-mod <id>` | Remove an imported mod. |
| `--list-mods` | The imported mods as JSON. |

`--crash-report <file>` shows the report of an instance that stopped; the game starts it itself.

## Diagnostics reports (docs/DIAGNOSTICS_UPLOAD.md)

| Command | |
|---|---|
| `--send-report <benchmark.json> [--with-log 1]` | Seals the benchmark result (and with `--with-log 1` the last log's tail, paths and names stripped) to the developer's key and sends it once. Exit 0 sent, 2 uploads off in this build, 3 the file is refused, 4 no connection, 5 the server refused, 8 sent before. |
| `--send-report <file> [--with-log 1] --preview 1` | Prints exactly the report that would be sealed. |
| `--send-report <file> --save-sealed <out> [--public-key <hex>]` | Writes the sealed report to `<out>` instead of sending it. |
| `--report-enabled 1` | Exit 0 when this build can send reports, 2 when not (the apps hide the choice). |
| `--open-report <file> --key <private key file>` | The developer's tool: prints the opened report (exit 7: not a report, another key or changed). |
| `--report-keygen <folder>` | The developer's key pair: writes the private key file, prints the public key for `diag_endpoint.h`. |
