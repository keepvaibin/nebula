---
title: kit.player.state
shortTitle: player.state
intro: "A new move (its trigger, animation, physics each frame, how it ends, whether it attacks) and the player's constants changed by name."
contentType: reference
---

## About kit.player.state

The `kit.player.state` module of LumaKit gives you a new move (its trigger, animation, physics each frame, how it ends, whether it attacks) and the player's constants changed by name.

To use the module, add `lumakit` to the `needs` of your mod's `luma.json` and write:

```lumascript
use kit.player.state
```

### How the module works

A mod's own move for the player (what starts it, its animation, its physics each frame, how it ends, whether it attacks) and the player's constants changed by name. Built on the game's player by name (MarioActor, Mario, MarioConst's setMarioConstant/getMarioConstant) and svc.input for a move's action. The move runs inside the game's own player step: after MarioActor::movement each frame the module writes the move's velocity where the game keeps it for the next step (in the air Mario::mJumpVec, which the jump adds its gravity to and moves the player by; on the ground Mario::mVelocity) and keeps its animation, so the game's collision, gravity and camera act on it. Every move and tuning is recorded under caller(); `on mods.stopped` takes a stopped mod's away (its move in progress ends without calling its closures; its tunings are undone exactly). `from` is a list; `motion` returns the Motion it changed and is `keep` when not given; `effect_start` and `sound_start` are content names.

The module's source is `lumakit/source/player/state.lum`.

## Declarations

