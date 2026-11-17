---
title: kit.gravity
shortTitle: gravity
intro: "The player's gravity overridden, new gravity shapes placeable like the game's, the gravity at a point."
contentType: reference
---

## About kit.gravity

The `kit.gravity` module of LumaKit gives you the player's gravity overridden, new gravity shapes placeable like the game's, the gravity at a point.

To use the module, add `lumakit` to the `needs` of your mod's `luma.json` and write:

```lumascript
use kit.gravity
```

### How the module works

The player's gravity overridden, new gravity shapes placeable like the game's, the gravity at a point. The player's override (player 1's, the game's own; extra players of svc.player keep the game's gravity, and an override's frames are player 1's steps): the player asks its gravity through MR::calcGravityVectorOrZero (MarioActor::updateGravityVec, at its place, its center and ahead of it); for the player the kit's field answers instead while an override holds, so the game's own turning, jumping and camera follow it as they follow a planet's. The newest override holds; ending it gives the one before, then the game's. Shapes: define_shape aliases the shape's name (std alias_class) to KitGravityPlace; where a stage places that name, its init makes a gravity of the kit's class KitGravity (base PlanetGravity, std make_gravity: registered with the scene's gravities at the shape's range and priority, so the game's own gravities and a mod's sort and add as the game's do), whose calcOwnGravityVector is the shape's function within its range from the placement. Every override and shape is recorded under caller(); `on mods.stopped` ends a stopped mod's. Override.end/alive and ShapeId.name are also module functions (`end_override`, `override_alive`, `shape_name`).

The module's source is `lumakit/source/gravity.lum`.

## Declarations

| Name | Kind | Description |
|---|---|---|
| [`Field`](#field) | Enum |  |
| [`Override`](#override) | Struct |  |
| [`Shape`](#shape) | Struct |  |
| [`ShapeId`](#shapeid) | Struct |  |
| [`override_player`](#override_player) | Function | The player's gravity is `f` from now on (for `frames` frames, or until ended); the newest override holds. |
| [`end_override`](#end_override) | Function |  |
| [`override_alive`](#override_alive) | Function |  |
| [`define_shape`](#define_shape) | Function | A new kind of gravity, placed in stages by its name: inside its range (from the placement) its function gives the pull at a point, or none where it gives none; `priority` sorts it among the game's. |
| [`shape_name`](#shape_name) | Function |  |
| [`at`](#at) | Function | The gravity at `p` (unit length; zero where none pulls), as the game's objects get it there: the game's gravities and the mods' shapes, not a player override. |

### `Field`

```lumascript
pub enum Field { Parallel { direction: vec3 }, Point { at: vec3 }, Away { at: vec3 } }
```

### `Override`

```lumascript
pub struct Override {
    n: u64
    fn end(self) { end_override(self) }
    fn alive(self) -> bool { return override_alive(self) }
}
```

### `Shape`

```lumascript
pub struct Shape { name: text, vector: fn(vec3) -> vec3?, range: f32 = 1000.0, priority: int = 0 }
```

### `ShapeId`

```lumascript
pub struct ShapeId {
    n: int
    fn name(self) -> text { return shape_name(self) }
}
```

### `override_player`

```lumascript
pub fn override_player(f: Field, frames: u32? = none) -> Override
```

The player's gravity is `f` from now on (for `frames` frames, or until ended); the newest override holds.

### `end_override`

```lumascript
pub fn end_override(o: Override)
```

### `override_alive`

```lumascript
pub fn override_alive(o: Override) -> bool
```

### `define_shape`

```lumascript
pub fn define_shape(s: Shape) -> ShapeId
```

A new kind of gravity, placed in stages by its name: inside its range (from the placement) its function gives the pull at a point, or none where it gives none; `priority` sorts it among the game's.

### `shape_name`

```lumascript
pub fn shape_name(id: ShapeId) -> text
```

### `at`

```lumascript
pub fn at(p: vec3) -> vec3
```

The gravity at `p` (unit length; zero where none pulls), as the game's objects get it there: the game's gravities and the mods' shapes, not a player override.

## Further reading

* [LumaKit reference](index.md)
* [About LumaKit](../../concepts/lumakit.md)
