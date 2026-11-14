# LumaKit: pointer

Gives: the star pointer: where it points on screen and in the world, what B throws and what it costs, grabbing, the
source that aims it (IR, gyro, mouse, stick), a co-star pointer.

```lumascript skip a listing from the part's contract, not a module that builds alone
pub fn screen() -> vec2?                          // MR::getStarPointerScreenPosition; none: off screen
pub fn world() -> (vec3, vec3)?                   // the map point and normal under the pointer
pub enum Cost { StarBits(u32), Coins(u32), Free }
pub struct Throw { shot: fn(vec3, vec3) -> projectile.Shot, cost: Cost = Cost.StarBits(1) }   // (from, toward)
pub struct ThrowSet { fn end(self) }
pub fn set_throw(t: Throw) -> ThrowSet            // what B throws; end(): star bits again
pub fn on_grab(f: fn(LiveActor) -> bool) -> ThrowSet   // A on a pointed actor
pub enum Source { Infrared, Gyro(sensitivity: f32), Mouse, Stick }
pub fn source(pad: input.Pad, s: Source)
```

Built on: `K.hook` (`StarPieceDirector`'s shot, `StarPointerController`), `K.call` (`MR::getStarPointerScreenPosition`,
`MR::getFirstPolyOnLineToMap`), `input` (`input.dev`† motion), `kit.projectile`. Conformance: B throwing coins at a
cost, out of coins (nothing thrown, the game's sound), a grab, gyro aim on a pad with motion and its refusal on one
without (named), a world point under the pointer on a planet.
