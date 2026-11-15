---
title: kit.player.form
shortTitle: player.form
intro: "A power-up: its look, aura, timer, music, what the player's actions do while it lasts, its own physics and scale, and the game's own forms by name to give from code."
contentType: reference
---

## About kit.player.form

The `kit.player.form` module of LumaKit gives you a power-up: its look, aura, timer, music, what the player's actions do while it lasts, its own physics and scale, and the game's own forms by name to give from code.

To use the module, add `lumakit` to the `needs` of your mod's `luma.json` and write:

```lumascript
use kit.player.form
```

### How the module works

A power-up: its look, aura, timer, music, what the player's actions do while it lasts, its physics and scale, and the game's own forms by name. One form holds the player at a time: giving a form puts it on top; when it ends the one under it holds again with the time it had left; under them all is the game's own form. What a form is, while it holds: base the game's form under it (MarioActor::setPlayerMode with the game's own number), so Bee's flight or Boo's float stay under a new look; Normal for none look its model drawn with the player's skeleton (content.model attach), the game's own body not drawn (MarioActor::drawMarioModel skipped); look_luigi when the player is Luigi aura its effect kept on the player music over the stage's music (the game's sub music, as its own forms) timer timer_frames world steps; with `meter` the time left on the game's own form meter (MarioSubMeter) scale the player's model, sensors and the camera's distance (draw.inst scale and the player's constants) opacity draw.inst on the player physics kit.player.state's tuning, ended exactly with the form callbacks on_start, on_spin (once a spin), on_jump (once a jump), on_act (its bindable action, once a press), on_end with why A form ends by its timer (Ended.Timer), by damage when ends_on_damage (Ended.Damage; the game's own reset of the form on damage is held back otherwise), by another form given over it for good (Ended.Replaced), by end() (Ended.Ended), or with its mod (Ended.ModStopped: its closures are not called). The game's own forms are given like a mod's: game(GameForm) is a FormId. Data: kit/player.form/<id>.json of the calling mod, read by find(): the fields but the `fn` ones, content by name, physics as a map of the player's constants by name.

The module's source is `lumakit/source/player/form.lum`.

## Declarations

| Name | Kind | Description |
|---|---|---|
| [`GameForm`](#gameform) | Enum |  |
| [`Ended`](#ended) | Enum |  |
| [`Form`](#form) | Struct |  |
| [`FormId`](#formid) | Struct | A form's handle: a mod's form is its place + 1; the game's own are 1000 + the game's number; 0 none. |
| [`define`](#define) | Function | The form defined for the calling mod (a second of the same id replaces the first). |
| [`find`](#find) | Function | The calling mod's form `id`: one it defined, else the one of its kit/player.form/<id>.json, defined now; none when there is neither, the file does not read as a form, or it names content the mod does not have. |
| [`find_for`](#find_for) | Function | Mod `owner`'s form `id` (a caller() number), as find: for the kit's own reading of a mod's data (kit.item's data items, defined on the mod's first frame). |
| [`game`](#game) | Function | The game's own forms, given like a mod's. |
| [`set`](#set) | Function | The form given to the player now: it holds; the one that held waits under it with the time it has left. The same form given again starts its time anew. |
| [`is_current_form`](#is_current_form) | Function |  |
| [`id_of`](#id_of) | Function | A form's id: a mod's as it named it, the game's by the game's names; empty for none. |
| [`current`](#current) | Function | The form holding the player; none for the game's plain form. |
| [`frames_left`](#frames_left) | Function | The steps the holder has left; none without a timer or a holder. |
| [`end`](#end) | Function | The holder ends (Ended.Ended); with none under it the player is Normal again. |
| [`kit_step`](#kit_step) | Function | The held form's step: LumaKit's one `on step` (kit.replay's) calls it first each world step. |

### `GameForm`

```lumascript
pub enum GameForm { Normal, Bee, Boo, Spring, Fire, Ice, Flying, Rainbow }   // the game's own (setPlayerMode)
```

### `Ended`

```lumascript
pub enum Ended { Timer, Damage, Replaced, Ended, ModStopped }
```

### `Form`

```lumascript
pub struct Form {
    id: text                                  // unique in the mod; "<mod id>/<id>" to the game
    look: model? = none
    look_luigi: model? = none
    aura: effect? = none
    timer_frames: u32? = none                 // none: lasts until damage or end()
    ends_on_damage: bool = true
    music: sound? = none
    meter: bool = false
    scale: f32 = 1.0
    opacity: f32 = 1.0
    hurts_on_touch: bool = false
    physics: pst.Tuning? = none
    base: GameForm = GameForm.Normal
    item: item.ItemId? = none
    on_spin: fn(MarioActor)? = none
    on_action: input.Action? = none
    on_act: fn(MarioActor)? = none
    on_jump: fn(MarioActor)? = none
    on_start: fn(MarioActor)? = none
    on_end: fn(MarioActor, Ended)? = none
}
```

### `FormId`

```lumascript
pub struct FormId {
    n: u32
    fn give(self) { set(self) }
    fn is_current(self) -> bool { return is_current_form(self) }
    fn id(self) -> text { return id_of(self) }
}
```

A form's handle: a mod's form is its place + 1; the game's own are 1000 + the game's number; 0 none.

### `define`

```lumascript
pub fn define(f: Form) -> FormId
```

The form defined for the calling mod (a second of the same id replaces the first).

### `find`

```lumascript
pub fn find(id: text) -> FormId?
```

The calling mod's form `id`: one it defined, else the one of its kit/player.form/<id>.json, defined now; none when there is neither, the file does not read as a form, or it names content the mod does not have.

### `find_for`

```lumascript
pub fn find_for(owner: u64, id: text) -> FormId?
```

Mod `owner`'s form `id` (a caller() number), as find: for the kit's own reading of a mod's data (kit.item's data items, defined on the mod's first frame).

### `game`

```lumascript
pub fn game(f: GameForm) -> FormId
```

The game's own forms, given like a mod's.

### `set`

```lumascript
pub fn set(f: FormId)
```

The form given to the player now: it holds; the one that held waits under it with the time it has left. The same form given again starts its time anew.

### `is_current_form`

```lumascript
pub fn is_current_form(f: FormId) -> bool
```

### `id_of`

```lumascript
pub fn id_of(f: FormId) -> text
```

A form's id: a mod's as it named it, the game's by the game's names; empty for none.

### `current`

```lumascript
pub fn current() -> FormId?
```

The form holding the player; none for the game's plain form.

### `frames_left`

```lumascript
pub fn frames_left() -> u32?
```

The steps the holder has left; none without a timer or a holder.

### `end`

```lumascript
pub fn end()
```

The holder ends (Ended.Ended); with none under it the player is Normal again.

### `kit_step`

```lumascript
pub fn kit_step()
```

The held form's step: LumaKit's one `on step` (kit.replay's) calls it first each world step.

## Further reading

* [LumaKit reference](index.md)
* [About LumaKit](../../concepts/lumakit.md)
