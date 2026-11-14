# The test harness

## More rules

### 1. Why it is built this way

The first Gold Flower runs misled because the test's precondition (the spin) was applied before the state it belongs
to existed (a grant at the title screen, then a new save dropped into a stage): the test ran, nothing said the setup
had not taken, and the missing throw looked like a mod or kernel fault. So:

- Setup is ordered by the game's own state and verified by reading it back. A test whose setup did not take ends
  as `setup-failed`, naming the item, and proves nothing either way.
- Only `pass` is proof. Every other verdict names what went wrong.
- Nothing is ignored. An unknown key, an unsupported argument, a missing reference, a mod that did not load:
  each is a named failure, never skipped.

### 2. Parts

| Part | Where | What it does |
|---|---|---|
| Runner | `tools/lumacore/luma_test.py` (Python 3, standard library only) | Reads tests, prepares a private run folder, installs the mods, starts the game hidden and silent, waits, evaluates, writes the result |
| Host half | `tools/test_driver/extension` (the host extension, `--extension`) | Injected controls and keys, pictures on the frames asked, the sound to a file, fixed time steps |
| In the game | `tools/test_driver/luma_test` (a LumaCore mod in C, `luma.h` only) | Setup in order with verification, the test clock, calls by name, the facts file |

The game half uses only what any mod can use (rule 1): functions, fields and hooks by name. It is installed first in
every run, before the test's own mods, and is not counted among them.

### 3. A test

A test is a folder holding `test.json` and the files it names (mods, reference pictures, fixtures). Paths in it are
relative to that folder. A suite is a `suite.json` listing test folders: `{"harness": 1, "tests": ["a", "b/c"]}`.

```json
{
  "harness": 1,
  "name": "gold-flower-throw",
  "about": "With the spin granted, a spin as gold Mario throws one gold ball",
  "mods": ["mods/gold-flower"],
  "code": "allow",
  "renderers": ["d3d12", "vulkan"],
  "limit_seconds": 120,
  "save": { "character": "mario", "spin": true, "stars": 0, "grand_stars": 0,
            "flags": ["PowerStarGet"], "values": { "LibraryOpenNewStarCount": 0 } },
  "stage": { "name": "EggStarGalaxy", "scenario": 1 },
  "player": { "at": "GoldFlowerItem#0", "form": "normal" },
  "steps": [
    { "at": 60, "picture": "start" },
    { "at": 120, "shake": 10 },
    { "at": 150, "picture": "after-spin" },
    { "at": 150, "call": "MR::getPlayerPos", "signature": "Vec3f *()", "fact": "player.pos" },
    { "at": 600, "end": true }
  ],
  "expect": [
    { "fact": "gold-flower.balls_thrown", "is": 1 },
    { "fact": "player.pos", "near": [0, 0, 0], "within": 5000 },
    { "picture": "after-spin", "like": "refs/after-spin.png", "within": { "rmse": 6.0 } },
    { "no_log": "not started" }
  ]
}
```

### 3.1 Keys

