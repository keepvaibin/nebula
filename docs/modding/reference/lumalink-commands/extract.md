---
title: lumalink extract
shortTitle: extract
intro: Write files out of a .luma, or out of your imported game.
contentType: reference
---

## About lumalink extract

Write files out of a .luma, or out of your imported game.

## Example

```shell
lumalink extract moon-jump.luma --output moon-jump-files
lumalink extract LayoutData/CoinCounter.arc --output game-files
```

## Usage

```text
lumalink extract --output <FOLDER> [OPTIONS] <PATH> [ENTRY]...
```

## Arguments

| Argument | Description |
|---|---|
| `<PATH>` | A .luma file, or a path in your imported game. [ENTRY]...  Entries or folders of the .luma to write (all when none). |

## Options

| Option | Description |
|---|---|
| `-o, --output <FOLDER>` | The folder to write into. |
| `--game <FILE>` | Your imported game (Nebula's game.pak): its files, tables and keys. Default: the installed game. |
| `--self <FILE>` | The game's self-description, for the names of table columns. |
| `-h, --help` | Print this help. |
| `--language <LANGUAGE>` | The language LumaLink and the validator speak: one of the game's 14, by its name in the game (UsEnglish, EuGerman, JpJapanese...). Default: UsEnglish. |

Every message and help text is available in each of the game's 14 languages, chosen with `--language`.
