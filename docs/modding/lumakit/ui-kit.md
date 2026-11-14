# LumaKit: ui.kit

Gives: widgets for a mod's own panels: buttons, sliders, toggles, colour pickers, lists, text fields, a wheel, a model
view; pad, mouse and touch focus.

```lumascript skip a listing from the part's contract, not a module that builds alone
pub enum Widget { Button(label: text, on_press: fn()), Slider(label: text, min: f32, max: f32, value: f32, on_change: fn(f32)),
                  Toggle(label: text, on: bool, on_change: fn(bool)), Colour(label: text, value: color, on_change: fn(color)),
                  List(rows: list<text>, on_choose: fn(int)), TextField(label: text, max_chars: u32, on_done: fn(text)),
                  Wheel(entries: list<(draw.Texture, text)>, on_choose: fn(int)), ModelView(look: model, size: vec2),
                  Picture(tex: draw.Texture, size: vec2), Label(s: text) }
pub struct Panel { title: text? = none, widgets: list<Widget>, at: hud.Corner? = none, pauses: bool = false }
pub struct Open { fn set(self, index: int, w: Widget); fn close(self); fn alive(self) -> bool }
pub fn open(p: Panel) -> Open
pub fn touch_layout(buttons: list<(input.Action, vec2, f32)>)   // on-screen buttons where the platform has touch
```

Built on: `draw`, `ui`, `input` (text entry, touch, inject), `draw.view`† (ModelView). Conformance: each widget by pad,
mouse and touch; a text field through the platform keyboard; a wheel choosing an item; a panel whose mod stops.
