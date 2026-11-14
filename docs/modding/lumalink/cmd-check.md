# lumalink check
State: generated from the tool's own help.

Check a mod folder or a .luma with the game's own validator and list every problem.

## Smallest example

```sh
lumalink check moon-jump
```

## Reference

```text
Check a mod folder or a .luma with the game's own validator and list every problem.

Usage: lumalink check [OPTIONS] <PATH>

Arguments:
  <PATH>  A mod folder or a .luma file.

Options:
      --names <FOLDER>       The game's self-description folder (function rows and types), for the rules on a mod's code. Default: the installed game's.
      --platform <PLATFORM>  A platform to check the code for (repeat for several). Default: all nine.
      --game <FILE>          Your imported game (Nebula's game.pak): its files, tables and keys. Default: the installed game.
      --no-installed         Leave out the installed game: only what --game and --names name is read, and the rules they would check are named as not checked.
      --mods <FOLDER>        Your mod folder: the mods (folders and .luma files) a mod's needs are found in. Without it needs are not checked.
      --json                 Print the report as JSON.
  -h, --help                 Print this help.
      --language <LANGUAGE>  The language LumaLink and the validator speak: one of the game's 14, by its name in the game (UsEnglish, EuGerman, JpJapanese...). Default: UsEnglish.
```

## Limits

Messages and help in any of the game's 14 languages (`--language`). Findings carry the validator's codes, the same the game shows (`lumalink explain`).

## See also

format/rules.md, format/validator.md