| Key | Required | Meaning |
|---|---|---|
| `harness` | yes | `1`. A runner refuses a higher number (`harness-error`) |
| `name` | yes | Unique within a suite; letters, digits, `-`, `_`, `.` |
| `about` | yes | One line: what the test proves |
| `proves` | yes | The capability ids the test proves, e.g. `["kernel.hook.wrap"]`; each must be in `tools/lumacore/capabilities.json` (the one list the status page is generated from, rule 8), or the test is `harness-error` naming the id. Copied into `result.json`. A test that proves nothing new about a capability (a fixture, a check of the harness itself) says `["harness.self"]` |
| `mods` | no | In load order. Each entry is a path (a mod folder with `luma.json`, or a `.luma` file) or an object (3.1.1) |
| `code` | no | `allow` (default) or `deny`: the consent to the mods' code, for every mod that does not say otherwise |
| `language` | no | One of the game's 14 languages by the game's own name (`UsEnglish`, `JpJapanese`, `EuGerman`, ...), applied as the setting is, read back as `setup.language`. Default: the disc's first |
| `seed` | no | A number. The game's random generator is seeded with it at setup and read back as `setup.seed`. Default `0`: every harness run seeds, so the same test gives the same random numbers |
| `continue_from` | no | `"<test name>"` of the same suite: this test starts with the saves, settings and installed mods (with their data) that test left, instead of new ones. That test must come earlier in the suite and have passed, or this one is `setup-failed` naming it |
| `renderers` | no | Any of `d3d12`, `vulkan`, `metal`, `gl`; default `["d3d12"]` on Windows. One run per renderer; each has its own verdict |
| `region` | no | `RMGE01`, `RMGP01`, `RMGJ01`; default the run's disc |
| `limit_seconds` | no | Wall-clock cap, default 180. Reaching it is `timeout` |
| `ends` | no | `end` (default): the harness ends the run at its `end` step. `crash`: the run must end in the host's crash reporter before its `end` step (a fault the guard never resumes); the test then needs a `log` expectation starting `crash: `, and the always-checked items become: the crash before the end step, a crash dump, a code other than 0, and the setup items verified before the crash |
| `bare` | no | `true`: the game half is not installed. Only `steps` of kinds `wait`, `picture`, `key`, `end` and expectations on the log, pictures and sound are allowed; anything else is `harness-error`. For runs with no mod code at all (cost with no mod, boot) |
| `save` | no | Save-file state (3.2) |
| `stage` | no | Where the test starts (3.3). Without it the test starts at the title screen |
| `player` | no | The player's state at the start (3.4); needs `stage` |
| `steps` | yes | What happens, on the test clock (3.6) |
| `expect` | yes | What must hold (3.7); may be empty only for a test that proves "the run reaches its end" |
| `faults` | no | Guard events the test expects (a broken-mod test): `[{"mod": "x"}]`. Without it any guard event fails the test |

Any other key, anywhere in the file, is `harness-error` naming the key and its place. Types are checked: a string
where a number belongs is `harness-error` naming the key.

### 3.1.1 A mod entry as an object

```json
{ "path": "mods/broken.luma", "options": { "Speed": 2, "Colour": "gold" }, "code": "allow",
  "permissions": "allow", "install": "import", "loads": false }
```

| Key | Default | Meaning |
|---|---|---|
| `path` | required | As a plain entry |
| `options` | the mod's defaults | The mod's options by name, set as the Mods screen sets them (6.3) |
| `code` | the test's `code` | Consent to this mod's code |
| `permissions` | `"allow"` | `"allow"` or `"deny"` every permission the mod asks for, or `{"<name>": "allow" or "deny"}` per permission (2.1). A permission named here that the mod does not ask for is `harness-error` |
| `install` | `"placed"` | `"placed"`: the runner puts the mod into the run's mods folder as an installed mod. `"import"`: the file goes through the game's own import (as a file dropped on the window, 6.1), so the game's validator checks it; each line of the import screen's result is a fact `import.<file name>` (4) |
| `loads` | `true` | `false`: the mod is expected not to start; the implicit "every mod loaded" check skips it, and the test must say by an expectation what message it gives |

Building is not the runner's job. A mod's folder or `.luma` must already hold its built code for the run's
platform. A `luma.json` that names code missing for that platform is `harness-error` naming the file. Until the
`luma-format` contract is signed, `mods` accepts the `luma.json` that `examples/luma-hello` uses on the base; after
it is signed, exactly that format.

### 3.2 `save`

The run's save folder is new and empty. The game half creates user file 1 through the game's own new-file path
(as the file select does), makes it the current file, and applies each item to it. Each item is then read back
through the game's own query of that state, and recorded as a fact `setup.save.<item>` with the value read. All of
it happens before the stage is entered.

| Item | Default | Meaning |
|---|---|---|
| `character` | `mario` | `mario` or `luigi`: the file's player |
| `spin` | `false` | The Gateway Luma's grant (spin and star pointer, the game's `SpinAndStarPointer` permission) |
| `stars` | 0 | Power stars owned, counted as the game counts them |
| `grand_stars` | 0 | Grand stars, 0 to 7 |
| `flags` | none | Game event flags switched on, by the game's own names |
| `values` | none | Game event values, by name |
| `from` | none | A save file (in the test folder) copied in instead of a new file; the other items then apply on top of it |

### 3.3 `stage`

`{"name": "<galaxy or stage name>", "scenario": <n>, "zone": "<optional>"}`. The stage is entered through the game's
own stage change from the current file, as a file starts a galaxy. The stage is ready when its scene has started,
its objects are made, and the player exists. A name the game does not have, or a stage that is not ready within
`limit_seconds`, is `setup-failed` naming it.

