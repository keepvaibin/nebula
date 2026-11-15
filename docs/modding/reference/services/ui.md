---
title: ui service
shortTitle: ui
intro: Show your mod's screens and layouts, and read and change their parts.
contentType: reference
---

## About the ui service

The `ui` service shows your mod's cooked screens and lets your code read and change their parts. For how screens are made, see [Adding screens and fonts](../../how-tos/adding-screens-and-fonts.md).

| Service | Version | Permission | C import | LumaScript |
|---|---|---|---|---|
| `ui` | 1.3 | None | `LUMA_SERVICE(slot, "ui", "1.3")` | `use svc.ui` |

The header is `native/include/luma_ui.h`. In LumaScript, a service is called by the last part of its name, such as `ui.` for `ui`. Each function below shows its LumaScript form first and its C form second. For how the forms are made, see [Calling services](calling-services.md).

### Details from the header

LumaCore's ui service: a screen layout of the mod's or the game's, shown over the game as the game shows its own (the game's layout drawer: picture and text panes, its fonts, its 16:9 widening), its panes found by name, their text, visibility, scale and place set. Import: `LUMA_SERVICE(slot, "ui", "1.3")` for set_position, share_rows, shared_focus and play_pane ("1.2" menu_rows and menu_chosen, "1.1" alive and set_scale, "1.0" the rest).

## Limits and constants

| Name | Value | Description |
|---|---|---|
| `LUMA_UI_MOST_SCREENS` | `16` | Screens one mod has open at once, and panes it has found across them. |
| `LUMA_UI_MOST_PANES` | `256` |  |
| `LUMA_UI_MENU_ROWS_MOST` | `32` | The rows one screen's menu takes (menu_rows). |
| `LUMA_UI_TEXT_MOST` | `255` | A text pane's text: at most this many UTF-16 units (a longer text is shown cut, with a note). |

## Types

| Name | Declaration | Description |
|---|---|---|
| `LumaUiScreen` | `typedef LumaHandle LumaUiScreen;` | An open screen: owned by the mod, ended by close, by its scene's end, or by the mod's. |
| `LumaUiPane` | `typedef LumaHandle LumaUiPane;` | A pane of an open screen: ends with its screen. |

## Enumerations

### Other constants

The events a screen's animation gives (`LumaEvent`.kind; handle: the screen, or the pane for play_pane).

| Name | Description |
|---|---|
| `LUMA_UI_EVENT_ANIMATION_END = 1` | Status `LUMA_OK`: it played to its end; `LUMA_E_CANCELLED`: another play, stop, close, the scene's end or the mod's stopped it first. |

### `LumaUiLayer`

Where a screen is drawn.

| Name | Description |
|---|---|
| `LUMA_UI_HUD = 0` | With the game's HUD: drawn and moving while the game runs, stopped under its pause menu. |
| `LUMA_UI_PAUSE = 1` | Drawn and moving while the game is paused as well, over its pause menu. |

## Structures

### `LumaUiMenuRow`

A row of a screen's menu (menu_rows, 1.2): its pane and whether it can be taken.

16 bytes.

| Name | Description |
|---|---|
| `LumaUiPane pane` | A pane of the screen. |
| `bool enabled` | False: shown at half its alpha, skipped by up and down, never taken. |

## `ui` functions

In C, each function takes your mod (`const LumaMod* mod`) first. LumaScript passes it for you.

### `open`

```lumascript
ui.open(layout: text, layer: ui.Layer) -> ui.Screen?
```

```c
int32_t open(const LumaMod* mod, const char* layout, LumaUiLayer layer, LumaUiScreen* out);
```

Game thread, in a scene. The layout `layout` (LayoutData/<layout>.arc: the mod's cooked one, 3.8, or the game's) opened on `layer`, shown. It returns `LUMA_OK` and sets `*out`. A layout in no store: `LUMA_E_NOT_FOUND`, `Ui.MissingLayout` in the mod log. No scene running (the game is loading one, or at boot its fonts): `LUMA_E_BUSY` (reported) (`Ui.NoScene`). An empty or null name, a layer outside the enum, a null out: `LUMA_E_ARGUMENT` (reported); `LUMA_UI_MOST_SCREENS` open (or 128 made in one scene): `LUMA_E_LIMIT` (reported). Off the game thread: `LUMA_E_THREAD` (reported); while the game takes the mod out: `LUMA_E_CANCELLED`. A layout opened again on the layer after this mod closed it may be that screen again: its panes' visibility is the layout's again, their text as the mod left it.

