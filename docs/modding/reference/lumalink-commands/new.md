---
title: lumalink new
shortTitle: new
intro: Start a mod - a folder with its luma.json and an empty base layer.
contentType: reference
---

<!-- Written by tools/modding/lumalink_commands.py from LumaLink's help. Do not edit by hand. -->

## About lumalink new

Start a mod: a folder with its luma.json and an empty base layer.

## Example

```shell
lumalink new moon-jump --id moon.jump --name "Moon jump" --summary "Hold jump to rise." --author me
```

## Usage

```text
lumalink new --id <ID> --name <NAME> --summary <TEXT> --author <AUTHOR>... [OPTIONS] <FOLDER>
```

## Arguments

| Argument | Description |
|---|---|
| `<FOLDER>` | The folder to make; it must not exist. |

## Options

| Option | Description |
|---|---|
| `--id <ID>` | The mod's id: 3 to 64 of a-z, 0-9, '-' and '.', starting with a letter. |
| `--name <NAME>` | The mod's name, as players see it. |
| `--summary <TEXT>` | One line on what the mod does. |
| `--author <AUTHOR>` | Who made it (repeat for several). |
| `-h, --help` | Print this help. |
| `--language <LANGUAGE>` | The language LumaLink and the validator speak: one of the game's 14, by its name in the game (UsEnglish, EuGerman, JpJapanese...). Default: UsEnglish. |

Every message and help text is available in each of the game's 14 languages, chosen with `--language`.

## Further reading

* [Quickstart for Nebula mods](../../get-started/quickstart.md)
* [luma.json](../luma-json.md)
