---
title: kit.ui.kit
shortTitle: ui.kit
intro: "Widgets for a mod's own panels: buttons, sliders, toggles, color pickers, lists, text fields, a wheel, a model view; pad, mouse and touch focus."
contentType: reference
---

## About kit.ui.kit

The `kit.ui.kit` module of LumaKit gives you widgets for a mod's own panels: buttons, sliders, toggles, color pickers, lists, text fields, a wheel, a model view; pad, mouse and touch focus.

To use the module, add `lumakit` to the `needs` of your mod's `luma.json` and write:

```lumascript
use kit.ui.kit
```

### How the module works

Widgets for a mod's own panels: buttons, sliders, toggles, color pickers, lists, text fields, a wheel, a model view, pictures and labels, with pad, mouse and touch focus. A panel is drawn by svc.draw over the game's HUD (LUMA_DRAW_HUD_AFTER) in the HUD's 4:3 frame, at a corner of it or in its middle, one widget a row (a list one row each of its rows, a wheel, a picture and a model view as tall as they are). The newest open panel with something to take takes the controls, read as the game reads them (input.game: after the bindings, so a keyboard and a pad count alike): up and down move the focus over the stops (a button, slider, toggle, color, text field, each row of a list, a wheel); left and right move a slider a twentieth, flip a toggle, step a color through the palette, turn a wheel; the stick points a wheel's entry; A presses a button, flips a toggle, steps a color, takes a list's row, takes a wheel's entry, opens a text field's platform keyboard (input's text request; its text given to on_done when the player confirms). The pointer (the mouse, touch) on a stop focuses it; A with the pointer on a slider's track sets it there, on a color's swatch takes that color, on a wheel's entry takes it. A panel that pauses holds the world (svc.time) while it is open. Callbacks run at the frame's start after the press . Everything belongs to the mod that opened it: its panels close when it stops. A ModelView shows its frame only, and touch_layout's buttons are drawn but press nothing.

The module's source is `lumakit/source/ui/kit.lum`.

## Declarations

| Name | Kind | Description |
|---|---|---|
| [`Widget`](#widget) | Enum |  |
| [`Panel`](#panel) | Struct |  |
| [`Open`](#open) | Struct | An open panel's handle: its number, 0 when none opened. Its functions are also module functions (`kit.set(o, i, w)`). |
| [`open`](#open) | Function |  |
| [`set`](#set) | Function | The widget at `index` replaced (a slider's value, a list's rows, a label's text...); the focus kept on its stop. |
| [`close`](#close) | Function |  |
| [`alive`](#alive) | Function |  |
| [`touch_layout`](#touch_layout) | Function |  |

### `Widget`

```lumascript
pub enum Widget {
    Button(label: text, on_press: fn())
    Slider(label: text, min: f32, max: f32, value: f32, on_change: fn(f32))
    Toggle(label: text, is_on: bool, on_change: fn(bool))
    Colour(label: text, value: color, on_change: fn(color))
    List(rows: list<text>, on_choose: fn(int))
    TextField(label: text, max_chars: u32, on_done: fn(text))
    Wheel(entries: list<(draw.Texture, text)>, on_choose: fn(int))
    ModelView(look: model, size: vec2)
    Picture(tex: draw.Texture, size: vec2)
    Label(s: text)
}
```

### `Panel`

```lumascript
pub struct Panel { title: text? = none, widgets: list<Widget>, at: hud.Corner? = none, pauses: bool = false }
```

### `Open`

```lumascript
pub struct Open {
    id: u32
    fn set(self, index: int, w: Widget) { set(self, index, w) }
    fn close(self) { close(self) }
    fn alive(self) -> bool { return alive(self) }
}
```

An open panel's handle: its number, 0 when none opened. Its functions are also module functions (`kit.set(o, i, w)`).

### `open`

```lumascript
pub fn open(p: Panel) -> Open
```

### `set`

```lumascript
pub fn set(o: Open, index: int, w: Widget)
```

The widget at `index` replaced (a slider's value, a list's rows, a label's text...); the focus kept on its stop.

### `close`

```lumascript
pub fn close(o: Open)
```

### `alive`

```lumascript
pub fn alive(o: Open) -> bool
```

### `touch_layout`

```lumascript
pub fn touch_layout(buttons: list<(input.Action, vec2, f32)>)
```

## Further reading

* [LumaKit reference](index.md)
* [About LumaKit](../../concepts/lumakit.md)