### `pane`

```lumascript
ui.pane(screen: ui.Screen, name: text) -> ui.Pane?
```

```c
int32_t pane(const LumaMod* mod, LumaUiScreen screen, const char* name, LumaUiPane* out);
```

Call it on the game thread. The pane named `name` (exactly, the first in the tree's order) of `screen`. `LUMA_OK` and `*out` (the same handle for the same pane). No pane of that name: `LUMA_E_NOT_FOUND`, `Ui.MissingPane` in the mod log. An ended screen: `LUMA_E_HANDLE` (reported); an empty or null name, a null out: `LUMA_E_ARGUMENT` (reported); `LUMA_UI_MOST_PANES` found: `LUMA_E_LIMIT` (reported). Thread and taking out as open.

### `set_text`

```lumascript
ui.set_text(pane: ui.Pane, text: text)
```

```c
int32_t set_text(const LumaMod* mod, LumaUiPane pane, const char* text);
```

Call it on the game thread. The text of the pane's text boxes, UTF-8 (copied; "" empties it), as the game sets its own: the pane itself when it holds text, and every text box under it (a counter's number and its shadow are two boxes under one pane), each drawn in its font, size and alignment. A game layout's boxes grow to the text as the game's own do; a mod's own layout keeps each box's `length` (its cook's). Longer than `LUMA_UI_TEXT_MOST` units (or, in a mod's own layout, the smallest box's length): cut there, `LUMA_OK`, `Ui.TextCut` in the mod log. No text box in or under the pane: `LUMA_E_ARGUMENT` (reported) (`Ui.NotText`); not UTF-8: `LUMA_E_ARGUMENT` (reported) (Ui.NotUtf8); null: `LUMA_E_ARGUMENT` (reported); an ended pane: `LUMA_E_HANDLE` (reported). Thread and taking out as open.

### `set_visible`

```lumascript
ui.set_visible(pane: ui.Pane, on: bool)
```

```c
int32_t set_visible(const LumaMod* mod, LumaUiPane pane, bool on);
```

Call it on the game thread. The pane shown (`on` true) or hidden, with the panes under it. `LUMA_OK`; an ended pane: `LUMA_E_HANDLE` (reported). Thread and taking out as open.

### `show`

```lumascript
ui.show(screen: ui.Screen, on: bool)
```

```c
int32_t show(const LumaMod* mod, LumaUiScreen screen, bool on);
```

Call it on the game thread. The whole screen shown or hidden (its panes keep their own visibility). `LUMA_OK`; an ended screen: `LUMA_E_HANDLE` (reported). Thread and taking out as open.

### `close`

```lumascript
ui.close(screen: ui.Screen)
```

```c
int32_t close(const LumaMod* mod, LumaUiScreen screen);
```

Call it on the game thread. The screen is no longer drawn; it and its panes' handles end. `LUMA_OK`; an ended screen: `LUMA_E_HANDLE` (reported). Thread and taking out as open.

### `play`

```lumascript
ui.play(screen: ui.Screen, animation: text, loop: bool, fn: fn(e: ui.Event)?) -> bool
```

```c
int32_t play(const LumaMod* mod, LumaUiScreen screen, const char* animation, bool loop, LumaEventFn fn, void* user);
```

Call it on the game thread. The layout's animation `animation` (its BRLAN, or a mod layout's named track set, 3.8) played on the whole screen from its first frame: once (`loop` false, then it stays on its last frame) or looping. With `fn`, the screen's `LUMA_UI_EVENT_ANIMATION_END` comes to fn(user, event) at the frame's start after its last frame (never for a loop), or `LUMA_E_CANCELLED` when it is stopped first. `LUMA_OK`; no animation of that name: `LUMA_E_NOT_FOUND`, `Ui.MissingAnimation` in the mod log; an ended screen: `LUMA_E_HANDLE` (reported); a null or empty name: `LUMA_E_ARGUMENT` (reported). Thread and taking out as open.