| Name | Kind | Description |
|---|---|---|
| [`State`](#state) | Struct |  |
| [`Start`](#start) | Enum |  |
| [`From`](#from) | Enum |  |
| [`End`](#end) | Enum |  |
| [`Motion`](#motion) | Struct |  |
| [`keep`](#keep) | Function | A move's motion when it gives none: the player's own, unchanged. |
| [`Tuning`](#tuning) | Struct | The player's constants by name (MarioConst's fields, "mJumpHeight"), each multiplied by its value. |
| [`StateId`](#stateid) | Struct | A defined move: its slot + 1 (0: none). Its functions are module functions too (`enter_state`), until struct functions cross between mods. |
| [`Tuned`](#tuned) | Struct | A tuning in force: its number (0: none). |
| [`define`](#define) | Function | A new move for the player; its id names it in the calling mod. |
| [`find`](#find) | Function | A move of the calling mod by its id (one defined by code or by kit/player.state data). |
| [`find_for`](#find_for) | Function | A move of mod `owner` (as caller() gives it) by its id: for a module reading that mod's data (kit.ride's mounts). |
| [`current`](#current) | Function | The move in progress, of any mod; none while the game's own moves run. |
| [`enter_state`](#enter_state) | Function | Starts the move now if the player is where its `from` allows (and once_per_air allows); false if not. |
| [`allow`](#allow) | Function | Whether move `id` may start, by its action or enter_state (every move may until a call says otherwise); one not allowed while it runs ends (its on_end called). kit.ride allows a mount's moves while the player rides it. |
| [`is_current_state`](#is_current_state) | Function |  |
| [`exit_state`](#exit_state) | Function | Ends the move in progress if it is `id` (End.Code's way out); its on_end is called. |
| [`is_constant`](#is_constant) | Function | Whether the player has a constant named `name` (one of MarioConst's fields, "mJumpHeight"). |
| [`known`](#known) | Function | The tuning with only the constants the player has; each other name is named in the log (`what` says whose tuning it was) and left out. For a tuning read from a data file. |
| [`tune`](#tune) | Function | The player's constants multiplied by name while the handle holds; `end` puts them back exactly. Two tunings of one constant multiply. A name MarioConst does not have is a script error (`is_constant` asks; `known` leaves such names out). |
| [`end_tuned`](#end_tuned) | Function |  |
| [`tuned_alive`](#tuned_alive) | Function |  |

### `State`

```lumascript
pub struct State {
    id: text
    starts: Start
    from: list<From> = [From.Air]
    once_per_air: bool = false                // double jump
    action: text                              // the player's animation (C.anim, bound to the skeleton by LumaLink)
    motion: fn(MarioActor, Motion) -> Motion = keep   // each frame while in the state; keep: the game's own motion
    ends: list<End> = [End.Ground]
    attack: actor.Attack? = none              // hurts what it touches as this attack while it lasts
    effect_start: text? = none                // an effect of the player's (MR::emitEffect by name)
    sound_start: text? = none                 // a sound of the player's (MR::startSoundPlayer by name)
    on_start: fn(MarioActor)? = none
    on_end: fn(MarioActor)? = none
}
```

### `Start`

```lumascript
pub enum Start { Action(a: input.Action), Code }
```

### `From`

```lumascript
pub enum From { Ground, Air, Water, Any }
```

### `End`

```lumascript
pub enum End { Ground, Wall, Water, Frames(n: u32), ActionEnd, Released(a: input.Action), Code }
```

### `Motion`

```lumascript
pub struct Motion { velocity: vec3, gravity_scale: f32 = 1.0, friction: f32 = 1.0, turn: f32 = 1.0 }
```

### `keep`

```lumascript
pub fn keep(p: MarioActor, m: Motion) -> Motion
```

A move's motion when it gives none: the player's own, unchanged.

### `Tuning`

```lumascript
pub struct Tuning { values: map<text, f32> }
```

The player's constants by name (MarioConst's fields, "mJumpHeight"), each multiplied by its value.

### `StateId`

```lumascript
pub struct StateId {
    n: int
    fn enter(self) -> bool { return enter_state(self) }
    fn is_current(self) -> bool { return is_current_state(self) }
}
```

A defined move: its slot + 1 (0: none). Its functions are module functions too (`enter_state`), until struct functions cross between mods.

### `Tuned`

```lumascript
pub struct Tuned {
    n: u64
    fn end(self) { end_tuned(self) }
    fn alive(self) -> bool { return tuned_alive(self) }
}
```

A tuning in force: its number (0: none).

### `define`

```lumascript
pub fn define(s: State) -> StateId
```

A new move for the player; its id names it in the calling mod.

### `find`

```lumascript
pub fn find(id: text) -> StateId?
```

A move of the calling mod by its id (one defined by code or by kit/player.state data).

### `find_for`

```lumascript
pub fn find_for(owner: u64, id: text) -> StateId?
```

A move of mod `owner` (as caller() gives it) by its id: for a module reading that mod's data (kit.ride's mounts).

### `current`

```lumascript
pub fn current() -> StateId?
```

The move in progress, of any mod; none while the game's own moves run.

### `enter_state`

```lumascript
pub fn enter_state(id: StateId) -> bool
```

Starts the move now if the player is where its `from` allows (and once_per_air allows); false if not.

### `allow`

```lumascript
pub fn allow(id: StateId, may: bool)
```

Whether move `id` may start, by its action or enter_state (every move may until a call says otherwise); one not allowed while it runs ends (its on_end called). kit.ride allows a mount's moves while the player rides it.

### `is_current_state`

```lumascript
pub fn is_current_state(id: StateId) -> bool
```

### `exit_state`

```lumascript
pub fn exit_state(id: StateId)
```

Ends the move in progress if it is `id` (End.Code's way out); its on_end is called.

### `is_constant`

```lumascript
pub fn is_constant(name: text) -> bool
```

Whether the player has a constant named `name` (one of MarioConst's fields, "mJumpHeight").

### `known`

```lumascript
pub fn known(t: Tuning, what: text) -> Tuning
```

The tuning with only the constants the player has; each other name is named in the log (`what` says whose tuning it was) and left out. For a tuning read from a data file.

### `tune`

```lumascript
pub fn tune(t: Tuning) -> Tuned
```

The player's constants multiplied by name while the handle holds; `end` puts them back exactly. Two tunings of one constant multiply. A name MarioConst does not have is a script error (`is_constant` asks; `known` leaves such names out).

### `end_tuned`

```lumascript
pub fn end_tuned(t: Tuned)
```

### `tuned_alive`

```lumascript
pub fn tuned_alive(t: Tuned) -> bool
```

## Further reading

* [LumaKit reference](index.md)
* [About LumaKit](../../concepts/lumakit.md)
