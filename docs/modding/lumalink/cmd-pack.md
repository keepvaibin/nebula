# lumalink pack
State: generated from the tool's own help.

Write a mod folder as a .luma, after the validator passes it with every rule checked.

## Smallest example

```sh
lumalink pack moon-jump
```

## Reference

```text
Write a mod folder as a .luma, after the validator passes it with every rule checked.

Usage: lumalink pack [OPTIONS] <FOLDER>

Arguments:
  <FOLDER>  The mod folder.

Options:
  -o, --output <FILE>        The .luma to write. Default: <id>-<version>.luma beside the folder.
      --names <FOLDER>       The game's self-description folder (function rows and types), for the rules on a mod's code. Default: the installed game's.
      --platform <PLATFORM>  A platform to check the code for (repeat for several). Default: all nine.
      --game <FILE>          Your imported game (Nebula's game.pak): its files, tables and keys. Default: the installed game.
      --no-installed         Leave out the installed game: only what --game and --names name is read, and the rules they would check are named as not checked.
      --mods <FOLDER>        Your mod folder: the mods (folders and .luma files) a mod's needs are found in. Without it needs are not checked.
  -h, --help                 Print this help.
      --language <LANGUAGE>  The language LumaLink and the validator speak: one of the game's 14, by its name in the game (UsEnglish, EuGerman, JpJapanese...). Default: UsEnglish.
```

## Limits

Messages and help in any of the game's 14 languages (`--language`). Findings carry the validator's codes, the same the game shows (`lumalink explain`).

## See also

format/luma.md
