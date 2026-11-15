---
title: kit.hud
shortTitle: hud
intro: "Counters, timers, meters, text, pictures in corners; text in 3D; popups; subtitles; the game's own HUD parts hidden."
contentType: reference
---

## About kit.hud

The `kit.hud` module of LumaKit gives you counters, timers, meters, text, pictures in corners; text in 3D; popups; subtitles; the game's own HUD parts hidden.

To use the module, add `lumakit` to the `needs` of your mod's `luma.json` and write:

```lumascript
use kit.hud
```

### How the module works

Text, counters, meters and timers in the screen's corners, and a popup, drawn by the game's own layout drawer through svc.ui on LumaKit's layout LumaKitHud (layers/base: eight corners of six rows each and a popup). Elements in one corner stack in the order made; a seventh in a full corner is not shown and its handle is empty (slot 0), which every call takes and does nothing with. A meter is a bar scaled to its share (ui set_scale). The screen ends with each scene (ui alive says so); the kit opens it again and puts every element back.

The module's source is `lumakit/source/hud.lum`.

## Declarations

| Name | Kind | Description |
|---|---|---|
| [`Corner`](#corner) | Enum | Where an element stands (in one corner, in the order made: top first for the top corners, bottom first for the bottom ones). |
| [`Text`](#text) | Struct | The handles: the element's slot, 0 when it was not shown. Their functions are also module functions (`hud.set_text(t, s)`). |
| [`Counter`](#counter) | Struct |  |
| [`Meter`](#meter) | Struct |  |
| [`Timer`](#timer) | Struct |  |
| [`text`](#text) | Function | `s` in the corner; with `frames`, it removes itself after that many frames. |
| [`set_text`](#set_text) | Function |  |
| [`remove_text`](#remove_text) | Function |  |
| [`counter`](#counter) | Function | A number in the corner. |
| [`set_counter`](#set_counter) | Function |  |
| [`remove_counter`](#remove_counter) | Function |  |
| [`meter`](#meter) | Function | A bar of 0 to `max` (less than 1 is taken as 1), empty at first. |
| [`set_meter`](#set_meter) | Function | The meter's value, held between 0 and its max. |
| [`remove_meter`](#remove_meter) | Function |  |
| [`timer`](#timer) | Function | A timer counting up from `frames`, one each frame, shown as minutes, seconds and hundredths. |
| [`set_timer`](#set_timer) | Function | The timer shows `frames` and counts on from there. |
| [`run_timer`](#run_timer) | Function | The timer held (`counting` false) or counting again. |
| [`remove_timer`](#remove_timer) | Function |  |
| [`popup`](#popup) | Function | The popup, over the middle of the screen for three seconds; a new one replaces the one shown. |

### `Corner`

```lumascript
pub enum Corner { TopLeft, Top, TopRight, Left, Right, BottomLeft, Bottom, BottomRight }
```

Where an element stands (in one corner, in the order made: top first for the top corners, bottom first for the bottom ones).

### `Text`

```lumascript
pub struct Text {
    slot: u32
    fn set(self, s: text) { set_text(self, s) }
    fn remove(self) { remove_text(self) }
}
```

The handles: the element's slot, 0 when it was not shown. Their functions are also module functions (`hud.set_text(t, s)`).

### `Counter`

```lumascript
pub struct Counter {
    slot: u32
    fn set(self, value: i64) { set_counter(self, value) }
    fn remove(self) { remove_counter(self) }
}
```

### `Meter`

```lumascript
pub struct Meter {
    slot: u32
    max: i32
    fn set(self, value: i32) { set_meter(self, value) }
    fn remove(self) { remove_meter(self) }
}
```

### `Timer`

```lumascript
pub struct Timer {
    slot: u32
    fn set(self, frames: u64) { set_timer(self, frames) }
    fn run(self, counting: bool) { run_timer(self, counting) }
    fn remove(self) { remove_timer(self) }
}
```

### `text`

```lumascript
pub fn text(at: Corner, s: text, frames: u32? = none) -> Text
```

`s` in the corner; with `frames`, it removes itself after that many frames.

### `set_text`

```lumascript
pub fn set_text(t: Text, s: text)
```

### `remove_text`

```lumascript
pub fn remove_text(t: Text)
```

### `counter`

```lumascript
pub fn counter(at: Corner, value: i64) -> Counter
```

A number in the corner.

### `set_counter`

```lumascript
pub fn set_counter(c: Counter, value: i64)
```

### `remove_counter`

```lumascript
pub fn remove_counter(c: Counter)
```

### `meter`

```lumascript
pub fn meter(at: Corner, max: i32) -> Meter
```

A bar of 0 to `max` (less than 1 is taken as 1), empty at first.

### `set_meter`

```lumascript
pub fn set_meter(m: Meter, value: i32)
```

The meter's value, held between 0 and its max.

### `remove_meter`

```lumascript
pub fn remove_meter(m: Meter)
```

### `timer`

```lumascript
pub fn timer(at: Corner, frames: u64 = 0) -> Timer
```

A timer counting up from `frames`, one each frame, shown as minutes, seconds and hundredths.

### `set_timer`

```lumascript
pub fn set_timer(t: Timer, frames: u64)
```

The timer shows `frames` and counts on from there.

### `run_timer`

```lumascript
pub fn run_timer(t: Timer, counting: bool)
```

The timer held (`counting` false) or counting again.

### `remove_timer`

```lumascript
pub fn remove_timer(t: Timer)
```

### `popup`

```lumascript
pub fn popup(title: text, line: text)
```

The popup, over the middle of the screen for three seconds; a new one replaces the one shown.

## Further reading

* [LumaKit reference](index.md)
* [About LumaKit](../../concepts/lumakit.md)
