---
title: lumalink update
shortTitle: update
intro: Look for a newer LumaLink and install it. On Linux and macOS, print where to download it.
contentType: reference
---

<!-- Written by tools/modding/lumalink_commands.py from LumaLink's help. Do not edit by hand. -->

## About lumalink update

Look for a newer LumaLink and install it. On Linux and macOS, print where to download it.

## Example

```shell
lumalink update
```

## Usage

```text
lumalink update [OPTIONS]
```

## Options

| Option | Description |
|---|---|
| `--check` | Only say whether a newer version is out. |
| `--check-at-start <KEY=VALUE>` | Whether LumaLink looks for updates by itself (the window when it opens, a command at most once a day): on or off. [possible values: on, off] |
| `-h, --help` | Print this help. |
| `--language <LANGUAGE>` | The language LumaLink and the validator speak: one of the game's 14, by its name in the game (UsEnglish, EuGerman, JpJapanese...). Default: UsEnglish. |

Every message and help text is available in each of the game's 14 languages, chosen with `--language`.
