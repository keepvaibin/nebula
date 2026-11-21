---
title: Test files
shortTitle: Test files
intro: Every key of a test.json file, every step a test can take, every check it can make, and what a run leaves behind.
contentType: reference
---

## About test files

A test starts the game with your mod, sets up a save, a stage and the player, plays a list of steps on a frame clock, and then checks what happened. `lumalink test` runs every `tests/**/test.json` in a mod folder. For a walkthrough, see [Testing a mod](../how-tos/testing-a-mod.md).

A test is a folder that holds `test.json` and the files it names, such as mods and reference pictures. Paths in `test.json` are relative to that folder. The game runs hidden, its sound goes to a file, and each run gets new saves, settings and app data, so a test never touches your own game.

```json
{
  "harness": 1,
  "name": "gold-throw",
  "about": "A spin as gold Mario throws one gold ball",
  "mods": ["../.."],
  "save": { "spin": true },
  "stage": { "name": "EggStarGalaxy", "scenario": 1 },
  "player": { "form": "normal" },
  "steps": [
    { "at": 60, "picture": "start" },
    { "at": 120, "shake": 10 },
    { "at": 150, "picture": "after-spin" },
    { "at": 150, "call": "MR::getPlayerPos", "signature": "Vec3f *()", "fact": "player.pos" },
    { "at": 600, "end": true }
  ],
  "expect": [
    { "fact": "mod.gold-throw.balls_thrown", "is": 1 },
    { "picture": "after-spin", "like": "refs/{renderer}/after-spin.png", "within": { "rmse": 6.0 } },
    { "no_log": "not started" }
  ]
}
```

A suite is a `suite.json` that lists test folders, run in that order:

```json
{ "harness": 1, "tests": ["first", "more/second"] }
```

Any key that is not on this page, anywhere in the file, stops the test with an error that names the key and its place. So does a value of the wrong type.

## Top-level keys

