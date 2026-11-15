---
title: Testing a mod
shortTitle: Test a mod
intro: Write tests that start the game with your mod, play it for you, and check what happened, then run them with one command.
contentType: how-tos
---

## About mod tests

A test plays the game for you. It sets up a save file, enters a stage, presses buttons on a frame clock, takes pictures, and then checks facts, log lines, pictures and sound. Because every run starts fresh and counts game frames rather than seconds, a test gives the same result every time, on any computer.

Tests live in your mod folder, one folder each, under `tests/`:

```text
moon-jump/
  luma.json
  source/main.lum
  tests/
    jump/
      test.json
      refs/after.png
```

For every key a test can use, see [Test files](../reference/test-files.md).

## Prerequisites

* Nebula, installed with Nebula Setup. `lumalink test` uses the installed game.
* A computer with Windows x64 or Linux x64. The test kit that LumaLink installs runs on these systems only.
* LumaLink. For more information, see [Installing LumaLink](../get-started/installing-lumalink.md).
* Python 3, which runs the tests. If `python` is not on your PATH, set `LUMALINK_PYTHON` to the Python program.

## Writing a test

1. Create a folder for the test in `tests/`, for example `tests/jump`.
1. In it, create `test.json`. This test enters Good Egg Galaxy, jumps once, and checks the mod's log:

   ```json
   {
     "harness": 1,
     "name": "jump",
     "about": "One jump shows 1 in the top left corner",
     "mods": ["../.."],
     "stage": { "name": "EggStarGalaxy", "scenario": 1 },
     "steps": [
       { "at": 30, "hold": ["A"], "frames": 8 },
       { "at": 120, "picture": "after" },
       { "at": 130, "end": true }
     ],
     "expect": [
       { "log": "jump 1" }
     ]
   }
   ```

   * `"mods": ["../.."]` is your mod: the folder two levels up.
   * Frame 0 is the first frame the player can move. `at` counts game frames from there, 60 a second.
   * Every test has exactly one `end` step.

## Running the tests

Run every test in the mod:

```shell
lumalink test moon-jump
```

LumaLink builds the mod, then starts the game once for each test, hidden and silent. It prints each check with what it found:

```text
jump (d3d12): pass
  pass  the run ended at its end step: found 130
  pass  the mod moon-jump loaded: found true
  pass  log matches jump 1: found ["[    2.104] game: [mod moon-jump] jump 1"]
1 of 1 tests passed; the runs are in moon-jump-tests
```

The command exits with 0 when every test passes, so you can run it in CI.

To run on Vulkan instead of Direct3D 12, add `--renderer vulkan`. To run each test on several renderers, list them in the test's `renderers` key.

## Checking what your mod does

### Facts from your mod

Your mod can report any value as a fact. In LumaScript, log it with `log fact`:

```lumascript
log fact "jumps" jumps
```

The test reads it as `mod.ID.jumps`, where ID is your mod's ID:

```json
{ "fact": "mod.moon-jump.jumps", "is": 1 }
```

`when` chooses which write counts: `last` (the default), `first`, `any` or `all`. Numbers can use `less`, `more`, `at_least`, `at_most` and `near`.

### Facts from the game

A `call` step calls a game function by name and records its result. A `read` step reads a field of the player or an object:

```json
{ "at": 120, "call": "MR::getPlayerPos", "signature": "Vec3f *()", "fact": "pos" },
{ "at": 120, "read": "Mario.mVelocity", "of": "player", "type": "TVec3f", "fact": "speed" }
```

### Pictures

A `picture` step saves the frame. Compare it with a reference picture, or check the color of part of it:

```json
{ "picture": "after", "like": "refs/after.png", "within": { "rmse": 6.0 } },
{ "picture": "after", "box": [40, 40, 120, 60], "mean_rgb": [240, 200, 60], "within": 30 }
```

Pictures are 1280×720. A reference path can hold `{renderer}`, as in `refs/{renderer}/after.png`, so each renderer has its own references.

To make the references, run the test once, look at the pictures in the run folder, and copy the right ones into `refs/`.