### 3.4 `player`

Applied after the stage is ready, then read back.

| Item | Meaning |
|---|---|
| `position` | `[x, y, z]` in game units (a glTF metre is 100) |
| `at` | `"<object name>#<n>"`: the n-th live object of that name (in creation order), the player placed above it by 150 units against its gravity. An absent object is `setup-failed` naming it |
| `form` | A form the game has, by the game's name (`normal`, `fire`, `ice`, `bee`, `boo`, `spring`, `flying`, `rainbow`, ...), through the game's own form change |
| `life` | Health, 1 to the game's maximum |

### 3.5 The test clock

Frame 0 is the first game frame after every setup item has been applied and read back. The clock counts game logic
frames (fixed time steps are always on in a harness run, so one frame is one step, at any speed of the machine). All
`at` values are on this clock. A test with no `stage` starts its clock at the first frame of the title screen.

### 3.6 `steps`

Each step has `at` (a frame) or `after`: `{"fact": "<key>", ...a check as in 3.7...}` or `{"log": "<regular
expression>"}`, met at the first frame it holds (for a log line: the first frame after the line is written), with
`"timeout": <frames>`, default 600; not met in time is `fail` naming the step. Exactly one action key:

| Action | Form | Meaning |
|---|---|---|
| `hold` | `{"hold": ["A", "B"], "frames": n}` | Buttons held from `at` for n frames. Names: `A B Z C PLUS MINUS ONE TWO HOME UP DOWN LEFT RIGHT` |
| `stick` | `{"stick": [x, y], "frames": n}` | Main stick, each in -1..1 |
| `shake` | `{"shake": n}` | The spin gesture for n frames |
| `pointer` | `{"pointer": [x, y], "frames": n}` | The pointer, in 0..1 of the screen |
| `key` | `{"key": "enter"}` | A key to the host's own menus (`up down left right enter esc tab space backspace`); while a mod's text request is open, `enter esc backspace left right` edit it instead |
| `text` | `{"text": "Gold Mario"}` | Typed into the open text request (input 1.1 `text_request`), 1 to 256 characters, no control characters: each code point goes through the host's text entry as the window's keyboard and input method give it (`text_utf8`, the path SDL's text input and the Win32 window's characters take), so the request's kind and length rules apply as to a player's typing. Nothing while no request is open. A host without text entry (the headless one) ends the test `harness-error` |
| `picture` | `{"picture": "<name>"}` | The first frame presented after game frame `at`, saved as `pictures/<name>.png`. Names unique in a test |
| `call` | `{"call": "<function name>", "signature": "<as the game writes it>", "args": [...], "fact": "<key>"}` | Calls a game function by name; its result, when it has one and `fact` is given, becomes that fact. Arguments: numbers, `true`/`false`, strings (for `const char *`), `[x,y,z]` (for `Vec3f`, `TVec3f` by value, reference or pointer), `"player"`, `"object:<name>#<n>"` (a live object). A signature with any other parameter type is `harness-error` naming the parameter |
| `read` | `{"read": "<Class.field>", "of": "player" or "object:<name>#<n>", "type": "<type>", "fact": "<key>"}` | A field by name, recorded as a fact |
| `stage` | `{"stage": {...as 3.3...}}` | Changes stage during the test; `stage.ready` is recorded when the new one is ready; the clock does not stop |
| `wait` | `{"wait": true}` | Nothing; a place to hang an `after` |
| `end` | `{"end": true}` | The test ends at this frame. Every test has exactly one `end` |

Steps at the same frame run in the order written. Input steps overlap freely; the held set is their union.

### 3.7 `expect`

Checked by the runner after the run, from the run's files. Each check is one object:

| Check | Form |
|---|---|
| A fact | `{"fact": "<key>", <op>: <value>, "when": "last" (default) or "first" or "any" or "all", "frames": [a, b]}`; op one of `is`, `is_not`, `less`, `at_most`, `more`, `at_least`, `near` (with `within`, numbers or vectors, by distance), `exists`, `absent` (value `true`) |
| The log | `{"log": "<regular expression>"}` must match a line; `{"no_log": "<regular expression>"}` must match none |
| A picture | `{"picture": "<name>", "like": "<reference png>", "within": {"rmse": r}}` or `"unlike"` with `"beyond": {"rmse": r}`; optional `"box": [x, y, w, h]` in pixels of the picture; or `{"picture": "<name>", "box": [...], "mean_rgb": [r, g, b], "within": d}` |
| The sound | `{"sound": [first_frame, last_frame], "rms": [low, high]}` on the run's mix (dBFS) |