Events: `ANIMATION_END(status)`.

### `stop`

```lumascript
ui.stop(screen: ui.Screen)
```

```c
int32_t stop(const LumaMod* mod, LumaUiScreen screen);
```

Call it on the game thread. The playing animation stops where it is (its end event: `LUMA_E_CANCELLED`). `LUMA_OK`, also with none playing; an ended screen: `LUMA_E_HANDLE` (reported). Thread and taking out as open.

### `alive`

```lumascript
ui.alive(screen: ui.Screen) -> bool
```

```c
int32_t alive(const LumaMod* mod, LumaUiScreen screen, bool* out);
```

Needs `ui` 1.1 or later.

Call it on the game thread. Whether `screen` is still open: `*out` true until close, its scene's end or the mod's end ended it (then false: open it again). `LUMA_OK`; a handle this run never gave: `LUMA_E_HANDLE` (reported); a null out: `LUMA_E_ARGUMENT` (reported). Thread and taking out as open.

### `set_scale`

```lumascript
ui.set_scale(pane: ui.Pane, x: f32, y: f32)
```

```c
int32_t set_scale(const LumaMod* mod, LumaUiPane pane, float x, float y);
```

Needs `ui` 1.1 or later.

Call it on the game thread. The pane drawn scaled by x across and y down about its own origin, with the panes under it (1 is the layout's own size; a meter's bar at 0.3 is three tenths of it; a negative one mirrors it), until it is set again or an animation that moves its scale plays. `LUMA_OK`; a number not finite or past 64 either way: `LUMA_E_ARGUMENT` (reported); an ended pane: `LUMA_E_HANDLE` (reported). Thread and taking out as open.

### `menu_rows`

```lumascript
ui.menu_rows(screen: ui.Screen, rows: list<ui.MenuRow>)
```

```c
int32_t menu_rows(const LumaMod* mod, LumaUiScreen screen, const LumaUiMenuRow* rows, uint32_t count);
```

Needs `ui` 1.2 or later.

