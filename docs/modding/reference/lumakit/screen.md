---
title: kit.screen
shortTitle: screen
intro: "Whole screens: a message, a choice, a list, a loading screen, a layout of the mod's as a screen."
contentType: reference
---

## About kit.screen

The `kit.screen` module of LumaKit gives you whole screens: a message, a choice, a list, a loading screen, a layout of the mod's as a screen.

To use the module, add `lumakit` to the `needs` of your mod's `luma.json` and write:

```lumascript
use kit.screen
```

### How the module works

Whole screens: a message, a choice, a list, a mod's layout as a screen, and a loading screen. Drawn by the game's layout drawer through svc.ui on LumaKit's layout LumaKitScreen (layers/base: a panel, a title, a line and ten rows), on the pause layer, the world held by svc.time while a screen that pauses is open. One kit screen is open at a time: a newer one closes the one before, whose waiter gets none (message: it ends; choose: none; a list: closed). A choice's or a list's rows in sight are a ui menu (ui 1.2): up and down, the pointer and touch move the focus as in the game's own menus, A takes the row; more than ten rows are pages, left and right turn them (the title says which). A closes a message; B backs out of any (MR::isPressed*, the game's reading of the player's controls after the bindings, so a keyboard bound to them counts too). Everything belongs to the mod that opened it: its screen closes when it stops, the world let go. The loading screen is drawn at svc.draw's loading point.

The module's source is `lumakit/source/screen.lum`.

## Declarations

| Name | Kind | Description |
|---|---|---|
| [`ListScreen`](#listscreen) | Struct | A list screen's handle: its number, 0 when none was opened. Its functions are also module functions (`screen.set_rows(s, rows)`). |
| [`List`](#list) | Struct |  |
| [`message`](#message) | Task | A line shown until A or B; the task ends then (or when another screen or the mod's stop ends it). |
| [`choose`](#choose) | Task | A choice of rows: the row taken, none when the player backed out (or another screen or the mod's stop closed it). |
| [`list`](#list) | Function | A list that stays open: on_choose gets each row taken (the screen closes then when `closes`); B closes it. |
| [`set_rows`](#set_rows) | Function | The list's rows changed while it is open (its page kept where it still is, the menu's focus on its row). |
| [`close`](#close) | Function |  |
| [`alive`](#alive) | Function |  |
| [`layout`](#layout) | Function | A mod's own layout as a screen: opened on the pause layer, the world held while it is open when `pauses`; the screen is the caller's to close (ui.close), and the world goes on when it does or its mod stops. |
| [`close_layout`](#close_layout) | Function | A layout the calling mod opened through layout(), closed (the kit opened it, so the kit closes it; the world goes on once no pausing one is left). Another mod's, or one already closed: nothing. |
| [`loading`](#loading) | Function | The tips shown while stages load, one each load in turn, with a picture over them; the newest caller's . An empty list takes them away. |

### `ListScreen`

```lumascript
pub struct ListScreen {
    id: u32
    fn set_rows(self, r: list<text>) { set_rows(self, r) }
    fn close(self) { close(self) }
    fn alive(self) -> bool { return alive(self) }
}
```

A list screen's handle: its number, 0 when none was opened. Its functions are also module functions (`screen.set_rows(s, rows)`).

### `List`

```lumascript
pub struct List { title: text, rows: list<text>, on_choose: fn(int)? = none, closes: bool = false }
```

### `message`

```lumascript
pub task message(line: text)
```

A line shown until A or B; the task ends then (or when another screen or the mod's stop ends it).

### `choose`

```lumascript
pub task choose(t: text, choices: list<text>) -> int?
```

A choice of rows: the row taken, none when the player backed out (or another screen or the mod's stop closed it).

### `list`

```lumascript
pub fn list(l: List) -> ListScreen
```

A list that stays open: on_choose gets each row taken (the screen closes then when `closes`); B closes it.

### `set_rows`

```lumascript
pub fn set_rows(s: ListScreen, r: list<text>)
```

The list's rows changed while it is open (its page kept where it still is, the menu's focus on its row).

### `close`

```lumascript
pub fn close(s: ListScreen)
```

### `alive`

```lumascript
pub fn alive(s: ListScreen) -> bool
```

### `layout`

```lumascript
pub fn layout(name: text, pauses: bool = true) -> ui.Screen?
```

A mod's own layout as a screen: opened on the pause layer, the world held while it is open when `pauses`; the screen is the caller's to close (ui.close), and the world goes on when it does or its mod stops.

### `close_layout`

```lumascript
pub fn close_layout(s: ui.Screen)
```

A layout the calling mod opened through layout(), closed (the kit opened it, so the kit closes it; the world goes on once no pausing one is left). Another mod's, or one already closed: nothing.

### `loading`

```lumascript
pub fn loading(t: list<text>, picture: draw.Texture? = none)
```

The tips shown while stages load, one each load in turn, with a picture over them; the newest caller's . An empty list takes them away.

## Further reading

* [LumaKit reference](index.md)
* [About LumaKit](../../concepts/lumakit.md)
