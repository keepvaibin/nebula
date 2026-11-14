# lumalink run
State: generated from the tool's own help.

Start the installed game with a mod: a folder is packed first, then imported into the game's mods.

## Smallest example

```sh
lumalink run moon-jump
```

## Reference

```text
Start the installed game with a mod: a folder is packed first, then imported into the game's mods.

Usage: lumalink run [OPTIONS] <PATH> [ARGUMENTS]...

Arguments:
  [ARGUMENTS]...  Arguments for the game after --, as its command line takes them (--seconds, --settings, --data...).
  <PATH>          A mod folder or a .luma file.

Options:
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

lumalink/loader.md, lumalink/mod-log.md
