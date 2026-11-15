---
title: kit.ride
shortTitle: ride
intro: "Mounts and vehicles: getting on and off, their moves, the player posed on them."
contentType: reference
---

## About kit.ride

The `kit.ride` module of LumaKit gives you mounts and vehicles: getting on and off, their moves, the player posed on them.

To use the module, add `lumakit` to the `needs` of your mod's `luma.json` and write:

```lumascript
use kit.ride
```

### How the module works

Mounts and vehicles: getting on and off, their moves, the player posed on them. A mount is defined once by a mod (define, or its kit/ride/<name>.json, read for every mod needing LumaKit at its start) and is an object of the kit's class KitMount wherever it stands: made by code (spawn: std make) or placed in a stage under "<name>" in its own mod's stages, "<mod id>/<name>" elsewhere (the kernel's run-time alias row; the first argument is the mount). The object shows the mount's model and carries a ride sensor, as the game's own rides (SurfRay, Tamakoro): the player touching it asks to ride (MsgAutoRushBegin) and is bound to it, the game's rush; each frame the game asks the mount where its rider sits (MsgUpdateBaseMtx): the rider's base matrix is the seat joint's; the rider plays `rider_action`. The mount drives with the player's stick in the camera's axes, turning `turn_degrees` a frame at most toward it, `speed` units a frame at a full stick; on the ground A jumps it `jump`. `surface`: along a planet's surface under its gravity (the game's binder and gravity), else it holds its height. The game ends the rush when the rider is hurt (MsgRushCancel): the rider thrown off, on_dismount called. dismount() lets the rider jump off. `moves`: the rider's player.state moves are allowed while mounted only (player.state's allow; `state` is a word, so the module is used as `pstate`). One rider (the player); the newest mount touched holds while another is ridden: it is refused (the game's own answer, false). Every mount belongs to the mod that defined it: when it stops, its objects are killed (a rider on one is let off first), its definitions go off, its alias rows go with the mod (the kernel's). MountId.spawn is also a module function (`ride.spawn(id, at)`); spawn gives none where the stage cannot make one (as item.spawn).

The module's source is `lumakit/source/ride.lum`.

## Declarations

| Name | Kind | Description |
|---|---|---|
| [`Mount`](#mount) | Struct |  |
| [`MountId`](#mountid) | Struct | A mount's handle: its number, 0 for none. |
| [`define`](#define) | Function | The mount defined for the calling mod (a second of the same name replaces the first; objects of it already standing keep their number and take the new definition). |
| [`find`](#find) | Function | The calling mod's mount `name`: one it defined (or its data defined at LumaKit's start), else none. |
| [`spawn`](#spawn) | Function | The mount at `at` in the stage playing now, for the calling mod; none for a handle not the calling mod's or when the stage cannot make one. |
| [`riding`](#riding) | Function | The mount the player rides now, or none. |
| [`dismount`](#dismount) | Function | The rider off the mount now, jumping off it (the game's end of a bind); nothing when not riding. |

### `Mount`

```lumascript
pub struct Mount {
    name: text                                // placeable by name
    look: model
    seat_joint: text                          // where the player sits
    rider_action: text = "Ride"               // the player's clip while riding
    speed: f32 = 15.0
    jump: f32 = 30.0
    turn_degrees: f32 = 6.0
    surface: bool = true                      // drives along a planet's surface under gravity (karts, boards)
    moves: list<pstate.StateId> = []           // states the rider has while mounted (a tongue, a flutter)
    on_mount: fn(LiveActor)? = none
    on_dismount: fn(LiveActor)? = none
}
```

### `MountId`

```lumascript
pub struct MountId {
    n: u32
    fn spawn(self, at: vec3) -> LiveActor? { return spawn(self, at) }
}
```

A mount's handle: its number, 0 for none.

### `define`

```lumascript
pub fn define(m: Mount) -> MountId
```

The mount defined for the calling mod (a second of the same name replaces the first; objects of it already standing keep their number and take the new definition).

### `find`

```lumascript
pub fn find(name: text) -> MountId?
```

The calling mod's mount `name`: one it defined (or its data defined at LumaKit's start), else none.

### `spawn`

```lumascript
pub fn spawn(id: MountId, at: vec3) -> LiveActor?
```

The mount at `at` in the stage playing now, for the calling mod; none for a handle not the calling mod's or when the stage cannot make one.

### `riding`

```lumascript
pub fn riding() -> ref<LiveActor>?
```

The mount the player rides now, or none.

### `dismount`

```lumascript
pub fn dismount()
```

The rider off the mount now, jumping off it (the game's end of a bind); nothing when not riding.

## Further reading

* [LumaKit reference](index.md)
* [About LumaKit](../../concepts/lumakit.md)
