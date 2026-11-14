# lumalink services
State: generated from the tool's own help.

List every service function of the game's SDK and whether scripts can call it: script, native (C mods only, by design) or c (C mods only).

## Smallest example

```sh
lumalink services
```

## Reference

```text
List every service function of the game's SDK and whether scripts can call it: script, native (C mods only, by design) or c (C mods only).

Usage: lumalink services [OPTIONS]

Options:
      --sdk <FOLDER>         The folder with the game's luma.h. Default: LUMA_INCLUDE.
      --unreachable          Only what scripts cannot reach.
  -h, --help                 Print this help.
      --language <LANGUAGE>  The language LumaLink and the validator speak: one of the game's 14, by its name in the game (UsEnglish, EuGerman, JpJapanese...). Default: UsEnglish.
```

## Limits

Messages and help in any of the game's 14 languages (`--language`).

## See also

services/draw.md, lumalink/editor.md
