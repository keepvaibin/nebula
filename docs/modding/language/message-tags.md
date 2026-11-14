# Message tags
State: generated from luma_text_tags.h.

The game's text holds tags for waits, pages, pictures, sizes, colours and the player's name. Mod text writes
each as a word in brackets; the cook, the game's mod API and LumaScript all read this one table. Words are
matched without case; a number is decimal or 0x and hex digits.

| Written | Group | Tag | Largest number | What it does |
|---|---|---|---|---|
| `[wait n]` | 1 | 0 | 65535 | stops the text for n frames |
| `[page]` | 1 | 1 |  | ends the box; the rest comes on the next press |
| `[middle]` | 1 | 2 |  | centres the text in the box from top to bottom |
| `[center]` | 1 | 3 |  | centres the line |
| `[sound NAME]` | 2 | 0 | 124 characters | plays the system sound NAME when the text reaches it |
| `[icon n]` | 3 | n | 65535 | picture n of the game's picture font (buttons, stars, coins) |
| `[small]` | 4 | 0 |  | text three quarters of its size from here |
| `[normal]` | 4 | 1 |  | text at its own size from here |
| `[large]` | 4 | 2 |  | text one and a half times its size from here |
| `[player]` | 5 | 0 |  | the player's name (Mario or Luigi) |
| `[player_name n]` | 5 | 0 | 255 | the player's name message n (System_PlayerName0n, 1n for Luigi) |
| `[number t i]` | 6 | t | 65535 | the game's number argument i; tag t 5 to 9 pads it to 2 to 6 digits |
| `[string t i]` | 7 | t | 65535 | the game's text argument i |
| `[race_time]` | 9 | 5 |  | the time of the race being run |
| `[best_time n]` | 9 | n | 65535 | the best time of race n |
| `[font NAME]` | 10 | 0 | 124 characters | the text in the game's font NAME from here |
| `[color n]` | 255 | 0 | 255 | the game's text colour n from here; 0 goes back to the text's own |

- `[[` is one `[`. Brackets that name no tag stay as written in a script's text; in a mod's text files the
  cook refuses them and names the line, the column and the way the word is written.
- `[tag G T u...]` writes any tag: its group, its tag number and its parameter units.
- `lumalink extract` writes the game's tags back as these words, and `[tag G T u...]` for one with no word.

## See also

- `language/text-and-services.md` (text of the game)
- `format/luma.md` (`merge/`)
