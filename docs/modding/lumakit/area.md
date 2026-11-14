# LumaKit: area

Gives: new kinds of area, placed like the game's, with what happens inside.

```lumascript skip a listing from the part's contract, not a module that builds alone
pub struct Kind {
    name: text
    shape_from_placement: bool = true         // the placement's own shape (box, cylinder, sphere), as AreaObj
    player_speed: f32 = 1.0                   // a zone that makes the player slow
    player_force: vec3? = none                // wind, along the area's own axes
    camera: draw.ViewCamera? = none           // the main camera while the player is inside
    on_enter: fn(MarioActor)? = none
    on_inside: fn(MarioActor)? = none          // each frame
    on_leave: fn(MarioActor)? = none
}
pub struct KindId { fn contains(self, at: vec3) -> bool; fn player_inside(self) -> bool }
pub fn define(k: Kind) -> KindId
pub fn find(name: text) -> KindId?
```

Data `kit/area/<name>.json`. Built on: `K.derive` (`AreaObj`), `K.hook` (`AreaObjMgr`), `K.call`, `C.stage`. Its
`camera` takes the main camera through `kit.camera` (rule 7). Conformance: an area placed in three shapes with enter,
inside and leave each once in order, slow and wind presets, a side camera in and out, two areas overlapping. Corpus:
29, 50, 68, 75, 76.
