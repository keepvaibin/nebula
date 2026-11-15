---
title: luma.json
shortTitle: luma.json
intro: Every key of a mod's manifest, luma.json, with the rules the validator checks.
contentType: reference
---

## About luma.json

`luma.json` is the manifest at the top of every mod. It says who made the mod, which game it is for, and what it brings. It is UTF-8 JSON with no comments and no duplicate keys, at most 1 MiB.

`lumalink new` writes the smallest valid manifest:

```shell
lumalink new moon-jump --id moon.jump --name "Moon jump" --summary "Hold jump to rise." --author me
```

```json
{
  "format": "1.0",
  "id": "moon.jump",
  "version": "0.1.0",
  "name": "Moon jump",
  "summary": "Hold jump to rise.",
  "authors": ["me"],
  "game": {"id": "RMG", "regions": ["RMGE01", "RMGP01", "RMGJ01"]}
}
```

`lumalink add` writes the other keys, such as `needs`, `options`, `permissions`, `layers` and `text`, so the file stays valid. For more information, see [lumalink add](lumalink-commands/add.md).

## Example of a full manifest

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
  "layers": [
    { "name": "base" },
    { "name": "matte", "when": { "option": { "look": "matte" } } },
    { "name": "pal-text", "when": { "region": ["RMGP01"] } }
  ],
  "code": { "platforms": ["windows-x64", "linux-x64", "android-arm64", "macos-arm64", "ios-arm64"],
            "libraries": { "windows-x64": ["steam_api64.dll"] } },
  "saves": [ { "region": "RMGE01", "file": "saves/RMGE01/GameData.bin", "why": "Starts you at the first level." } ],
  "kit": { "lumakit.player.form": 1 },
  "build": { "lumalink": "1.0.0+abcdef0", "self": "SELF-DESCRIPTION-ID" }
}
```

## Keys

A key not on this list is an error (L202), and a missing required key is an error (L203).

| Key | Required | Meaning and rules |
|---|---|---|
| `format` | Yes | The format version the mod needs, such as `"1.0"`. A game older than that refuses the mod (L205). |
| `id` | Yes | 3 to 64 characters: lowercase letters, digits, `-` and `.`, starting with a letter (L206). |
| `version` | Yes | A semantic version, `MAJOR.MINOR.PATCH`, with an optional `-PRERELEASE`. Compared by semantic versioning rules. |
| `name` | Yes | 1 to 64 characters on one line. |
| `summary` | Yes | 1 to 200 characters on one line. |
| `text` | No | The mod's text in other languages. For more information, see [text](#text). |
| `authors` | Yes | 1 to 16 names. |
| `license` | No | An SPDX license expression. |
| `links` | No | `home`, `source` and `issues`: each an `https` URL. |
| `icon` | No | A square PNG under `media/`, 64 to 1024 pixels. |
| `pictures` | No | Up to 8 PNG or JPEG files under `media/`, each at most 4096 pixels on a side. A file under `media/` that neither `icon` nor `pictures` names is an error. |
| `game` | Yes | `id`: `"RMG"` (Super Mario Galaxy). Any other game is refused, with the game named. `regions`: the discs the mod works with (`RMGE01`, `RMGP01`, `RMGJ01`). The default is all three. A player with another region sees the mod as off, with the reason. |
| `library` | No | `true`: the mod gives functions to other mods (its build writes `code/MOD-ID.lumi`) and runs only while a mod that is on needs it, directly or through another library. Until then, it adds nothing. The default is `false`: a mod that gives functions still runs on its own. |
| `nebula` | No | A semantic version range of Nebula releases. The default is any release that reads this format. |
| `needs` | No | Other mods by `id`, each with a `version` range. Each must be installed, on and loaded before this mod, or this mod is off with the reason. |
| `conflicts` | No | Mods by `id` that must not be on with this mod, each with `why`, which the player sees. |
| `load` | No | `after` and `before`: mod IDs this mod loads after or before, when both are on. A cycle is an error that names it (L212). |
| `takes_over` | No | Earlier IDs of this mod. Their saves and options move to this ID. |
| `options` | No | Up to 64 options the player sets. For more information, see [options](#options). |
| `permissions` | No | What the mod's code may do beyond the game. For more information, see [permissions](#permissions). |
| `renderer` | No | The graphics features the mod cannot do without. For more information, see [renderer](#renderer). |
| `layers` | No | The layers, in the order they apply. For more information, see [layers](#layers). |
| `code` | When `code/` exists | `platforms`: the platforms built. `libraries`: for each platform, extra library file names in `code/PLATFORM/`, in the order they load. Any extra library needs the `native` permission. |
| `saves` | When `saves/` exists | Each save: `region`, `file` (under `saves/REGION/`) and `why` (one line, which the player sees when asked). |
| `kit` | When `kit/` exists | Each kit module's ID and the version of its data schema. The module's schema checks the data. |
| `build` | When LumaLink built `code/` | What `lumalink verify` needs: the LumaLink version and commit, and the self-description the build used. |
| `pre_standard` | No | Keys of the previous mod format. For more information, see [Keys of the previous format](#keys-of-the-previous-format). |

### text

`text` holds the mod's text in other languages, keyed by the game's names for its 14 languages: `UsEnglish`, `EuEnglish`, `UsFrench`, `EuFrench`, `UsSpanish`, `EuSpanish`, `EuGerman`, `EuItalian`, `EuDutch`, `UsPortuguese`, `JpJapanese`, `KrKorean`, `CnSimpChinese` and `TwTradChinese`, plus any language a language mod adds.

Each language can have:

* `name` and `summary`.
* For each option, `name` and `value_names`.
* `permissions`: each permission's `why`, by the permission's name, 1 to 200 characters. A permission the mod does not ask for is an error (L202).

The game shows the text in the player's language. When it is missing, the game shows the same language for the other region (for example `EuFrench` for a `UsFrench` player), and then the manifest's own text.

### options

Each option has a `key` (the same characters as `id`), a `kind`, a `default` and a `name`. Each kind has its own keys:

| Kind | Keys | Value |
|---|---|---|
| `choice` | `values`, and `value_names` for their names | One of `values`. |
| `number` | `min`, `max` and `step` | A number in the range. |
| `toggle` | None | `true` or `false`. |
| `text` | `max_length`, from 1 to 256 | Text of at most `max_length` code points, with no newline or other control character. |
| `key` | None | A bindable action. |

A `default` outside its own range is an error (L210).

A `key` option is one of the mod's input actions. Its `default` is one of the game's controls, as the input bindings name them, without regard to case: `a`, `b`, `one`, `two`, `plus`, `minus`, `home`, `dpad_up`, `dpad_down`, `dpad_left`, `dpad_right`, `stick_up`, `stick_down`, `stick_left`, `stick_right`, `c`, `z`, `shake`, `nunchuk_shake`, `pointer_up`, `pointer_down`, `pointer_left`, `pointer_right`, `pointer_recenter` or `jump_back`. Anything else is an error (L210 `Option.KeyDefaultUnknown`). The action follows that control until the player binds it on the **CONTROLS** page, and the player's binding is kept only in the game's settings.

A layer whose `when.option` names a `text` or `key` option is an error (L301 `Layer.OptionNotChoosing`): the mod reads those options itself, and they choose no layer.

### permissions

Each permission is an object with a `name` and a `why`. At most 11 entries are allowed.

| Name | What the player is told |
|---|---|
| `audio.microphone` | **Microphone**: "Hears your microphone." |
| `capture.camera` | **Camera**: "Sees your camera." |
| `fs.exchange` | **Exchange folder**: "Reads and writes files in the Exchange folder, which you and other mods can open." |
| `input.devices` | **Other devices**: "Uses Bluetooth, USB and MIDI devices connected to this device." |
| `input.inject` | **Controls**: "Presses buttons and moves the controls as if it were you." |
| `native` | **Native libraries**: "Runs libraries of its own with full access to this device." |
| `net.internet` | **Internet**: "Connects to the internet:" followed by the hosts in `hosts`. |
| `net.local` | **Local network**: "Connects to devices on your network and to programs on this device." |
| `save.game` | **Game saves**: "Reads your save files and may offer one to replace; you confirm each replacement." |
| `system.clipboard` | **Clipboard**: "Reads what you copy, text and pictures, and can replace it." |
| `system.links` | **Web pages**: "Opens web pages in your browser." |

* Any other name is an error (L211 `Perm.Unknown`). The older single `net` is an error that says to write `net.local` or `net.internet` (L211 `Perm.NetSplit`).
* Each name appears at most once (L211 `Perm.Twice`).
* Each `why` is one line of 1 to 200 characters (L211 `Perm.NoWhy`). Translate it in `text.LANGUAGE.permissions.NAME`.
* A name `test.PARTS` is a test service's permission. It is a note (L219), not an error: the mod still loads, but its import of a `test.` service fails outside a test run (L505).
* Asking for a permission no code needs is a note (L218): `native` with no `code.libraries` entry, or any other permission when no entry library imports a service that needs it. It is a note, never an error, because a library can ask for a service by name while it runs.
* Importing a service whose permission the mod does not ask for is an error (L506), because a permission the mod does not declare is never granted.

#### hosts

Only `net.internet` takes `hosts` (L211 `Perm.HostsOnlyNet`), and it requires it (L211 `Perm.NetHosts`): 1 to 32 entries (L211 `Perm.HostCount`). Each entry is one of these:

* `*` alone.
* A host name, such as `api.example.org`.
* A host name under a domain, such as `*.example.org`.
* An IPv4 address in dotted digits: four numbers from 0 to 255, with no leading zero.

An entry has no port. IPv6 literals and names whose last part is all digits are not allowed (L204 `Expect.Host`). Letters can be either case, but two entries that are equal in lowercase are an error (L211 `Perm.HostTwice`).

#### roots

A `net.local` or `net.internet` permission can take `roots`: the TLS root certificates the mod adds for its own hosts.

* 1 to 8 paths (L211 `Perm.RootCount`), each `certs/PATH` with no empty, `.` or `..` part (L211 `Perm.RootPath`).
* Each path appears once (L211 `Perm.RootTwice`) and is a file in the package (L211 `Perm.RootMissing`).
* Each file is at most 64 KiB (L211 `Perm.RootSize`) and holds at least one `-----BEGIN CERTIFICATE-----` block (L211 `Perm.RootNotPem`).
* `roots` on any other permission is an error (L211 `Perm.RootsOnlyNet`).

For how the game uses them, see [net service](services/net.md).

### renderer

`renderer` has one key, `features`: the graphics features the mod cannot do without, for example `{"features": ["FEATURE-NAME"]}`.

* 1 to 64 names (L204 `Expect.Count`). Each name matches `[a-z][a-z0-9_]*`, is at most 32 bytes, has no `__` and does not end in `_` (L204 `Expect.FeatureName`). Each appears once (L204 `Expect.Once`).
* A name the game's renderer does not list is an error (L204 `Expect.Feature`).
* When a player's graphics card lacks a listed feature, the mod is off with the message "MOD needs FEATURE, which this device's graphics do not have." (L806). The game checks this at every start, so a new graphics card counts at once. LumaLink has no device, so it does not check L806.

A mod that can work without a feature does not list it, and asks while it runs instead.

### layers

Each layer has a `name`, written with the same characters as an option key, and an optional `when`. When `luma.json` has no `layers` key and `layers/base/` exists, the default is `[{"name": "base"}]`.

A layer is on when every condition in its `when` holds:

| Condition | Value |
|---|---|
| `option` | An option key, with a value or a list of values. |
| `region` | A list of regions. |
| `language` | A list of language folders. |
| `platform` | A list of platforms. |
| `code` | `true`: the layer is on only when the mod's code chooses it. |

A `code` layer is chosen in the mod's `on layers` block (or `luma.h`'s `layers`), which the game asks once at each start, before any mod starts. The game prepares its files with the answer. If a later answer differs from the one the files were prepared with, the game starts again once, before it reads them. A mod with no such block has none of its `code` layers on. A `code` layer on a platform the mod has no code for is an error.

A folder under `layers/` that is not listed, and a listed layer without a folder, are both errors.

## Keys of the previous format

Keys of the previous mod format are refused (L220), with what to write instead:

| Previous key | Write |
|---|---|
| `plugins` | `code` (`code/PLATFORM/mod.ENDING`) |
| `description` | `summary` |
| `homepage` | `links.home` |
| `load_after`, `load_before` | `load.after`, `load.before` |
| `regions`, `versions` | `game.regions` |
| `requires` | `renderer.features` |
| A layer's `path` | The layer's `name` |
| An option's `id`, `choices`, `range` | `key`, `values`, `min`, `max` and `step` |
| A conflict's `below` | `why` |
| A layer's rows, `when.mods`, `when.version`, `when.not`, `when.any` | `pre_standard.layers.LAYER.KEY` |

`pre_standard` holds the previous format's rows: `galaxies`, `scenario`, `stages`, `story`, `music`, `event_flags`, `open_conditions`, `hungry_luma`, `change_area`, `star_bits`, `text`, `comet_schedule`, `map_position`, `achievements`, `save`, and each layer's rows under `pre_standard.layers.LAYER`. The game still reads them, and each one is a note (L221).

### A layer's pre_standard keys

A row that breaks a rule here refuses the whole manifest.

| Key | Form | Rules |
|---|---|---|
| `object_changes` | Rows `{ "object", "becomes", "in" }` | `object` is required. `in` is `placed` (the default), `display_models` or `treasure_boxes`. |
| `comet_time_limits` | Rows `{ "galaxy", "mission", "seconds" }` | All three are required. `mission` and `seconds` are whole numbers. |
| `stages` | Rows `{ "stage", "scenario", "acts_as", "own_rules" }` | `stage` is required. `acts_as` is a list of stage names. `own_rules` is `true` or `false`. |
| `player_constants` | `{ "mario" \| "luigi" \| "both": { "NAME": VALUE } }` | A value is a number, a list of numbers (`null` keeps the game's number at that place), or `{ "option": "KEY" }` for an option's value. A number out of a float's range is refused when the game starts, and nothing is set. |
| `tunables` | `{ "CLASS.FIELD": VALUE }` | The key contains a `.`. A value is a number, `true` or `false`, a list (`null` keeps the game's value at that place), or `{ "option": "KEY" }`. |
| `sound_effects` | Rows `{ "name", "file" }` | Both are required. |
| `player_animations` | Rows `{ "name", "file", "rate", "blend_frames" }` | `name` and `file` are required. `rate` is above 0 and at most 100. `blend_frames` is a whole number from 0 to 600. |
| `object_classes` | Rows `{ "name", "like" }` | A new class name, made like a game class. Both are required. |
| `forms` | Rows `{ "name", "model", "music", "acts_like", "looks_like", "color_frame", "frames" }` | `name` is required. `acts_like` and `looks_like` name a game form: `normal`, `invincible`, `fire`, `ice`, `bee`, `spring`, `boo`, `flying` or `tornado`. `color_frame` is a whole number from 0 to 255, and `frames` from 0 to 65535. |
| `materials` | `{ "MATERIAL": "FILE" }` | The path stays inside the mod. |

A row's `script`, and the keys `objects` and `scripts`, belong to the previous format's scripts. The game refuses the mod at load with the reason. Use LumaScript in `source/` instead.

## Further reading

* [The .luma file format](luma-file-format.md)
* [Validator rules](validator-rules.md)
* [lumalink new](lumalink-commands/new.md)
