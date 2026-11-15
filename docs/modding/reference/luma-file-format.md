---
title: The .luma file format
shortTitle: .luma file format
intro: Every rule a .luma file follows, from the ZIP container to its folders, layers, patches, code and saves.
contentType: reference
---

## About .luma files

A `.luma` file is one mod for every platform. It holds sources and patches, which the game cooks on the player's device, and, when the mod has code, one set of built libraries for each platform. Nothing in it is specific to one computer: it has no absolute paths, no compiled shaders and no cooked game data.

The format is strict. Every key, folder and file is known, and anything unknown is an error that names it. There is no "ignored" content and no space for extensions.

The format grows only by additions. Format 1 adds keys and folders in minor versions (1.1, 1.2 and so on), and a minor version never changes the meaning of anything in an earlier one. A mod states the format it needs, and an older game refuses a mod made for a newer format with a message that says so. A change of meaning would be format 2, read beside format 1.

## The container

A `.luma` file is a ZIP file whose name ends in `.luma`.

| Rule | Value |
|---|---|
| Compression | Stored or deflate only. |
| Zip64 | Allowed. |
| Encryption and multi-disk archives | Refused. |
| Entries | Files and folders only: no links and no device files. No entry appears twice, no two entries share data, and a stated size or checksum that differs from the data is an error. |
| Limits | At most 200,000 entries, each file at most 4 GiB unpacked, and the whole archive at most 64 GiB unpacked. The game checks the stated sizes against the bytes it unpacks. |
| Case | Two entries that differ only in case are an error, because Windows, macOS and Android storage treat them as one. |

### Entry names

* Names are UTF-8, use `/` between folders, and are relative.
* A name has no `.` or `..` part, no empty part, and no part that ends in `.` or a space.
* A name uses none of the names Windows reserves, such as `CON`, `NUL` or `COM1`, none of the characters `< > : " | ? *`, and no control characters.
* A name has no combining marks or conjoining jamo. Write names precomposed, because macOS treats a decomposed name as the same file as its precomposed twin.
* Each name is at most 240 bytes.
* A layer's name, and the first part of a path under `files/` or `assets/`, does not start with `@`.

An archive that uses `\` between folders throughout, as .NET Framework's `ZipFile` and PowerShell's `Compress-Archive` write, is read with `/` and a note (L108). An archive that mixes `\` and `/` is refused.

## Folders

| Folder | Read by the game | Contents |
|---|---|---|
| `luma.json` | Yes | The manifest. For more information, see [luma.json](luma-json.md). |
| `layers/LAYER/` | Yes | Content: files placed at game paths, patches, new assets and texture pictures. For more information, see [Layers](#layers). |
| `code/PLATFORM/` | Yes | The built libraries for each platform: the entry library and any others. For more information, see [Code](#code). |
| `media/` | Yes, on the **MODS** page | The icon and pictures named in `luma.json`, and nothing else. |
| `saves/REGION/` | Yes, offered to the player and never written silently | Save files the mod brings, for each region. For more information, see [Saves a mod brings](#saves-a-mod-brings). |
| `certs/` | Yes, only through a network permission's `roots` | PEM files of the TLS root certificates the mod adds for its own hosts. |
| `kit/MODULE/` | No | Data for library mods such as LumaKit, checked against the schema that module supplies. A schema is data in a fixed subset of JSON Schema, never code that the validator runs. |
| `source/` | No. LumaLink's `verify` reads it. | LumaScript and C sources, and the build description. |
| `tests/` | No. LumaLink's `test` reads it. | The mod's tests: each `tests/**/test.json` is run by the test harness. |

Any other top-level entry is an error. An empty folder is allowed and means nothing.

## Layers

A layer is a folder of content under `layers/`. The manifest's `layers` key lists the layers in the order they apply, and when each one is on.

```text
layers/LAYER/
  files/GAME-PATH              A whole file placed at a game path, new or replacing the game's.
  patches/GAME-PATH.json       Rows changed, added or removed, in a file that has rows.
  merge/GAME-PATH              Rows, lines or particles added to a file that many mods add to at once.
  assets/MOD-PATH              The mod's own new assets: models, textures, sounds, effects.
  textures/TEXTURE.png         The game's textures replaced by name, as a texture pack does (.png or .dds).
