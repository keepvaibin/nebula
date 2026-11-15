---
title: lumalink explain
shortTitle: explain
intro: Say what a validator code (L203, or 203) means, in a language.
contentType: reference
---

<!-- Written by tools/modding/lumalink_commands.py from LumaLink's help. Do not edit by hand. -->

## About lumalink explain

Say what a validator code (L203, or 203) means, in a language.

## Example

```shell
lumalink explain L505
```

## Usage

```text
lumalink explain [OPTIONS] <CODE>
```

## Arguments

| Argument | Description |
|---|---|
| `<CODE>` | The code: L203 or 203. |

## Options

| Option | Description |
|---|---|
| `-h, --help` | Print this help. |
| `--language <LANGUAGE>` | The language LumaLink and the validator speak: one of the game's 14, by its name in the game (UsEnglish, EuGerman, JpJapanese...). Default: UsEnglish. |

Every message and help text is available in each of the game's 14 languages, chosen with `--language`.
Findings carry the validator's codes, the same ones the game shows.

## Further reading

* [Validator rules](../validator-rules.md)
