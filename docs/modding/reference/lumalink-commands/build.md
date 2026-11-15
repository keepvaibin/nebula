---
title: lumalink build
shortTitle: build
intro: Build a mod's code (LumaScript and C) for every platform.
contentType: reference
---

<!-- Written by tools/modding/lumalink_commands.py from LumaLink's help. Do not edit by hand. -->

## About lumalink build

Build a mod's code (LumaScript and C) for every platform.

## Example

```shell
lumalink build moon-jump
```

## Usage

```text
lumalink build [OPTIONS] <FOLDER>
```

## Arguments

| Argument | Description |
|---|---|
| `<FOLDER>` | The mod folder. |

## Options

| Option | Description |
|---|---|
| `--names <FOLDER>` | The game's self-description folder (function rows and types), for the rules on a mod's code. Default: the installed game's. |
| `--sdk <FOLDER>` | The folder with the game's luma.h. Default: LUMA_INCLUDE, then the self-description folder's sdk. |
| `--clang <FILE>` | The clang to build with. Default: LUMALINK_CLANG, then the clang that comes with LumaLink, then LLVM's own folder, then the system's search path. |
| `--work <FOLDER>` | Keep the generated C in this folder. Default: a temporary folder, removed after. |
| `--platform <PLATFORM>` | A platform to build for (repeat for several). Default: all nine. |
| `-h, --help` | Print this help. |
| `--language <LANGUAGE>` | The language LumaLink and the validator speak: one of the game's 14, by its name in the game (UsEnglish, EuGerman, JpJapanese...). Default: UsEnglish. |

Every message and help text is available in each of the game's 14 languages, chosen with `--language`.

## Further reading

* [About LumaScript](../../concepts/lumascript.md)
