# Reloading a mod while the game runs

## More rules

### 1. What exists and what does not

- The game half exists: `luma_unload_mod` (LumaLoader.cpp `unloadMod`): the mod's hooks go, its `stop` runs, the
  mods it imported from are told (`dependent_gone`), then after the grace `finish` (its C++ destructors), `freeAll`,
  its library closed. Taking out a mod another loaded mod uses is a panic naming both.
- Missing: nothing in the host calls it; nothing says how much the mod still held; the host loads a mod's library
  from its installed path, which Windows then locks, so a rebuilt library cannot be written over it while the game
  runs.

### 2. Taking a mod out (the host, `runtime/src/mod_plugins.cpp`)

- `galaxy::mods::take_out(id)` queues a request; the game thread takes requests at the start of a frame (beside
  `luma::frame()`), never inside a hook, and before the registry's frameStart deliveries: no service event
  is delivered to a mod in the frame it is taken out (its queued events are dropped at its modGone). Any thread may
  queue; a second request for a queued id is one request.
- Order: the mods that use it (its dependents, through `uses`, transitively) are taken out first, in the reverse of
  their start order, then the mod. Each is `luma_unload_mod`. The host forgets each one's `PluginFile` state as
  started; the Mods page reads it as on and not running with the reason `Mods.TakenOut` (new row,
  14 languages: "Taken out while the game runs.").
- A mod that is not running (refused, never started, already taken out) is not an error: the request says so in the
  log once and does nothing.
- No request, no cost: the queue is empty and the frame check is one load.

### 3. What the game frees, counted

- A service frees what it allocated for the mod in its mod_end, before `freeAll`; a block `freeAll` finds is
  the mod's own.
- `freeAll` counts the blocks and bytes it walks and compares them with the mod's `blockCount` and `bytes`; a
  difference is `NEBULA_PANIC("LumaCore: %s's memory count is wrong: %u blocks and %llu bytes kept, %u and %llu
  found")` (a broken block set would free the wrong memory).
- A mod taken out (or ended after a refused start) that still held blocks gets one log line `[mod <id>] held <n>
  blocks (<bytes> bytes) when it ended; the game freed them`, and the fact `takeout.<id>` {"blocks", "bytes",
  "stopped", "finished"} (`stopped`: its `stop` ran; `finished`: its `finish` ran) once the library is closed. A mod
  holding none writes the fact with 0, 0.
- The destructor order is C++'s, run by the mod's own `finish` (LUMA_MOD): reverse of construction. Nothing for the
  game to order; the test proves it (section 6).

### 4. Reloading a mod's code

- Loaded from a copy (dev mode only). Started with `--mod-dev <id>` (repeatable; `lumalink run` passes it), the
  host loads that mod from a copy of its whole installed folder in `<appdata>/luma-dev/<id>/<k>/` (`k` counting up
  from 1 this session), so its installed files can be rebuilt in place. Copies left by an earlier session (a crash
  skips the game's end) are deleted at a dev session's start, before the first copy. Without the flag the
  host loads as today (no copy, no cost).
- The request. `lumalink run` rebuilds the mod into its installed folder, then writes
  `<settings>/mods/<id>/reload.request` (written whole, then renamed into place) holding `{"n": <its number>}`. In
  dev mode only, the host looks for that file once every 30 frames on its file thread (one stat of one path per dev
  mod).
- The folder is frozen while a request stands. Finding the request, the host copies the installed folder
  whole to the next `<k>`, then deletes the request, then queues the reload. Steps 2 to 4 read and load that copy
  only. LumaLink installs a build only while no request is present (a change meanwhile is built and held). So a
  present request means the installed folder is not being written, one rule each way. A request whose file is not
  `{"n": <integer>}` is taken as a reload with `n` null, and the log says so.
- The result. Each reload ends by writing `<settings>/mods/<id>/reload.result` (written whole, then
  renamed): `{"n", "ok", "code", "names", "values", "took_out", "started", "frames"}`: `n` the request's; `code`,
  `names` and `values` the refusal's (an L-code or a row, and its values; null, [] and [] when ok). LumaLink shows it
  in its window with the validator's rows in its language and deletes the file. A result not yet taken is written
  over by the next.
- The reload, at the start of a frame on the game thread:
  1. Take out the mod and its dependents as section 2 (each `stop`, the grace, `finish`, `freeAll`, the library
     closed). The grace runs across frames: the game keeps running with the mod not live; step 2 comes at the
     start of the frame after the grace ends, never a stall of the game thread.
  2. Read the copy (luma.json, its code), and check it as a start's check does (start-check section 1,
     the validator with the kept report): a check that fails leaves the mod not running with that reason on its page
     and in the mod log, the old code already gone; the dependents stay out, each with `Start.NeedsNotRunning`.
  3. Consent: the code's digest changed. In dev mode the person running `lumalink run` built it: the code answer
     carries over for that mod; a permission it did not declare before is asked as consent 2.3 says (the menu opens
     on the MODS tab). Step 4 waits for the answer, so the registry's copy of the grants at modBegin holds a
     newly allowed permission; a declined new required permission leaves the mod off with `Consent.OffDeclined` and
     its dependents out with `Start.NeedsNotRunning`.
  4. Load the copy (`luma_load_mod` at its old list order), then its dependents in start order. The old copies are
     deleted at the game's end.
