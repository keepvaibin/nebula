---
title: kit.stage
shortTitle: stage
intro: "New galaxies with stars and comets in the domes, a new dome, music per galaxy, warping, stars appearing, the hub replaced, stages built in code."
contentType: reference
---

## About kit.stage

The `kit.stage` module of LumaKit gives you new galaxies with stars and comets in the domes, a new dome, music per galaxy, warping, stars appearing, the hub replaced, stages built in code.

To use the module, add `lumakit` to the `needs` of your mod's `luma.json` and write:

```lumascript
use kit.stage
```

### How the module works

Galaxies a mod adds, found by name, and warping to a galaxy's scenario. A galaxy a mod defines is recorded under the mod (caller()) and leaves with it. The data (Dome, Unlock, Comet, Star, Galaxy, DomeDef), define_galaxy, define_dome, find, find_dome, go_to, warp (GalaxyId.go), name and hub (its hook on NebulaMods::hubStage); below them, a galaxy listed in its dome with its stars counted (the game's questions answered from the definitions and kit.progress), comet stars (comets_of), music for a galaxy (music), a star appearing (appear_star) and stages built in code (build).

The module's source is `lumakit/source/stage.lum`.

## Declarations

| Name | Kind | Description |
|---|---|---|
| [`Dome`](#dome) | Enum |  |
| [`Unlock`](#unlock) | Enum |  |
| [`Comet`](#comet) | Enum |  |
| [`Star`](#star) | Struct |  |
| [`Galaxy`](#galaxy) | Struct |  |
| [`DomeDef`](#domedef) | Struct |  |
| [`GalaxyId`](#galaxyid) | Struct | A galaxy by its stage name. Its functions are also module functions (`stage.warp(g, scenario)`). |
| [`define_galaxy`](#define_galaxy) | Function | The galaxy recorded for the calling mod (a second of the same name replaces the first); its id. |
| [`define_dome`](#define_dome) | Function | A dome recorded for the calling mod (a second of the same name replaces the first). |
| [`find_dome`](#find_dome) | Function | A dome a mod defined, by its name; none for any other. |
| [`hub`](#hub) | Function | The hub the game returns to: `stage` for the calling mod until it asks for another or ends; the newest caller's holds it. An empty name gives the hub back. |
| [`find`](#find) | Function | A galaxy a mod defined, by its stage name; none for any other. |
| [`go_to`](#go_to) | Function | The game moves to `galaxy`'s scenario `scenario` as it does through a launch star (MR::requestChangeStageInGameMoving). |
| [`warp`](#warp) | Function | The game moves to the galaxy's scenario `scenario` as it does through a launch star. |
| [`name`](#name) | Function |  |
| [`comets_of`](#comets_of) | Function | A star's comet (Star.comet) is the comet over its galaxy for that scenario while the mod is on (kit.progress, the game's comet schedule). Called for the calling mod after it defines or redefines a galaxy. |
| [`sound_path`](#sound_path) | Function | A sound as svc.audio names it for the kit, which plays the sounds other mods give it: "<id>:<path>", the mod `owner` gave it (a caller() number); its path alone when that mod is gone. |
| [`music`](#music) | Function | `s` is the music of `galaxy` (of its scenario `scenario`, or of all with none) while the calling mod is on: it plays over the game's own when that stage's music starts, and the game's is back in any other stage. |
| [`appear_star`](#appear_star) | Function | The star of `scenario` appears at `at` in the stage playing now, with the game's own appearing scene. |
| [`star_holder`](#star_holder) | Function | The holder the star of `scenario` appears from in the stage playing now (appear_star made it); none before. |
| [`Builder`](#builder) | Struct | A builder is a handle: its number while its stage is being built, 0 after. What it is asked for is kept here until the builder function returns. Its functions are also module functions (`stage.place(b, ...)`). |
| [`place`](#place) | Function | An object of the game or of a mod by its name, placed at `at` (rotation in degrees, up to eight arguments). |
| [`planet`](#planet) | Function | A planet: a model and the collision the player walks on. |
| [`field`](#field) | Function | A gravity field of the stage (kit.gravity's Field). |
| [`build`](#build) | Function | The stage `name` is built by `make_stage` each time it loads, for the calling mod: its objects, planets and gravity are made when the stage starts, on top of whatever the stage's own files hold. |

### `Dome`

```lumascript
pub enum Dome { Terrace, Fountain, Kitchen, Bedroom, Engine, Garden, Mod(name: text) }
```

### `Unlock`

```lumascript
pub enum Unlock { Always, Stars(count: u32), Star(galaxy: text, scenario: u32), Code(check: fn() -> bool) }
```

### `Comet`

```lumascript
pub enum Comet { Speedy, Daredevil, Cosmic, FastFoe, Purple }
```

### `Star`

```lumascript
pub struct Star { scenario: u32, title: text, comet: Comet? = none, hidden: bool = false }
```

### `Galaxy`

```lumascript
pub struct Galaxy { name: text, title: text, dome: Dome, unlock: Unlock = Unlock.Always, stars: list<Star>,
                    music: sound? = none, icon: draw.Texture? = none }
```

### `DomeDef`

```lumascript
pub struct DomeDef { name: text, title: text, unlock: Unlock = Unlock.Always }
```

### `GalaxyId`

```lumascript
pub struct GalaxyId {
    name: text
    fn warp(self, scenario: u32) { warp(self, scenario) }
}
```

A galaxy by its stage name. Its functions are also module functions (`stage.warp(g, scenario)`).

### `define_galaxy`

```lumascript
pub fn define_galaxy(g: Galaxy) -> GalaxyId
```

The galaxy recorded for the calling mod (a second of the same name replaces the first); its id.

### `define_dome`

```lumascript
pub fn define_dome(d: DomeDef)
```

A dome recorded for the calling mod (a second of the same name replaces the first).

### `find_dome`

```lumascript
pub fn find_dome(name: text) -> DomeDef?
```

A dome a mod defined, by its name; none for any other.

### `hub`

```lumascript
pub fn hub(stage: text)
```

The hub the game returns to: `stage` for the calling mod until it asks for another or ends; the newest caller's holds it. An empty name gives the hub back.

### `find`

```lumascript
pub fn find(name: text) -> GalaxyId?
```

A galaxy a mod defined, by its stage name; none for any other.

### `go_to`

```lumascript
pub fn go_to(galaxy: text, scenario: u32)
```

The game moves to `galaxy`'s scenario `scenario` as it does through a launch star (MR::requestChangeStageInGameMoving).

### `warp`

```lumascript
pub fn warp(g: GalaxyId, scenario: u32)
```

The game moves to the galaxy's scenario `scenario` as it does through a launch star.

### `name`

```lumascript
pub fn name(g: GalaxyId) -> text
```

### `comets_of`

```lumascript
pub fn comets_of(g: GalaxyId)
```

A star's comet (Star.comet) is the comet over its galaxy for that scenario while the mod is on (kit.progress, the game's comet schedule). Called for the calling mod after it defines or redefines a galaxy.

### `sound_path`

```lumascript
pub fn sound_path(owner: u64, s: sound) -> text
```

A sound as svc.audio names it for the kit, which plays the sounds other mods give it: "<id>:<path>", the mod `owner` gave it (a caller() number); its path alone when that mod is gone.

### `music`

```lumascript
pub fn music(galaxy: text, scenario: u32?, s: sound)
```

`s` is the music of `galaxy` (of its scenario `scenario`, or of all with none) while the calling mod is on: it plays over the game's own when that stage's music starts, and the game's is back in any other stage.

### `appear_star`

```lumascript
pub fn appear_star(at: vec3, scenario: u32)
```

The star of `scenario` appears at `at` in the stage playing now, with the game's own appearing scene.

### `star_holder`

```lumascript
pub fn star_holder(scenario: u32) -> LiveActor?
```

The holder the star of `scenario` appears from in the stage playing now (appear_star made it); none before.

### `Builder`

```lumascript
pub struct Builder {
    id: u32
    fn place(self, kind: text, at: vec3, rotation: vec3 = vec3(0.0, 0.0, 0.0), args: list<int> = []) {
        place(self, kind, at, rotation, args)
    }
    fn planet(self, look: model, collision: model, at: vec3) { planet(self, look, collision, at) }
    fn field(self, f: gravity.Field) { field(self, f) }
}
```

A builder is a handle: its number while its stage is being built, 0 after. What it is asked for is kept here until the builder function returns. Its functions are also module functions (`stage.place(b, ...)`).

### `place`

```lumascript
pub fn place(b: Builder, kind: text, at: vec3, rotation: vec3 = vec3(0.0, 0.0, 0.0), args: list<int> = [])
```

An object of the game or of a mod by its name, placed at `at` (rotation in degrees, up to eight arguments).

### `planet`

```lumascript
pub fn planet(b: Builder, look: model, collision: model, at: vec3)
```

A planet: a model and the collision the player walks on.

### `field`

```lumascript
pub fn field(b: Builder, f: gravity.Field)
```

A gravity field of the stage (kit.gravity's Field).

### `build`

```lumascript
pub fn build(name: text, make_stage: fn(Builder))
```

The stage `name` is built by `make_stage` each time it loads, for the calling mod: its objects, planets and gravity are made when the stage starts, on top of whatever the stage's own files hold.

## Further reading

* [LumaKit reference](index.md)
* [About LumaKit](../../concepts/lumakit.md)
