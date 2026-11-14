# lumalink convert
State: generated from the tool's own help.

Convert a published mod (Riivolution pack, disc-layout folder, loose files, texture pack, Gecko list) to a mod folder, each file cooked as the game cooks it.

## Smallest example

```sh
lumalink convert MoonJumpPack.zip moon-jump-converted
```

## Reference

```text
Convert a published mod (Riivolution pack, disc-layout folder, loose files, texture pack, Gecko list) to a mod folder, each file cooked as the game cooks it.

Usage: lumalink convert [OPTIONS] <PATH> <FOLDER>

Arguments:
  <FOLDER>  The mod folder to write; it must not exist.
  <PATH>    The mod: a folder, an archive (.zip, .7z, .rar) or a file.

Options:
      --game <FILE>          Your imported game (Nebula's game.pak): files are placed and textures named against it.
      --name <NAME>          The mod's name, in place of the pack's or the file's.
      --author <AUTHOR>      Who made it (repeat for several), in place of the pack's authors.
      --accept-losses        Write the mod without the files the game cannot use, naming each.
  -h, --help                 Print this help.
      --language <LANGUAGE>  The language LumaLink and the validator speak: one of the game's 14, by its name in the game (UsEnglish, EuGerman, JpJapanese...). Default: UsEnglish.
```

## Limits

Messages and help in any of the game's 14 languages (`--language`). Findings carry the validator's codes, the same the game shows (`lumalink explain`).

## See also

format/luma.md
