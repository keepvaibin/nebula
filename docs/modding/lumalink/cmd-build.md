# lumalink build
State: generated from the tool's own help.

Build a mod's code (LumaScript and C) for every platform.

## Smallest example

```sh
lumalink build moon-jump
```

## Reference

```text
Build a mod's code (LumaScript and C) for every platform.

Usage: lumalink build --names <FOLDER> [OPTIONS] <FOLDER>

Arguments:
  <FOLDER>  The mod folder.

Options:
      --names <FOLDER>       The game's self-description folder (function rows and types), for the rules on a mod's code. Default: the installed game's.
      --sdk <FOLDER>         The folder with the game's luma.h. Default: LUMA_INCLUDE.
      --clang <FILE>         The clang to build with. Default: LUMALINK_CLANG, then LLVM's own folder, then the system's search path.
      --work <FOLDER>        Keep the generated C in this folder. Default: a temporary folder, removed after.
      --platform <PLATFORM>  A platform to build for (repeat for several). Default: all nine.
  -h, --help                 Print this help.
      --language <LANGUAGE>  The language LumaLink and the validator speak: one of the game's 14, by its name in the game (UsEnglish, EuGerman, JpJapanese...). Default: UsEnglish.
```

## Limits

Messages and help in any of the game's 14 languages (`--language`).

## See also

language/language.md, language/compiled-form.md
