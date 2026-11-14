# LumaKit: menu

Gives: entries in the game's pause menu. (A mod's settings are its `luma.json` options on the Mods screen, read with
`option` and `on option`: a setting is an option, not a menu entry; one way, language rule 5.)

```lumascript skip a listing from the part's contract, not a module that builds alone
pub struct Entry { fn set_label(self, s: text); fn enable(self, on: bool); fn remove(self) }
pub fn add_pause_entry(label: text, on_choose: fn()) -> Entry
pub fn close_pause()                                   // the game's pause menu closed as its pause button closes it
```

The entries show as the game's own buttons, made from the game's pause menu layout: the game's Back and Quit move up
and shrink to make room, and up to four entries stand under them in two columns of two, the size of the Settings
button. With more than four, the last place shows ▼ and the first ▲ to page through them; ten entries at most. The pad,
the pointer and touch reach them as they reach the game's own buttons, in one highlight with them. An entry lights and
plays its decide as the game's buttons do, and its function runs when the decide ends. A disabled entry is never
highlighted or taken.

Built on: `K.hook` (`PauseMenu`), `ui`, `C.text`. Conformance: entries of two mods in list order, chosen, disabled,
removed; an entry whose mod stops; the pause menu closed from an entry. An entry is changed or removed only by its
mod.