Call it on the game thread. The screen's panes `rows` (count of them, top first, at most `LUMA_UI_MENU_ROWS_MOST`, each a pane of this screen and whether it can be taken) made a menu the player moves through and takes a row of as the game's own menus: up and down move the focus over the rows that are on, wrapping at the ends; the pointer (and touch, which is the pointer) on a row that is on focuses it; A takes the focused row, once a press. The focused row is drawn a tenth larger, a row that is off at half its alpha (the pane's own scale and alpha given back when the menu changes or the screen ends). A second call on the screen replaces the menu and keeps the focus on the same row when it is still on (else the first that is on). count 0 takes the menu away. One screen's menu moves at a time: the newest screen with one. `LUMA_OK`; an ended screen or a pane that is not one of the screen's: `LUMA_E_HANDLE` (reported); null rows with a count, or a count past the most: `LUMA_E_ARGUMENT` (reported). Thread and taking out as open.

### `menu_chosen`

```lumascript
ui.menu_chosen(screen: ui.Screen) -> i32
```

```c
int32_t menu_chosen(const LumaMod* mod, LumaUiScreen screen, int32_t* row);
```

Needs `ui` 1.2 or later.

Call it on the game thread. The row of the screen's menu taken at the last reading of the controls into *row (0 the top), -1 when none was: no press, a host menu open (Settings, the HOME menu), or no menu. The controls are read once a frame after the mods' frame handlers, and a row taken stays until the next reading, so the next frame's handlers see it. `LUMA_OK`; an ended screen: `LUMA_E_HANDLE` (reported); a null row: `LUMA_E_ARGUMENT` (reported). Thread and taking out as open.

### `set_position`

```lumascript
ui.set_position(pane: ui.Pane, x: f32, y: f32)
```

```c
int32_t set_position(const LumaMod* mod, LumaUiPane pane, float x, float y);
```

Needs `ui` 1.3 or later.

Call it on the game thread. The pane placed with its origin at x across and y up from its parent's origin, in layout units in the parent's space (the parent's scale and turn apply), with the panes under it: the layout's own place of the pane is replaced, not added to, so copies of one pane stand where the mod puts each. Kept until it is set again or an animation that moves the pane's place plays; such an animation places it every frame from then, stopped or ended too, until a play of another animation on the screen (set it again after that play). `LUMA_OK`; a number not finite or past 4096 either way: `LUMA_E_ARGUMENT` (reported); an ended pane: `LUMA_E_HANDLE` (reported). Thread and taking out as open.

### `share_rows`

```lumascript
ui.share_rows(rows: list<ui.MenuRow>)
```

```c
int32_t share_rows(const LumaMod* mod, const LumaUiMenuRow* rows, uint32_t count);
```

Needs `ui` 1.3 or later.

Call it on the game thread. The panes `rows` (count of them, at most `LUMA_UI_MENU_ROWS_MOST`, each a pane of any of this mod's open screens and whether it can be taken) join the game's own menu focus, beside the game's buttons, instead of covering it as menu_rows does: each frame each row that is on, shown (its screen and the pane with its parents) and drawn is offered at its center on screen; up, down, left and right move the one highlight to the nearest button or row that way, as between the game's buttons; the pointer (and touch) on a row that is on highlights it while the pointer leads. A (the game's own decide press) on the highlighted row takes it. The pad's presses stay the game's (nothing is taken from it), so the game's buttons keep working beside the rows; a row left over the HUD while the game plays takes the pad's directions into the ring, so share the rows of pause screens. The rows' look is the mod's: shared_focus says which row has the highlight. A second call replaces the mod's rows; count 0 takes them away. A row's screen ending ends the row (the highlight it had leaves at the next frame, to nothing until the next press). Nothing moves or is taken while menu_rows's menu covers the game's or a host menu is open. `LUMA_OK`; a pane that is not one of this mod's open screens': `LUMA_E_HANDLE` (reported); null rows with a count, or a count past the most: `LUMA_E_ARGUMENT` (reported). Thread and taking out as open.

### `shared_focus`

```lumascript
ui.shared_focus() -> (ui.Pane, ui.Pane)
```

```c
int32_t shared_focus(const LumaMod* mod, LumaUiPane* focused, LumaUiPane* chosen);
```

Needs `ui` 1.3 or later.

Call it on the game thread. The mod's shared row (share_rows) with the highlight at the last reading into *focused, and the row taken then into *chosen, each 0 when none. The reading is made once a frame after the mods' frame handlers, so the next frame's handlers see it, and a row taken stays until the next reading (as menu_chosen). `LUMA_OK`; a null focused or chosen: `LUMA_E_ARGUMENT` (reported). Thread and taking out as open.

### `play_pane`

```lumascript
ui.play_pane(screen: ui.Screen, pane: ui.Pane, animation: text, loop: bool, fn: fn(e: ui.Event)?) -> bool
```

```c
int32_t play_pane(const LumaMod* mod, LumaUiScreen screen, LumaUiPane pane, const char* animation, bool loop, LumaEventFn fn, void* user);
```

Needs `ui` 1.3 or later.

Call it on the game thread. The layout's animation `animation` played from its first frame on the pane `pane` of `screen` and the panes under it only, as the game plays a button's own (its ButtonSelectIn, ButtonSelectOut): two buttons of one screen light one at a time. Once (`loop` false, then it stays on its last frame) or looping. With `fn`, `LUMA_UI_EVENT_ANIMATION_END` comes to fn(user, event) as play's does, its handle the pane; another play_pane on the pane, the screen's end or the mod's give `LUMA_E_CANCELLED`. On the screen's root pane it is the screen's play (each cancels the other). `LUMA_OK`; no animation of that name: `LUMA_E_NOT_FOUND`, `Ui.MissingAnimation` in the mod log; an ended screen or pane, or a pane not of that screen: `LUMA_E_HANDLE` (reported); a null or empty name: `LUMA_E_ARGUMENT` (reported). Thread and taking out as open.

Events: `ANIMATION_END(status)`.

## Further reading

* [Calling services](calling-services.md)
* [Services reference](index.md)
* [About services](../../concepts/services.md)
* [Permissions](../../concepts/permissions.md)
