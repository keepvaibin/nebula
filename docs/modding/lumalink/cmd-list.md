# lumalink list
State: generated from the tool's own help.

List what is in a .luma, or (with --game) a kind of thing in your imported game.

## Smallest example

```sh
lumalink list moon.jump-0.1.0.luma
```

## Reference

```text
List what is in a .luma, or (with --game) a kind of thing in your imported game.

Usage: lumalink list [OPTIONS] <ITEM> [FILTER]

Arguments:
  [FILTER]  Only those whose path or column holds this (tables, models); the folder (files); the language (messages).
  <ITEM>    A .luma file; or, with --game: tables, models, messages or files.

Options:
      --game <FILE>          Your imported game (Nebula's game.pak): its files, tables and keys. Default: the installed game.
      --self <FILE>          The game's self-description, for the names of table columns.
  -h, --help                 Print this help.
      --language <LANGUAGE>  The language LumaLink and the validator speak: one of the game's 14, by its name in the game (UsEnglish, EuGerman, JpJapanese...). Default: UsEnglish.
```

## Limits

Messages and help in any of the game's 14 languages (`--language`).

## See also

format/luma.md
