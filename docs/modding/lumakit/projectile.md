# LumaKit: projectile

Gives: things thrown or shot, with bounces, life, trail, burst and a hit callback.

```lumascript skip a listing from the part's contract, not a module that builds alone
pub struct Shot {
    look: model? = none                       // none: only its trail
    from: vec3
    velocity: vec3                            // game units per frame
    falls: bool = false                       // pulled by gravity
    bounces: u32 = 0
    life_frames: u32 = 180
    radius: f32 = 30.0
    trail: effect? = none
    burst: effect? = none
    sound_throw: sound? = none
    sound_hit: sound? = none
    hits_map: bool = true
    on_hit: fn(Hit, HitSensor, HitSensor) -> Then = fn(h: Hit, shot: HitSensor, touched: HitSensor) -> Then { return Then.Burst }
}
// The shot's sensor and the touched one are the call's parameters: views, valid in the call (a struct field holds no
// view, lumascript rule 3, S0103).
pub struct Hit { at: vec3; map: bool }
pub enum Then { Bounce, Burst, Pass, Stick }
pub fn throw(s: Shot) -> ref<LiveActor>
```

Built on: `K.derive` (`LiveActor`), `K.call` (`MR::addHitSensor`, `MR::getFirstPolyOnLineToMap`, `MR::calcGravity`,
`MR::emitEffect`, `MR::startSound`), `C.model`, `C.fx`, `C.sound`. Conformance: a ball bouncing three times then
bursting, `on_hit` choosing each `Then`, falling shots along a planet's gravity, 64 at once without a frame over its
budget, the thrower's mod off (all its shots killed). Corpus: 1, 19, 40. B: F. A: G.
