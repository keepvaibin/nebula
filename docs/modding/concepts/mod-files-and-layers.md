---
title: About mod files and layers
shortTitle: Mod files and layers
intro: Learn how a mod folder is laid out, how layers group a mod's content, and how a mod replaces, patches and adds the game's files.
contentType: concepts
---

## About the mod folder

A mod is a folder. LumaLink packs it into a `.luma` file, which is a zip file with the same folders. Every folder and file has a fixed meaning, and LumaLink refuses anything it does not know.

```text
moon-jump/
  luma.json           the mod's name, ID, version, options, permissions and layers
  layers/base/        content: files, patches, new assets, textures
  source/main.lum     code, in LumaScript or C
  code/               the built code, one folder for each platform
  media/              the icon and pictures for the Mods page
  tests/              the mod's tests
```

| Folder | What it holds | Read by the game |
|---|---|---|
| `luma.json` | The manifest. For more information, see [luma.json](../reference/luma-json.md). | Yes |
| `layers/LAYER/` | Content, in layers. | Yes |
| `code/PLATFORM/` | The built libraries for each platform. `lumalink build` writes them. | Yes |
| `media/` | The icon and pictures named in `luma.json`. | Yes, on the **Mods** page |
| `saves/REGION/` | Save files the mod offers the player. | Yes, offered to the player |
| `certs/` | Certificates the mod trusts for its own internet hosts. | Yes, through a network permission |
| `kit/MODULE/` | Data for library mods such as LumaKit. | By the library mod |
| `source/` | LumaScript and C sources. | No. `lumalink verify` reads it. |
| `tests/` | The mod's tests. | No. `lumalink test` reads it. |

For every rule of the file format, see [The .luma file format](../reference/luma-file-format.md).

## About layers

A layer is a folder of content under `layers/`. Most mods have one layer, `base`. A mod with more than one layer lists them in `luma.json`, in the order they apply, and can turn each one on or off with a condition:

* `option`: one of the mod's options has a given value. For example, a "Look" option with the values "Shiny" and "Matte" can choose between two layers of textures.
* `region`: the player's game is one of the listed regions.
* `language`: the game runs in one of the listed languages.
* `platform`: the game runs on one of the listed platforms.
* `code`: the mod's code chooses the layer when the game starts.

```json
"layers": [
  { "name": "base" },
  { "name": "matte", "when": { "option": { "look": "matte" } } }
]
```

## What a layer holds

```text
layers/base/
  files/GAME-PATH            a whole file at a game path, new or replacing the game's
  patches/GAME-PATH.json     rows changed, added or removed in a file that has rows
  assets/MOD-PATH            new assets of the mod's own
  textures/TEXTURE.png       the game's textures replaced by name
```

### Replacing a whole file

A file in `files/` replaces the game's file at the same path, or adds a new one. Game paths are the paths on the disc, such as `ObjectData/Kuribo.arc`. A file inside an archive is written as a path inside it, such as `ObjectData/Kuribo.arc/Kuribo.bdl`.

A whole file can be in the game's own format, or in an ordinary format that Nebula converts: glTF models, PNG and DDS pictures, WAV and Ogg sounds, TrueType fonts, WebM and MP4 movies. Put it at the game path with its own extension, for example `files/ObjectData/Mario.arc/Mario.glb`.

### Patching a file with rows

Files with rows are never replaced whole. Tables, stage placements, scenario data and message files are patched by key: a patch names the rows it changes, adds or removes. Two mods can patch different rows of the same file. For more information, see [Patching game tables](../how-tos/patching-game-tables.md).

### Adding new assets

Files in `assets/` belong to the mod alone. The game knows them as `MOD-ID:PATH`, so they never collide with the game's files or another mod's.

### Replacing textures

Pictures in `textures/` replace the game's textures by name, as a texture pack does. A mod can consist of textures alone:

```text
shiny-coins/
  luma.json
  layers/base/textures/Coin/CoinBody.png
```

## When two mods change the same file

When two mods replace the same file, or set the same value in a table, the mod higher in the **Mods** list wins. The **Mods** page names both mods and the file. For more information, see [How Nebula loads mods](how-nebula-loads-mods.md).

## Further reading

* [The .luma file format](../reference/luma-file-format.md)
* [luma.json](../reference/luma-json.md)
* [Table keys](../reference/table-keys.md)
