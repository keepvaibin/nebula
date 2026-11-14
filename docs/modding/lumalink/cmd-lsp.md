# lumalink lsp
State: generated from the tool's own help.

The editor server: the Language Server Protocol over stdin and stdout for a mod's LumaScript (errors, completion, hover, go to definition, rename).

An editor starts it; nobody types it. Visual Studio Code's LumaScript extension (LumaLink's `editors/vscode`) starts it for every `.lum` file; any other editor with a Language Server Protocol client runs `lumalink lsp` with the same options (lumalink/editor.md). The errors it shows are `lumalink build`'s, from the same check.

## Smallest example

```sh
lumalink lsp
```

## Reference

```text
The editor server: the Language Server Protocol over stdin and stdout for a mod's LumaScript (errors, completion, hover, go to definition, rename).

Usage: lumalink lsp [OPTIONS]

Options:
      --names <FOLDER>       The game's self-description folder (function rows and types), for the rules on a mod's code. Default: the installed game's.
      --sdk <FOLDER>         The folder with the game's luma.h, for the services' functions. Default: LUMA_INCLUDE, then the self-description folder's sdk.
      --no-installed         Leave out the installed game: only what --game and --names name is read, and the rules they would check are named as not checked.
  -h, --help                 Print this help.
      --language <LANGUAGE>  The language LumaLink and the validator speak: one of the game's 14, by its name in the game (UsEnglish, EuGerman, JpJapanese...). Default: UsEnglish.
```

## Limits

Messages and help in any of the game's 14 languages (`--language`).

## See also

lumalink/editor.md, language/language.md
