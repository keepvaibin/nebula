---
title: lumalink convert
shortTitle: convert
intro: Convert a published mod (Riivolution pack, disc-layout folder, loose files, texture pack, Gecko list) to a mod folder, each file cooked as the game cooks it.
contentType: reference
---

<!-- Written by tools/modding/lumalink_commands.py from LumaLink's help. Do not edit by hand. -->

## About lumalink convert

Convert a published mod (Riivolution pack, disc-layout folder, loose files, texture pack, Gecko list) to a mod folder, each file cooked as the game cooks it.

## Example

```shell
lumalink convert MyRiivolutionMod moon-jump
```

## Usage

```text
lumalink convert [OPTIONS] <PATH> <FOLDER>
```

## Arguments

| Argument | Description |
|---|---|
| `<FOLDER>` | The mod folder to write; it must not exist. |
| `<PATH>` | The mod: a folder, an archive (.zip, .7z, .rar) or a file. |

## Options

| Option | Description |
|---|---|
| `--game <FILE>` | Your imported game (Nebula's game.pak): files are placed and textures named against it. |
| `--name <NAME>` | The mod's name, in place of the pack's or the file's. |
| `--author <AUTHOR>` | Who made it (repeat for several), in place of the pack's authors. |
| `--accept-losses` | Write the mod without the files the game cannot use, naming each. |
| `-h, --help` | Print this help. |
| `--language <LANGUAGE>` | The language LumaLink and the validator speak: one of the game's 14, by its name in the game (UsEnglish, EuGerman, JpJapanese...). Default: UsEnglish. |

Every message and help text is available in each of the game's 14 languages, chosen with `--language`.

## Further reading

* [Converting existing mods](../../how-tos/converting-existing-mods.md)
