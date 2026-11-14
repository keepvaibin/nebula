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
| Case | two entries differing only in case are an error (Windows, macOS and Android's storage treat them as one) |
| Entries | files and folders only: no links, no device files; no entry twice |
| Encryption, multi-disk | refused |
| Limits | at most 200,000 entries; a file at most 4 GiB unpacked; the whole at most 64 GiB unpacked; the stated sizes are checked against the bytes actually unpacked |

### 3. The folders

| Folder | Read by the game | What |
|---|---|---|
| `luma.json` | yes | The manifest (section 4) |
| `layers/<layer>/` | yes | Content: files placed at game paths, patches, new assets (section 5) |
| `code/<platform>/` | yes | The built libraries for each platform: the entry library and any others (section 6) |
| `media/` | yes (Mods screen) | The icon and pictures named in `luma.json`; nothing else |
| `saves/<region>/` | yes (offered, never written silently) | Save files of the game the mod brings, per region (section 6.1) |
| `kit/<module>/` | no (data for library mods such as LumaKit, validated by the schema that module supplies; a schema is data in a fixed JSON-schema subset, never code the validator runs) | Module data |
| `source/` | no (LumaLink `verify`) | LumaScript and C sources, and the build description |
| `tests/` | no (LumaLink `test`) | The mod's own tests: `tests//test.json`, each run by the proof harness |

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
  "text": { "JpJapanese": { "name": "ゴールドフラワー", "summary": "..." } },
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
  "permissions": [ { "name": "net", "hosts": ["api.example.org"], "why": "Uploads your best times." } ],
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
| `text` | no | Per language, by the game's names for its 14 (`UsEnglish`, `EuEnglish`, `UsFrench`, `EuFrench`, `UsSpanish`, `EuSpanish`, `EuGerman`, `EuItalian`, `EuDutch`, `UsPortuguese`, `JpJapanese`, `KrKorean`, `CnSimpChinese`, `TwTradChinese`, as `localization/NebulaText.tsv` has them), plus any a language mod adds (`C.lang` in the idea corpus): `name`, `summary`, and option `name`/`value_names` in that language. The game shows the player's language when present, else `name`/`summary` |
| `authors` | yes | 1 to 16 strings |
| `license` | no | An SPDX expression |
| `links` | no | `home`, `source`, `issues`: `https` URLs |
| `icon` | no | A PNG under `media/`, square, 64 to 1024 pixels |
| `pictures` | no | Up to 8 PNG or JPEG under `media/`, at most 4096 pixels a side. A file under `media/` named by neither `icon` nor `pictures` is an error |
| `game` | yes | `id`: `"RMG"` (Super Mario Galaxy; anything else is refused with the game named). `regions`: the programs the mod works with; default all three. A player whose game is another region sees the mod as off, with the reason |
| `nebula` | no | A semver range of Nebula releases. Default: any release with this format |
| `needs` | no | Other mods by id, with a semver range; each must be installed, on and loaded before this one, or this mod is off with the reason |
| `conflicts` | no | Mods by id that must not be on with this one, each with `why` (shown to the player) |
| `load` | no | `after`/`before`: ids this mod loads after or before when both are on (order of patches and hooks). A cycle is an error naming it |
| `takes_over` | no | Earlier ids of this mod: their saves and options move to this id |
| `options` | no | Up to 64. `key` (as `id`'s characters), `kind` (`choice`, `number`, `toggle`, `text`, `key`), `default`, `name`, and by kind: `values` + `value_names` (choice), `min`/`max`/`step` (number), `max_length` (text, at most 256), nothing more for `toggle` and `key` (a bindable action). A default outside its own range is an error |
| `permissions` | no | Each `name` one of the services' permission names (`net` takes `hosts`), with `why` (one line, shown in the consent screen). A service used by the code without its permission fails at the call with a named error, and the validator refuses a library whose imports name a permissioned service the manifest lacks |
| `layers` | no | The layers, in the order they apply. Default `[{"name": "base"}]` when `layers/base/` exists. `name` as an option key. `when` (all must hold): `option` (key to value or list of values), `region` (list), `language` (list of language folders), `platform` (list), `code` (`true`: on only when the mod's code chooses it, in `on layers` (or luma.h's `layers`), asked once a start before any mod starts; the game's files are prepared with the answer, and an answer that differs from the one they were prepared with starts the game again once, before it reads them; a mod with no such call has none of these layers on; a layer with `code: true` on a platform the mod has no code for is an error). A folder under `layers/` not listed, or a listed layer without its folder, is an error |
| `code` | when `code/` exists | `platforms`: those built (section 6). `libraries`: per platform, the extra libraries in the order they load, each a file name in `code/<platform>/` (DECISIONS 3). Any extra library needs the permission `native` |
| `saves` | when `saves/` exists | Each: `region`, `file` (under `saves/<region>/`), `why` (one line, shown when the player is asked). Section 6.1 |
| `kit` | when `kit/` exists | Module ids and the schema version of each module's data; validated by that module's schema |
| `build` | when `code/` exists and LumaLink built it | What `verify` needs: the LumaLink version and commit, and the self-description it built against |

### 5. Layers

```lumascript skip a listing from the part's contract, not a module that builds alone
layers/<layer>/
  files/<game path>              a whole file placed at a game path (new, or replacing the game's)
  patches/<game path>.json       rows changed, added or removed by key or by selector in a file that has rows
  assets/<mod path>              new assets of the mod's own, named by mod path (models, textures, sounds, effects...)
  textures/<texture>.png|.dds    the game's textures replaced by name, as a texture pack (G, 2026-11-11 21:41)
```

- textures/ is the game's by-name texture replacement (runtime mods.cpp, the texture index; the same as a Dolphin
  pack the player is given). Names are the game's `<model>/<texture>.png` (or `.dds`, which keeps its own mip levels)
  or Dolphin's `tex1_...` names, which the game names once at install against the player's game (dolphin_pack.cpp).
  Anything else in textures/ is L306. A texture of the mod's own models goes in the model's file or `assets/`, not
  here.

- Game paths are the disc's paths (`ObjectData/Kuribo.arc`, `StageData/EggStarGalaxy/EggStarGalaxyMap.arc`), a
  file inside an archive written as `ObjectData/Kuribo.arc/Kuribo.bdl`. They are matched to the player's game without
  regard to case and written in the game's case; a path that matches nothing is a new file, and is allowed only
  for the kinds the game finds by name (an archive, a stream, a movie...). Each kind's contract says which.
- Files with rows are patched, never replaced. Tables (`.bcsv` and the store's tables), stage placement, scenario
  data, message files and any other kind with rows: a `files/` entry at such a path is an error naming it and saying
  "patch it by key" (rule 2). LumaLink's `convert` makes the patches by comparing a Wii mod's whole file
  with the player's game.
- Whole files (models, textures, streams, layouts' pictures, movies) replace the game's. Two mods that are on and
  replace the same path conflict: the one later in the load order is used, and the Mods screen names both and the
  path (never silent).
- Sources or game formats. A whole file may be a game format (an `.arc` with `.bdl`, a `.bti`, an `.ast`) or an
  ordinary source (`.gltf`/`.glb`, `.png`, `.ktx2`, `.dds`, `.wav`, `.ogg`, `.mid`, `.json`, `.ttf`, `.otf`,
  `.webm`, `.mp4`) at the game path with its own ending: `files/ObjectData/Mario.arc/Mario.glb`. Each source kind's
  contract says what it may stand in for and how it is checked (units, joints, materials).
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

- `key` must be the key the content system defines for that kind of file: the validator says which key it
  expected. A row not found by `where` or `remove` is an error naming the key's values; an `add` whose key exists is
  an error.
- Patches apply in load order, layer order, then file order. Two mods that `set` the same cell, or one `add`s a row
  another `remove`s, conflict: the later wins and both are named on the Mods screen with the cell.
- A patch may target a file another mod adds only when this mod `needs` that mod (DECISIONS 3).
- `select` (DECISIONS 3) changes every row whose cells match: `equals`, `prefix` or `glob` on one column
  each, all of them holding. Selectors are data, never code. The validator checks that each column exists for the
  file's kind; at load the number of rows each selector matched is reported, and a selector that matched none is a
  Note naming it. Within one mod, `set` by key applies before `select`; between mods, load order as above.
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

- The entry library exports `luma_mod` (`luma.h`) and imports nothing from the system but what the platform's
  loader contract allows (kernel contract, 1.1, which lists each platform's forced imports from evidence; the
  compiler's own `memcpy`/`memset` calls are given by `luma.h`); the validator reads its import list and refuses any
  other, naming it. It may import from the mod's extra libraries.
- An extra library (a physics or XR library) imports from the system by nature. It is allowed
  only with the permission `native`, whose consent text says the mod runs code with full access to the device; the
  validator lists every system import of each extra library in its report. On iOS an extra library is allowed only as
  the platform's signing allows; otherwise it is an error with the reason.
- Every name the library asks for (`LumaImport`) is checked against the game's self-description at import, and at
  pack time by LumaLink against the self-description it built with: same rule, same message.
- A platform missing from `code.platforms` means the mod's code does not run there: the mod is off on that platform
  with the reason, unless it has no code at all.
- No other files under `code/`.

### 6.1 Saves a mod brings

A mod may bring save files of the game (`saves/<region>/`, named in `saves`): a mod that starts later in the game,
a challenge file. They are never written silently. When the mod is turned on, the game offers each one for the
player's region through `save.game`'s import (DECISIONS 3): the player sees `why`, chooses a user file to replace
or declines, and confirms in the game's own screen. A file that is not a save of the named region, or a region the
mod does not list in `game.regions`, is an error. Corpus mod 2 (Kaizo) is the test.

### 7. What is an error, and how it is reported

Every rule above is a rule of the validator, with a code and a message in each of the game's languages (contract
`validator`). A message always names the mod (`id`, or the file name when the manifest cannot be read), the entry
inside the `.luma`, the line and column for JSON, and what was expected. The validator never stops at the first
error: it reports every error it can find in one pass.

### 8. Versioning

- Format `1.0` is this revision once signed. A minor adds keys, folders, layer conditions, patch kinds; it never
  changes the meaning of anything in an earlier minor.
- A game supports a range of minors and refuses a higher one with "made for a newer Nebula (format 1.n)".
- Each content kind's contract is versioned on its own, inside the format (a `patch` or a source's own version key).

### 9. How it is tested

- One broken `.luma` per rule of sections 2 to 6, each giving exactly its message in LumaLink `check` and in the
  game's import (same text, rule 7), through the harness's `install: "import"`.
- A round trip: LumaLink `pack` then the game's import of the same project accepts it with nothing reported.
- The conversion of each SMG1 corpus mod produces a `.luma` that passes.

1. Section 2, Names: also none of Windows' forbidden characters `< > : " | ? *`, no combining marks or conjoining
   jamo (names are written precomposed: a decomposed name is the same file as its twin on macOS), and a layer's
   name, or the first part of a path under `files/` or `assets/`, does not start with `@`. Entries: no two
   entries share data. A stated size or checksum that differs from the data is an error.
2. Section 5.1: a patch on a file another mod brings is `layers/<layer>/patches/@<id>/<mod path>.json`,
   and that mod must be in `needs`. A table's key matches with or without regard to case as the content system
   defines it for that table (the ray tracing tables match without; the validator's `Table.case_insensitive`).
3. Section 6: a function may be replaced (LUMA_REPLACE) by one installed mod at a time; another mod that is on
   and replaces it too is refused (validator L805), the order being 1.1's one rule. `luma.h` gives the compiler's
   stack probe for the MSVC ABI targets as it gives memcpy/memset, so a frame over 4 KiB still imports nothing
   (1.1). A service a mod's code uses is listed in its import rows as `LUMA_IMPORT_SERVICE = 5` (name, `major.minor`),
   bound with the rest (1.1, 2.1).
4. Section 6.1: a save's region is its folder (`saves/<region>/<file>`; a save holds none). It is checked by the
   game's own save check (the validator's port of it, one corpus).
5. Section 9: one broken `.luma` per rule is a case made in code.

- Item 3, "the order being 1.1's one rule" reads: "the mod higher in the list wins: 1.1's loader binds from the top
  down, so the higher mod's replace binds and the lower mod's is refused (L805 naming the higher)".
- The same rule in section 5's words: "Whole files ... the one higher in the mod list is used"; "Patches apply from the
  bottom of the mod list up, then layer order, then file order ... the higher mod wins"; selectors "between mods, as
  above". (table-keys 12; the applier is given the patches bottom up and the last given wins.)

Changes to section 4's `permissions` row; the rest of revisions 1 and 2 stands.

1. Names. `name` is one of the closed list of services-registry 5.1: `native`, `fs.exchange`, `net.local`,
   `net.internet`, `input.inject`, `input.devices`, `audio.microphone`, `capture.camera`, `system.clipboard`,
   `system.links`, `save.game`. A permission added later is a revision of the registry and of this list. Anything else
   is L211 `Perm.Unknown`. Each name at most once (L211 `Perm.Twice`), each with `why` (one line, 1 to 200 characters;
   L211 `Perm.NoWhy`). At most 11 entries.
2. A test permission. A name `test.<parts>` (section 2's grammar of the registry) is a test service's (registry 14).
   It is the Note L219 (`{mod}: permission "{key}" is a test permission: only a test run provides it.`), not an error:
   the mod still loads, and its import of a `test.` service is L505 outside a test run, since no game catalogs one.
3. hosts. Only `net.internet` takes `hosts` (L211 `Perm.HostsOnlyNet`), and it must (L211 `Perm.NetHosts`): 1 to 32
   entries (L211 `Perm.HostCount`). An entry is `*` alone, a name (`api.example.org`), a name under a domain
   (`*.example.org`), or an IPv4 address in dotted digits (four numbers 0 to 255, no leading zero). No port, no IPv6
   literal, no name whose last part is all digits. Anything else is L204 `Expect.Host`. Letters of either case; two
   entries equal once lowercased are L211 `Perm.HostTwice` (the host lowercases them, consent 1.2).
4. Declared and not needed is the Note L218 (`{mod} asks for the "{permission}" permission, but no code of it imports
   a service that needs it.`): `native` with no `code.libraries` entry on any platform; any other when no entry
   library of any platform imports (kind 5, optional or not) the service of that name (a permissioned service's
   permission is its own name, registry 9). It is said only when every listed platform's entry library was read, and
   for a mod with no code always. A Note, never an Error: a library may ask `core->service()` by name at run time.
5. Imports: a kind 5 import of a permissioned service whose permission is not declared is L506, optional or not (an
   undeclared permission is never granted, so the import could never bind).

A new optional key in section 4; the rest of revisions 1 to 3 stands. Built on Nebula g/3.1-render e3d1f1cfd (from
g/3.1-perm) with LumaLink g/3.1-render 0b35b2b.

1. `renderer` (object, optional): `{"features": ["ray_tracing"]}`, the renderer features the mod cannot do without.
   `features` is required in it and is its only key (L203, L202). 1 to 64 names (L204 Expect.Count), each
   `[a-z][a-z0-9_]*`, at most 32 bytes, no `__`, not ending in `_` (L204 Expect.FeatureName), each once (L204
   Expect.Once). A mod that can do without a feature does not list it and asks at run time (`draw.native`'s capability
   query).
2. The game's list is the self-description's `renderer.features`: the names any device of this build may have, in byte
   order, each once, at most 64, with the same grammar. In a names folder it is `luma_renderer.json`, an object with
   that `renderer` key, read strictly like `luma_services.json`. With the list, a mod's name not on it is L204
   Expect.Feature ("a graphics feature the game's renderer lists").
3. L806 (Error; the mod is off): `{mod} needs {feature}, which this device's graphics do not have.` One finding per
   missing feature, at `/renderer/features/<i>` of luma.json. The game gives the device's features (the C input's
   `device_features`, names; `Game.device_features`); a device name not on the game's list is ignored. Without the
   game's list L806 is not checked (Missing.RendererFeatures); without the device's features it is not checked either
   (Missing.Device: LumaLink, which has no device). The manifest's rules run at every start, so a new GPU is read at
   once; the list and the device's features are also part of the kept report's game key.
4. C input (validator section 3, appended): `const char* renderer; uint64_t renderer_size;` (the list as JSON; null:
   not checked) and `const char* const* device_features; uint32_t device_feature_count;` (null: not checked). A list
   not in form, a list without `names`, or a device name that is null or not in the grammar is the caller's fault
   (state -1, said). sizeof(LumaValidateInput) is 208 on the seven targets.

Proven on this machine: luma-validate tests (tests/renderer.rs, 8 tests) and nebula-import tests all ok; clippy -D
warnings clean on luma-validate, no new lint in nebula-import; luma_validate_layout.c clean on the seven targets;
LumaLink workspace tests and clippy clean against it.

Item 3's last sentence reads: "the list is also part of the kept report's game key; the device's features are not
(a laptop that switches GPUs keeps its kept reports, and L806 is said at the start anyway)." Built on Nebula-LG
g/3.1-c.

No format version change (v1 is unreleased).
1. Every kind has a `default` (a `key` option's was optional).
2. `text`: `max_length` 1-256 (required, as before). The default is at most `max_length` code points and holds no
   newline or other control character; spaces are the mod's. Else L210 `Option.DefaultTooLong`.
3. `key`: the default is one of the game's controls as its input bindings name them (`runtime/src/input_bindings.cpp`
   kTargetIds, without case): `a b one two plus minus home dpad_up dpad_down dpad_left dpad_right stick_up stick_down
   stick_left stick_right c z shake nunchuk_shake pointer_up pointer_down pointer_left pointer_right pointer_recenter
   jump_back`. Else L210 `Option.KeyDefaultUnknown`. The input service makes it an action with that default; the
   player's binding lives only in input's store.
4. `layers[].when.option` naming a `text` or `key` option: L301 `Layer.OptionNotChoosing` (the mod reads those itself;
   they choose no layer). Before, a text option's any string matched and a key option's said "a value it can never
   have".
Validator:

Two changes; the rest of revisions 1 to 5 stands.

1. certs/. A new top-level folder in section 3: `certs/`, read by the game only through a net permission's
   `roots` (below), PEM files of the TLS roots the mod adds for its own hosts.
2. roots. A `net.local` or `net.internet` permission may take `roots`: an array of 1 to 8 package paths
   (L211 `Perm.RootCount`), each `certs/<path>` with no empty, `.` or `..` part and the container's name rules
   (L211 `Perm.RootPath`), each once (L211 `Perm.RootTwice`), each a file of the package (L211 `Perm.RootMissing`)
   of at most 64 KiB (L211 `Perm.RootSize`) holding at least one `-----BEGIN CERTIFICATE-----` block (L211
   `Perm.RootNotPem`). On any other permission it is L211 `Perm.RootsOnlyNet`; a value not an array is L204
   `Expect.Array`. L211's `entry` argument names the file. What the host does with them is services-net 7.
