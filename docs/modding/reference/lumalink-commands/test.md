---
title: lumalink test
shortTitle: test
intro: Build the mod, run each tests/**/test.json in the game (hidden, silent) and print each check with what was found.
contentType: reference
---

<!-- Written by tools/modding/lumalink_commands.py from LumaLink's help. Do not edit by hand. -->

## About lumalink test

Build the mod, run each tests/**/test.json in the game (hidden, silent) and print each check with what was found.

## Example

```shell
lumalink test moon-jump
```

## Usage

```text
lumalink test [OPTIONS] <FOLDER>
```

## Arguments

| Argument | Description |
|---|---|
| `<FOLDER>` | The mod folder. |

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

## Options

| Option | Description |
|---|---|
| `--bin <FOLDER>` | A game build's folder with the harness's parts (the host extension and the game half). Default: LUMA_TEST_BIN, then the installed game's version folder. |
| `--runner <FILE>` | The harness's runner (luma_test.py). Default: LUMA_TEST_RUNNER, then the test kit's tools/lumacore/luma_test.py. With a --bin build folder that has its own test parts: the one in its luma/test, then tools/lumacore/luma_test.py above it. |
| `--python <FILE>` | The Python that runs it. Default: LUMALINK_PYTHON, then python. |
| `--work <FOLDER>` | Where the runs are left. Default: <folder>-tests beside the mod. |
| `--lock <FILE>` | The run lock. Default: LUMA_TEST_LOCK, then run.lock in the work folder. |
| `--renderer <RENDERER>` | d3d12 or vulkan. Default: the runner's. |
| `--game <FILE>` | Your imported game (Nebula's game.pak), the one each run plays. Default: the game.pak of the install the bin folder belongs to, then the installed game's. |
| `-h, --help` | Print this help. |
| `--language <LANGUAGE>` | The language LumaLink and the validator speak: one of the game's 14, by its name in the game (UsEnglish, EuGerman, JpJapanese...). Default: UsEnglish. |

Every message and help text is available in each of the game's 14 languages, chosen with `--language`.

## Further reading

* [Testing a mod](../../how-tos/testing-a-mod.md)
* [Test files](../test-files.md)