### Waiting for something

Instead of a fixed frame, a step can wait for a fact or a log line:

```json
{ "after": { "fact": "mod.moon-jump.landed", "is": true }, "timeout": 600, "picture": "landed" }
```

## Setting up the start

| To | Use |
|---|---|
| Start with the spin, stars or game flags | `"save": { "spin": true, "stars": 10, "flags": ["PowerStarGet"] }` |
| Start in a stage and star | `"stage": { "name": "EggStarGalaxy", "scenario": 1 }` |
| Place the player | `"player": { "position": [0, 1150, 0] }` or `"player": { "at": "Kuribo#0" }` |
| Start as a form | `"player": { "form": "fire" }` |
| Set your mod's options | `"mods": [{ "path": "../..", "options": { "Speed": 2 } }]` |
| Deny a permission | `"mods": [{ "path": "../..", "permissions": { "net.internet": "deny" } }]` |
| Use another language | `"language": "EuGerman"` |

Each item is read back from the game before frame 0. An item that did not take ends the test as `setup-failed`, so a test never passes on a setup that did not happen.

## Pressing buttons in menus

In the game's menus, A presses the button that is highlighted or under the pointer. To press a menu button, point at it first, then press A:

```json
{ "at": 60, "hold": ["PLUS"], "frames": 4 },
{ "at": 90, "pointer": [0.371, 0.654], "frames": 40 },
{ "at": 120, "hold": ["A"], "frames": 4 }
```

`pointer` takes the place on the screen from 0 to 1, left to right and top to bottom. Take a `picture` of the menu to find the place. In Nebula's own menus and settings, use `key` steps such as `{"key": "enter"}`.

## Testing a mod that must fail

To check that a broken mod is refused with the right message, set `loads` to `false` and check the message:

```json
"mods": [{ "path": "broken", "loads": false }],
"expect": [{ "log": "broken not started: " }]
```

To check a mod that faults on purpose, list it in `faults`: `"faults": [{"mod": "broken"}]`.

## Reading a failed test

Each run leaves a folder, `TEST/RENDERER/`, in the work folder. By default, the work folder is the mod folder's name with `-tests` added, beside the mod.

* `result.json`: the verdict and every check, with what it found.
* `run.log`: the game's log, including your mod's lines.
* `facts.jsonl`: every fact, with its frame.
* `pictures/`: the pictures the test took.
* `audio.wav`: the run's sound.

| Verdict | What to do |
|---|---|
| `fail` | A check did not hold. Look at what it found, and at the pictures. |
| `setup-failed` | The start did not take. The reason names the item, such as a stage name the game does not have. |
| `crashed` | The game crashed. The result names the crash dump and the last lines of the log. |
| `timeout` | The run took longer than `limit_seconds`. A step's `after` may never have held. |
| `harness-error` | The test file is wrong. The reason names the key and its place. |

## The test kit

The installed game has no test parts. `lumalink test` uses the test kit that LumaLink Setup installs in the `test` folder beside the `lumalink` program. The folder holds a `kit.json` file. To use a kit in another folder, set `LUMA_TEST_KIT` to it.

From the kit, `lumalink test` takes:

* Nebula's test runner, `tools/lumacore/luma_test.py`, run with Python. Give another runner with `--runner` or `LUMA_TEST_RUNNER`, and another Python with `--python` or `LUMALINK_PYTHON`.
* The test extension that the game loads for each run.
* The source of the test's game half, `luma_test.c`. LumaLink builds it against the installed game's SDK before the tests run.

The kit is built for Windows x64 and Linux x64. On other systems, including Windows on ARM, `lumalink test` stops with this message:

```text
this LumaLink has no test kit for this system: lumalink test runs on Windows x64 and Linux x64. Reinstall LumaLink with its Setup, or set LUMA_TEST_KIT
```

When `--bin` names a game build's folder that has its own test extension, `lumalink test` uses that build's parts and no kit.

## Further reading

* [Test files](../reference/test-files.md)
* [lumalink test](../reference/lumalink-commands/test.md)