| Key | Required | Description |
|---|---|---|
| `harness` | Yes | `1`. |
| `name` | Yes | The test's name: letters, digits, `-`, `_` and `.`. Unique within a suite. |
| `about` | Yes | One line that says what the test shows. |
| `steps` | Yes | What happens, on the test clock. See [Steps](#steps). Exactly one step is an `end` step. |
| `expect` | Yes | What must hold after the run. See [Checks](#checks). It can be empty for a test that only has to reach its end. |
| `proves` | No | A list of IDs from `capabilities.json`, recorded in the result. Nebula's own tests use it to track what each test covers. |
| `mods` | No | The mods to install, in load order. Each is a path to a mod folder or `.luma` file, or an object. See [Mod entries](#mod-entries). A test in `tests/NAME/` names its own mod as `"../.."`. |
| `code` | No | `allow` (the default), `deny` or `ask`: the answer to the question about running the mods' code. `ask` leaves the question on the screen. |
| `language` | No | A language of the game's discs, by its name in the game: `UsEnglish`, `UsSpanish`, `UsFrench`, `EuEnglish`, `EuSpanish`, `EuFrench`, `EuGerman`, `EuItalian`, `EuDutch` or `JpJapanese`. The default is the disc's first language. A language that only one disc has also needs `disc`. |
| `disc` | No | The disc the test needs: `RMGE01` (USA), `RMGP01` (Europe) or `RMGJ01` (Japan). The test runs on the run's game when it is of that disc, else on the game `--pak-for DISC=PATH` or the `LUMA_TEST_PAKS` environment variable (`DISC=PATH` pairs joined by `;`) names for it. With neither, its result is `not-run`, naming the disc, and the run does not pass. |
| `window` | No | The window's size in pixels as `[width, height]`, whole numbers from 64 to 16384. The default is `[1280, 720]`. A picture has this size. |
| `seed` | No | A number that seeds the game's random numbers. The default is `0`, so a test gives the same random numbers each run. |
| `renderers` | No | Any of `d3d12`, `vulkan` and `d3d11`. The test runs once for each. The default is `["d3d12"]`. |
| `limit_seconds` | No | The longest the run may take, in seconds. The default is 180. |
| `save` | No | The save file. See [Save](#save). |
| `stage` | No | Where the test starts. See [Stage](#stage). Without it, the test starts on the title screen. |
| `player` | No | The player at the start. See [Player](#player). Needs `stage`. |
| `continue_from` | No | The name of an earlier test in the same suite. This test starts with the saves, settings and mods that test left, and needs that test to have passed. |
| `device` | No | `keyboard` (the default) or `pad`. With `pad`, the controller's rumble is recorded as the fact `input.motor`. |
| `faults` | No | Mods that are expected to fault, as `[{"mod": "ID"}]`. Without it, any fault in a mod fails the test. |
| `ends` | No | `end` (the default), `crash` or `crash-at-stop`. With `crash`, the game must crash before the `end` step. With `crash-at-stop`, the run must reach its `end` step and the game must then crash while it stops (a mod's stop function runs then); a crash before the `end` step fails the test. Both need a `log` check whose pattern starts with `crash: `. |
| `starts` | No | How many times the game process must have started during the run, when a step restarts the game. |
| `safe_start` | No | `asked` starts the game in safe mode. `crash` starts it as after a crash with mods on that named no mod while no mod's code ran: every mod is off for the start, and the **Mods** page says why. `crash-code` starts it as after such a crash while mods' code ran: every mod's code is off for the start. To test the start after a crash that names your mod, end one test with `"ends": "crash"` and continue from it (`continue_from`). |
| `fresh` | No | `true` starts with no shader cache, as on a first start. |
| `time` | No | `steps` (the default) or `real`. See [The test clock](#the-test-clock). |
| `clock` | No | `title` (the default) or `start`, for a test with no `stage`. See [The test clock](#the-test-clock). |
| `saves` | No | A folder in the test's folder holding the saves the run starts with. It is laid out as the game's save folder: the save of the USA disc is `RMGE/Nebula.sav`. |
| `visible` | No | `true` shows the game's window. The default is a hidden window. Use it only to measure how pictures are shown. |
| `settings` | No | Game settings for the run, as a list of `"GALAXY_NAME=value"` (`"GALAXY_VSYNC_INTERVAL=1"`). Each name must be one the game's settings file takes; the list is `tools/verify/settings_args.py` in Nebula's source. Any other name stops the test with a harness error. |
| `diagnostics` | No | The game's own records for the run, as a list of the names its `--diagnostics` option takes (`present-stats`, `frame-times`, `timeline`; the list is `runtime/include/galaxy/launch_args.h`). With `timeline`, the record is the run folder's `timeline.bin`. |
| `bare` | No | `true` runs the game with no test code in it. Only `wait`, `picture`, `key`, `text`, `settings` and `end` steps are allowed, and only log, picture and sound checks. A bare test has no mods. |
| `hide_game_files` | No | Paths inside the game's folder that the run's copy of the game leaves out. |

## Mod entries

A mod in `mods` can be an object instead of a path:

```json
{ "path": "mods/broken.luma", "options": { "Speed": 2 }, "code": "allow", "permissions": "allow", "install": "import", "loads": false }
```

| Key | Default | Description |
|---|---|---|
| `path` | Required | The mod folder or `.luma` file. |
| `options` | The mod's defaults | The mod's options by key, set as the **MODS** page sets them. A number option can be `{"free_port": "NAME"}`: it gets a free TCP port, the same one for every option with that NAME. |
| `code` | The test's `code` | `allow`, `deny` or `ask` for this mod. |
| `permissions` | `"allow"` | `"allow"` or `"deny"` for every permission the mod asks for, or an object with one answer per permission, such as `{"net.internet": "deny"}`. Naming a permission the mod does not ask for is an error. |
| `install` | `"placed"` | `"placed"` puts the mod straight into the mods folder. `"import"` sends it through the game's own import, as a file dropped on the window, so the import screen's checks run. |
| `loads` | `true` | `false` means the mod is expected not to start. The test must then check the message the game gives, for example `{"log": "gold-flower not started: "}`. |
| `enabled` | `true` | `false` installs the mod switched off. For a mod kept from the test named in `continue_from`, `true` or `false` switches it on or off; without `enabled`, it stays as that test left it, so a player's choice saved then holds. |
| `remove` | `false` | `true` removes a mod kept from the test named in `continue_from`, as a player deleting it. The entry has only `path` and `remove`. |
| `folder` | The mod's ID | The folder the mod is placed in. Lowercase letters, digits, `-` and `.`, up to 64 characters. |

A mod must already hold its built code for this computer. `lumalink test` builds your mod before it runs the tests. A mod that is not expected to fail, and does not load, fails the test.

## Save

The run starts with no saves. The test creates file 1 through the game's own new-file path, makes it the current file, sets each item, and then reads each item back. A setting that does not read back ends the test as `setup-failed`.

| Item | Default | Description |
|---|---|---|
| `character` | `mario` | `mario` or `luigi`. |
| `spin` | `false` | `true` gives the spin and the star pointer, as the Luma at the start of the game does. |
| `stars` | `0` | Power stars owned. |
| `grand_stars` | `0` | Grand stars, 0 to 7. |
| `flags` | None | Game event flags to switch on, by the game's names, such as `"PowerStarGet"`. |
| `values` | None | Game event values by name, such as `{ "LibraryOpenNewStarCount": 0 }`. |
| `tico_seeds` | None | Star bits already given to each Hungry Luma, by its seed number (the luma's placement argument 7), such as `{ "4": 50 }`. A luma given its full count is fed when its stage loads. |

## Stage

```json
"stage": { "name": "EggStarGalaxy", "scenario": 1 }
```

The game enters the stage the way a file starts a galaxy. The stage is ready, and the clock starts, when its scene has started, its objects exist, no cutscene plays, and the player can move. A stage that is not ready within the time limit ends the test as `setup-failed`, with the condition that held it, such as `ready: MarioModule::isInputDisable still true`.

## Player

Applied after the stage is ready, then read back.

| Item | Description |
|---|---|
| `position` | `[x, y, z]` in game units. A glTF meter is 100 units. |
| `at` | `"NAME#N"`: the N-th object of that name, counted from 0 in the order the game made them. The player is placed 150 units above it, against its gravity. `"class:CLASS#N"` counts objects of a class instead. |
| `form` | A form by the game's name: `normal`, `fire`, `ice`, `bee`, `boo`, `spring`, `flying`, `rainbow` or `tornado`. |
| `life` | Health, from 1 to the game's maximum. |

## The test clock

Frame 0 is the first game frame after every setup item is set and read back. The clock counts game frames, one fixed step each, so a test runs the same on a slow computer and a fast one. A test with no `stage` starts its clock on the first frame of the title screen; with `"clock": "start"` it starts on the game's first frame, for what the game shows before the title (a window about the save).

With `"time": "real"`, the game steps on the computer's own clock, as when you play, and the display paces the pictures. Steps still count game frames, but two runs no longer match frame for frame. Use it only to measure pacing.

## Steps

Each step has `at` (a frame) or `after` (a condition), and exactly one action. Steps on the same frame run in the order written. Input steps can overlap: the buttons held are all the buttons of every input step that is running.

### Waiting for a condition

`after` waits for a fact or a log line, then runs the step on the first frame the condition holds:

```json
{ "after": { "fact": "mod.gold-throw.ready", "is": true }, "timeout": 600, "picture": "ready" }
{ "after": { "log": "boss defeated" }, "end": true }
```

A fact condition takes any check from [Fact checks](#fact-checks) except `far`. With `"when": "next"`, only a value written after the wait began counts. `timeout` is in frames, 600 by default. A condition that does not hold in time fails the test and names the step.

### Actions

| Action | Example | Description |
|---|---|---|
| `hold` | `{"hold": ["A", "B"], "frames": 10}` | Holds buttons for a number of frames: `A`, `B`, `Z`, `C`, `PLUS`, `MINUS`, `ONE`, `TWO`, `HOME`, `UP`, `DOWN`, `LEFT`, `RIGHT`. |
| `stick` | `{"stick": [0, 1], "frames": 30}` | Tilts the main stick. Each value is from -1 to 1. |
| `shake` | `{"shake": 10}` | Shakes for a number of frames, which spins. |
| `pointer` | `{"pointer": [0.5, 0.5], "frames": 30}` | Moves the pointer, from 0 to 1 across the screen. |
| `key` | `{"key": "enter"}` | Presses a key in Nebula's own menus: `up`, `down`, `left`, `right`, `enter`, `esc`, `tab`, `space` or `backspace`. While a mod's text box is open, `enter`, `esc`, `backspace`, `left` and `right` edit it. |
| `text` | `{"text": "Gold Mario"}` | Types into the open text box, 1 to 256 characters. |
| `settings` | `{"settings": "mods"}` | Opens Nebula's settings on a page: `display`, `graphics`, `controls`, `audio`, `general`, `mods`, `system` or `advanced`. Then `key` steps move in it. |
| `picture` | `{"picture": "after-spin"}` | Saves the next frame shown as `pictures/NAME.png`. Each name is used once in a test. |
| `call` | `{"call": "MR::getPlayerPos", "signature": "Vec3f *()", "args": [], "fact": "pos"}` | Calls a game function by name. See [Calling game functions](#calling-game-functions). |
| `read` | `{"read": "Mario.mVelocity", "of": "player", "type": "TVec3f", "fact": "speed"}` | Reads a field by name, from `"player"` or `"object:NAME#N"`, and records it as a fact. Types: `f32`, `s32`, `u32`, `u16`, `s16`, `u8`, `bool`, `TVec3f`, `Vec3f` and `const char *`. |
| `stage` | `{"stage": {"name": "HeavensDoorGalaxy", "scenario": 1}}` | Changes stage. The fact `stage.ready` is written when the new stage is ready. The clock keeps running. |
| `new_file` | `{"new_file": 2}` | Creates save file 1, 2 or 3 and makes it the current file. Writes `file.created`. |
| `load_file` | `{"load_file": 2}` | Loads a save file as the file select does. Writes `file.loaded`. |
| `copy_file` | `{"copy_file": [1, 2]}` | Copies one save file onto another. Writes `file.copied`. |
| `erase_file` | `{"erase_file": 2}` | Erases a save file. Writes `file.erased`. |
| `take_out` | `{"take_out": "gold-throw"}` | Takes a running mod out of the game. The next step waits until it is gone, unless the step has `"wait": false`. |
| `reload` | `{"reload": "gold-throw"}` | Reloads a running mod's code from its installed folder, as dev mode does. The next step waits until the reload ends, up to 600 frames, unless the step has `"wait": false`. The fact `reload.MOD-ID` holds the result. |
| `forget` | `{"forget": "gold-throw"}` | Presses **FORGET** on a removed mod in the **MODS** page. |
| `wait` | `{"wait": true}` | Does nothing. Use it to hang an `after` on. |
| `window` | `{"window": [960, 540]}` | Resizes the inside of the game's window, as a player dragging its edge does: `[width, height]` in pixels, each 1 to 65535. |
| `lose_device` | `{"lose_device": true}` | Loses the graphics device at the renderer's next frame, as a driver reset does. The game must make it again and go on. |
| `end` | `{"end": true}` | Ends the test on this frame. |

### Calling game functions

`signature` is the function's type as the game writes it, such as `bool()` or `void(s32, const char *)`. `args` holds one value for each parameter:

* Numbers, and `true` or `false`. Each is checked against the parameter's type.
* Strings, for `const char *`.
* `[x, y, z]`, for `Vec3f` or `TVec3f` by value, reference or pointer. A position parameter also takes `"object:NAME#N"` or `"class:CLASS#N"`, which passes that object's position.
* `"player"`, `"object:NAME#N"` or `"class:CLASS#N"`, for an object.

The result becomes the fact named by `fact`. Results can be numbers, `bool`, `const char *`, `Vec3f` or `TVec3f`, `void`, or another pointer, which records whether there is one. Any other type stops the test before it runs, naming the type. A call line has at most 16 parameters and 1,023 bytes.

## Facts

A fact is a value with a key and a frame. The run writes them to `facts.jsonl`, one per line: `{"frame": 120, "key": "mod.gold-throw.balls_thrown", "value": 1}`.

| Key | Written when |
|---|---|
| `setup.save.ITEM`, `setup.file`, `setup.stage`, `setup.player.ITEM`, `setup.language`, `setup.seed` | Each setup item is read back. |
| `setup.done` | Frame 0. |
| `stage.ready` | A stage is ready: `{"name", "scenario", "frame"}`. |
| `mod.ID.loaded` | `true` when the mod started, or the message it was refused with. |
| `guard.ID` | The mod faulted. |
| `mod.ID.KEY` | The mod logged `fact KEY VALUE`, where VALUE is JSON. |
| The `fact` of a step | The step runs. |
| `import.FILE` | The import screen shows a line about that file: `{"line", "text", "code", "file", "at"}`. |
| `modlog` | The mod log shows a line: `{"mod", "file", "line", "text"}`. |
| `modscreen.ID` | The **MODS** page shows a line about that mod. |
| `input.motor` | The controller's rumble changes, from 0 to 255, with `"device": "pad"`. |
| `file.created`, `file.loaded`, `file.copied`, `file.erased` | A save-file step finishes. |

A mod writes its own facts through its log. In LumaScript:

```lumascript
log fact "balls_thrown" thrown
```

The fact is then `mod.ID.balls_thrown`. A line that starts with `fact ` but whose value is not JSON fails the test.

The `import`, `modlog` and `modscreen` facts come from the code that draws those lines, as it draws them, so a check on them checks what a player sees.

## Checks

The runner checks `expect` after the run, from the files the run left.

### Fact checks

```json
{ "fact": "KEY", "OP": VALUE, "when": "last", "frames": [100, 200] }
```

| OP | Holds when the value |
|---|---|
| `is` | Equals VALUE. Strings, numbers, lists and objects compare as JSON. |
| `is_not` | Does not equal VALUE. |
| `less`, `at_most`, `more`, `at_least` | Compares with the number VALUE. |
| `near` | Is within `within` of VALUE: a number, or `[x, y, z]` by distance. |
| `far` | Is more than `beyond` from VALUE: a number, or `[x, y, z]` by distance. Not in `after`. |
| `has` | Is an object that holds every key of VALUE with the same value. |
| `exists` | Was written at all (VALUE is `true`). |
| `absent` | Was never written (VALUE is `true`). |
| `count` | Was written exactly VALUE times. |

For `near` and `far`, a value that is not a number or `[x, y, z]` is neither near nor far, so the check fails.

| Key | Description |
|---|---|
| `when` | `last` (the default), `first`, `any` or `all` of the writes. Not used with `count`. |
| `within`, `beyond` | The distance for `near` and for `far`. Each goes only with its own check. |
| `frames` | Only writes from frame A to frame B count. |
| `matching` | Only writes whose value holds this object count, as `has` reads it. |
| `field` | Reads one member of each write's object, as a dotted path such as `"at.x"`. |
| `systems` | The check holds only on these systems, and is listed as not checked on others. |
| `scope` | `"installation"` reads what every installed mod adds to, not only the test's own mods. |

### Log checks

| Check | Holds when |
|---|---|
| `{"log": "PATTERN"}` | A line of the game's log matches the regular expression. With `"times": N`, exactly N lines match. |
| `{"no_log": "PATTERN"}` | No line matches. |

### Save file checks

| Check | Holds when |
|---|---|
| `{"save_file": "RMGE/Nebula.sav", "is": "same"}` | At the end, the file is as the test's `saves` folder placed it. |
| `{"save_file": "RMGE/Nebula.sav.1", "is": "absent"}` | At the end, the save folder has no such file. |

The path is inside the run's save folder, with forward slashes.

### Picture checks

| Check | Holds when |
|---|---|
| `{"picture": "NAME", "like": "refs/NAME.png", "within": {"rmse": 6.0}}` | The picture differs from the reference by at most this RMSE. |
| `{"picture": "NAME", "unlike": "refs/NAME.png", "beyond": {"rmse": 6.0}}` | The picture differs by more than this. |
| `{"picture": "NAME", "box": [x, y, w, h], "mean_rgb": [r, g, b], "within": 12}` | The box's average color is within this distance of `[r, g, b]`. |

`like` and `unlike` also take `"box": [x, y, w, h]` to compare part of the picture. A reference path can hold `{renderer}`, which becomes the run's renderer, so each renderer has its own references. A picture and a reference of different sizes fail the check. A missing reference stops the test.

### Sound checks

`{"sound": [FIRST_FRAME, LAST_FRAME], "rms": [LOW, HIGH]}` holds when the loudness of the run's sound over those frames, in dBFS, is between LOW and HIGH.

### Checks every test makes

Every test also checks, without being told:

* The run ended at its `end` step, not by a crash, a hang or the time limit.
* The game quit by itself with exit code 0, and wrote no crash dump.
* Every mod in `mods` loaded, except those with `"loads": false`.
* No mod faulted, except those in `faults`.
* Every setup item was set and read back.
* Nothing the game asked for was lost: the counts the game logs at its exit on its `[loss]` line (a draw with no material, a texture drawn blank, a sound read past its end) are all 0.
* Every refusal of a mod's file was given in words, not only as a code.

With `"ends": "crash"`, these become: the game crashed before the `end` step, wrote a crash dump, and quit with a code other than 0. With `"ends": "crash-at-stop"`: the run reached its `end` step, then the game crashed before it quit, with a code other than 0. The `[loss]` check is made only for `"ends": "end"`.

## Results

Each test and renderer leaves a folder in the work folder, `TEST/RENDERER/`:

| File | Contents |
|---|---|
| `result.json` | The verdict, the reason, each check with what was found, the setup, the frames and seconds, and the computer's system and GPU. |
| `facts.jsonl` | Every fact. |
| `run.log` | The game's log. |
| `pictures/NAME.png` | The pictures the steps took. |
| `audio.wav` | The run's sound. |
| `test.json` | The test as run. |
| `bin/`, `*.dmp` | The game's files and the crash dump, kept only when the game crashed. |

| Verdict | Meaning |
|---|---|
| `pass` | Every check held. |
| `fail` | A check did not hold. The first failed check is the reason, and every check is listed. |
| `setup-failed` | A setup item did not take. The test shows nothing either way. |
| `not-run` | The test needs another disc's game (`disc`) and none was given. The run does not pass. |
| `crashed` | The game crashed. The result names the dump and the last lines of the log. |
| `timeout` | The run reached `limit_seconds` and the game had never taken a step. |
| `hung` | The game's process was alive and it completed no step for 120 seconds. The result names the step it stood at and the dump the game wrote, if any. |
| `not-reached` | The run reached `limit_seconds` with the game stepping at 80% of full speed or more (48 steps a second), and its end step was not reached. The machine was not the reason. |
| `too-slow` | The run reached `limit_seconds` with the game stepping at under 80% of full speed. The gate runs it again alone: it passes there (a flake), or it is `fail`. |
| `output-missing` | The game ended, and a picture a step asked for, or the loss report, was still not there 60 seconds later. |
| `no-exit-record` | On a device: Android's record of how the game's process ended never came. |
| `harness-error` | The test file is wrong, or the runner had a problem. Never the mod's fault. |

`result.json` also says the kind of the result, `"kind"`: `pass`; `fail` (the game or the mod is wrong: `fail`, `crashed`, `timeout`, `hung`, `not-reached`); `tool` (the test tool is wrong: `harness-error`, `setup-failed`, `output-missing`, `no-exit-record`); or `env` (the machine was not fit: `not-run`, a device that left, a drive with no room, `too-slow`). Only `fail` says something about the game.

`lumalink test` prints one line for each test and renderer, and exits with 0 when every verdict is `pass`. The runner itself ends with 3, and writes no result, when the machine is not fit to start (the run lock is held, the device does not answer).

## Further reading

* [Testing a mod](../how-tos/testing-a-mod.md)
* [lumalink test](lumalink-commands/test.md)
* [Reading the mod log](../how-tos/reading-the-mod-log.md)
