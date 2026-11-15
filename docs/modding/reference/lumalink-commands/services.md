---
title: lumalink services
shortTitle: services
intro: List every service function of the game's SDK and whether scripts can call it - script, native (C mods only, by design) or c (C mods only).
contentType: reference
---

<!-- Written by tools/modding/lumalink_commands.py from LumaLink's help. Do not edit by hand. -->

## About lumalink services

List every service function of the game's SDK and whether scripts can call it: script, native (C mods only, by design) or c (C mods only).

## Example

```shell
lumalink services
```

## Usage

```text
lumalink services [OPTIONS]
```

## Options

| Option | Description |
|---|---|
| `--sdk <FOLDER>` | The folder with the game's luma.h. Default: LUMA_INCLUDE, then the installed game's sdk. |
| `--unreachable` | Only what scripts cannot reach. |
| `-h, --help` | Print this help. |
| `--language <LANGUAGE>` | The language LumaLink and the validator speak: one of the game's 14, by its name in the game (UsEnglish, EuGerman, JpJapanese...). Default: UsEnglish. |

Every message and help text is available in each of the game's 14 languages, chosen with `--language`.
