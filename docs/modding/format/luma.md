# The .luma format

## More rules

### 1. Principles

- One file, every platform. A `.luma` holds sources and patches (cooked on the player's device) and, if the mod
  has code, one built library set per platform. Nothing in it is specific to one machine (no absolute paths, no
  compiled shaders, no cooked store blocks).
- Strict. Every key, folder and file is known. Anything unknown is an error naming it (rule 6). There is no
  "ignored" and no extension namespace.
- Additions only. Format 1 grows by minors (1.1, 1.2...) that add keys and folders. A mod states the format it
  needs; a game that is older than that refuses it with a message saying so. A change of meaning is format 2, read
  beside 1.

### 2. The container

A zip file named anything ending in `.luma`.

| Rule | Value |
|---|---|
| Compression | stored or deflate only |
| Zip64 | allowed |
| Names | UTF-8, `/` separators (an archive with `\` between folders throughout, as .NET Framework's ZipFile and Compress-Archive write, is read with `/` and a note, L108; a mix is refused), relative, no `.` or `..` part, no empty part, no part ending in `.` or space, none of the names Windows reserves (`CON`, `NUL`, `COM1`...), no control characters, at most 240 bytes each |
| More on names | none of Windows' forbidden characters `< > : " \| ? *`; no combining marks or conjoining jamo (names are written precomposed: a decomposed name is the same file as its twin on macOS); a layer's name, or the first part of a path under `files/` or `assets/`, does not start with `@` |
| Case | two entries differing only in case are an error (Windows, macOS and Android's storage treat them as one) |
| Entries | files and folders only: no links, no device files; no entry twice; no two entries share data; a stated size or checksum that differs from the data is an error |
| Encryption, multi-disk | refused |
| Limits | at most 200,000 entries; a file at most 4 GiB unpacked; the whole at most 64 GiB unpacked; the stated sizes are checked against the bytes actually unpacked |

### 3. The folders

| Folder | Read by the game | What |
|---|---|---|
| `luma.json` | yes | The manifest (section 4) |
| `layers/<layer>/` | yes | Content: files placed at game paths, patches, new assets, texture pictures (section 5) |
| `code/<platform>/` | yes | The built libraries for each platform: the entry library and any others (section 6) |
| `media/` | yes (Mods screen) | The icon and pictures named in `luma.json`; nothing else |
| `saves/<region>/` | yes (offered, never written silently) | Save files of the game the mod brings, per region (section 6.1) |
| `certs/` | yes, only through a net permission's `roots` (section 4.1) | PEM files of the TLS roots the mod adds for its own hosts |
| `kit/<module>/` | no (data for library mods such as LumaKit, validated by the schema that module supplies; a schema is data in a fixed JSON-schema subset, never code the validator runs) | Module data |
| `source/` | no (LumaLink `verify`) | LumaScript and C sources, and the build description |
| `tests/` | no (LumaLink `test`) | The mod's own tests: `tests/**/test.json`, each run by the test harness |

Any other top-level entry is an error. An empty folder is allowed and means nothing.

### 4. `luma.json`

UTF-8 JSON, no comments, no duplicate keys, at most 1 MiB. Every key below; nothing else.

```json
{
  "format": "1.0",
  "id": "gold-flower",
  "version": "1.0.0",
  "name": "Gold Flower",
  "summary": "Gold Mario throws gold balls; enemies they hit turn to coins.",
  "text": { "JpJapanese": { "name": "ゴールドフラワー", "summary": "...",
                            "permissions": { "net.internet": "..." } } },
  "authors": ["keepvaibin"],
  "license": "GPL-3.0-only",
  "links": { "home": "https://example.org/gold-flower" },
  "icon": "media/icon.png",
  "pictures": ["media/shot1.png"],
  "game": { "id": "RMG", "regions": ["RMGE01", "RMGP01", "RMGJ01"] },
  "nebula": ">=2.0.0",
  "needs": [ { "id": "lumakit", "version": ">=1.0.0" } ],
  "conflicts": [ { "id": "other-flower", "why": "Both change the Fire Flower." } ],
  "load": { "after": ["lumakit"], "before": [] },
  "takes_over": ["goldflower-old"],
  "options": [
    { "key": "balls", "kind": "number", "default": 3, "min": 1, "max": 10, "step": 1, "name": "Balls at once" },
    { "key": "look", "kind": "choice", "default": "shiny", "values": ["shiny", "matte"], "name": "Look",
      "value_names": { "shiny": "Shiny", "matte": "Matte" } }
  ],
  "permissions": [ { "name": "net.internet", "hosts": ["api.example.org"], "why": "Uploads your best times." } ],
  "renderer": { "features": ["ray_tracing"] },
  "layers": [
    { "name": "base" },
    { "name": "matte", "when": { "option": { "look": "matte" } } },
    { "name": "pal-text", "when": { "region": ["RMGP01"] } }
  ],
  "code": { "platforms": ["windows-x64", "linux-x64", "android-arm64", "macos-arm64", "ios-arm64"],
            "libraries": { "windows-x64": ["steam_api64.dll"] } },
  "saves": [ { "region": "RMGE01", "file": "saves/RMGE01/GameData.bin", "why": "Starts you at the first level." } ],
  "kit": { "lumakit.player.form": 1 },
  "build": { "lumalink": "1.0.0+abcdef0", "self": "<self-description module id>" }
}
```

| Key | Required | Meaning and checks |
|---|---|---|
| `format` | yes | `"1.<minor>"`: the format this file needs. A game reading format 1.n refuses a higher n |
| `id` | yes | 3 to 64 characters: lowercase letters, digits, `-`, `.`; starts with a letter; no `..`. The mod's identity: needs, conflicts, save data and its folders are keyed by it. Two installed mods with one id are an error |
| `version` | yes | Semantic version `MAJOR.MINOR.PATCH` with an optional `-pre`; compared by semver rules |
| `name` | yes | 1 to 64 characters, one line |
| `summary` | yes | 1 to 200 characters, one line |
| `text` | no | Per language, by the game's names for its 14 (`UsEnglish`, `EuEnglish`, `UsFrench`, `EuFrench`, `UsSpanish`, `EuSpanish`, `EuGerman`, `EuItalian`, `EuDutch`, `UsPortuguese`, `JpJapanese`, `KrKorean`, `CnSimpChinese`, `TwTradChinese`), plus any a language mod adds: `name`, `summary`, option `name`/`value_names`, and `permissions` (each asked permission's `why` by its name, 1 to 200 characters; a permission the mod does not ask is L202). The game shows the player's language when present, else the manifest's own text |
| `authors` | yes | 1 to 16 strings |
| `license` | no | An SPDX expression |
| `links` | no | `home`, `source`, `issues`: `https` URLs |
| `icon` | no | A PNG under `media/`, square, 64 to 1024 pixels |
| `pictures` | no | Up to 8 PNG or JPEG under `media/`, at most 4096 pixels a side. A file under `media/` named by neither `icon` nor `pictures` is an error |
| `game` | yes | `id`: `"RMG"` (Super Mario Galaxy; anything else is refused with the game named). `regions`: the programs the mod works with; default all three. A player whose game is another region sees the mod as off, with the reason |
| `nebula` | no | A semver range of Nebula releases. Default: any release with this format |
| `needs` | no | Other mods by id, with a semver range; each must be installed, on and loaded before this one, or this mod is off with the reason |
| `conflicts` | no | Mods by id that must not be on with this one, each with `why` (shown to the player) |
| `load` | no | `after`/`before`: ids this mod loads after or before when both are on. A cycle is an error naming it (L212) |
| `takes_over` | no | Earlier ids of this mod: their saves and options move to this id |
| `options` | no | Up to 64 (section 4.3) |
| `permissions` | no | What the mod's code may do beyond the game (section 4.1) |
| `renderer` | no | The graphics features the mod cannot do without (section 4.2) |
| `layers` | no | The layers, in the order they apply. Default `[{"name": "base"}]` when `layers/base/` exists. `name` as an option key. `when` (all must hold): `option` (key to value or list of values), `region` (list), `language` (list of language folders), `platform` (list), `code` (`true`: on only when the mod's code chooses it, in `on layers` (or luma.h's `layers`), asked once a start before any mod starts; the game's files are prepared with the answer, and an answer that differs from the one they were prepared with starts the game again once, before it reads them; a mod with no such call has none of these layers on; a layer with `code: true` on a platform the mod has no code for is an error). A folder under `layers/` not listed, or a listed layer without its folder, is an error |
| `code` | when `code/` exists | `platforms`: those built (section 6). `libraries`: per platform, the extra libraries in the order they load, each a file name in `code/<platform>/`. Any extra library needs the permission `native` |
| `saves` | when `saves/` exists | Each: `region`, `file` (under `saves/<region>/`), `why` (one line, shown when the player is asked). Section 6.1 |
| `kit` | when `kit/` exists | Module ids and the schema version of each module's data; validated by that module's schema |
| `build` | when `code/` exists and LumaLink built it | What `verify` needs: the LumaLink version and commit, and the self-description it built against |

### 4.1 Permissions

1. Names. `name` is one of: `native`, `fs.exchange`, `net.local`, `net.internet`, `input.inject`, `input.devices`,
   `audio.microphone`, `capture.camera`, `system.clipboard`, `system.links`, `save.game`. Anything else is L211
   `Perm.Unknown`; the older single `net` is L211 `Perm.NetSplit` ("write net.local or net.internet"). Each name at
   most once (L211 `Perm.Twice`), each with `why` (one line, 1 to 200 characters; L211 `Perm.NoWhy`), translated in
   `text.<language>.permissions.<name>`. At most 11 entries.
2. A test permission. A name `test.<parts>` is a test service's (services/registry.md). It is the Note L219
   (`{mod}: permission "{key}" is a test permission: only a test run provides it.`), not an error: the mod still
   loads, and its import of a `test.` service is L505 outside a test run, since no game catalogs one.
3. `hosts`. Only `net.internet` takes `hosts` (L211 `Perm.HostsOnlyNet`), and it must (L211 `Perm.NetHosts`): 1 to 32
   entries (L211 `Perm.HostCount`). An entry is `*` alone, a name (`api.example.org`), a name under a domain
   (`*.example.org`), or an IPv4 address in dotted digits (four numbers 0 to 255, no leading zero). No port, no IPv6
   literal, no name whose last part is all digits. Anything else is L204 `Expect.Host`. Letters of either case; two
   entries equal once lowercased are L211 `Perm.HostTwice` (the game lowercases them).
4. `roots`. A `net.local` or `net.internet` permission may take `roots`: 1 to 8 package paths (L211
   `Perm.RootCount`), each `certs/<path>` with no empty, `.` or `..` part and the container's name rules (L211
   `Perm.RootPath`), each once (L211 `Perm.RootTwice`), each a file of the package (L211 `Perm.RootMissing`) of at
   most 64 KiB (L211 `Perm.RootSize`) holding at least one `-----BEGIN CERTIFICATE-----` block (L211
   `Perm.RootNotPem`). On any other permission it is L211 `Perm.RootsOnlyNet`; a value not an array is L204
   `Expect.Array`. What the game does with them is in services/net.md.
5. Declared and not needed is the Note L218 (`{mod} asks for the "{permission}" permission, but no code of it imports
   a service that needs it.`): `native` with no `code.libraries` entry on any platform; any other when no entry
   library of any platform imports (optional or not) the service of that name. It is said only when every listed
   platform's entry library was read, and for a mod with no code always. A Note, never an Error: a library may ask
   `core->service()` by name at run time.
6. Needed and not declared: an import of a service whose permission the mod does not declare is L506, optional or
   not (an undeclared permission is never granted, so the import could never bind).

### 4.2 Renderer features

1. `renderer` (object): `{"features": ["ray_tracing"]}`, the renderer features the mod cannot do without. `features`
   is required in it and is its only key (L203, L202). 1 to 64 names (L204 `Expect.Count`), each `[a-z][a-z0-9_]*`,
   at most 32 bytes, no `__`, not ending in `_` (L204 `Expect.FeatureName`), each once (L204 `Expect.Once`). A mod
   that can do without a feature does not list it and asks at run time (`draw.native`'s capability query).
2. The game's list is the self-description's `renderer.features`: the names any device of this build may have. A
   mod's name not on it is L204 `Expect.Feature` ("a graphics feature the game's renderer lists").
3. L806 (Error; the mod is off): `{mod} needs {feature}, which this device's graphics do not have.` One finding per
   missing feature, at `/renderer/features/<i>` of luma.json. The manifest's rules run at every start, so a new GPU
   is read at once. LumaLink, which has no device, leaves L806 not checked (`Missing.Device`).

### 4.3 Options

`key` (as `id`'s characters), `kind` (`choice`, `number`, `toggle`, `text`, `key`), `default`, `name`, and by kind:
`values` and `value_names` (choice), `min`/`max`/`step` (number), `max_length` (text, 1 to 256), nothing more for
`toggle` and `key` (a bindable action).
1. Every kind has a `default`. A default outside its own range is L210.
2. `text`: the default is at most `max_length` code points and holds no newline or other control character
   (L210 `Option.DefaultTooLong`).
3. `key`: the default is one of the game's controls as its input bindings name them, without case: `a b one two plus
   minus home dpad_up dpad_down dpad_left dpad_right stick_up stick_down stick_left stick_right c z shake
   nunchuk_shake pointer_up pointer_down pointer_left pointer_right pointer_recenter jump_back`. Else L210
   `Option.KeyDefaultUnknown`. The input service makes it an action with that default; the player's binding lives
   only in input's store.
4. `layers[].when.option` naming a `text` or `key` option is L301 `Layer.OptionNotChoosing`: the mod reads those
   itself; they choose no layer.

### 5. Layers

```lumascript skip a listing from the part's contract, not a module that builds alone
layers/<layer>/
  files/<game path>              a whole file placed at a game path (new, or replacing the game's)
  patches/<game path>.json       rows changed, added or removed by key or by selector in a file that has rows
  assets/<mod path>              new assets of the mod's own, named by mod path (models, textures, sounds, effects...)
  textures/<texture>.png|.dds    the game's textures replaced by name, as a texture pack
```

- `textures/` replaces the game's textures by name, as a texture pack the player is given does. Names are the
  game's `<model>/<texture>.png` (or `.dds`, which keeps its own mip levels) or Dolphin's `tex1_...` names, which the
  game names once at install against the player's game. Each picture is read at the start as the pack reader reads
  it, and one it would refuse is a finding in the mod log naming the mod and the file. Anything else in `textures/` is
  L306. A texture of the mod's own models goes in the model's file or `assets/`, not here.
- A mod may be made of texture pictures alone: a `luma.json` and `layers/base/textures/`, no code, no other files.
  Its pictures are checked and cooked at the start like any other mod's:

  ```text
  shiny-coins/
    luma.json
    layers/base/textures/Coin/CoinBody.png
  ```

- Game paths are the disc's paths (`ObjectData/Kuribo.arc`, `StageData/EggStarGalaxy/EggStarGalaxyMap.arc`), a
  file inside an archive written as `ObjectData/Kuribo.arc/Kuribo.bdl`. They are matched to the player's game without
  regard to case and written in the game's case; a path that matches nothing is a new file, and is allowed only
  for the kinds the game finds by name (an archive, a stream, a movie...). Each kind's page says which.
- Files with rows are patched, never replaced. Tables (`.bcsv` and the store's tables), stage placement, scenario
  data, message files and any other kind with rows: a `files/` entry at such a path is an error naming it and saying
  "patch it by key". LumaLink's `convert` makes the patches by comparing a Wii mod's whole file with the player's
  game.
- Whole files (models, textures, streams, layouts' pictures, movies) replace the game's. Two mods that are on and
  replace the same path conflict: the one higher in the mod list is used (L305), and the Mods screen names both and
  the path (never silent).
- Sources or game formats. A whole file may be a game format (an `.arc` with `.bdl`, a `.bti`, an `.ast`) or an
  ordinary source (`.gltf`/`.glb`, `.png`, `.ktx2`, `.dds`, `.wav`, `.ogg`, `.mid`, `.json`, `.ttf`, `.otf`,
  `.webm`, `.mp4`) at the game path with its own ending: `files/ObjectData/Mario.arc/Mario.glb`. Each source kind's
  page says what it may stand in for and how it is checked (units, joints, materials).
- Mod paths under `assets/` are the mod's own namespace: the game knows them as `<id>:<path>` and nothing else
  can collide with them.

### 5.1 Patches

`layers/<layer>/patches/<game path>.json`:

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

- `key` must be the key the game defines for that kind of file (format/table-keys.md): the validator says which key
  it expected. A table's key matches with or without regard to case as defined for that table (the ray tracing
  tables match without). A row not found by `where` or `remove` is an error naming the key's values; an `add` whose
  key exists is an error.
- Patches apply from the bottom of the mod list up, then in layer order, then file order. Two mods that `set` the
  same cell, or one `add`s a row another `remove`s, conflict: the one higher in the list wins (L404) and both are
  named on the Mods screen with the cell.
- A patch on a file another mod brings is `layers/<layer>/patches/@<id>/<mod path>.json`, and that mod must be in
  `needs`.
- `select` changes every row whose cells match: `equals`, `prefix` or `glob` on one column each, all of them
  holding. Selectors are data, never code. The validator checks that each column exists for the file's kind; at load
  the number of rows each selector matched is reported, and a selector that matched none is a Note naming it. Within
  one mod, `set` by key applies before `select`; between mods, list order as above.
- What a cell's value may be is checked against the column's type from the game's own description of the file.

### 6. Code

```lumascript skip a listing from the part's contract, not a module that builds alone
code/<platform>/mod.<ending>          the entry library: exports luma_mod
code/<platform>/<library file>        each extra library named in code.libraries[<platform>], loaded first, in order
```

| Platform | Library | Notes |
|---|---|---|
| `windows-x64`, `windows-arm64` | `mod.dll` | |
| `linux-x64`, `linux-arm64` | `mod.so` | |
| `macos-arm64`, `macos-x64` | `mod.dylib` | |
| `android-arm64`, `android-x64` | `mod.so` | Loaded by the game's own loader |
| `ios-arm64` | `mod.dylib` | Unsigned; signed on the device |

`lumalink build` makes all nine by default.

- The entry library exports `luma_mod` (`luma.h`) and imports nothing from the system but what the platform's
  loader allows (lumalink/loader.md lists each platform's); `luma.h` gives the compiler's own `memcpy`/`memset` calls
  and the stack probe of the Windows targets, so a frame over 4 KiB still imports nothing. The validator reads the
  import list and refuses any other, naming it. It may import from the mod's extra libraries.
- An extra library (a physics or XR library) imports from the system by nature. It is allowed only with the
  permission `native`, whose consent text says the mod runs code with full access to the device; the validator lists
  every system import of each extra library in its report. On iOS an extra library is allowed only as the platform's
  signing allows; otherwise it is an error with the reason.
- Every name the library asks for (`LumaImport`) is checked against the game's self-description at import, and at
  pack time by LumaLink against the self-description it built with: same rule, same message. A service the code uses
  is an import row of kind 5 (`LUMA_IMPORT_SERVICE`, name and `major.minor`), bound with the rest.
- A function may be replaced (LUMA_REPLACE) by one installed mod at a time: the mod higher in the list wins, since
  the loader binds from the top down, and the lower mod's replace is refused (L805 naming the higher).
- A platform missing from `code.platforms` means the mod's code does not run there: the mod is off on that platform
  with the reason, unless it has no code at all.
- No other files under `code/`.

### 6.1 Saves a mod brings

A mod may bring save files of the game (`saves/<region>/`, named in `saves`): a mod that starts later in the game,
a challenge file. They are never written silently. When the mod is turned on, the game offers each one for the
player's region through `save.game`'s import: the player sees `why`, chooses a user file to replace or declines, and
confirms in the game's own screen. A save's region is its folder (`saves/<region>/<file>`); the game's own save check
reads it. A file that is not a save of the named region, or a region the mod does not list in `game.regions`, is an
error.

### 7. What is an error, and how it is reported

Every rule above is a rule of the validator (format/validator.md), with a code and a message in each of the game's
languages. A message always names the mod (`id`, or the file name when the manifest cannot be read), the entry
inside the `.luma`, the line and column for JSON, and what was expected. The validator never stops at the first
error: it reports every error it can find in one pass.

### 8. Versioning

- Format `1.0` is this one. A minor adds keys, folders, layer conditions, patch kinds; it never changes the meaning
  of anything in an earlier minor.
- A game supports a range of minors and refuses a higher one with "made for a newer Nebula (format 1.n)".
- Each content kind is versioned on its own, inside the format (a `patch` or a source's own version key).

### 9. How it is tested

- One broken `.luma` per rule of sections 2 to 6, each giving exactly its message in LumaLink `check` and in the
  game's import (same text, section 7), through the harness's `install: "import"`.
- A round trip: LumaLink `pack` then the game's import of the same project accepts it with nothing reported.
- The conversion of each SMG1 mod LumaLink reads produces a `.luma` that passes.
