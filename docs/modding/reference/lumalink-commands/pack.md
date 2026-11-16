---
title: lumalink pack
shortTitle: pack
intro: Write a mod folder as a .luma, after the validator passes it with every rule checked.
contentType: reference
---

## About lumalink pack

Write a mod folder as a .luma, after the validator passes it with every rule checked.

## Example

```shell
lumalink pack moon-jump
```

## Usage

```text
lumalink pack [OPTIONS] <FOLDER>
```

## Arguments

| Argument | Description |
|---|---|
| `<FOLDER>` | The mod folder. |

## Options

| Option | Description |
|---|---|
| `-o, --output <FILE>` | The .luma to write. Default: <id>-<version>.luma beside the folder. |
| `--names <FOLDER>` | The game's self-description folder (function rows and types), for the rules on a mod's code. Default: the installed game's. |
| `--platform <PLATFORM>` | A platform to check the code for (repeat for several). Default: all nine. |
| `--game <FILE>` | Your imported game (Nebula's game.pak): its files, tables and keys. Default: the installed game. |
| `--no-installed` | Leave out the installed game: only what --game and --names name is read, and the rules they would check are named as not checked. |
| `--mods <FOLDER>` | Your mod folder: the mods (folders and .luma files) a mod's needs are found in. Without it, they are looked for beside the mod, in the installed game's mods and in the mods that come with the game. |
| `-h, --help` | Print this help. |
| `--language <LANGUAGE>` | The language LumaLink and the validator speak: one of the game's 14, by its name in the game (UsEnglish, EuGerman, JpJapanese...). Default: UsEnglish. |

Every message and help text is available in each of the game's 14 languages, chosen with `--language`.
Findings carry the validator's codes, the same ones the game shows.

## Further reading

* [Packing and sharing a mod](../../how-tos/packing-and-sharing-a-mod.md)
