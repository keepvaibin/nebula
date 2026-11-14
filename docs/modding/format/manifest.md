# luma.json

The manifest at the top of every mod: who made it, what game it is for, and what it brings.

## Smallest example

`lumalink new moon-jump --id moon.jump --name "Moon jump" --summary "Hold jump to rise." --author me` writes:

```json luma.json
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

`format`, `id`, `version`, `name`, `summary`, `authors` and `game` are required. Every key, with its rules, is in
[the .luma format, section 4](luma.md). `lumalink add` writes the others (needs, options, permissions, layers, text)
so the file stays valid.

## Reference

A key that is not in section 4 is L202; a required key that is missing is L203. Keys of the previous engine are
refused with L220, which names what to write instead:

| previous key | write |
|---|---|
| `plugins` | `code` (`code/<platform>/mod.<ending>`) |
| `description` | `summary` |
| `homepage` | `links.home` |
| `load_after`, `load_before` | `load.after`, `load.before` |
| `regions`, `versions` | `game.regions` |
| `requires` | `renderer.features` |
| a layer's `path` | its `name` |
| option `id`, `choices`, `range` | `key`, `values`, `min` / `max` / `step` |
| conflict `below` | `why` |
| a layer's rows, `when.mods`, `when.version`, `when.not`, `when.any` | `pre_standard.layers.<name>.<key>` |

`pre_standard` holds the previous engine's rows (`galaxies`, `scenario`, `stages`, `story`, `music`, `event_flags`,
`open_conditions`, `hungry_luma`, `change_area`, `star_bits`, `text`, `comet_schedule`, `map_position`,
`achievements`, `save`, and a layer's under `pre_standard.layers.<layer>`). The game still reads them; each one is
the Note L221.

### A layer's `pre_standard` keys

`pre_standard.layers.<layer>` takes these keys. A row that breaks a rule here refuses the whole manifest.

| Key | Form | Rules |
|---|---|---|
| `object_changes` | rows `{ "object", "becomes", "in" }` | `object` is required. `in` is `placed` (the default), `display_models` or `treasure_boxes`. |
| `comet_time_limits` | rows `{ "galaxy", "mission", "seconds" }` | All three are required. `mission` and `seconds` are whole numbers. |
| `stages` | rows `{ "stage", "scenario", "acts_as", "own_rules" }` | `stage` is required. `acts_as` is a list of stage names. `own_rules` is `true` or `false`. |
| `player_constants` | `{ "mario" \| "luigi" \| "both": { "<name>": value } }` | A value is a number, a list of numbers (a `null` keeps the game's number at that place), or `{ "option": "<key>" }` for an option's value. A number past a float's range is refused when the game starts, and nothing is set. |
| `tunables` | `{ "<Class>.<field>": value }` | The key holds a `.`. A value is a number, `true` or `false`, a list (a `null` keeps the game's value at that place), or `{ "option": "<key>" }`. |
| `sound_effects` | rows `{ "name", "file" }` | Both are required. |
| `player_animations` | rows `{ "name", "file", "rate", "blend_frames" }` | `name` and `file` are required. `rate` is above 0 and at most 100. `blend_frames` is a whole number from 0 to 600. |
| `object_classes` | rows `{ "name", "like" }` | A new class name made like a game class. Both are required. |
| `forms` | rows `{ "name", "model", "music", "acts_like", "looks_like", "color_frame", "frames" }` | `name` is required. `acts_like` and `looks_like` name a game form: `normal`, `invincible`, `fire`, `ice`, `bee`, `spring`, `boo`, `flying` or `tornado`. `color_frame` is a whole number from 0 to 255, `frames` from 0 to 65535. |
| `materials` | `{ "<material>": "<file>" }` | The path stays inside the mod. |

A row's `script`, and the keys `objects` and `scripts`, are the previous engine's scripts: the mod is refused at load with
the reason, and LumaScript (`source/`) takes their place.

## Limits

- An id is 3 to 64 of lowercase letters, digits, `-` and `.`, starting with a letter (L206).
- `summary` is one line of at most 200 characters; `authors` holds 1 to 16 names.
- A `.luma` made for a newer format than the game reads is refused (L205).

## See also

- `format/luma.md` (section 4: every key; section 5: layers and `when`)
- `format/rules.md` (L2xx)
- `lumalink/cmd-add.md`, `lumalink/cmd-new.md`
