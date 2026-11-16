---
title: kit.actor
shortTitle: actor
intro: "A new object kind in a few lines, and the helpers a mod's own `class` uses for the usual parts."
contentType: reference
---

## About kit.actor

The `kit.actor` module of LumaKit gives you a new object kind in a few lines, and the helpers a mod's own `class` uses for the usual parts.

To use the module, add `lumakit` to the `needs` of your mod's `luma.json` and write:

```lumascript
use kit.actor
```

### How the module works

New kinds of object in a few lines, and the helpers a mod's own `class` uses for the usual parts of the game's enemies. A kind is defined once by a mod (define, or its kit/actor/<name>.json) and is an object of the kit's class KitActor wherever it stands: made by code (spawn: std make) or placed in a stage under "<mod id>/<name>" (the kernel's run-time alias row; alias names are one space for every mod, so two mods' "walker" are "a/walker" and "b/walker"). A mod's data kinds are defined on the first frame it runs, before any stage it is in loads, so its stage patches can place them. KitActor shows the kind's model, starts its idle action, has its sensors, stands on the ground under gravity with a shadow, runs the kind's callbacks, and is defeated by the kind's attacks: its "Down" action, then its drops, then gone. Everything belongs to the mod that defined it: when it stops, its objects are killed with the game's own kill and its attached models taken away; its kinds stay in their places, off, so other mods' numbers hold.

The module's source is `lumakit/source/actor.lum`.

## Declarations

