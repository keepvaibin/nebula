# lumalink verify
State: generated from the tool's own help.

Check that a .luma is exactly what its folder packs to.

## Smallest example

```sh
lumalink verify moon.jump-0.1.0.luma --source moon-jump
```

## Reference

```text
Check that a .luma is exactly what its folder packs to.

Usage: lumalink verify --source <FOLDER> [OPTIONS] <FILE>

Arguments:
  <FILE>  The .luma file.

Options:
      --source <FOLDER>      The mod folder it was packed from.
  -h, --help                 Print this help.
      --language <LANGUAGE>  The language LumaLink and the validator speak: one of the game's 14, by its name in the game (UsEnglish, EuGerman, JpJapanese...). Default: UsEnglish.
```

## Limits

Messages and help in any of the game's 14 languages (`--language`).

## See also

format/luma.md
