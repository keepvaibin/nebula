# Command line

`NebulaNative.exe` (Direct3D 12), `NebulaNativeVk.exe` (Vulkan) and the SDL hosts take the same arguments, each a name
and a value. The launcher passes `--pak` and `--saves`; the Android app also passes `--data` and `--module`.

| Argument | Value |
|---|---|
| `--pak <file>` | The game content (`game.pak`). Required. |
| `--saves <folder>` | The saves folder. Required. |
| `--module <file>` | The game module; default `smg_game.dll` (`libsmg_game.so`, `libsmg_game.dylib`) beside the executable. |
| `--data <folder>` | Nebula's data folder (shader caches, crash dumps, downloaded libraries under `vendor`, settings unless `--settings` names others); default `%LOCALAPPDATA%\Nebula`, else `$XDG_DATA_HOME/Nebula`, else `~/.local/share/Nebula`. |
| `--settings <folder>` | The folder of `config.ini` and `runtime_settings.ini` (and `Movies`, `TextureDumps` and `mods` beside them); default the data folder. |
| `--crash-dumps <folder>` | Crash dumps and reports; default `crashes` in the data folder. |
| `--gpu-validation 1\|2` | The graphics API's validation layer: 1 the Direct3D 12 debug layer or the Khronos validation layer (when installed), 2 also Direct3D 12's GPU-based validation; after a lost Direct3D 12 device, the objects still alive (`[D3D12-LIVE-OBJECT]`). Slow. |
| `--console-log 1` | The log stays on the console instead of `logs/latest.log` in the settings folder (a start with its output in a pipe or a file keeps it there anyway). |
| `--diagnostics <what>[,<what>]` | What this run records, into the log and `diagnostics` in the data folder (DIAGNOSTICS.md). `1` is the launcher's recording: the frame timeline, the frame and pointer monitors, present statistics and GPU time by pass. |
| `--luma-describe <file> [--module <file>]` | First argument only, for the build, not for players: loads the game module (default as `--module`), writes its type rows as JSON (contract types) to the file and exits: 0, or 2 when the file cannot be written. tools/lumacore/compare_types.py checks them against `luma_types.json`. Needs no `--pak`. |
| `--timeline <file>` | Where the recorded timeline goes; default `diagnostics/timeline-<process id>.bin` in the data folder. |
| `--seconds <n>` | End the game after n seconds. |
| `--gpu-start-retry <n>` | Set by the game itself when it starts again because no graphics memory was free: the tries so far (5 at most). |

The GPU is a setting: `gpu=` in `config.ini`, chosen in Nebula Settings (empty: the high-performance GPU).

## Mod tools (the launcher's Mods page)

| Command | |
|---|---|
| `--import-mod <folder or archive>...` | Import mods into the settings folder's `mods`; prints the result as JSON. |
| `--remove-mod <id>` | Remove an imported mod. |
| `--list-mods` | The imported mods as JSON. |

`--crash-report <file>` shows the report of an instance that stopped; the game starts it itself.