- A reload is a new life for the services. The old load's service state ends at its mod_end; the new
  load begins from nothing (time: real rate, no hold, no timers or handlers; system: no requests; the registry's
  record with the grants as step 3 left them). `keep`/`kept` is the only thing carried.
- State across a reload (a kernel primitive, not a concept). Two LumaCore calls, appended at the end of the
  table (its `size` grows; LUMA_API stays 1):
  `int32_t (*keep)(const LumaMod* mod, const void* bytes, uint32_t size)` (in `stop` only: the host holds a copy, at
  most 1 MiB, the last call wins; 0 kept, -1 not in stop or too big, -3 not on the game thread) and
  `const void* (*kept)(const LumaMod* mod, uint32_t* size)` (in `start` only: the bytes the mod's previous load kept
  this session, or null; valid until `start` returns). Kept bytes go when they are read or when the mod is removed
  from the list. With no reload `kept` gives null.
- Facts: `reload.<id>` {"ok", "took_out": [ids], "started": [ids], "frames", "why"} once each reload ends, as
  reload.result is written (`why` the failing check's row or L-code, "" when ok). The log says each step in one line.

### 5. LumaLink

`lumalink run [--renderer r]`: builds the mod, installs it into the game's mods folder for the person, starts the
game with `--mod-dev <id>`, then on each change of the mod's source folder rebuilds and writes `reload.request`; a
build that fails writes nothing and says why in LumaLink's window. Content changes (layers) are not reloaded in a
running game by this task: LumaLink says "content changes show at the next start" until a content reload exists.

### 6. Tests

`{"take_out": "<mod id>"}` and `{"reload": "<mod folder>"}` (the runner copies that folder's files, luma.json
included, over the installed mod's, then queues the reload; the test runs in dev mode for that mod). Each step's next
step runs once its fact (`takeout.<id>`, `reload.<id>`) is written, within 600 frames; otherwise the test is
setup-failed naming the step and the last log step of section 4 it reached . `"wait": false` on the step opts out, for
a test of what happens during the grace.

| Test | Must show |
|---|---|
| 1140 | take out lt-mem holding 3 blocks: `takeout.lt-mem` {"blocks": 3, "bytes": the sum, "stopped": true, "finished": true}; the log line; a later call through its old hook does not reach it |
| 1141 | take out a mod holding none: 0, 0; and a mod with one timer open at take-out: 0, 0 (the time service freed its block at mod_end) |
| 1142 | take out lt-lib while lt-uses is on: lt-uses taken out first (its stop before lt-lib's), both facts; no panic |
| 1143 | lt-cxx taken out: its destructors' log lines in reverse construction order, after its stop |
| 1144 | reload lt-frame with a second build (another constant): the new value in the facts, `reload.lt-frame` ok, hooks counted once (not twice); reload.result ok true with the request's n |
| 1145 | reload with keep/kept: a counter kept in stop, read in start, one more after each of two reloads |
| 1146 | reload with a build the validator refuses: the mod not running, its reason on the page and in the mod log, `reload` ok false with the row; reload.result ok false with that code, names and values and the request's n |
| 1147 | no dev flag: no copy folder, no request polling (no stat in the file thread's log) |

### Kernel promise: an arena's objects released

`NebulaArena::destroy` calls `lumacore::released(const void* pStart, const void* pEnd)` first, before the arena's
memory goes and before anything in it is reused. The objects in [pStart, pEnd) die there without their destructors
(a stage's NameObjs). `released` is a function row like any other, empty in the source, so a before hook sees each
release on the rows module. It is kept, with this signature, for as long as arenas free without destructors. A mod
holding addresses (lumart's ref slots) empties them there. If the kernel later delivers this itself (lumascript-abi
A-4), it calls `lumart_refs_release(lo, hi)`.
