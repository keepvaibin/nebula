---
title: lumalink verify
shortTitle: verify
intro: Check that a .luma is exactly what its folder packs to.
contentType: reference
---

<!-- Written by tools/modding/lumalink_commands.py from LumaLink's help. Do not edit by hand. -->

## About lumalink verify

Check that a .luma is exactly what its folder packs to.

## Example

```shell
lumalink verify moon-jump.luma --source moon-jump
```

## Usage

```text
lumalink verify --source <FOLDER> [OPTIONS] <FILE>
```

## Arguments

| Argument | Description |
|---|---|
| `<FILE>` | The .luma file. |

## Options

| Option | Description |
|---|---|
| `--source <FOLDER>` | The mod folder it was packed from. |
| `-h, --help` | Print this help. |
| `--language <LANGUAGE>` | The language LumaLink and the validator speak: one of the game's 14, by its name in the game (UsEnglish, EuGerman, JpJapanese...). Default: UsEnglish. |

Every message and help text is available in each of the game's 14 languages, chosen with `--language`.