Always checked, without being written: the run ended at its `end` frame by the harness (not by a crash, a hang or
the limit; with `"ends": "crash"`, a crash before it, a dump and a code other than 0); no crash dump; every mod in `mods` loaded (unless an expectation names the message it must give, as
`{"log": "gold-flower not started: ..."}` together with `"loads": false` on that mod's entry, written
`{"path": "...", "loads": false}`); no guard event unless listed in `faults`; every setup item verified.

A reference path may hold `{renderer}`, replaced by the run's renderer (`"like": "refs/{renderer}/after-spin.png"`),
so each renderer is compared against its own reference.

A picture compared with a reference of another size is `fail` naming both sizes. A missing reference is
`harness-error`; `luma-test refs <test>` writes the run's pictures as the references after the person running it has
looked at them, and the test's commit records that.

### 4. Facts

The game half writes `facts.jsonl`, one object a line: `{"frame": <test clock, or -1 before frame 0>, "key": "<key>",
"value": <JSON>}`. Keys:

| Key | Written by | When |
|---|---|---|
| `setup.save.<item>`, `setup.stage`, `setup.player.<item>`, `setup.language`, `setup.seed` | game half | as each is read back; value as read |
| `setup.done` | game half | at frame 0 |
| `stage.ready` | game half | each time a stage becomes ready: `{"name", "scenario", "frame"}` |
| `mod.<id>.loaded` | game half from the host's report | `true`, or the message it was refused with |
| `guard.<id>` | game half | a guard event in that mod |
| `<id>.<key>` | a mod under test | when it logs a line `fact <key> <JSON value>` through `LumaCore.log` |
| any `fact` of a step | game half | when the step runs |
| `import.<file name>` | host half | each line of the import screen's result for that file: `{"line": <n>, "text", "code", "file", "at"}` (the validator's code, the file inside the mod and the place in it, when the line names them) |
| `modlog` | host half | each line the on-screen mod log shows: `{"mod", "file", "line", "text"}` |
| `modscreen.<id>` | host half | each line the Mods screen shows about that mod (what it is, what it carries, what is wrong) |

A mod under test writes facts through the ordinary log, so a test mod needs nothing beyond `luma.h`. A line that
starts `fact ` but whose value is not JSON is itself a fact `harness.bad_fact` with the line, and fails the test.

The `import`, `modlog` and `modscreen` facts are written by the code that shows those lines, as it shows them, so a
test proves what is on screen and not only what is in the log (rule 6). They exist from the tasks that build those
screens (3.12, 6.1, 6.2); a test that names them before then is `harness-error: no such fact source yet`.

### 5. What a run leaves

`<work>/runs/<suite or test>/<test>/<renderer>/`:

| File | What |
|---|---|
| `result.json` | `{"harness": 1, "test", "proves": [...], "renderer", "platform", "os", "gpu", "commit", "module": "hooks" or "plain", "verdict", "reason", "checks": [{"check", "result", "actual"}], "setup": {...}, "frames", "seconds", "pictures": [...]}`. Machine-readable: the status page (rule 8) and the gate files are generated from these |
| `facts.jsonl` | 4 |
| `run.log` | the game's log |
| `pictures/<name>.png` | 3.6 |
| `audio.wav` | the mix |
| `test.json` | the test as run, with the runner's version |
| `bin/`, `*.dmp` | kept only when the run crashed |

