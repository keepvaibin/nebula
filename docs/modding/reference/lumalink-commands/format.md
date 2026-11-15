---
title: lumalink format
shortTitle: format
intro: Format LumaScript files - four spaces of indent for each open bracket, at most one blank line, LF line ends. Strings and comments are not changed.
contentType: reference
---

<!-- Written by tools/modding/lumalink_commands.py from LumaLink's help. Do not edit by hand. -->

## About lumalink format

Format LumaScript files: four spaces of indent for each open bracket, at most one blank line, LF line ends. Strings and comments are not changed.

## Example

```shell
lumalink format moon-jump/source
```

## Usage

```text
lumalink format [OPTIONS] <PATH>...
```

## Options

| Option | Description |
|---|---|
| `--check` | Write nothing: name each file not in the style, and exit 1 if there is one. |
| `-h, --help` | Print this help. |
| `--language <LANGUAGE>` | The language LumaLink and the validator speak: one of the game's 14, by its name in the game (UsEnglish, EuGerman, JpJapanese...). Default: UsEnglish. |

Every message and help text is available in each of the game's 14 languages, chosen with `--language`.
