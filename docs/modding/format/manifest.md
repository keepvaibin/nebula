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

## Limits

- An id is 3 to 64 of lowercase letters, digits, `-` and `.`, starting with a letter (L206).
- `summary` is one line of at most 200 characters; `authors` holds 1 to 16 names.
- A `.luma` made for a newer format than the game reads is refused (L205).

## See also

- `format/luma.md` (section 4: every key; section 5: layers and `when`)
- `format/rules.md` (L2xx)
- `lumalink/cmd-add.md`, `lumalink/cmd-new.md`
