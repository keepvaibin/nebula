---
title: kit.menu
shortTitle: menu
intro: "Entries in the game's pause menu. (A mod's settings are its `luma.json` options on the Mods screen, read with `option` and `on option`: a setting is an option, not a menu entry; one way, language rule 5.)"
contentType: reference
---

## About kit.menu

The `kit.menu` module of LumaKit gives you entries in the game's pause menu. (A mod's settings are its `luma.json` options on the Mods screen, read with `option` and `on option`: a setting is an option, not a menu entry; one way, language rule 5.).

To use the module, add `lumakit` to the `needs` of your mod's `luma.json` and write:

```lumascript
use kit.menu
```

### How the module works

Entries in the game's pause menu. While the pause menu is open (PauseMenu::appear to PauseMenu::kill) the entries that are on show as the game's own buttons: each pair of rows is a screen of the game's PauseMenu layout with everything hidden but its two buttons (NBackUp, NBackDown), their text the entries' labels, as Nebula's Settings button is made from the same layout. The game's Back and Quit move up and shrink to make room; the rows stand under them at the Settings button's size, two columns of two. Past four a page shows ▲ first and ▼ last to page through them; at most ten entries in all (`most`; one more is not added, logged). Order: the Mods list's order (top first), then each mod's in the order made. The rows join the game's own menu highlight beside Back and Quit (ui.share_rows); the pad, pointer and touch reach them as the game's buttons. The button with the highlight lights as the game's (ButtonSelectIn, ButtonSelectOut on its own pane); one taken plays the game's decide and its function runs when that ends, at a frame's start. Disabled entries are never highlighted or taken. Everything belongs to the mod that added it.

The module's source is `lumakit/source/menu.lum`.

## Declarations

| Name | Kind | Description |
|---|---|---|
| [`Entry`](#entry) | Struct | An entry's handle: its number, 0 for none. Its functions are also module functions (`menu.set_label(e, s)`). |
| [`add_pause_entry`](#add_pause_entry) | Function |  |
| [`set_label`](#set_label) | Function |  |
| [`enable`](#enable) | Function |  |
| [`remove`](#remove) | Function |  |
| [`alive`](#alive) | Function |  |
| [`close_pause`](#close_pause) | Function |  |

### `Entry`

```lumascript
pub struct Entry {
    id: u32
    fn set_label(self, s: text) { set_label(self, s) }
    fn enable(self, is_on: bool) { enable(self, is_on) }
    fn remove(self) { remove(self) }
    fn alive(self) -> bool { return alive(self) }
}
```

An entry's handle: its number, 0 for none. Its functions are also module functions (`menu.set_label(e, s)`).

### `add_pause_entry`

```lumascript
pub fn add_pause_entry(label: text, on_choose: fn()) -> Entry
```

### `set_label`

```lumascript
pub fn set_label(e: Entry, s: text)
```

### `enable`

```lumascript
pub fn enable(e: Entry, is_on: bool)
```

### `remove`

```lumascript
pub fn remove(e: Entry)
```

### `alive`

```lumascript
pub fn alive(e: Entry) -> bool
```

### `close_pause`

```lumascript
pub fn close_pause()
```

## Further reading

* [LumaKit reference](index.md)
* [About LumaKit](../../concepts/lumakit.md)