```

### Game paths

Game paths are the disc's paths, such as `ObjectData/Kuribo.arc` or `StageData/EggStarGalaxy/EggStarGalaxyMap.arc`. Write a file inside an archive as `ObjectData/Kuribo.arc/Kuribo.bdl`.

Game paths match the player's game without regard to case, and the game uses its own case. A path that matches nothing is a new file. New files are allowed only for the kinds of file the game finds by name, such as an archive, a stream or a movie. Each content kind's page says which.

### Whole files and patches

Files with rows are patched, never replaced. These include tables (`.bcsv` and the game data's tables), stage placement, scenario data and message files. A `files/` entry at such a path is an error that names it and says to patch it by key. LumaLink's `convert` makes the patches by comparing a Wii mod's whole file with the player's game.

Whole files, such as models, textures, streams, layout pictures and movies, replace the game's. When two mods that are on replace the same path, the one higher in the mod list wins (L305), and the **MODS** page names both mods and the path.

A whole file can be a game format, such as an `.arc` with `.bdl` files, a `.bti` or an `.ast`. It can also be an ordinary source at the game path with its own extension, for example `files/ObjectData/Mario.arc/Mario.glb`. These source formats are accepted: `.gltf`, `.glb`, `.png`, `.ktx2`, `.dds`, `.wav`, `.ogg`, `.mid`, `.json`, `.ttf`, `.otf`, `.webm` and `.mp4`. Each content kind's page says what a source can stand in for and how it is checked, for example units, joints and materials.

### Mod paths

Paths under `assets/` are the mod's own. The game knows them as `MOD-ID:PATH`, so nothing else can collide with them.

### Texture replacements

`textures/` replaces the game's textures by name, as a texture pack does. Names are the game's `MODEL/TEXTURE.png` (or `.dds`, which keeps its own mip levels), or Dolphin's `tex1_...` names, which the game matches once, at install, against the player's game.

Nebula reads each picture at start the same way it reads a texture pack. A picture it would refuse is a finding in the mod log that names the mod and the file. Anything else in `textures/` is an error (L306). A texture for the mod's own models goes in the model's file or in `assets/`, not here.

A mod can be texture pictures alone: a `luma.json` and `layers/base/textures/`, with no code and no other files.

```text
shiny-coins/
  luma.json
  layers/base/textures/Coin/CoinBody.png
