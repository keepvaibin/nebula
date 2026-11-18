---
title: lumalink check
shortTitle: check
intro: Check a mod folder or a .luma with the game's own validator and list every problem.
contentType: reference
---

<!-- Written by tools/modding/lumalink_commands.py from LumaLink's help. Do not edit by hand. -->

## About lumalink check

Check a mod folder or a .luma with the game's own validator and list every problem.

## Example

```shell
lumalink check moon-jump
```

## Usage

```text
lumalink check [OPTIONS] <PATH>
```

## Arguments

| Argument | Description |
|---|---|
| `<PATH>` | A mod folder or a .luma file. |

## Options

| Option | Description |
|---|---|
| `--names <FOLDER>` | The game's self-description folder (function rows and types), for the rules on a mod's code. Default: the installed game's. |
| `--platform <PLATFORM>` | A platform to check the code for (repeat for several). Default: all nine. |
| `--game <FILE>` | Your imported game (Nebula's game.pak): its files, tables and keys. Default: the installed game. |
| `--no-installed` | Leave out the installed game: only what --game and --names name is read, and the rules they would check are named as not checked. |
| `--mods <FOLDER>` | Your mod folder: the mods (folders and .luma files) a mod's needs are found in. Without it, they are looked for beside the mod, in the installed game's mods and in the mods that come with the game. |
| `--json` | Print the report as JSON. |
| `-h, --help` | Print this help. |
| `--language <LANGUAGE>` | The language LumaLink and the validator speak: one of the game's 14, by its name in the game (UsEnglish, EuGerman, JpJapanese...). Default: UsEnglish. |

Every message and help text is available in each of the game's 14 languages, chosen with `--language`.
Findings carry the validator's codes, the same ones the game shows.

## Further reading

* [Validator rules](../validator-rules.md)
