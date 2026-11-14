# LumaKit: menu

Gives: entries in the game's pause menu. (A mod's settings are its `luma.json` options on the Mods screen, read with
`option` and `on option`: a setting is an option, not a menu entry; one way, language rule 5.)

```lumascript skip a listing from the part's contract, not a module that builds alone
pub struct Entry { fn set_label(self, s: text); fn enable(self, on: bool); fn remove(self) }
pub fn add_pause_entry(label: text, on_choose: fn()) -> Entry
pub fn close_pause()                                   // the game's pause menu closed as its pause button closes it
```

Built on: `K.hook` (`PauseMenu`), `ui`, `C.text`. Conformance: entries of two mods in list order, chosen, disabled,
removed; an entry whose mod stops; the pause menu closed from an entry. An entry is changed or removed only by its
mod.