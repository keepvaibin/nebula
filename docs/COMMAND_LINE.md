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
| `--dump-tunables <file>` | Every tuning number a mod can set (luma.json `tunables`), one `<key>\t<kind>\t<game's number>` line each, written there as the game module starts. docs/modding/TUNABLES.md is the same list from the source. |
| `--timeline <file>` | Where the recorded timeline goes; default `diagnostics/timeline-<process id>.bin` in the data folder. |
| `--seconds <n>` | End the game after n seconds. |
| `--home 0\|1` | 1: the home screen (Play, Settings, Benchmark, Quit, an update's notice) before the title, in the game's menus; 0: straight into the game. Default 1 on macOS and Linux, 0 on Windows and Android (their launchers are the home screen) and in a hidden or `--seconds` run. |
| `--benchmark 1` | Runs the benchmark and exits: real stages (the list in `benchmark.tsv` beside `game.pak`, else the built-in one), then a probe of internal heights and optional passes, on a blank save kept in memory. The settings are read, never written; no save, Mii, achievement or statistic is read or written, and no mod loads. The result is `diagnostics/benchmark-<utc>.json` in the data folder: the measurements, the recommended settings and a summary (exit 0 and `"status":"complete"` when it finished). |
| `--benchmark-out <file>` | With `--benchmark 1`: the result goes to this file instead. |
| `--benchmark-return 1` | Set by the home screen's Benchmark (with `--benchmark 1`): when the run ends, the game starts again on the home screen with `--benchmark-result`. |
| `--benchmark-result <file>` | Set by that start: the result's recommended settings open in the settings menu as changes to apply. |
| `--gpu-start-retry <n>` | Set by the game itself when it starts again because no graphics memory was free: the tries so far (5 at most). |

The GPU is a setting: `gpu=` in `config.ini`, chosen in Nebula Settings (empty: the high-performance GPU).

## Mod tools (the launcher's Mods page)

| Command | |
|---|---|
| `--import-mod <folder or archive>...` | Import mods into the settings folder's `mods`; prints the result as JSON. |
| `--remove-mod <id>` | Remove an imported mod. |
| `--list-mods` | The imported mods as JSON. |

`--crash-report <file>` shows the report of an instance that stopped; the game starts it itself.
