# LumaKit: actor

Gives: a new object kind in a few lines, and the helpers a mod's own `class` uses for the usual parts.

```lumascript skip a listing from the part's contract, not a module that builds alone
pub struct Kind {
    name: text                                // the class name stages place it by
    base: text = "LiveActor"                  // a derivable game class (K.derive)
    look: model? = none
    idle_action: text? = none                 // started when it appears
    sensors: list<Sensor> = []
    on_ground: bool = true                    // bound to the ground and pulled by gravity
    shadow: bool = true
    collision: text? = none                   // the model's collision mesh as a moving collision part
    defeated_by: set<Attack> = []             // these attacks defeat it: its "Down" action, then drops
    drops: Drops = Drops.None
    on_init: fn(LiveActor)? = none
    on_frame: fn(LiveActor)? = none
    on_touch_player: fn(LiveActor)? = none
    on_attacked: fn(LiveActor, Attack) -> bool = fn(a: LiveActor, k: Attack) -> bool { return false }
    on_talk: fn(LiveActor)? = none            // makes it talkable (the A button prompt), with kit.talk
}
pub enum Sensor { Body(radius: f32), Attack(radius: f32), Eye(radius: f32), Push(radius: f32) }
pub enum Attack { Spin, Trample, HipDrop, StarBit, Fire, Any }   // MR::isMsgPlayerSpinAttack ... isMsgFireBallAttack
pub enum Drops { None, Coins(u32), StarBits(u32), Life }
pub struct KindId { fn spawn(self, at: vec3) -> LiveActor; fn all(self) -> list<ref<LiveActor>>; fn name(self) -> text }
pub struct PlacementKey { stage: text, zone: text, layer: text, index: u32 }
pub struct Attached { fn remove(self); fn alive(self) -> bool }
pub fn define(k: Kind) -> KindId
pub fn find(name: text) -> KindId?
pub fn add_body(a: LiveActor, radius: f32)    // body sensor, ground binder, shadow, gravity: for a mod's class
pub fn add_collision(a: LiveActor, name: text)
pub fn move_forward(a: LiveActor, speed: f32)                 // along the surface, under gravity
pub fn move_toward(a: LiveActor, at: vec3, speed: f32)
pub fn move_toward_player(a: LiveActor, speed: f32)
pub fn turn_toward(a: LiveActor, at: vec3, degrees_per_frame: f32)
pub fn sees_player(a: LiveActor, distance: f32, degrees: f32) -> bool
pub fn shockwave(a: LiveActor, radius: f32, effect: effect, speed: f32 = 20.0, height: f32 = 60.0)
pub fn attach(to: LiveActor, look: model, joint: text, offset: mat34? = none) -> Attached   // idea 25
pub fn placement(a: LiveActor) -> PlacementKey?          // P-15: stable across machines with the same mods
pub fn find_placed(key: PlacementKey) -> LiveActor?
pub fn spawn_game(name: text, at: vec3) -> LiveActor?     // a game class by name, its files loaded first (idea 194)
```

Data `kit/actor/<name>.json`: a kind without code (fields but `fn`). Built on: `K.derive`, `K.hook`, `K.call`
(`MR::initDefaultPos`, `MR::addHitSensor`, `addHitSensorEnemy`, `initShadowVolumeSphere`, `connectToSceneEnemy`,
`MR::startAction`, `MR::isMsgPlayerSpinAttack`, `isMsgPlayerTrample`, `isMsgPlayerHipDrop`, `isMsgStarPieceAttack`,
`isMsgFireBallAttack`, `MR::appearCoinPop`, `MR::isNearPlayer`, `addVelocityMoveToDirection`), `K.field`, `C.model`,
`C.anim`, `C.stage` (placement), `assets` (`spawn_game`'s files), `mods` (names). Conformance: a kind from
data placed in a stage, the same kind spawned mid-stage, each sensor kind, each `Attack` defeating it, drops, a talk
callback, a hat on Mario's head joint and on a Goomba's, `placement`/`find_placed` equal on two runs, a game class
spawned that the stage did not load, two mods' kinds of one short name (rule 7). Corpus: 5, 25, 31, 32, 41, 43, 46,
47, 50, 52, 57, 81, 91, 194. B: F. A: G.
