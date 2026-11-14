# LumaKit: the rules every module keeps

## More rules

The builders are as ASSIGNMENTS phase 5 gives them; `ride` and `pointer` (added by DECISIONS 3) are proposed for G,
which makes eleven modules each.

This file fixes the names, types and data each module offers so the programs and the corpus can be checked against
them now. Game names cited were checked in `Nebula-LG/native/petari/include` (grep).

### 0. Rules every module keeps

1. One library mod. LumaKit is the mod `lumakit` (version 1.x), written in LumaScript, built by LumaLink for every
   platform like any mod, and installed as a `.luma`. A mod that writes `use kit.<module>` must list `{ "id":
   "lumakit", "version": "^1" }` in `needs`; without it the compiler stops naming the `use` line and the `needs` entry
   to add (`lumalink add need lumakit` writes it). `kit.` is the short form of `lumakit.` (language section 1). One
   copy is loaded however many mods use it, so state the game has one of (the player's form, the main camera, the
   pause menu, the HUD corners) has one owner that settles between mods (rule 7).
2. How a call binds. Each `pub fn` of a library mod's module is exported by its library under its ABI name
   (`lm_<module>__<fn>`, abi section 2, `.` in a module name written `_`), with a hash of its LumaScript signature. A
   mod calling it gets one import row of a new kind, `LUMA_IMPORT_MOD_FUNCTION` (mod id, version range, symbol,
   signature hash), which the loader binds at load to the library's address: a direct native call, no lookup per call,
   nothing translated. A wrong hash (a library built from another interface) stops the calling mod's load naming the
   function, both versions and both hashes. Types a module declares (`Form`, `Hit`) are compiled into the caller from
   the library's interface file (`lumakit.lumi`, written by the build next to the libraries; LumaLink reads it from
   the needed `.luma`), so both sides have one layout.
3. Values across the boundary. Plain values (numbers, vectors, structs and enums of them) pass by value. `text`,
   `bytes` and containers a module keeps are copied into LumaKit's memory when kept (abi 7.1: each mod's `alloc` is
   its own, and a caller's memory is freed when it stops). A closure given to a module (`on_spin`, `on_choose`) is
   kept with the mod that gave it; the module calls it only while that mod is on. Views (`LiveActor`) are never kept:
   a module turns a view it must keep into a `ref` (language 6.1, 6.2).
4. Everything belongs to the mod that made it. A form, an actor kind, a HUD element, a menu entry, a hook added for a
   caller: each is recorded under the calling mod and undone, newest first, when that mod stops or reloads
   (`K.reload`), before its library is unloaded. A mod turned off mid-stage leaves no entry, element or override
   behind; actors of its kinds are killed with the game's own `kill`, and a kind placed in a stage that no mod defines
   any more is not made (the stage loads without it; the log names the kind, the stage and the mod).
5. Public parts only. A module uses the kernel (K.*), the services (svc.*), content (C.*), the game by name through
   `K.call`/`K.hook`, and the parts DECISIONS 3 added (†). Nothing else exists for it: LumaKit is built with the same
   LumaLink, checked by the same validator, and its `luma.json` declares its permissions like any mod. Each module's
   "Built on" line lists what it uses, and section 3's check confirms every id there is a public part.
6. Permissions are the caller's. A module function that uses a service needing a permission (`net`, `fs` exchange,
   `capture`) says so in its interface (`// needs: net`). The compiler requires the calling mod to declare it, as for
   a direct service call, and the module makes the service call for the caller, so the player sees each permission on
   the mod that uses it and LumaKit itself declares none of them.
7. One owner, many mods. Where two mods ask for the one thing, the newest request holds it and the earlier one holds
   it again when the newer ends: the player's form, an override of the player's gravity, the main camera, the player's
   character. HUD elements in one corner stack in the order made. Pause entries are listed in the mod list's order
   (top first), each mod's in the order made. A clash of names (two mods defining the actor kind `LumaBalloon`) is not
   possible: kinds, items and forms are named `<mod id>/<name>` to the game (allocated through `svc.mods`, unique and
   stable across sessions), and a stage places the short name only from its own mod's layers or with the full name.
8. Failures are named. A wrong call is a script error at the caller's line (language 9, abi 6), its text naming the
   module, the function and what was expected ("kit.hud.meter: max is 0; a meter needs max of 1 or more"). Data in
   `kit/<module>/` is checked when the mod is checked or packed (5.3), with the validator's codes. Nothing a module
   does fails silently; a module that cannot do what was asked returns `none` or `false` where its interface says so,
   else the call is an error.
9. Game thread, named moments. Module functions are called on the game thread; a call from elsewhere is a script
   error. `on_touch_player` from the actor's `attackSensor`, `on_spin` at the player's spin, `on_choose` at the
   frame's start after the screen closed.
10. No cost with no mod. LumaKit loads only when an enabled mod needs it. A module adds its hooks when a caller first
    uses it and removes them when its last caller's registrations end (`unhook`). A mod using one module pays for that
    module's hooks only.
11. Data or code, one form each. Most things a module defines can be written as data in `kit/<module>/*.json` (no
    code: a mod of only content and data gets its form, item, galaxy or dialogue) or by `define` in code. Both produce
    the same thing; code can find a data-defined one by its id (`find`) to add callbacks. A data file is read at
    LumaKit's start for each enabled mod that needs LumaKit, through `svc.mods`, validated against the module's schema
    (5.3) before the mod loads.
12. Versions. LumaKit 1.x only adds within 1: new functions, new struct fields with defaults, new enum cases marked
    open (a `match` over an open enum needs `_`). A module's data format has its own version, given in `luma.json`
    `"kit": { "lumakit.player.form": 1 }` (luma-format); a schema version the installed LumaKit does not know refuses
    the mod, naming the module, the version wanted and the versions known.
