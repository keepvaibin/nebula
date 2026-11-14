# lumalink add
State: generated from the tool's own help.

Add a part to a mod by command, checked before it is kept: `add need lumakit <folder>`, `add option balls <folder> --kind number --default 3 --name Balls`, `add file title.arc <folder> --at LayoutData/Title.arc`, `add character <model.glb> <folder> --replaces mario`.

`add character <model.glb> <folder> --replaces mario --as <path>` writes the model, bound to Mario's skeleton, as the mod's own model `layers/base/assets/<path>.glb`, for the mod's code to draw (a form's look). The game's Mario, his hands and his face stay as they are.

## Smallest example

```sh
lumalink add need lumakit moon-jump
```

## Reference

```text
Add a part to a mod by command, checked before it is kept: `add need lumakit <folder>`, `add option balls <folder> --kind number --default 3 --name Balls`, `add file title.arc <folder> --at LayoutData/Title.arc`, `add character <model.glb> <folder> --replaces mario`.

Usage: lumalink add [OPTIONS] <ITEM> <VALUE> <FOLDER>

Arguments:
  <FOLDER>  The mod folder.
  <VALUE>   What the item takes: a model (.glb), a mod's id, a name, a language, a file.
  <ITEM>    What to add: character, need, conflict, after, before, takes-over, author, text, permission, option, layer, file, asset, texture, icon, picture, save, script.

Options:
      --replaces <NAME>           The game character or model it stands in for: mario, luigi, or a model's name.
      --joint <GAME=SOURCE>       character: the game's joint GAME follows the model's joint SOURCE, whatever the automatic match finds; GAME= follows none (repeat for several).
      --version <RANGE>           need: the versions of the mod that do (a semver range). Default: any.
      --why <TEXT>                conflict, permission, save: why, one line shown to the player.
      --host <HOST>               permission net.internet: a host it reaches (repeat for several).
      --root <ROOT>               permission fs.exchange: a root it reads or writes (repeat for several).
      --kind <KIND>               option: choice, number, toggle, text or key.
      --default <VALUE>           option: its value until the player sets one.
      --name <TEXT>               option: its name on the Mods screen; text: the mod's name in that language.
      --summary <TEXT>            text: the mod's summary in that language.
      --min <NUMBER>              option number: the least value.
      --max <NUMBER>              option number: the most value.
      --step <NUMBER>             option number: the step between values.
      --value <VALUE>             option choice: a value, or value=name shown (repeat for each, in order).
      --max-length <COUNT>        option text: the most characters.
      --language <LANGUAGE>       The language LumaLink and the validator speak: one of the game's 14, by its name in the game (UsEnglish, EuGerman, JpJapanese...). Default: UsEnglish.
      --when-option <KEY=VALUE>   layer: it applies when the option has the value (repeat; a key twice: any of its values).
      --when-region <REGION>      layer: it applies in these regions (repeat for several).
      --when-language <LANGUAGE>  layer: it applies in these languages' folders (repeat for several).
      --when-platform <PLATFORM>  layer: it applies on these platforms (repeat for several).
      --when-code                 layer: the mod's code chooses it before content loads.
      --at <PATH>                 file: the game path it is placed at; asset: its path in the mod (default its name); texture: the game texture's name (default the file's name).
      --layer <LAYER>             file, asset, texture: the layer it goes into.
      --region <REGION>           save: the region the save file is for.
      --names <FOLDER>            The game's self-description folder (function rows and types), for the rules on a mod's code. Default: the installed game's.
      --platform <PLATFORM>       A platform to check the code for (repeat for several). Default: all nine.
      --game <FILE>               Your imported game (Nebula's game.pak): its files, tables and keys. Default: the installed game.
      --no-installed              Leave out the installed game: only what --game and --names name is read, and the rules they would check are named as not checked.
      --mods <FOLDER>             Your mod folder: the mods (folders and .luma files) a mod's needs are found in. Without it needs are not checked.
  -h, --help                      Print this help.
```

## Limits

Messages and help in any of the game's 14 languages (`--language`). Findings carry the validator's codes, the same the game shows (`lumalink explain`).

## See also

content/models.md, content/animation.md
