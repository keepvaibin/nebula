# lumalink format
State: generated from the tool's own help.

Format LumaScript files: four spaces of indent for each open bracket, at most one blank line, LF line ends. Strings and comments are not changed.

## Smallest example

```sh
lumalink format moon-jump/source
```

## Reference

```text
Format LumaScript files: four spaces of indent for each open bracket, at most one blank line, LF line ends. Strings and comments are not changed.

Usage: lumalink format [OPTIONS] <PATH>...

Arguments:
  <PATH>...  .lum files, or folders (every .lum under each).

Options:
      --check                Write nothing: name each file not in the style, and exit 1 if there is one.
  -h, --help                 Print this help.
      --language <LANGUAGE>  The language LumaLink and the validator speak: one of the game's 14, by its name in the game (UsEnglish, EuGerman, JpJapanese...). Default: UsEnglish.
```

## Limits

Messages and help in any of the game's 14 languages (`--language`).

## See also

language/language.md
