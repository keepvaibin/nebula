# LumaKit: ride

Gives: mounts and vehicles: getting on and off, their moves, the player posed on them.

```lumascript skip a listing from the part's contract, not a module that builds alone
pub struct Mount {
    name: text                                // placeable by name
    look: model
    seat_joint: text                          // where the player sits
    rider_action: text = "Ride"               // the player's clip while riding
    speed: f32 = 15.0
    jump: f32 = 30.0
    turn_degrees: f32 = 6.0
    surface: bool = true                      // drives along a planet's surface under gravity (karts, boards)
    moves: list<player.state.StateId> = []    // states the rider has while mounted (a tongue, a flutter)
    on_mount: fn(LiveActor)? = none
    on_dismount: fn(LiveActor)? = none
}
pub struct MountId { fn spawn(self, at: vec3) -> LiveActor }
pub fn define(m: Mount) -> MountId
pub fn find(name: text) -> MountId?
pub fn riding() -> ref<LiveActor>?
pub fn dismount()
```

Data `kit/ride/<name>.json`. Built on: `K.derive` (`LiveActor`), `K.hook` (the player's control while mounted),
`kit.player.state`, `kit.actor`, `C.skin`, `C.anim`, `input`. Conformance: mount, ride on a planet, jump, a mounted
move, damage throwing the rider off, dismount; the mod off while riding. Corpus: 44, 45. B: G (proposed). A: F.
