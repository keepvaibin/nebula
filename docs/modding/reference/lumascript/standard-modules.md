---
title: Standard modules
shortTitle: Standard modules
intro: The names and types every LumaScript module has without `use`.
contentType: reference
---

## About standard names

Every module has these names without `use`. A module's own names, and the names it brings in with `use`, come first:
a local or an item of the same name hides a standard one. Everything else is a service, a LumaKit module or a
library mod.

## Values and math

| Name | Form | Gives |
|---|---|---|
| `min`, `max` | `min(a, b)` | the smaller or larger of two numbers of one type |
| `abs`, `sqrt`, `sin`, `cos` | `abs(x)` | the function of one number |
| `atan2` | `atan2(y, x)` | the angle, in radians |
| `clamp`, `lerp` | `clamp(x, lo, hi)`, `lerp(a, b, t)` | `x` kept in `lo..=hi`; `a` moved toward `b` by `t` |
| `dot`, `cross`, `length`, `normal` | `dot(a, b)`, `normal(v)` | vector math |
| `vec2`, `vec3`, `vec4`, `quat`, `color`, `mat34` | `vec3(x, y, z)`, `vec3()` | a value; with no arguments every component 0, but a `quat`'s `w`, a `color`'s alpha and a `mat34`'s diagonal 1 |
| `low_bits` | `low_bits<u8>(n)` | an integer's low bits in the type named, with no script error |
| `hash32` | `hash32(t)` | a 32-bit hash of a `text` |
| `same` | `same(a, b)` | whether two views or service handles are the same object or stream |
| `json` | `json.encode(v)`, `json.decode<T>(t)`, `json.Value` | JSON text, and a value read from it |

For the operators and the types, see [Detailed rules](detailed-rules.md) and [Types](types.md).

## The mod and its content

| Name | Form | Gives |
|---|---|---|
| `keep`, `kept` | `keep(v)` in `on stop`'s own body; `kept<T>()` in `on start`'s own body | `keep` hands the game a value of JSON data for the mod's next load this session (`bool`); `kept` gives it back, a `T?`. See [Reloading a mod while playing](../../how-tos/reloading-a-mod-while-playing.md). |
| `caller` | `caller()` | in an `export` function, the number of the mod that called it; elsewhere, this mod's |
| `model_named`, `effect_named`, `sound_named`, `layout_named`, `font_named`, `movie_named` | `sound_named(t)` | the mod's content by a name made while running. See [Lifecycle](lifecycle.md). |
| `can` | `can(MR::someFunction)` | `true` when the running game has that function; inside `if can(...)` the script may call it |
| `holds` | `holds("SET")` | `true` when the running game has everything the named set needs; inside `if holds(...)` the script may use it. A set the game does not know is S0156. |
| `is_state` | `is_state(S)`, `is_state(M.S)` | `true` while the object's machine is in state `S`. See [State machines](state-machines.md). |

## Objects and classes

| Name | Form | Gives |
|---|---|---|
| `make` | `make(name, place)`, `make(name, place, count)` | `count` objects (1 by default) of a class the game's factory makes, or a mod class, at a `MakePlace`; a `list` of their `LiveActor` parts |
| `placement_of` | `placement_of(actor)` | where a stage placed the object, a `Placement`; `none` for one made by code or by `make` |
| `placed_at` | `placed_at(placement)` | the object a scene made from a placement (an empty `stage` is the main stage); `none` when there is none |
| `as_class` | `as_class<C>(x)` | the object as the game class or mod class `C`, or `none` when it is not one |
| `alias_class` | `alias_class(name, class, args)` | `true` when a stage that places `name`, and `make` with it, now make an object of the live mod class `class` (any mod's), its `Obj_arg` values from `args` (at most 8; the rest, and any -1, the placement's own); the game says each refusal in the mod log |
| `make_gravity` | `make_gravity<C>(place)` | an object of the mod's gravity class `C` (base `PlanetGravity`) at a `GravityPlace`, added to the scene's gravities; `none` when the game makes none (said in the mod log) |

For classes, see [Defining classes](defining-classes.md).

## The game's numbers and tables

| Name | Form | Gives |
|---|---|---|
| `set_number` | `set_number(name, value)`, `set_number(name, signature, place, value)` | `true` when a number in the game's code is set for this mod: a file's constant (`"File.cpp#name"`), or the `place`-th number written in a function; put back when the mod goes. `false`: no such number, several, or a value not of its kind (the mod log says which). |
| `table`, `set_table` | `table(name, steps...)`, `set_table(name, steps..., value)` | one place of a table in the game's code: `table` reads it as it is now (or `none`); `set_table` writes it for the mod (`bool`). See [The game by name](the-game-by-name.md). |

## Types

| Type | Fields |
|---|---|
| `MakePlace` | `at: vec3`; `rotation: vec3` (degrees, default 0); `scale: vec3` (default 1); `args: list<i32>` (`Obj_arg` values from 0, at most 8; the rest -1); `dead: bool` (made dead until something makes it appear; actors only) |
| `Placement` | `stage: text`, `zone: text`, `layer: text` (`"Common"`, `"LayerA"` to `"LayerAE"`), `index: u32` (among that zone's placements of that layer) |
| `GravityPlace` | `range: f32` (below 0: everywhere); `priority: i32` (default 0; only the highest pulling at a point counts); `types: u32` (default 1; bits 1 normal, 2 shadow, 4 magnet, 8 launcher); `inverse: bool` (the pull reversed) |

## Further reading

* [LumaScript reference](index.md)
