---
title: lumalink lsp
shortTitle: lsp
intro: The editor server - the Language Server Protocol over stdin and stdout for a mod's LumaScript (errors, completion, hover, go to definition, rename).
contentType: reference
---

<!-- Written by tools/modding/lumalink_commands.py from LumaLink's help. Do not edit by hand. -->

## About lumalink lsp

The editor server: the Language Server Protocol over stdin and stdout for a mod's LumaScript (errors, completion, hover, go to definition, rename).

## Example

```shell
lumalink lsp
```

## Usage

```text
lumalink lsp [OPTIONS]
```

## Options

| Option | Description |
|---|---|
| `--names <FOLDER>` | The game's self-description folder (function rows and types), for the rules on a mod's code. Default: the installed game's. |
| `--sdk <FOLDER>` | The folder with the game's luma.h, for the services' functions. Default: LUMA_INCLUDE, then the self-description folder's sdk. |
| `--no-installed` | Leave out the installed game: only what --game and --names name is read, and the rules they would check are named as not checked. |
| `-h, --help` | Print this help. |
| `--language <LANGUAGE>` | The language LumaLink and the validator speak: one of the game's 14, by its name in the game (UsEnglish, EuGerman, JpJapanese...). Default: UsEnglish. |

Every message and help text is available in each of the game's 14 languages, chosen with `--language`.

## Further reading

* [Setting up an editor](../../how-tos/setting-up-an-editor.md)