Verdicts: `pass`; `fail` (a check did not hold; the first failing check is the reason, all are listed); `setup-failed`
(the item); `crashed` (the dump's path and the last log lines); `timeout`; `harness-error` (the runner's own problem or
a malformed test, never the mod's). The runner prints one line per test and renderer and exits 0 when every verdict is
`pass`, 1 otherwise.

### 6. The run itself

- Each test and renderer gets new saves, settings, app data and a frozen copy of the binaries under the run folder;
  nothing outside `NEBULA_WORK` is read or written but the binaries, the game data and the test folder.
- The window is hidden and the sound goes to the file only.
- `luma-test` takes `board/run.lock` (writing who and what) for the whole suite and refuses to start while it exists;
  it never starts, stops or touches a game process it did not start.
- `luma-test run <test or suite> --bin <folder> [--renderer r] [--work <folder>] [--who F|G]`.

### 7. Platforms

The test format is the same on every platform. The runner has one adapter per platform for starting the game, placing
files and reading the results back: A platform without its adapter proven says so in the result (`harness-error: no
adapter for <platform>`).

### 8. Versioning

`harness: 1` may gain new keys, step kinds, check kinds and facts; never a change of meaning. A change of meaning is
`harness: 2`, and the runner keeps reading 1.

Planned additions under `harness: 1` (nobody waits for them):

| Addition | For |
|---|---|
| `type` step: text through the platform's text entry | 2.5 |
| `touch` and `mouse` steps | 2.5, 6.x on touch platforms |
| A `sound` check against a reference file | 3.7 |
| `servers`: loopback fixtures the runner starts before the game and stops after (`tcp-echo`, `udp-echo`, `http-static`, `websocket-echo`, standard-library Python), their ports given to the test as facts, so network tests need no outside network | 2.7 |

### Revisions

- 1: `proves`; mod entries as objects (`options`, `code`, `permissions`, `install`, `loads`); `import`, `modlog`,
  `modscreen` facts; `language`; `seed`; `continue_from`; `after` on a log line; building is not the runner's job;
  `{renderer}` in reference paths; planned additions; tests 11 to 13.

- 3.3, the ready sentence reads: "The stage is ready when its scene has started, its objects are made, the
  player exists, and no demo runs (`MR::isDemoActive` false), so the player takes input." (Run 6's 04a: the
  Welcome layout held input at test frame 90.)
- 3.2 gains: "With `continue_from`, user file 1 is loaded from the saves that test left (as the file select
  loads a file), not made new; `setup.file` is `created` or `loaded`." Without `continue_from` it is made
  through `GameSequenceFunction::startCreateUserFileSequence` (the new-file path), `setup.file` `created`.
- 4, the facts table's setup row gains `setup.file` (game half, before `setup.save.<item>`).
- 9, test 13 asks a language of the bin's disc: `UsFrench` on RMGE01 (run 5); JpJapanese belongs to the 0.8
  runs with a JP bin.

- Note: 3.3 holds as written, but run 7's 04a picture still shows the Welcome layout at frame 90 with isDemoActive
  false: that layout is no demo, see RUNS run 7)

