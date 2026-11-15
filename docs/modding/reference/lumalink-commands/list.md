---
title: lumalink list
shortTitle: list
intro: List what is in a .luma, or a kind of thing in your imported game.
contentType: reference
---

<!-- Written by tools/modding/lumalink_commands.py from LumaLink's help. Do not edit by hand. -->

## About lumalink list

List what is in a .luma, or a kind of thing in your imported game.

## Example

```shell
lumalink list moon-jump.luma
lumalink list tables --game game.pak Kuribo
lumalink list sounds JUMP
```

## Usage

```text
lumalink list [OPTIONS] <ITEM> [FILTER]
```

## Arguments

| Argument | Description |
|---|---|
| `<ITEM>` | A .luma file, or what to list from your imported game: tables, models, messages, sounds or files. |

## Options

| Option | Description |
|---|---|
| `--game <FILE>` | Your imported game (Nebula's game.pak): its files, tables and keys. Default: the installed game. |
| `--self <FILE>` | The game's self-description, for the names of table columns. |
| `-h, --help` | Print this help. |
| `--language <LANGUAGE>` | The language LumaLink and the validator speak: one of the game's 14, by its name in the game (UsEnglish, EuGerman, JpJapanese...). Default: UsEnglish. |

Every message and help text is available in each of the game's 14 languages, chosen with `--language`.
