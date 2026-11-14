# The game's two modules

## More rules

### Two modules and the choice at start

Header: `runtime/include/galaxy/game_module.h` (host). Code: `native/CMakeLists.txt` (the two targets),
`runtime/src/game_module.cpp` (the choice), `native/host/host_main.cpp` (its one call), the packaging lists.

### 1. The two modules

One build makes both from the same source and flags:

| | `smg_game` (rows) | `smg_game_plain` |
|---|---|---|
| sources | instrument.py's copies (rows, hook tests, numbers and tables named, tables writable) | the source as written |
| tables | function, type, number rows; luma_types.json | none (the row sections are empty) |
| RTTI | data (-frtti) | none (-fno-rtti) |
| exports | every function (WINDOWS_EXPORT_ALL_SYMBOLS; default visibility elsewhere) | NEBULA_GAME_API only |

The kernel (Game/System/Luma*) is in both. In the plain module it finds no rows, so it never binds a mod's library,
and no mod's code is ever loaded with it. The file names are `smg_game.dll` / `smg_game_plain.dll`,
`libsmg_game.so` / `libsmg_game_plain.so` and `libsmg_game.dylib` / `libsmg_game_plain.dylib`, side by side.

### 2. The choice (host, every platform, before the module loads)

`galaxy::game_module::choose(rows_path, why)` returns the file to load:
- the rows module when any of: an enabled mod has code for this device that the player allowed (`code_token`
  set, `code_allowed`) and the last run did not crash with mods' code on (`.code_crashed` absent); `--luma-describe`
  (it reads the type rows). The test extension alone does not: a bare harness test (no game half, no mod) runs on
  the plain module, which is how the harness proves the choice;
- the plain module otherwise, when its file is beside the rows module;
- the rows module when the plain one is missing (a development build), said in the log.

`--module PATH` names the rows module; the plain one is its sibling (`<stem>_plain<ext>`). The log says once, before
the module loads: `game module: plain (no mod with code on)`, or `game module: rows (<reason>)` naming the first
mod with code or the option. A change made in the Mods screen applies at the next start, as every code change does.

### 3. Cost

No mod with code: the plain module, which is the game without rows: the same code as a build with
`NEBULA_LUMA_HOOKS=OFF`, so no hook test, no row, no RTTI data and no export is loaded. The early look at the mods
reads the manifests once more before the module loads (one scan; its time is logged in the boot marks).

### 4. Proof

- Build: both modules link on Windows; the plain module's size is back to l12's (no RTTI data)
  and it has no row section; Android x64 builds both.
- Tests 1701-1702: a bare test (no mods, no game half) logs `game module: plain`; a test with
  a code mod logs `game module: rows (mod <id> has code)` and the mod's hook runs.
- Perf: a no-mod tour on the plain module against the rows module, and against an OFF build; plain
  equals OFF within the noise.

### 5. Known limits

- A mod enabled in the Mods screen during a session takes effect at the next start (the screen already says so for
  code).
- The crash flag's case (a code mod on, `.code_crashed` there: plain) has no harness test: the harness cannot write
  the flag. It is the same condition svc_mods_start already acts on.
- iOS has no packaging script yet; the framework layout (`smg_game.framework/smg_game`) has no plain sibling, so iOS
  loads the rows module until one exists.
- The old generated tables (Game/System/generated: classes, fields, names, toolkit) are in both modules until 1.10
  removes them; the plain module's size is measured with them.
- ELF: the row sections' bounds are weak (LumaHook, LumaNumbers, as LumaTypes already were), so the plain module links
  with none.
