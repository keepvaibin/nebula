---
title: lumalink add
shortTitle: add
intro: Add a part to a mod by command, checked before it is kept - `add need lumakit <folder>`, `add option balls <folder> --kind number --default 3 --name Balls`, `add file title.arc <folder> --at LayoutData/Title.arc`, `add character <model.glb> <folder> --replaces mario`.
contentType: reference
---

## About lumalink add

Add a part to a mod by command, checked before it is kept: `add need lumakit <folder>`, `add option balls <folder> --kind number --default 3 --name Balls`, `add file title.arc <folder> --at LayoutData/Title.arc`, `add character <model.glb> <folder> --replaces mario`.

## Example

```shell
lumalink add need lumakit moon-jump
lumalink add option balls moon-jump --kind number --default 3 --name Balls
lumalink add file title.arc moon-jump --at LayoutData/Title.arc
```

## Usage

```text
lumalink add [OPTIONS] <ITEM> <VALUE> <FOLDER>
```

## Arguments

| Argument | Description |
|---|---|
| `<FOLDER>` | The mod folder. |
| `<VALUE>` | What the item takes: a model (.glb), a mod's id, a name, a language, a file. |
| `<ITEM>` | What to add: character, need, conflict, after, before, takes-over, author, text, permission, option, layer, file, asset, texture, icon, picture, save, script. |

## Options

| Option | Description |
|---|---|
| `--replaces <NAME>` | The game character or model it stands in for: mario, luigi, or a model's name. |
| `--as <PATH>` | character: written as the mod's own model layers/base/assets/PATH.glb, drawn on that character's skeleton by the mod's code; the game's model and its parts stay. |
| `--joint <GAME=SOURCE>` | character: the game's joint GAME follows the model's joint SOURCE, whatever the automatic match finds; GAME= follows none (repeat for several). |
| `--version <RANGE>` | need: the versions of the mod that do (a semver range). Default: any. |
| `--why <TEXT>` | conflict, permission, save: why, one line shown to the player. |
| `--host <HOST>` | permission net.internet: a host it reaches (repeat for several). |
| `--root <ROOT>` | permission fs.exchange: a root it reads or writes (repeat for several). |
| `--kind <KIND>` | option: choice, number, toggle, text or key. |
| `--default <VALUE>` | option: its value until the player sets one. |
| `--name <TEXT>` | option: its name on the **Mods** page; text: the mod's name in that language. |
| `--summary <TEXT>` | text: the mod's summary in that language. |
| `--min <NUMBER>` | option number: the least value. |
| `--max <NUMBER>` | option number: the most value. |
| `--step <NUMBER>` | option number: the step between values. |
| `--value <VALUE>` | option choice: a value, or value=name shown (repeat for each, in order). |
| `--max-length <COUNT>` | option text: the most characters. |
| `--when-option <KEY=VALUE>` | layer: it applies when the option has the value (repeat; a key twice: any of its values). |
| `--language <LANGUAGE>` | The language LumaLink and the validator speak: one of the game's 14, by its name in the game (UsEnglish, EuGerman, JpJapanese...). Default: UsEnglish. |
| `--when-region <REGION>` | layer: it applies in these regions (repeat for several). |
| `--when-language <LANGUAGE>` | layer: it applies in these languages' folders (repeat for several). |
| `--when-platform <PLATFORM>` | layer: it applies on these platforms (repeat for several). |
| `--when-code` | layer: the mod's code chooses it before content loads. |
| `--at <PATH>` | file: the game path it is placed at; asset: its path in the mod (default its name); texture: the game texture's name (default the file's name). |
| `--layer <LAYER>` | file, asset, texture: the layer it goes into. |
| `--region <REGION>` | save: the region the save file is for. |
| `--names <FOLDER>` | The game's self-description folder (function rows and types), for the rules on a mod's code. Default: the installed game's. |
| `--platform <PLATFORM>` | A platform to check the code for (repeat for several). Default: all nine. |
| `--game <FILE>` | Your imported game (Nebula's game.pak): its files, tables and keys. Default: the installed game. |
| `--no-installed` | Leave out the installed game: only what --game and --names name is read, and the rules they would check are named as not checked. |
| `--mods <FOLDER>` | Your mod folder: the mods (folders and .luma files) a mod's needs are found in. Without it, they are looked for beside the mod, in the installed game's mods and in the mods that come with the game. |
| `-h, --help` | Print this help. |

Every message and help text is available in each of the game's 14 languages, chosen with `--language`.
Findings carry the validator's codes, the same ones the game shows.

## Further reading

* [luma.json](../luma-json.md)
