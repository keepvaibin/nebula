# LumaKit: gravity

Gives: the player's gravity overridden, new gravity shapes placeable like the game's, the gravity at a point.

```lumascript skip a listing from the part's contract, not a module that builds alone
pub enum Field { Parallel { direction: vec3 }, Point { at: vec3 }, Away { at: vec3 } }
pub struct Override { fn end(self); fn alive(self) -> bool }
pub fn override_player(f: Field, frames: u32? = none) -> Override
pub struct Shape { name: text, vector: fn(vec3) -> vec3?, range: f32 = 1000.0, priority: int = 0 }
pub struct ShapeId { fn name(self) -> text }
pub fn define_shape(s: Shape) -> ShapeId          // placed in stages by name (idea 73)
pub fn at(p: vec3) -> vec3
```

Built on: `K.derive` (`PlanetGravity`, overriding `calcOwnGravityVector`), `K.call` (`MR::registerGravity`, `GravityCreator`,
`MR::calcGravityVector`), `K.hook` (the player's gravity), `C.stage`. Conformance: a parallel override flipped and
ended, an override that ends by its frames, a new shape placed with the game's own beside it (priority), two mods'
overrides (rule 7). Corpus: 51, 73, 74. B: F. A: G.
