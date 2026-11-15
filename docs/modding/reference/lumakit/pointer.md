---
title: kit.pointer
shortTitle: pointer
intro: "The star pointer: where it points on screen and in the world, what B throws and what it costs, grabbing, the source that aims it (IR, gyro, mouse, stick), a co-star pointer."
contentType: reference
---

## About kit.pointer

The `kit.pointer` module of LumaKit gives you the star pointer: where it points on screen and in the world, what B throws and what it costs, grabbing, the source that aims it (IR, gyro, mouse, stick), a co-star pointer.

To use the module, add `lumakit` to the `needs` of your mod's `luma.json` and write:

```lumascript
use kit.pointer
```

### How the module works

The star pointer: where it points on screen and in the world, what B throws and what it costs, grabbing, the source that aims it. What B throws: the player's shooter (StarPieceShooter, one a pointer port) charges and shoots as the game's; while a throw is set, its shot replaces the star bit (StarPieceShooter::shoot skipped, kit.projectile throws the mod's shot from where the game's leaves, the camera's side, toward the game's goal: the locked or searched target, else the pointing point) and its cost is paid in place of the star bit. Whether the player can pay is what the shooter asks (MR::getStarPieceNum, inside tryShoot only), so the game's own refusal and its "no star bits" sound play when not. Grab: A (the jump button's press) while the shooter has a target under the pointer (its search or lock-on sensor's actor) calls the newest grab function with it. The shooter searches only while the player has star bits (the game's tryShoot): with none there is no target, so no grab. The newest throw and the newest grab hold; ending one gives the one before, then the game's. Throws and grabs are recorded under caller(); `on mods.stopped` ends a stopped mod's. ThrowSet.end is also a module function (`end`). Cost's cases name their number (`StarBits(n: u32)`), as item.Gives. Coins are paid with MR::payCoin (false with fewer than n; no 1-up, no coin count, no life taken).

The module's source is `lumakit/source/pointer.lum`.

## Declarations

| Name | Kind | Description |
|---|---|---|
| [`screen`](#screen) | Function | The pointer's place on screen (the game's layout units); none: off screen. |
| [`world`](#world) | Function | The map point under the pointer and the map's normal there; none: off screen or no map along the look. |
| [`Cost`](#cost) | Enum |  |
| [`Throw`](#throw) | Struct |  |
| [`ThrowSet`](#throwset) | Struct |  |
| [`Source`](#source) | Enum |  |
| [`set_throw`](#set_throw) | Function | What B throws from now on; end(): star bits again (or the throw before). |
| [`on_grab`](#on_grab) | Function | A on an actor under the pointer calls `f` with it (true: grabbed). |
| [`end`](#end) | Function |  |
| [`source`](#source) | Function | The pointer on pad `pad` aimed by `s`. False, and a log line naming the source, when the service refuses it: the player has no such device now (the service reports no LUMA_E_ABSENT), or pad 1 (the co-star's pointer follows its own controller). |

### `screen`

```lumascript
pub fn screen() -> vec2?
```

The pointer's place on screen (the game's layout units); none: off screen.

### `world`

```lumascript
pub fn world() -> (vec3, vec3)?
```

The map point under the pointer and the map's normal there; none: off screen or no map along the look.

### `Cost`

```lumascript
pub enum Cost { StarBits(n: u32), Coins(n: u32), Free }
```

### `Throw`

```lumascript
pub struct Throw { shot: fn(vec3, vec3) -> projectile.Shot, cost: Cost = Cost.StarBits(1) }   // (from, toward)
```

### `ThrowSet`

```lumascript
pub struct ThrowSet {
    n: u64
    fn end(self) { end(self) }
}
```

### `Source`

```lumascript
pub enum Source { Infrared, Gyro(sensitivity: f32), Mouse, Stick }
```

### `set_throw`

```lumascript
pub fn set_throw(t: Throw) -> ThrowSet
```

What B throws from now on; end(): star bits again (or the throw before).

### `on_grab`

```lumascript
pub fn on_grab(f: fn(LiveActor) -> bool) -> ThrowSet
```

A on an actor under the pointer calls `f` with it (true: grabbed).

### `end`

```lumascript
pub fn end(s: ThrowSet)
```

### `source`

```lumascript
pub fn source(pad: input.Pad, s: Source) -> bool
```

The pointer on pad `pad` aimed by `s`. False, and a log line naming the source, when the service refuses it: the player has no such device now (the service reports no LUMA_E_ABSENT), or pad 1 (the co-star's pointer follows its own controller).

## Further reading

* [LumaKit reference](index.md)
* [About LumaKit](../../concepts/lumakit.md)
