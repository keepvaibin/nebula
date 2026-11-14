# lumalink new
State: generated from the tool's own help.

Start a mod: a folder with its luma.json and an empty base layer.

## Smallest example

```sh
lumalink new moon-jump --id moon.jump --name "Moon jump" --summary "Hold jump to rise." --author me
```

## Reference

```text
Start a mod: a folder with its luma.json and an empty base layer.

Usage: lumalink new --id <ID> --name <NAME> --summary <TEXT> --author <AUTHOR>... [OPTIONS] <FOLDER>

Arguments:
  <FOLDER>  The folder to make; it must not exist.

Options:
      --id <ID>              The mod's id: 3 to 64 of a-z, 0-9, '-' and '.', starting with a letter.
      --name <NAME>          The mod's name, as players see it.
      --summary <TEXT>       One line on what the mod does.
      --author <AUTHOR>      Who made it (repeat for several).
  -h, --help                 Print this help.
      --language <LANGUAGE>  The language LumaLink and the validator speak: one of the game's 14, by its name in the game (UsEnglish, EuGerman, JpJapanese...). Default: UsEnglish.
```

## Limits

Messages and help in any of the game's 14 languages (`--language`).

## See also

guides/first-mod.md, format/luma.md
