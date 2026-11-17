---
title: kit.area
shortTitle: area
intro: "New kinds of area, placed like the game's, with what happens inside."
contentType: reference
---

## About kit.area

The `kit.area` module of LumaKit gives you new kinds of area, placed like the game's, with what happens inside.

To use the module, add `lumakit` to the `needs` of your mod's `luma.json` and write:

```lumascript
use kit.area
```

### How the module works

New kinds of area, placed like the game's, with what happens inside. A kind is defined once by a mod (define, or its kit/area/<name>.json, read for every mod needing LumaKit at its start). Its areas are placed as the game's are, the shape by the name's end as the game names its own: "<name>Cube" (a box from its base), "<name>Sphere", "<name>Cylinder" (in other mods' stages "<mod id>/<name>Cube" ...): the kernel's run-time alias rows make each an object of the kit's class for that shape (KitAreaCube, KitAreaSphere, KitAreaCylinder, made by the factory rows LumaAreaCube, LumaAreaSphere, LumaAreaCylinder), whose first argument is the kind. The objects are the game's AreaObj: the placement's own form, switches and following; each joins the manager its row names, "LumaArea" (no game code reads it, so no game query sees them). The player is player 1, the game's own; extra players (svc.player) are not counted. Once a frame, before the player moves (MarioActor::movement), each kind finds whether the player is inside one of its areas: enter, then inside each frame, then leave, each once in that order. Inside, the player's walking speed is the game's own resist (MR::setPlayerWalkingResist, as SnowFloor; the player's movement clears it each frame), the force moves the player through the game's own push (Mario::addTrans, its collision after; a box turns it to its own axes), and the camera is taken through kit.camera (the area entered last holds; leaving gives the one before). Overlapping kinds all act: speeds multiply, forces add. Every kind is recorded under caller(); `on mods.stopped` ends a stopped mod's (the player leaves it without its callback, its camera given back; its alias rows go with the mod, the kernel's). KindId's functions are also module functions (`area.contains(id, at)`, `area.player_inside(id)`). `camera` is a view.Camera; data cameras are rigs on the player made when the player enters (a view is never kept).

The module's source is `lumakit/source/area.lum`.

## Declarations

| Name | Kind | Description |
|---|---|---|
| [`Kind`](#kind) | Struct |  |
| [`KindId`](#kindid) | Struct | A kind's handle: its number, 0 for none. |
| [`define`](#define) | Function | A new kind of area for the calling mod: its areas placed as "<name>Cube", "<name>Sphere", "<name>Cylinder". |
| [`find`](#find) | Function | The calling mod's kind `name`: one it defined (or its data defined at LumaKit's start), else the one of its kit/area/<name>.json, defined now; none when there is neither or the file does not read as a kind. |
| [`contains`](#contains) | Function | Whether `at` is inside one of the stage's areas of the kind. |
| [`player_inside`](#player_inside) | Function | Whether the player was inside one of the kind's areas at this frame's step. |

### `Kind`

```lumascript
pub struct Kind {
    name: text
    shape_from_placement: bool = true         // the placement's own shape (box, cylinder, sphere), as AreaObj
    player_speed: f32 = 1.0                   // a zone that makes the player slow
    player_force: vec3? = none                // wind, along the area's own axes
    camera: view.Camera? = none               // the main camera while the player is inside
    on_enter: fn(MarioActor)? = none
    on_inside: fn(MarioActor)? = none          // each frame
    on_leave: fn(MarioActor)? = none
}
```

### `KindId`

```lumascript
pub struct KindId {
    n: u32
    fn contains(self, at: vec3) -> bool { return contains(self, at) }
    fn player_inside(self) -> bool { return player_inside(self) }
}
```

A kind's handle: its number, 0 for none.

### `define`

```lumascript
pub fn define(k: Kind) -> KindId
```

A new kind of area for the calling mod: its areas placed as "<name>Cube", "<name>Sphere", "<name>Cylinder".

### `find`

```lumascript
pub fn find(name: text) -> KindId?
```

The calling mod's kind `name`: one it defined (or its data defined at LumaKit's start), else the one of its kit/area/<name>.json, defined now; none when there is neither or the file does not read as a kind.

### `contains`

```lumascript
pub fn contains(id: KindId, at: vec3) -> bool
```

Whether `at` is inside one of the stage's areas of the kind.

### `player_inside`

```lumascript
pub fn player_inside(id: KindId) -> bool
```

Whether the player was inside one of the kind's areas at this frame's step.

## Further reading

* [LumaKit reference](index.md)
* [About LumaKit](../../concepts/lumakit.md)