13. Handles. A value a module returns for something that lasts (`hud.Timer`, `gravity.Override`, `camera.Control`) is
    a small copyable handle. `end()`/`remove()` on it may be called any number of times; any other call on an ended
    handle is a script error naming it; `alive()` says whether it still holds. Everything a mod's handles hold ends
    with the mod (rule 4).

### 1. Shared types

Types used by several modules live in the module that owns the idea and are named from there:
`actor.Attack` (the player's attacks), `item.Gives`, `form.FormId`, `hud.Corner`, `draw.Texture` and
`draw.ViewCamera` (svc.draw), `input.Action`, `input.Pad` (svc.input). A module never redefines another's type.

### 2. The modules

Each: what it gives; its interface (signatures only); its `kit/` data; what it is built
on; its conformance mods (5.4, harness tests under `lumakit/tests/<module>/`, each proving what is listed); the corpus
rows that use it; the builder (B) and auditor (A).

### 2.1 `player.form`

Gives: a power-up: its look, aura, timer, music, what the player's actions do while it lasts, its own physics and
scale, and the game's own forms by name to give from code.

```lumascript skip a listing from the part's contract, not a module that builds alone
pub struct Form {
    id: text                                  // unique in the mod; "<mod id>/<id>" to the game
    look: model? = none                       // C.skin bound to the player's skeleton; none: the player's own
    look_luigi: model? = none                 // when the player is Luigi; none: look
    aura: effect? = none                      // kept on the player while the form lasts
    timer_frames: u32? = none                 // none: lasts until damage or end()
    ends_on_damage: bool = true
    music: sound? = none                      // over the stage's music while it lasts, as the game's forms do
    meter: bool = false                       // the time left shown as the game's form meter (MarioSubMeter)
    scale: f32 = 1.0                          // model, collision, sensors and camera distance together (mini, mega)
    opacity: f32 = 1.0                        // see-through forms (draw.inst)
    hurts_on_touch: bool = false              // touching an enemy attacks it (mega)
    physics: player.state.Tuning? = none      // the player's constants while it lasts
    base: GameForm = GameForm.Normal          // keep a game form's moves (Bee's flight, Boo's float) under this look
    item: item.ItemId? = none                 // the item that gives it (set by item.Gives.Form)
    on_spin: fn(MarioActor)? = none
    on_action: input.Action? = none           // with on_act: a bindable action, listed in the controls menu
    on_act: fn(MarioActor)? = none
    on_jump: fn(MarioActor)? = none
    on_start: fn(MarioActor)? = none
    on_end: fn(MarioActor, Ended)? = none
}
pub enum GameForm { Normal, Bee, Boo, Spring, Fire, Ice, Flying, Rainbow }   // the game's own (setPlayerMode)
pub enum Ended { Timer, Damage, Replaced, Ended, ModStopped }
pub struct FormId { fn give(self); fn is_current(self) -> bool; fn id(self) -> text }
pub fn define(f: Form) -> FormId
pub fn find(id: text) -> FormId?              // a form of this mod defined in kit/player.form
pub fn game(f: GameForm) -> FormId            // the game's own forms, given like a mod's (idea 14)
pub fn current() -> FormId?
pub fn frames_left() -> u32?
pub fn end()                                  // back to Normal, Ended.Ended
```

Data `kit/player.form/<id>.json`: every field above but the `fn` ones and `physics` by constant name (the constants
`player.state.Tuning` names). Built on: `K.hook` (`MarioActor::setPlayerMode`, `resetPlayerModeOnDamage`,
`updatePlayerMode`), `K.field` (`mPlayerModeTimer`), `K.data` (the player's constants), `C.skin`, `C.fx`, `C.sound`,
`draw.inst`† (opacity), `audio`, `input`, `ui` (the meter). Conformance: a form given by an item and by code, its look
on Mario and on Luigi, its timer and meter ending it, damage ending it, `on_spin` and `on_act` called once per press,
`scale` 0.5 and 2 (collision and camera follow), a game form given from code, two mods' forms (rule 7: the newer
holds, the older returns when it ends), the mod turned off while its form is on (Normal again, no crash). Corpus: 1,
2, 7, 8, 14, 19, 23. B: G. A: F.

### 2.2 `player.state`

Gives: a new move (its trigger, animation, physics each frame, how it ends, whether it attacks) and the player's
constants changed by name.

```lumascript skip a listing from the part's contract, not a module that builds alone
pub struct State {
    id: text
    starts: Start
    from: set<From> = [From.Air]
    once_per_air: bool = false                // double jump
    action: text                              // the player's animation (C.anim, bound to the skeleton by LumaLink)
    motion: fn(MarioActor, var Motion)? = none   // each frame while in the state
    ends: list<End> = [End.Ground]
    attack: actor.Attack? = none              // hurts what it touches as this attack while it lasts
    effect_start: effect? = none
    sound_start: sound? = none
    on_start: fn(MarioActor)? = none
    on_end: fn(MarioActor)? = none
}
pub enum Start { Action(input.Action), Code }
pub enum From { Ground, Air, Water, Any }
pub enum End { Ground, Wall, Water, Frames(u32), ActionEnd, Released(input.Action), Code }
pub struct Motion { velocity: vec3, gravity_scale: f32 = 1.0, friction: f32 = 1.0, turn: f32 = 1.0 }
pub struct Tuning { values: map<text, f32> } // the player's constants by name (MarioConst), multiplied
pub struct StateId { fn enter(self) -> bool; fn is_current(self) -> bool }
pub fn define(s: State) -> StateId
pub fn find(id: text) -> StateId?
pub fn current() -> StateId?
pub fn tune(t: Tuning) -> Tuned                // a handle; end() puts the constants back (idea 28)
pub struct Tuned { fn end(self); fn alive(self) -> bool }
```

Data `kit/player.state/<id>.json`: the fields but the `fn` ones; `motion` as fixed numbers (`{"velocity_up": 30,
"gravity_scale": 0.3}`) for the simple moves. Built on: `K.hook` (the player's action and physics steps, rows from
the self-description), `K.field`, `K.data` (`MarioConst`, `MarioConstTable`), `C.anim`, `input`. Conformance: a double
jump (once per air), a glide ending on ground, a dive started by an action in the air, a ground pound shock wave
(`attack`), `tune` doubling run speed and `end` restoring it exactly, two mods' states on different actions, the mod
off mid-move. Corpus: 2, 6, 20, 22, 28. B: G. A: F.

### 2.3 `player.character`

Gives: a playable character (name, icon, model, animations, voice, constants), switching mid-level, a second player.

```lumascript skip a listing from the part's contract, not a module that builds alone
pub struct Character {
    id: text
    name: text                                // C.text
    icon: draw.Texture? = none
    look: model                               // its own skeleton or Mario's (LumaLink binds and scales it)
    actions: map<text, text> = {}             // the game's action name -> this character's clip
    voice: map<text, sound> = {}              // the game's voice name -> this character's sound
    tuning: player.state.Tuning? = none
    as_luigi: bool = false                    // the game's Luigi checks answer true (MR::isPlayerLuigi)
}
pub struct CharacterId { fn play(self); fn is_current(self) -> bool }
pub fn define(c: Character) -> CharacterId
pub fn find(id: text) -> CharacterId?
pub fn current() -> CharacterId?              // none: the game's own Mario or Luigi
pub fn mario() -> CharacterId                 // the game's own, to switch back (idea 4)
pub struct Second { look: model, pad: input.Pad, character: CharacterId? = none }
pub struct SecondPlayer { fn view(self) -> MarioActor; fn remove(self); fn alive(self) -> bool }
pub fn second_player(s: Second) -> SecondPlayer    // a second instance of the game's player (idea 89, 140)
```

Data `kit/player.character/<id>.json`: all fields. Built on: `K.derive` and `K.hook` (the player object and the
globals it reads, idea 195's method), `K.data`, `C.skin`, `C.anim`, `C.sound`, `C.text`, `assets` (a model made at
run time, idea 151), `input`. Conformance: a character from data with its own clips and voice, switching mid-level and
back, Luigi's checks with `as_luigi`, a downloaded model (assets, checked by the validator) as a character, a second
player on pad 2 moving on a planet beside the first, the mod off while playing it (the game's Mario again). Corpus: 3,
4, 24, 151 (89, 140 through `second_player`). B: G. A: F.

### 2.4 `item`

Gives: things picked up, placed in stages by name or made by code, in containers, carried in an inventory.

```lumascript skip a listing from the part's contract, not a module that builds alone
pub struct Item {
    id: text                                  // placeable as "<id>" in its mod's stages, "<mod id>/<id>" elsewhere
    look: model
    shine: effect? = none
    sound_get: sound? = none
    gives: Gives
    container: Container = Container.None
    floats: bool = true                       // hovers and turns, as the game's power-ups
    respawns: bool = false
}
pub enum Gives { Form(form.FormId), Coins(u32), StarBits(u32), Lives(u32), Health(u32), Carry(u32),
                 Call(fn(MarioActor)) }        // Carry: n into the inventory under the item's id
pub enum Container { None, Bubble, Crystal }
pub struct ItemId { fn spawn(self, at: vec3) -> LiveActor; fn give(self); fn carried(self) -> u32;
                    fn use_one(self) -> bool }
pub fn define(i: Item) -> ItemId
pub fn find(id: text) -> ItemId?
pub fn carried_all() -> list<(ItemId, u32)>  // the inventory, in the order first carried (idea 56's wheel)
```

`form.Form` refers to `item.ItemId` and `item.Gives` to `form.FormId`: the two are one library and the cycle is
between types of one build. Data `kit/item/<id>.json`. Built on: `K.derive` (`LiveActor`), `K.call` (`MR::incCoin`,
`incStarPiece`, `incPlayerLife`, `MR::addHitSensor`), `C.model`, `C.fx`, `C.sound`, `save` (the inventory per file).
Conformance: an item placed by name and one spawned, each kind of `Gives`, both containers, an inventory kept across
a save and reload, an item of a mod that is off (not made, logged). Corpus: 1, 38, 39, 51, 56. B: F. A: G.

### 2.5 `projectile`

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

### 2.6 `actor`

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

### 2.7 `area`

Gives: new kinds of area, placed like the game's, with what happens inside.

```lumascript skip a listing from the part's contract, not a module that builds alone
pub struct Kind {
    name: text
    shape_from_placement: bool = true         // the placement's own shape (box, cylinder, sphere), as AreaObj
    player_speed: f32 = 1.0                   // a zone that makes the player slow (idea 75)
    player_force: vec3? = none                // wind, along the area's own axes (idea 68)
    camera: draw.ViewCamera? = none           // the main camera while the player is inside (idea 76)
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
29, 50, 68, 75, 76. B: F. A: G.

### 2.8 `gravity`

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

### 2.9 `camera`

Gives: the main camera taken for a time, camera rigs for views (follow, eye, side, above), mirror, field of view.
A rig is a `draw.ViewCamera` (svc.draw), so the same rig drives the main camera or a `draw.view`.

```lumascript skip a listing from the part's contract, not a module that builds alone
pub struct Control { fn end(self); fn alive(self) -> bool }
pub fn take(rig: draw.ViewCamera) -> Control      // the main camera follows the rig until end()
pub fn free(speed: f32 = 40.0) -> Control         // a free camera moved with the sticks (ideas 85, 88, 137, 161)
pub fn follow(target: LiveActor, distance: f32, height: f32 = 300.0) -> draw.ViewCamera
pub fn eye(of: LiveActor, joint: text = "Head", near: f32 = 5.0) -> draw.ViewCamera   // first person (idea 18)
pub fn side(normal: vec3, distance: f32) -> draw.ViewCamera                          // 2D sections (idea 76)
pub fn stereo(rig: draw.ViewCamera, eye_distance: f32) -> (draw.ViewCamera, draw.ViewCamera)   // VR (idea 92)
pub fn mirror(on: bool)                            // the image and the stick's x flipped (ideas 66, 191)
pub fn fov(degrees: f32?)                          // none: the game's own
pub fn shake(strength: f32, frames: u32)
pub fn now() -> draw.Eye                           // where the main camera is
```

Built on: `K.hook` (`CameraDirector`), `K.call` (`MR::startEventCamera`, `MR::hidePlayer`, `showPlayer`), `draw`
(`draw.view`†, `draw.post`† for mirror), `input`, `time` (`time.ctl`† for photo mode's hold, used by the mod).
Request R-7: `draw.ViewCamera.Look(fn() -> draw.Eye)`, a camera computed each frame. Conformance: take and end with
the game's camera back at its own state, free camera, first person (model hidden, near plane), side camera, mirror
(image and controls), a rig in a `draw.view` region, two mods taking the camera (rule 7). Corpus: 5, 18, 66, 76, 85,
88, 89, 90, 92, 137, 161, 176, 191. B: F. A: G.
As built (FB, 11-12): data rigs (`view.Camera { rig: view.Rig.Fixed|Follow|Joint|World, ... }`)
replace R-7's `Look(fn)`; a computed camera calls `aim(c, e: view.Eye)` each frame (draw.view `main_eye`, held while
the world is held). `end(c)` and `alive(c)` are module functions too.

### 2.10 `hud`

Gives: counters, timers, meters, text, pictures in corners; text in 3D; popups; subtitles; the game's own HUD parts
hidden.

```lumascript skip a listing from the part's contract, not a module that builds alone
pub enum Corner { TopLeft, Top, TopRight, Left, Right, BottomLeft, Bottom, BottomRight }
pub struct Timer { fn set(self, frames: u64); fn follow(self, f: fn() -> u64); fn remove(self) }
pub struct Text { fn set(self, s: text); fn remove(self) }
pub struct Meter { fn set(self, value: int); fn remove(self) }
pub struct Picture { fn set(self, tex: draw.Texture); fn remove(self) }
pub struct Counter { fn set(self, value: i64); fn remove(self) }
pub fn timer(at: Corner, frames: u64 = 0) -> Timer
pub fn text(at: Corner, s: text, frames: u32? = none) -> Text   // frames: removes itself after
pub fn meter(at: Corner, max: int, label: text? = none) -> Meter
pub fn picture(at: Corner, tex: draw.Texture, size: vec2) -> Picture
pub fn counter(at: Corner, value: i64, icon: draw.Texture? = none) -> Counter
pub fn above(of: LiveActor, s: text, height: f32 = 150.0) -> Text   // follows the actor (idea 54)
pub fn float_text(at: vec3, s: text, frames: u32 = 60) // rises and fades (idea 135)
pub fn popup(title: text, line: text, icon: draw.Texture? = none)  // the achievement popup (idea 127)
pub fn subtitle(line: text, speaker: text? = none, frames: u32 = 180)   // idea 172
pub enum GamePart { Lives, Coins, StarBits, Stars, Health, All }
pub fn hide_game(part: GamePart, hidden: bool)
```

Built on: `ui` (layouts), `draw` (2D at the HUD point, text in the game's font), `C.layout`, `C.font`, `C.text`,
`K.hook` (the game's counters, `CounterLayoutController`). Text drawn with the disc's message font; a
mod's text is checked against it by `nfnt_glyphs.py` and by the validator. Conformance: each element in
each corner, stacking, removal, `follow`, the game's lives hidden, a popup at the owner's 1.3x, a mod off with five
elements up (all gone). Corpus: 13, 23, 32, 53, 54, 55, 78, 93, 120, 121, 127, 135, 136, 144, 172. B: G. A: F.

### 2.11 `screen`

Gives: whole screens: a message, a choice, a list, a loading screen, a layout of the mod's as a screen.

```lumascript skip a listing from the part's contract, not a module that builds alone
pub fn message(line: text) -> pending                  // waitable; closes with A or B
pub fn choose(title: text, rows: list<text>) -> pending<int?>  // none: the player backed out
pub struct List { title: text, rows: list<text>, on_choose: fn(int)? = none, closes: bool = false }
pub struct ListScreen { fn set_rows(self, rows: list<text>); fn close(self); fn alive(self) -> bool }
pub fn list(l: List) -> ListScreen
pub fn layout(l: layout, pauses: bool = true) -> ui.Open   // a mod's layout as a screen (ideas 81, 131)
pub fn loading(tips: list<text>, picture: draw.Texture? = none)   // shown while stages load (idea 124)
```

Built on: `ui`, `draw` (the loading point: "one on every presented frame, loading included"), `input`, `time`
(the game held while a screen pauses), `C.layout`, `C.text`. Conformance: each kind opened, answered and backed out of
with pad, keyboard and touch; a list updated while open; a screen open when its mod stops (closed, the game resumed).
Corpus: 81, 124, 131, 138. B: G. A: F.

### 2.12 `menu`

Gives: entries in the game's pause menu. (A mod's settings are its `luma.json` options on the Mods screen, read with
`option` and `on option`: ideas 27 and 86 are options, not menu entries; one way, language rule 5.)

```lumascript skip a listing from the part's contract, not a module that builds alone
pub struct Entry { fn set_label(self, s: text); fn enable(self, on: bool); fn remove(self) }
pub fn add_pause_entry(label: text, on_choose: fn()) -> Entry
```

Built on: `K.hook` (`PauseMenu`), `ui`, `C.text`. Conformance: entries of two mods in list order, chosen, disabled,
removed; an entry whose mod stops. Corpus: 14, 27, 62, 63, 86, 112, 125, 133, 162. B: G. A: F.

### 2.13 `cutscene`

Gives: scripted scenes of shots and lines, waited on, skippable.

```lumascript skip a listing from the part's contract, not a module that builds alone
pub struct Scene { shots: list<Shot>, letterbox: bool = true, skippable: bool = true, holds_world: bool = true,
                   music: sound? = none }
pub enum Shot { Look(at: ref<LiveActor>, from_offset: vec3, frames: u32), Move(from: draw.Eye, to: draw.Eye, frames: u32),
                Line(speaker: ref<LiveActor>?, line: text), Action(who: ref<LiveActor>, action: text),
                Wait(frames: u32), Call(fn()) }
pub fn play(s: Scene) -> pending<bool>     // true: it ran to its end; false: skipped
pub fn find(id: text) -> Scene?            // a scene of this mod from kit/cutscene
```

Data `kit/cutscene/<id>.json` (actors by placement name). Built on: `kit.camera` (`take`), `K.call`
(`MR::startEventCamera`, `CinemaFrame`), `kit.talk` (lines), `time` (hold), `audio`, `C.text`. Conformance: a scene of
each shot kind, skipped and run through, a scene whose actor dies in it, letterbox. Corpus: 32, 57. B: G. A: F.

### 2.14 `talk`

Gives: lines and choices from an actor in the game's balloon, dialogue trees, voice for lines.

```lumascript skip a listing from the part's contract, not a module that builds alone
pub fn say(who: ref<LiveActor>, line: text) -> pending
pub fn ask(who: ref<LiveActor>, line: text, choices: list<text>) -> pending<int>
pub fn run(who: ref<LiveActor>, tree: text) -> pending<text?>  // a tree of kit/talk; its end node's name
pub fn voice(line: text, s: sound)          // the sound played when the line is shown (idea 114)
```

Data `kit/talk/<tree>.json`: nodes of a line, choices, each choice's next node and a per-file flag it sets (through
`kit.progress`). Built on: `K.call` (`MR::createTalkCtrl`, `tryTalkNearPlayer`, `TalkMessageCtrl`), `C.text`,
`C.sound`, `kit.progress`. Conformance: say, ask with two and four choices, a tree with a loop and a flag, a voiced
line, the speaker killed mid-line. Corpus: 41, 114, 130. B: G. A: F.

### 2.15 `shop`

Gives: buying things with coins or star bits.

```lumascript skip a listing from the part's contract, not a module that builds alone
pub enum Currency { Coins, StarBits }
pub struct Ware { name: text, price: u32, gives: item.Gives, stock: u32? = none }
pub struct Shop { title: text, wares: list<Ware>, currency: Currency, keeper: ref<LiveActor>? = none }
pub fn open(s: Shop) -> pending<Ware?>      // none: left without buying
```

Data `kit/shop/<id>.json`. Built on: `kit.screen`, `kit.item`, `kit.talk`, `K.call` (`MR::incCoin`, `incStarPiece`
with negative counts, read from the game's counters), `save` (stock per file). Conformance: buy, too poor, out of
stock, stock kept across a reload. Corpus: 42. B: G. A: F.

### 2.16 `stage`

Gives: new galaxies with stars and comets in the domes, a new dome, music per galaxy, warping, stars appearing, the
hub replaced, stages built in code.

```lumascript skip a listing from the part's contract, not a module that builds alone
pub enum Dome { Terrace, Fountain, Kitchen, Bedroom, Engine, Garden, Mod(name: text) }
pub enum Unlock { Always, Stars(u32), Star(galaxy: text, scenario: u32), Code(fn() -> bool) }
pub enum Comet { Speedy, Daredevil, Cosmic, FastFoe, Purple }
pub struct Star { scenario: u32, title: text, comet: Comet? = none, hidden: bool = false }
pub struct Galaxy { name: text, title: text, dome: Dome, unlock: Unlock = Unlock.Always, stars: list<Star>,
                    music: sound? = none, icon: draw.Texture? = none }
pub struct GalaxyId { fn go(self, scenario: u32); fn name(self) -> text }
pub struct DomeDef { name: text, title: text, unlock: Unlock = Unlock.Always }
pub fn define_galaxy(g: Galaxy) -> GalaxyId
pub fn define_dome(d: DomeDef)
pub fn find(name: text) -> GalaxyId?
pub fn go_to(galaxy: text, scenario: u32)            // MR::requestChangeStageInGameMoving (idea 63)
pub fn appear_star(at: vec3, scenario: u32)
pub fn music(galaxy: text, scenario: u32?, s: sound)  // idea 110
pub fn hub(stage: text)                               // the hub replaced (idea 79)
pub fn build(name: text, make: fn(var Builder))      // a stage made in code when it loads (ideas 70, 71)
pub struct Builder { fn place(var self, kind: text, at: vec3, rotation: vec3 = vec3(0, 0, 0), args: list<int> = []);
                     fn planet(var self, look: model, collision: model, at: vec3); fn gravity(var self, f: gravity.Field) }
```

Data `kit/stage/<name>.json`: galaxies and domes. Built on: `C.stage`, `C.text`, `C.sound`, `assets.provide`†
(`build`: the stage made at run time goes through the cooker and validator), `K.hook` (`AstroDomeGalaxySelector`,
`ScenarioSelectLayout`, `ScenarioSelectScene`, `MR::startStageBGM`), `K.call` (`MR::requestChangeStageInGameMoving`, `MR::requestAppearPowerStar`,
`PowerStarEventKeeper`), `kit.progress`. Conformance: a galaxy listed in its dome after 20 stars, its stars got and
counted, a comet star, warp, music, a stage built in code with a planet and two objects, a downloaded stage (through
`svc.mods` offer for one start, P-18) as a galaxy. Corpus: 57, 61, 70, 71, 72, 79, 110. B: F. A: G.

### 2.17 `progress`

Gives: stars beyond 120 and new star kinds counted, comets added to any galaxy, per-file flags and counters,
what a star gives, new-file rules.

```lumascript skip a listing from the part's contract, not a module that builds alone
pub fn stars() -> u32                                  // the game's and every mod's, on this file
pub fn has_star(galaxy: text, scenario: u32) -> bool
pub fn give_star(galaxy: text, scenario: u32)
pub fn on_star(f: fn(text, u32) -> StarGet)       // (galaxy, scenario)   // what getting a star does (ideas 64, 65)
pub enum StarGet { Star, Nothing, Instead(item.Gives) }
pub fn add_comet(galaxy: text, scenario: u32, comet: stage.Comet)   // ideas 77, 78, 82
pub struct Flag { fn get(self) -> bool; fn set(self, on: bool) }
pub struct Number { fn get(self) -> i64; fn set(self, v: i64); fn add(self, d: i64) -> i64 }
pub fn flag(id: text) -> Flag                          // per file, following its copy and erase (idea 197)
pub fn number(id: text) -> Number                      // experience, achievements (ideas 55, 127)
pub fn on_new_file(f: fn())                            // New Game+ (idea 83)
```

Built on: `save` (per-file values following copy and erase), `K.hook` (star counting, the comet table,
`GameDataFunction`), `K.call`, `kit.stage` (comet kinds). Conformance: stars counted past 120, `on_star` giving an
item instead, a comet added, flags and numbers following a file copy and erase, a new file rule. Corpus: 55, 57, 60,
61, 62, 64, 65, 77, 78, 82, 83, 127, 197. B: F. A: G.

### 2.18 `net.kit`

Gives: reliable ordered messages and rooms over the `net` service's datagrams; a clock shared between peers; a small
HTTP and WebSocket server for a page (a phone as a controller).

```lumascript skip a listing from the part's contract, not a module that builds alone
pub struct Link { fn send(self, to: u8, data: bytes); fn send_all(self, data: bytes); fn peers(self) -> list<u8>;
                  fn close(self) }                // events: net_kit.received(link, from, data), net_kit.peer(link, id, joined)
pub fn reliable(over: net.Datagrams) -> Link   // needs: net
pub fn clock(l: Link) -> pending<f64>          // the host's time minus ours, in seconds (idea 141)
pub struct Page { fn url(self) -> text; fn close(self) }   // events: net_kit.page_message(page, data)
pub fn serve_page(port: u16, files: list<data>) -> Page   // needs: net (listen)
```

Built on: `net` (datagrams, STUN, listening), `time`. Conformance: messages in order with 20% of
datagrams dropped, a peer leaving, the clock within one frame on a local link, a page served and its messages
received. Corpus: 65, 81, 116, 138, 140, 141, 142, 147. B: F. A: G.

As built (11-12): `reliable(port: u16, local_network: bool) -> Link` and `connect(l, host,
port) -> bool` (no net.Datagrams type); module functions `send/send_all -> bool`, `peers`, `id`, `port`, `close`,
`alive` (struct functions do not cross mods yet); callbacks `on_received(l, fn(Link, u8, bytes))`, `on_peer(l, fn(Link,
u8, bool))`, `on_page_message(p, fn(Page, bytes))` in place of events; `pub task clock(l) -> f64`; `PageFile { path,
body }` for `files`; `page_send(p, data, binary)`, `page_port`, `page_alive`, `close_page`; `url` from net.local 1.4
`address`. A star of up to 32 over UDP, the page server's WebSocket on tcp_listen.
Source k/mods/lumakit/source/net/kit.lum; conformance h/mods/kit-net.kit, h/tests/kit-net.kit.

### 2.19 `replay`

Gives: recording and playing back poses, puppets posed from data, input files played frame-exactly, rewind of the
player and chosen actors.

```lumascript skip a listing from the part's contract, not a module that builds alone
pub enum What { Player, Actor(ref<LiveActor>), Inputs }
pub struct Pose {
    at: vec3
    rotation: quat
    action: text
    frame: f32
    form: text?
    fn encode(self) -> bytes
    fn decode(b: bytes) -> Pose?                       // called as replay.Pose.decode(b)
}
pub struct Track {
    frames: u32
    fn encode(self) -> bytes
    fn decode(b: bytes) -> Track?                      // replay.Track.decode(b)
    fn pose(self, frame: u32) -> Pose?
}
pub struct Recorder { fn stop(self) -> Track; fn discard(self) }
pub struct Puppet { fn pose(self, t: Track, frame: u32); fn set_pose(self, p: Pose); fn view(self) -> LiveActor;
                    fn remove(self); fn alive(self) -> bool }   // view() after remove(): a script error (rule 13)
pub fn record(w: What) -> Recorder
pub fn current_pose(w: What) -> Pose
pub fn puppet(look: model) -> Puppet                  // lit as the player (up to 15, P-19)
pub fn play_inputs(t: Track) -> pending               // What.Inputs, each frame injected (idea 157)
pub fn seed(n: u32)                                   // the game's random numbers from n (MR::getRandom)
pub struct Rewind { fn back(self, frames: u32) -> u32; fn end(self) }
pub fn rewind(what: list<What>, seconds: f32) -> Rewind   // idea 155; the whole world is plan 17's limit
```

Built on: `K.field`, `K.hook` (`MR::getRandom`, the player's pose), `K.call`, `input` (inject), `time.ctl`† (step),
`draw.inst`† (puppet opacity), `C.skin`, `C.anim`. Conformance: a recorded run played back pose for pose (difference
0 on the same build), a track decoded on another platform, 15 puppets, inputs replayed giving the same end position,
rewind of the player 5 s. Corpus: 136, 137, 140, 141, 155, 157. B: F. A: G.

### 2.20 `ui.kit`

Gives: widgets for a mod's own panels: buttons, sliders, toggles, colour pickers, lists, text fields, a wheel, a model
view; pad, mouse and touch focus.

```lumascript skip a listing from the part's contract, not a module that builds alone
pub enum Widget { Button(label: text, on_press: fn()), Slider(label: text, min: f32, max: f32, value: f32, on_change: fn(f32)),
                  Toggle(label: text, on: bool, on_change: fn(bool)), Colour(label: text, value: color, on_change: fn(color)),
                  List(rows: list<text>, on_choose: fn(int)), TextField(label: text, max_chars: u32, on_done: fn(text)),
                  Wheel(entries: list<(draw.Texture, text)>, on_choose: fn(int)), ModelView(look: model, size: vec2),
                  Picture(tex: draw.Texture, size: vec2), Label(s: text) }
pub struct Panel { title: text? = none, widgets: list<Widget>, at: hud.Corner? = none, pauses: bool = false }
pub struct Open { fn set(self, index: int, w: Widget); fn close(self); fn alive(self) -> bool }
pub fn open(p: Panel) -> Open
pub fn touch_layout(buttons: list<(input.Action, vec2, f32)>)   // on-screen buttons where the platform has touch (idea 170)
```

Built on: `draw`, `ui`, `input` (text entry, touch, inject), `draw.view`† (ModelView). Conformance: each widget by pad,
mouse and touch; a text field through the platform keyboard; a wheel choosing an item; a panel whose mod stops.
Corpus: 26, 56, 103, 126, 132, 149, 170, 171. B: F. A: G.

### 2.21 `ride` (DECISIONS 3)

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

### 2.22 `pointer` (DECISIONS 3)

Gives: the star pointer: where it points on screen and in the world, what B throws and what it costs, grabbing, the
source that aims it (IR, gyro, mouse, stick), a co-star pointer.

```lumascript skip a listing from the part's contract, not a module that builds alone
pub fn screen() -> vec2?                          // MR::getStarPointerScreenPosition; none: off screen
pub fn world() -> (vec3, vec3)?                   // the map point and normal under the pointer
pub enum Cost { StarBits(u32), Coins(u32), Free }
pub struct Throw { shot: fn(vec3, vec3) -> projectile.Shot, cost: Cost = Cost.StarBits(1) }   // (from, toward)
pub struct ThrowSet { fn end(self) }
pub fn set_throw(t: Throw) -> ThrowSet            // what B throws (idea 40); end(): star bits again
pub fn on_grab(f: fn(LiveActor) -> bool) -> ThrowSet   // A on a pointed actor
pub enum Source { Infrared, Gyro(sensitivity: f32), Mouse, Stick }
pub fn source(pad: input.Pad, s: Source)          // idea 166
```

Built on: `K.hook` (`StarPieceDirector`'s shot, `StarPointerController`), `K.call` (`MR::getStarPointerScreenPosition`,
`MR::getFirstPolyOnLineToMap`), `input` (`input.dev`† motion), `kit.projectile`. Conformance: B throwing coins at a
cost, out of coins (nothing thrown, the game's sound), a grab, gyro aim on a pad with motion and its refusal on one
without (named), a world point under the pointer on a planet. Corpus: 40, 71, 162, 166. B: G (proposed). A: F.

### 3. Coverage (the check of 5.1's condition)

1. a module named in a corpus row's LumaKit cell without a section here, or a section's "Corpus" line missing a row
   that names it, or naming a row that does not;
2. a name `<module>.<Name>` a program uses (A and B programs) that the module's interface does not declare;
3. an id in a "Built on" line that is not a part of corpus section 1 (K.*, a service, C.*, a † part) or another
   LumaKit module;

| Id | To | What |
|---|---|---|
| R-1 | F, kernel 1.1 (loader) | `LUMA_IMPORT_MOD_FUNCTION`: a mod's import of another mod's exported function (mod id, version range, symbol), bound at load from the needed mod's export list; a missing or mismatched one stops the load naming both |
| R-2 | G, lumascript-abi | the export list of a library mod and the interface file (`.lumi`) the compiler reads for `use <mod>.<module>` (rule 2) |
| R-3 | F, kernel 1.1 | a library mod is told when a mod that needs it stops or reloads, before that mod's library is unloaded (rule 4) |
| R-4 | F, services 2.x | a service call made by a library for a mod that needs it is checked against, and logged as, that mod's permissions (rule 6); the kernel checks the needs graph |
| R-5 | G, validator | L700 range: `kit/<module>/` data against the module's schema (5.3), naming the file, the line and the schema's expectation |
| R-6 | F, `mods` 2.x | a library mod reads the `kit/<its module>/` files of each enabled mod that needs it |
| R-7 | F, `draw` 2.x | `draw.ViewCamera.Look(fn() -> draw.Eye)` and `draw.Eye { eye: vec3, at: vec3, up: vec3, fov: f32 }` |
| R-8 | G, validator | a mod's `C.text` rows checked against the message font of each disc its regions name (as a rule) |

Three interfaces that read the player said nothing for a moment with no player (the title, a stage's loading, after a
death):
- `replay.current_pose(w: What) -> Pose?` (was `Pose`): `none` when `What.Player` has no player or `What.Actor`'s
  handle is empty.
- `actor.sees_player(...)` gives `false` with no player.
- `gravity.override_player(...)` holds with no player and acts on the next one; `end()` works either way.
No other function of the 22 modules takes or gives the player without a handle that can be tested. The coverage check,
run again: 0 findings on the board, 3 of 3 on its own broken program (g/tests/lumakit-coverage.out).

---

Each item replaces the text it names.

K-1, rule 12. "(a `match` over an open enum needs `_`)" becomes "(a `match` over an open enum needs an
`else =>` arm, language D5.2)".

K-2, rule 2 and R-1. The import row is kind 6, `LUMA_IMPORT_MOD_FUNCTION` (5 is `LUMA_IMPORT_SERVICE`). Added to rule
2:
- Needs order. A needed library mod is loaded and started before every mod that needs it, and stopped after them. This
  order comes from `needs` alone, and the mod list's order does not change it.
- List order still decides hooks, files and who wins (rule 7, L305).
- A needs cycle refuses each mod in it (L800's family).
R-1's row reads "kind 6" and "loaded and started before, stopped after, the mods that need it".

K-3, rule 7. After "the newest request holds it": requests made in one frame take effect in the mod list's
order, the lowest mod's first. So of two requests in one frame, the higher mod's is the newest and holds.

K-4 and K-8, 2.4 `item` and 2.6 `actor`.
- `Gives` effects, in 2.4's "Built on":
  - `Coins(n)`: `MR::incCoin(n)`, the game's own coin, with its heal, 1-up steps and event;
  - `StarBits(n)`: `MR::addStarPiece(n)` (EventUtil.hpp:19). It adds to the stocked count in the hub and the galaxy's
    count elsewhere (EventUtil.cpp:96).
  - `Lives(n)`: `MR::incPlayerLeft()` n times (EventUtil.hpp:10);
  - `Health(n)`: `MR::incPlayerLife(n)`, health with the game's heal sound (PlayerUtil.hpp:102).
- 2.6: `drops: Drops = Drops.None` and `enum Drops` are replaced by `drops: item.Gives? = none` (one vocabulary):
  - a defeated actor gives it as an item would;
  - `Form` and `Call` are refused at `define` (S0914: "kit.actor.define: drops is Form; it must be Coins, StarBits,
    Lives, Health or Carry");
  - The data form takes the same names.

K-5, 2.1 `GameForm`. The game's own names and numbers, as the code and its data use them
(MarioActor.hpp:56):
`pub enum GameForm { Normal = 0, Invincible = 1, Fire = 2, Ice = 3, Bee = 4, Hopper = 5, Teresa = 6, Foo = 7,
Tornado = 9 }`.
- The comments give the names players know: Invincible is the rainbow star, Hopper the spring, Teresa Boo, Foo the
  red star's flying.
- Tornado (9) is kept. The game makes its model at every player start (MarioActorDraw.cpp:109 and 300:
  MarioTornado.arc and LuigiTornado.arc are on the disc) and has `MR::isPlayerElementModeTornado`. No stage or item
  of the game gives it.
  - `game(GameForm.Tornado)` gives it like the others.
  - Its conformance item ("Tornado given from code: its look on Mario and Luigi, its moves, ended by damage")
    proves what it does. Until that run it is written, not proven, and the docs say so.
  - A form whose model is missing (`canTakePowerUp` false: a costume mod's player without it) is refused, as
    S0914 "kit.player.form.give: form is Tornado; it must be a form the player's models have".

K-6 (must), 2.15 `shop` and 2.22 `Cost.Coins`: spending coins.
- `MR::incCoin(-price)` is wrong. SceneUtil.cpp:360 passes the count to `incPlayerLife(u32)`, so a negative price
  becomes a heal of about 4 billion, clamped at full health, with the heal sound. It also fires the old coin event
  with a negative count.
- One detail in K-6 does not hold: `NebulaAchievements::countStat` adds only positive counts
  (NebulaAchievements.cpp:768). A debit counts no coins in either path.
- The kit's debit path: `ScenePlayingResult::incCoin(-price)` on `MR::getSceneObj` for `SceneObj_ScenePlayingResult`
  (through `K.call`: `MR::getSceneObjHolder`, `SceneObjHolder::getObj`). It is clamped at `cCoinNumMin`, gives no
  heal and no 1-up (a 1-up needs the count to rise past a step, ScenePlayingResult.cpp:40), and counts no stat.
  The kit checks the price against `MR::getCoinNum()` first, so "too poor" never reaches the clamp.
- Star bits are spent as the game spends them: `MR::addStarPiece(-n)` (StarPiece.cpp:811), after checking the count
  (the stocked count in the hub).
- Coins in the hub. Coins exist only in a galaxy, so `MR::isStageAstroLocation()` decides:
  - `shop.open` with `Currency.Coins` there returns `none` at once and raises S0914 ("kit.shop.open: currency is
    Coins; it must be StarBits where MR::isStageAstroLocation()"). That is the mod's mistake, named.
  - `Cost.Coins` there throws nothing, with the game's out-of-ammo sound, as with too few coins.
- 2.15 "Built on": `K.call` (`ScenePlayingResult::incCoin`, `MR::getCoinNum`, `MR::addStarPiece`,
  `MR::getStarPieceNum`, `MR::getStockedStarPieceNum`, `MR::isStageAstroLocation`).
- Conformance gains: buying with coins does not heal (health unchanged, no heal sound), and counts no coins
  collected (the stat unchanged); a coin shop in the hub is refused, named; star bits are bought in the hub from
  the stock.

K-7, 2.19 `replay.seed`. `seed(n)` sets `GameSystemObjHolder::mRandom` (a FastRandom, made with 0,
GameSystemObjHolder.cpp:24), which `MR::getRandom` reads (MathUtil.cpp:51), through `K.field`.
- It takes effect at the next stage start, for recording and playback alike, so a recording and its playback start
  from one state.
- Particles (`Game/Particle`, `RandomEffectObj`) and the sound sequencer (`SeqTrack`) have their own random numbers.
  They change looks and sound, never play, and are not seeded.
- Two mods seeding follow rule 7: the newest holds until it ends.
- "Built on" drops `K.hook (MR::getRandom)` for `K.field (GameSystemObjHolder::mRandom)`.

K-9, 2.9 `camera`. `MR::startEventCamera` takes an `ActorCameraInfo` from a placed camera (CameraUtil.hpp:133),
so it cannot drive a rig made in code.
- `take`, `free` and the cutscene's shots go through the `CameraDirector` hook alone.
- 2.9's "Built on" drops `MR::startEventCamera`. A stage's own placed cameras are 2.13 `cutscene`'s, by their
  camera name, and that one call is listed there.

K-10, rule 3 and R-3. Added to rule 3: when a mod that needs LumaKit stops or reloads, LumaKit drops that mod's
closures (`on_frame`, `on_touch_player`, `on_spin`, the hud's follow, every `fn` it gave) at the unbind, before
the grace. No closure of a stopping mod is called after its unbind, so no library mod calls into code
being unloaded.
- R-3 becomes: a library mod is told at a needing mod's unbind, before its `stop`. F puts the notification in 1.1
  part 4 (`dependent_gone`).

Every LumaKit wrong call is S0914, and an ended handle is S0913 (rule 13), so no module has text of its own.