```

### Merged files

Some files take additions from many mods at once. Each mod brings only what it adds, in a layer's `merge/` folder, and the game's file gets all of it. A file in `merge/` of any other kind is an error (L309).

| File in `merge/` | What it adds |
|---|---|
| `GAME-PATH.json`, for a table | Rows: `{ "rows": [ { "COLUMN": VALUE } ] }`. A column left out is `0` or empty. `remove` and `change` (with `where` and `set`) also work. |
| `MessageData/messages.json` | Lines and talk flows for every language the game has. |
| `LANGUAGE/MessageData/Message.arc/message.bmg.json` | Lines and talk flows for one language. |
| `Particles.jpc`, with `ParticleNames.bcsv.json` beside it | New particles. Each resource's index is its place in the list of names. |

When a merge has no table to add to (in the game or the mod), names a column the table lacks, or gives a value of the wrong kind, the game leaves that file out with the reason. A `remove` or `change` that matches no row is a note, and the rest of the file is added.

#### Lines and talk flows

```json
{
  "lines": {
    "AstroGalaxy_Tico034": "New words for a line of the game: its talk settings stay",
    "MyNpc_Hello": { "default": "Hi, [player]!", "fr": "Salut, [player] !", "like": "AstroGalaxy_Tico034" },
    "MyNpc_Ask": { "default": "Want a star?", "talk": "flow", "balloon": 0 }
  },
  "flows": {
    "MyNpc_Hello": "MyNpc_Ask",
    "MyNpc_Ask": { "ask": "select", "param": 0, "yes": { "event": "func", "value": 1, "next": "MyNpc_Yes" }, "no": "MyNpc_No" }
  }
}
```

* A line is a string for every language, or an object with `default` and words for each language. Name a language by code (`fr-CA`, then `fr`) or by the game's language folder (`UsFrench`).
* A line's words are the game's text, with its message tags in brackets. For more information, see [Message tags](lumascript/message-tags.md).
* A new line takes the talk settings of the game line it is `like`, or an ordinary talk. `talk` (`normal`, `short`, `event`, `compose`, `flow` or `null`), `balloon`, `camera` (`normal`, `event` or `none`), `camera_set`, `sound`, `area` and `already_read` set them one at a time.
* A flow says what comes after a line: another line, `null` (the talk ends), a branch or an event.
* A branch has `ask` (a condition), its `param`, then `yes` and `no`. A `select` that the player answers takes its choices' words in `choices`, or one of the game's 19 choice sets by number in `param`.
* An event has `event` (`func`, `func_stop`, `none`, `skip`, `anime`, `switch_a`, `switch_b` or `kill`), its `value`, then `next`.
* The conditions are `select` (the player chooses), `func` (the talker's own test), `near_player`, `switch_a`, `switch_b`, `player_normal`, `player_bee`, `player_boo`, `power_star_appeared`, `already_done`, `player_luigi`, `galaxy_result`, `time_keep_demo` and `message_read`.

The game adds the lines to its own messages when the mod is prepared, so nothing is looked up from mods while playing.

## Patches

A patch is a JSON file at `layers/LAYER/patches/GAME-PATH.json`.

```json
{
  "patch": 1,
  "key": ["name"],
  "set":    [ { "where": { "name": "Kuribo" }, "values": { "speed": 2.5 } } ],
  "add":    [ { "name": "GoldKuribo", "speed": 3.0 } ],
  "remove": [ { "name": "Unused" } ],
  "select": [ { "match": { "name": { "glob": "Kuribo*" } }, "values": { "speed": 2.5 } } ]
}
```

| Key | Meaning |
|---|---|
| `patch` | The patch format version, `1`. |
| `key` | The key the game defines for this kind of file. The validator names the key it expects. For each table's key, see [Table keys](table-keys.md). |
| `set` | Rows found by `where`, with the values to change. |
| `add` | New rows. Adding a row whose key already exists is an error. |
| `remove` | Rows to remove, found by their key. |
| `select` | Every row whose cells match, with the values to change. Each match is `equals`, `prefix` or `glob` on one column, and all of them must hold. |

A row that `where` or `remove` does not find is an error that names the key's values. A table's key matches with or without regard to case, as defined for that table.

Selectors are data, never code. The validator checks that each column exists for the file's kind. When the game loads the patch, it reports how many rows each selector matched, and a selector that matched none is a note that names it.

The validator checks each value against the column's type, from the game's own description of the file.

### Patch order

* Patches apply from the bottom of the mod list up, then in layer order, then in file order.
* Within one mod, `set` by key applies before `select`.
* When two mods `set` the same cell, or one mod adds a row that another removes, the mod higher in the list wins (L404). The **MODS** page names both mods and the cell.

### Patching another mod's file

To patch a file that another mod brings, use `layers/LAYER/patches/@MOD-ID/MOD-PATH.json`, and list that mod in `needs`.

## Code

```text
code/PLATFORM/mod.ENDING           The entry library. It exports luma_mod.
code/PLATFORM/LIBRARY-FILE         Each extra library named in code.libraries, loaded first, in order.
```

| Platform | Entry library | Notes |
|---|---|---|
| `windows-x64`, `windows-arm64` | `mod.dll` | None |
| `linux-x64`, `linux-arm64` | `mod.so` | None |
| `macos-arm64`, `macos-x64` | `mod.dylib` | None |
| `android-arm64`, `android-x64` | `mod.so` | Loaded by the game's own loader. |
| `ios-arm64` | `mod.dylib` | Unsigned. Signed on the device. |

`lumalink build` builds all nine platforms by default.

### Rules for code

* The entry library exports `luma_mod` (from `luma.h`) and imports nothing from the system except what the platform's loader allows. `luma.h` supplies `memcpy`, `memset` and the stack probe of the Windows targets, so even a stack frame over 4 KiB imports nothing. The validator reads the import list and refuses any other import, naming it.
* The entry library may import from the mod's extra libraries.
* An extra library, such as a physics or XR library, imports from the system by nature. It needs the `native` permission, whose consent text says the mod runs code with full access to the device. The validator lists every system import of each extra library in its report. On iOS, an extra library is allowed only as the platform's signing allows.
* Every name the library asks for (each `LumaImport`) is checked against the game's self-description when the mod is imported, and against the self-description LumaLink built with when the mod is packed. The rule and the message are the same.
* A service the code uses is an import row of kind 5 (`LUMA_IMPORT_SERVICE`), with its name and `major.minor` version. It binds with the rest.
* One installed mod at a time can replace a function (`LUMA_REPLACE`). The mod higher in the list wins, and the lower mod's replacement is refused with L805, which names the higher mod.
* A platform missing from `code.platforms` means the mod's code does not run there. The mod is off on that platform, with the reason, unless it has no code at all.
* No other files are allowed under `code/`.

## Saves a mod brings

A mod can bring save files of the game in `saves/REGION/`, named in the manifest's `saves` key: for example, a mod that starts later in the game, or a challenge file.

Nebula never writes them silently. When the player turns the mod on, Nebula offers each save for the player's region through the game's own save import: the player sees the save's `why`, chooses a file to replace or declines, and confirms on the game's own screen.

A save's region is its folder, and the game's own save check reads it. A file that is not a save for the named region, or a region the mod does not list in `game.regions`, is an error.

## Errors

Every rule on this page is a validator rule with a code and a message in each of the game's languages. A message always names:

* The mod, by its `id`, or by its file name when the manifest cannot be read.
* The entry inside the `.luma` file.
* The line and column, for JSON.
* What was expected.

The validator never stops at the first error. It reports every error it can find in one pass. For every code, see [Validator rules](validator-rules.md).

## Versions

* Format `1.0` is described on this page.
* A minor version adds keys, folders, layer conditions and patch kinds. It never changes the meaning of anything in an earlier minor version.
* A game supports a range of minor versions, and refuses a higher one with "made for a newer Nebula (format 1.N)".
* Each content kind has its own version inside the format, such as a patch's `patch` key or a source's own version key.

## Further reading

* [luma.json](luma-json.md)
* [Table keys](table-keys.md)
* [Validator rules](validator-rules.md)