- 3.3, the ready sentence reads: "... and no demo runs (`MR::isDemoActive` false) and the player takes input
  (`MarioModule::isInputDisable` false on the player's Mario)." Run 8 (04d): at test frame 60 every MarioActor
  spin gate was open and `isDemoActive` false, yet the shake did nothing; `Mario::checkKeyLock` holds input for the
  player's first 75 frames on a stage (and in the landing and stage-in animations, a remote demo). The game half
  takes the Mario from its own `Mario::checkKeyLock` (after hook) and asks the game's function; no frame count of
  ours. Built: Nebula-LH 5730300d6, luma-test.dll c0a71f13ea7c5025.
- 9, test 4a's shake at 60 is then 60 frames after the player takes input, as written.

- G:

R3-1: a stage that never becomes ready says which condition held it. Today the 7,200-frame timeout says
"the stage was not ready within 7,200 frames" for any of seven conditions (stage name, scenario, opening camera,
scenario start, demo, no player, input disabled). The refusal names the first condition still unmet at the timeout and
its value ("ready: MarioModule::isInputDisable still true", "ready: stage EggStarGalaxy expected, found
AstroGalaxy"), as every refusal names what was expected.

1. Lines and facts have one named limit: a line the game half is given is at most 1,023 bytes and 16 words (words
   split at spaces; a `call` line's words are its tab-separated name, at most 16), a signature at most 16 parameters,
   and a fact the game half writes at most 1,023 bytes. The runner refuses a plan over them (harness-error naming the
   step's path or the line); the game half fails a line that is over, or whose fact would be, naming it. Nothing is
   cut.
2. The host's log reaches the extension whole: `NebulaHostExtension.log_line(user, tag, text)`, the line being "<tag>
   <text>", of any length.
3. Objects and the player's Mario leave the game half's lists when their memory goes: NameObj::~NameObj, and
   NebulaArena::destroy for everything that lies in that arena. "The n-th live object" counts only those.
4. Facts and conds compare as JSON values: strings after `\u` escapes are decoded to UTF-8 (surrogate pairs included);
   objects member by member in any order.
5. Results: a call's result type is one of f32/float, f64/double, s32/int, u32, u16, s16, u8, s8, bool, const char *,
   Vec3f/TVec3f (by value or pointer), void, or another pointer (taken as whether there is one); any other is refused
   before the call, naming it. Parameters add f64/double, s16, u8, s8, each checked against its type's range by the
   runner.
6. Always checked adds "the game quit by itself with exit 0". Crashed is decided by the host's own crash line
   (`[<time>] crash: `), not a word anywhere. The sound check reads the WAV by its chunks and refuses any format other
   than 16-bit PCM stereo, naming it.
7. `save.character` sets `GameDataHolder.mUser` only to the file `setup file` made current.

Built:

1. The service `harness` is minor 1 (`LUMA_HARNESS_MINOR`, luma_harness.h). A mod asking for a later minor is not
   given it: its import is filled as for an absent service (refused when required, null when optional). Test 1112.
2. `int32_t (*other_thread)(void (*fn)(void* user), void* user)`, the last field of `LumaHarness` (minor 1): runs
   `fn(user)` on a thread of the harness's own and returns when it has ended; the game thread waits. It gives 1, or
   0 when no thread could be made (the log says so once per call); a null `fn` gives 0. No harness lock is held
   while `fn` runs, so `fn` may log and give facts. A mod reads it only when `size >= sizeof(LumaHarness)`.
3. Why: a harness run ticks the sound on the game thread, so nothing calls a hooked function off it; a test of what a
   hook sees off the game thread (1116) makes that call itself, the same way on every platform.

- R6-1: a mod's own facts are `mod.<id>.<key>`, not `<id>.<key>`, so no game key can be a mod's (a mod "modlog"
  logging `fact notice` wrote `modlog.notice`). `loaded` is the game half's own (`mod.<id>.loaded`): a mod logging
  `fact loaded ...` gets `harness.bad_fact` naming it. The runner reads a test's `fact` keys as written; every test
  generator writes the new form, and every suite runs again with the commit that brings it.
- R6-2: section 4's table gains (the tasks that write them: 3.12 and 3.13): `modlog` with mod-log 4.3's fields;
  `modlog.notice`, `modlog.state`, `modlog.page` (the Log page, once per opening; `modscreen.log` is retired);
  `refs.after_start` (references 3: each class looked up after the last mod's start, with its answer); `placeholder`
  (a placement's stand-in made for a class the check found missing: class, file, line); `startcheck` (`{"part",
  "why"}`, once at start-check step 1, none with no mod on).
- R6-3: a `settings` step: `{"settings": <page>}` opens the host's own Settings on that page at `at` (MODS is 5), the
  cursor on the page's last list row (the test extension's existing open, which GALAXY_TEST_SETTINGS_OPEN used in runs
  21 and 21b); then `key` steps move in it. `space` presses the selected button, `enter` is Apply on this page, `esc`
  closes. `{"settings": 5}`, then `down` once (the LOG row, below the last mod row), then `space` (its OPEN), each
  step 60 frames apart or more.
- R6-4: `hide_game_files`, a list of paths relative to the game's folder: the runner leaves them out of the run's own
  copy of the game (runs/<test>/<renderer>/bin, section 6), so nothing of the person's install is moved; a path not in
  the source folder is `setup-failed` naming it.
- R6-5 (run 22h): a `class:<Class>#<n>` argument beside `object:<name>#<n>`: the n-th live object (creation order)
  whose own class is `<Class>` or one made from it (is_a), for stages whose placements name objects in Japanese.
  Wherever `object:` is taken (call arguments, `of`, `player.at`).

- R8-1, save-file steps (services-save 5; kit-progress): four step kinds on the run's own saves, through the game's
  own `GameSequenceFunction` (the file select's operations), each answered when the game's save sequence has ended and
  its result been read back; a step that did not take is `fail` naming it. Files are 1 to 3.

  | Step | Meaning | Fact |
  |---|---|---|
  | `{"copy_file": [from, to]}` | file `from` copied onto file `to` (two different files). When the current file is `from`, it is first put into its slot as the first step of the game's own save does, so the copy holds what the game and the mods have set on it | `file.copied` `{"from", "to"}` |
  | `{"load_file": n}` | file `n` made current as the file select loads it (as the character `save.character` set, Mario by default); the stage is not changed | `file.loaded` `n` |
  | `{"erase_file": n}` | file `n` erased; the current file is then the erased one, not created | `file.erased` `n` |
  | `{"new_file": n}` | file `n` made through the new-file path (`startCreateUserFileSequence`); it is then the current file | `file.created` `n` |

- R8-2, `after` with `"when": "next"`: `{"after": {"fact": k, <check>, "when": "next"}}` is met by a write of `k`
  made after the wait began, not by a value written earlier (a mod's `done` of a first stage run does not meet the
  `after` of the second). Without `when` the newest value counts, as before. `when` is for fact conditions only
  (a log condition takes none); any other value is `harness-error`.
- R8-3, a place argument: a `Vec3f`, `TVec3f`, `Vec3f *` or `const Vec3f &` parameter of a `call` also takes
  `"object:<name>#<n>"` or `"class:<Class>#<n>"`: the position of that live actor as the game holds it (not raised
  by 150 like `player.at`). A missing object is `setup-failed` as for an object argument; an object that is no actor
  (no position) is `fail`.
- R8-4, no top-level `permissions`: `permissions` belongs to a mod entry (3.1.1), where the default is `"allow"`;
  `code` is the only consent default of the test. A test that wants every permission allowed writes nothing.

- R9-1, `device`: a test's top-level `"device"` is `"keyboard"` (the default, as before) or `"pad"`. The host gives
  the extension the motor's strength (0 to 255, the game's or a mod's, the stronger) at each change
  (`NebulaHostExtension.motor`, appended; its `size` versions it), and the harness writes it as the fact
  `input.motor`. Any other value is `harness-error`.

- R10-1, `ends`: a test's top-level `"ends"` is `"end"` (the default, as before) or `"crash"`. With `crash` the
  run must end in the host's crash reporter (its `crash: ` line) before the end step, and the harness judges it as
  a run that ended: the verdict is `pass` when every check passes, not `crashed`. The always-checked items for such
  a test are "the game ended in a crash before its end step", "a crash dump was written", "the game ended with a code
  other than 0" (not 0 and not killed at the limit), and "every setup item verified before the crash" (a fault on the
  first frame of play can come before the setup's last item; a setup item that failed stops the run first). The test
  must have a `log` expectation whose pattern starts with `crash: ` (else `harness-error`). Any other value is
  `harness-error`. A crash in a test without it stays `crashed`. f/guard/judge-crash.py now only reads the verdict.

- R11-1, a check's `scope`: a `fact` or `log`/`no_log` check may say `"scope": "installation"`: it reads what every
  installed mod adds to, not only the test's own mods. These are the Mods screen's lines and pages (`modscreen.*`,
  reached by position), the mod log's notices, pages, entries and state (`modlog.notice`, `modlog.page`,
  `modlog.entry`, `modlog.state`), `startcheck`, a `modlog` check that names no mod, and a `modlog` check judged on
  the last entry of the whole log. The harness judges it like any other check. A run that installs other mods beside
  the test's leaves the check to the test's own run and lists it
  (`<pairs>/left-to-alone.tsv`). Any other value is `harness-error`. Absent: the check is about the test's own mods.
- R11-2, a mod entry's `folder`: the folder the mod is placed in under the run's mods folder; without it the folder
  is the mod's id, as before. It places a second copy of a mod, one id in two folders, as a player can install it.
  The game then refuses both by name (validator L802, one mod log entry each). Lowercase letters, digits, `-` and `.`,
  up to 64; anything else is `harness-error`. Two entries placed in one folder stay `harness-error`.
- R11-3, a held binary: after a run, the harness removes the run's copies of the binaries and tries each for up to
  30 s. A copy still held is left in place and the run's log names the holder (Windows' Restart Manager). The verdict
  and `result.json` do not depend on the removal. Before this revision a copy held for more than 5 s stopped the
  harness before it wrote `result.json`.