| Name | Kind | Description |
|---|---|---|
| [`Kind`](#kind) | Struct |  |
| [`Sensor`](#sensor) | Enum | The sensors a kind may have, each a sphere `radius` across centered `radius` above its feet. Body: what touches and is touched; Attack: what hurts the player; Eye: what sees; Push: what pushes others away. |
| [`Attack`](#attack) | Enum | The player's attacks an object tells apart in its messages (MR::isMsgPlayerSpinAttack ... isMsgFireBallAttack). Any: every one of them. An open enum: LumaKit 1.x only adds cases. |
| [`KindId`](#kindid) | Struct | A kind's handle: its number, 0 for none. Its functions are also module functions (`actor.spawn(k, at)`). |
| [`PlacementKey`](#placementkey) | Struct | Where a placed object stands in its stage's data: the same on every machine with the same mods. |
| [`Attached`](#attached) | Struct | A model attached to a joint of an object (attach): its number, 0 for none. |
| [`define`](#define) | Function | A kind of the calling mod (its `name` placeable as "<mod id>/<name>"). A second define of one name by one mod replaces the first (its objects keep their number); two mods' kinds of one name are two kinds. A base other than LiveActor and drops of Form or Call are the caller's script errors. |
| [`find`](#find) | Function | The calling mod's kind `name`: one it defined, else the one of its kit/actor/<name>.json, defined now (data and code make the same thing; code finds a data kind to give it callbacks). none when there is neither, or the file does not read as a kind (an unknown sensor, attack or content name). |
| [`spawn`](#spawn) | Function | The kind made at `at` in the stage playing now, for the calling mod: its object, none for a handle that is not the calling mod's or when no stage can make one now (no scene, or the scene is starting). |
| [`all`](#all) | Function | Every object of the kind standing now, in the order made. A defeated one is dead, not gone (the game keeps a killed actor; a ref is none only once the object is destroyed), so liveness is the game's MR::isDead. |
| [`name`](#name) | Function | The kind's name ("" for a handle that is not the calling mod's). |
| [`attack_of`](#attack_of) | Function | Which of the player's attacks the message `msg` is (an override of receiveMsgPlayerAttack is given it), or none. |
| [`add_body`](#add_body) | Function | An enemy's body: a sensor named "body" of the enemy kind, `radius` across and centered `radius` above its feet; a shadow of the same size; gravity worked out for it each frame. Called once, from its init (after initHitSensor). |
| [`add_collision`](#add_collision) | Function | The model's collision mesh `name` as a moving collision part on its body sensor. Called once, from its init. |
| [`move_forward`](#move_forward) | Function | One frame's move along where it faces, `speed` a frame, along the ground under gravity (call from control). |
| [`move_toward`](#move_toward) | Function | One frame's move toward `at`, `speed` a frame, along the ground under gravity (call from control). |
| [`move_toward_player`](#move_toward_player) | Function | One frame's move toward the player; nothing when there is no player. |
| [`turn_toward`](#turn_toward) | Function | One frame's turn of where it faces toward `at`, at most `degrees_per_frame`. |
| [`sees_player`](#sees_player) | Function | Whether the player stands within `distance` of it and inside the fan of `degrees` about where it faces. |
| [`near_player`](#near_player) | Function | Whether the player is within `distance` of the object. |
| [`fall`](#fall) | Function | Pulls the object toward the ground (`strength` a frame) while it is in the air, and keeps `keep` (0 to 1) of its speed. On the ground the speed it has into the ground is taken away, as the ground stops it: left there, the binder would turn it into a slide along a curved or sloped floor every frame. |
| [`shockwave`](#shockwave) | Function | A ring that spreads from the object along the ground: `effect` played on it, and the player hurt as by an enemy's attack when on the ground within `radius` and below `height` above its feet once the ring has reached it (`speed` units a frame). |
| [`drop`](#drop) | Function | What a defeated object leaves: coins pop out of it as from the game's own enemies; anything else is given to the player. |
| [`attach`](#attach) | Function | The model `look` riding on joint `joint` of `to` (offset from the joint, none for none), each frame after `to` moved, until removed, `to` is gone or the mod stops. Its handle. |
| [`remove`](#remove) | Function | Takes the attached model away (nothing for one already gone or not the calling mod's). |
| [`alive`](#alive) | Function | Whether the attached model still rides. |
| [`placement`](#placement) | Function | Where the placed object stands in its stage's data: stage, zone and layer by name, its index in that layer's list. none for an object made by code (spawn, make) or not placed by a stage. |
| [`find_placed`](#find_placed) | Function | The object standing at that placement now, none when nothing stands there. |
| [`spawn_game`](#spawn_game) | Function | A game class made by its name at `at`, its files loaded first when its stage did not load them. none when the game has no such class or it cannot be made now. |

### `Kind`

```lumascript
pub struct Kind {
    name: text                                // the class name stages place it by
    base: text = "LiveActor"                  // a derivable game class; KitActor's is LiveActor
    look: model? = none
    idle_action: text? = none                 // started when it appears
    sensors: list<Sensor> = []
    on_ground: bool = true                    // stands on the ground as the game's walkers do: falls onto it, stays
                                              // where it stands, upright to its gravity; false: stays as placed
    shadow: bool = true
    collision: text? = none                   // the model's collision mesh as a moving collision part
    defeated_by: list<Attack> = []            // these attacks defeat it: its "Down" action, then drops
    drops: item.Gives? = none                 // Coins, StarBits, Lives, Health or Carry
    on_init: (fn(LiveActor))? = none
    on_frame: (fn(LiveActor))? = none
    on_touch_player: (fn(LiveActor))? = none
    on_attacked: (fn(LiveActor, Attack) -> bool)? = none   // true: taken care of (not defeated)
    on_talk: (fn(LiveActor))? = none          // makes it talkable: the game's A button prompt by it; A there stops the
                                              // player and calls it, and the lines it starts with kit.talk are said
}
```

### `Sensor`

```lumascript
pub enum Sensor { Body(radius: f32), Attack(radius: f32), Eye(radius: f32), Push(radius: f32) }
```

The sensors a kind may have, each a sphere `radius` across centered `radius` above its feet. Body: what touches and is touched; Attack: what hurts the player; Eye: what sees; Push: what pushes others away.

### `Attack`

```lumascript
pub enum Attack { Spin, Trample, HipDrop, StarBit, Fire, Any }
```

The player's attacks an object tells apart in its messages (MR::isMsgPlayerSpinAttack ... isMsgFireBallAttack). Any: every one of them. An open enum: LumaKit 1.x only adds cases.

### `KindId`

```lumascript
pub struct KindId {
    n: u32
    fn spawn(self, at: vec3) -> LiveActor? { return spawn(self, at) }
    fn all(self) -> list<ref<LiveActor>> { return all(self) }
    fn name(self) -> text { return name(self) }
}
```

A kind's handle: its number, 0 for none. Its functions are also module functions (`actor.spawn(k, at)`).

### `PlacementKey`

```lumascript
pub struct PlacementKey { stage: text, zone: text, layer: text, index: u32 }
```

Where a placed object stands in its stage's data: the same on every machine with the same mods.

### `Attached`

```lumascript
pub struct Attached {
    n: u32
    fn remove(self) { remove(self) }
    fn alive(self) -> bool { return alive(self) }
}
```

A model attached to a joint of an object (attach): its number, 0 for none.

### `define`

```lumascript
pub fn define(k: Kind) -> KindId
```

A kind of the calling mod (its `name` placeable as "<mod id>/<name>"). A second define of one name by one mod replaces the first (its objects keep their number); two mods' kinds of one name are two kinds. A base other than LiveActor and drops of Form or Call are the caller's script errors.

### `find`

```lumascript
pub fn find(name: text) -> KindId?
```

The calling mod's kind `name`: one it defined, else the one of its kit/actor/<name>.json, defined now (data and code make the same thing; code finds a data kind to give it callbacks). none when there is neither, or the file does not read as a kind (an unknown sensor, attack or content name).

### `spawn`

```lumascript
pub fn spawn(k: KindId, at: vec3) -> LiveActor?
```

The kind made at `at` in the stage playing now, for the calling mod: its object, none for a handle that is not the calling mod's or when no stage can make one now (no scene, or the scene is starting).

### `all`

```lumascript
pub fn all(k: KindId) -> list<ref<LiveActor>>
```

Every object of the kind standing now, in the order made. A defeated one is dead, not gone (the game keeps a killed actor; a ref is none only once the object is destroyed), so liveness is the game's MR::isDead.

### `name`

```lumascript
pub fn name(k: KindId) -> text
```

The kind's name ("" for a handle that is not the calling mod's).

### `attack_of`

```lumascript
pub fn attack_of(msg: u32) -> Attack?
```

Which of the player's attacks the message `msg` is (an override of receiveMsgPlayerAttack is given it), or none.

### `add_body`

```lumascript
pub fn add_body(a: LiveActor, radius: f32)
```

An enemy's body: a sensor named "body" of the enemy kind, `radius` across and centered `radius` above its feet; a shadow of the same size; gravity worked out for it each frame. Called once, from its init (after initHitSensor).

### `add_collision`

```lumascript
pub fn add_collision(a: LiveActor, name: text)
```

The model's collision mesh `name` as a moving collision part on its body sensor. Called once, from its init.

### `move_forward`

```lumascript
pub fn move_forward(a: LiveActor, speed: f32)
```

One frame's move along where it faces, `speed` a frame, along the ground under gravity (call from control).

### `move_toward`

```lumascript
pub fn move_toward(a: LiveActor, at: vec3, speed: f32)
```

One frame's move toward `at`, `speed` a frame, along the ground under gravity (call from control).

### `move_toward_player`

```lumascript
pub fn move_toward_player(a: LiveActor, speed: f32)
```

One frame's move toward the player; nothing when there is no player.

### `turn_toward`

```lumascript
pub fn turn_toward(a: LiveActor, at: vec3, degrees_per_frame: f32)
```

One frame's turn of where it faces toward `at`, at most `degrees_per_frame`.

### `sees_player`

```lumascript
pub fn sees_player(a: LiveActor, distance: f32, degrees: f32) -> bool
```

Whether the player stands within `distance` of it and inside the fan of `degrees` about where it faces.

### `near_player`

```lumascript
pub fn near_player(a: LiveActor, distance: f32) -> bool
```

Whether the player is within `distance` of the object.

### `fall`

```lumascript
pub fn fall(a: LiveActor, strength: f32, keep: f32)
```

Pulls the object toward the ground (`strength` a frame) while it is in the air, and keeps `keep` (0 to 1) of its speed. On the ground the speed it has into the ground is taken away, as the ground stops it: left there, the binder would turn it into a slide along a curved or sloped floor every frame.

### `shockwave`

```lumascript
pub fn shockwave(a: LiveActor, radius: f32, effect: effect, speed: f32 = 20.0, height: f32 = 60.0)
```

A ring that spreads from the object along the ground: `effect` played on it, and the player hurt as by an enemy's attack when on the ground within `radius` and below `height` above its feet once the ring has reached it (`speed` units a frame).

### `drop`

```lumascript
pub fn drop(a: LiveActor, gives: item.Gives)
```

What a defeated object leaves: coins pop out of it as from the game's own enemies; anything else is given to the player.

### `attach`

```lumascript
pub fn attach(to: LiveActor, look: model, joint: text, offset: mat34? = none) -> Attached
```

The model `look` riding on joint `joint` of `to` (offset from the joint, none for none), each frame after `to` moved, until removed, `to` is gone or the mod stops. Its handle.

### `remove`

```lumascript
pub fn remove(h: Attached)
```

Takes the attached model away (nothing for one already gone or not the calling mod's).

### `alive`

```lumascript
pub fn alive(h: Attached) -> bool
```

Whether the attached model still rides.

### `placement`

```lumascript
pub fn placement(a: LiveActor) -> PlacementKey?
```

Where the placed object stands in its stage's data: stage, zone and layer by name, its index in that layer's list. none for an object made by code (spawn, make) or not placed by a stage.

### `find_placed`

```lumascript
pub fn find_placed(key: PlacementKey) -> LiveActor?
```

The object standing at that placement now, none when nothing stands there.

### `spawn_game`

```lumascript
pub fn spawn_game(name: text, at: vec3) -> LiveActor?
```

A game class made by its name at `at`, its files loaded first when its stage did not load them. none when the game has no such class or it cannot be made now.

## Further reading

* [LumaKit reference](index.md)
* [About LumaKit](../../concepts/lumakit.md)
