# lumalink extract
State: generated from the tool's own help.

Write files out of a .luma, or (with --game) out of your imported game.

## Smallest example

```sh
lumalink extract moon.jump-0.1.0.luma -o moon-jump-out
```

## Reference

```text
Write files out of a .luma, or (with --game) out of your imported game.

Usage: lumalink extract --output <FOLDER> [OPTIONS] <PATH> [ENTRY]...

Arguments:
  <PATH>      A .luma file; or, with --game, a path in the game.
  [ENTRY]...  Entries or folders of the .luma to write (all when none).

Options:
  -o, --output <FOLDER>      The folder to write into.
      --game <FILE>          Your imported game (Nebula's game.pak): its files, tables and keys. Default: the installed game.
      --self <FILE>          The game's self-description, for the names of table columns.
  -h, --help                 Print this help.
      --language <LANGUAGE>  The language LumaLink and the validator speak: one of the game's 14, by its name in the game (UsEnglish, EuGerman, JpJapanese...). Default: UsEnglish.
```

## Limits

Messages and help in any of the game's 14 languages (`--language`).

## See also

format/luma.md
