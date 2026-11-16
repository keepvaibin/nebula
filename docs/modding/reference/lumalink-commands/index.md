---
title: LumaLink commands
shortTitle: LumaLink commands
intro: Every command of the lumalink command-line tool, with its arguments and options.
contentType: reference
children:
  - /new
  - /check
  - /pack
  - /list
  - /extract
  - /explain
  - /verify
  - /build
  - /run
  - /test
  - /format
  - /add
  - /convert
  - /lsp
  - /services
  - /update
---

## About LumaLink commands

Run `lumalink COMMAND --help` to print any command's help, or `lumalink --help` for the list.

| Command | Description |
|---|---|
| [`new`](new.md) | Start a mod: a folder with its luma.json and an empty base layer. |
| [`check`](check.md) | Check a mod folder or a .luma with the game's own validator and list every problem. |
| [`pack`](pack.md) | Write a mod folder as a .luma, after the validator passes it with every rule checked. |
| [`list`](list.md) | List what is in a .luma, or a kind of thing in your imported game. |
| [`extract`](extract.md) | Write files out of a .luma, or out of your imported game. |
| [`explain`](explain.md) | Say what a validator code (L203, or 203) means, in a language. |
| [`verify`](verify.md) | Check that a .luma is exactly what its folder packs to. |
| [`build`](build.md) | Build a mod's code (LumaScript and C) for every platform. |
| [`run`](run.md) | Start the installed game with a mod: a folder is packed first, then imported into the game's mods. A mod folder with code plays in dev mode: each change to its code is built and reloaded in the running game until the game is closed. |
| [`test`](test.md) | Build the mod, run each tests/**/test.json in the game (hidden, silent) and print each check with what was found. |
| [`format`](format.md) | Format LumaScript files: four spaces of indent for each open bracket, at most one blank line, LF line ends. Strings and comments are not changed. |
| [`add`](add.md) | Add a part to a mod by command, checked before it is kept: `add need lumakit <folder>`, `add option balls <folder> --kind number --default 3 --name Balls`, `add file title.arc <folder> --at LayoutData/Title.arc`, `add character <model.glb> <folder> --replaces mario`. |
| [`convert`](convert.md) | Convert a published mod (Riivolution pack, disc-layout folder, loose files, texture pack, Gecko list) to a mod folder, each file cooked as the game cooks it. |
| [`lsp`](lsp.md) | The editor server: the Language Server Protocol over stdin and stdout for a mod's LumaScript (errors, completion, hover, go to definition, rename). |
| [`services`](services.md) | List every service function of the game's SDK and whether scripts can call it: script, native (C mods only, by design) or c (C mods only). |
| [`update`](update.md) | Look for a newer LumaLink and install it. On Linux and macOS, print where to download it. |

## Options of every command

| Option | Description |
|---|---|
| `--language LANGUAGE` | The language LumaLink and the validator use: one of the game's 14, by its name in the game (`UsEnglish`, `EuGerman`, `JpJapanese` and so on). The default is `UsEnglish`. |
| `-h`, `--help` | Prints the help. |
| `-V`, `--version` | Prints LumaLink's version. |
