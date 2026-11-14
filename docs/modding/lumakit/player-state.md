# LumaKit: player.state

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
pub fn tune(t: Tuning) -> Tuned                // a handle; end() puts the constants back
pub struct Tuned { fn end(self); fn alive(self) -> bool }
```

Data `kit/player.state/<id>.json`: the fields but the `fn` ones; `motion` as fixed numbers (`{"velocity_up": 30,
"gravity_scale": 0.3}`) for the simple moves. Built on: `K.hook` (the player's action and physics steps, rows from
the self-description), `K.field`, `K.data` (`MarioConst`, `MarioConstTable`), `C.anim`, `input`. Conformance: a double
jump (once per air), a glide ending on ground, a dive started by an action in the air, a ground pound shock wave
(`attack`), `tune` doubling run speed and `end` restoring it exactly, two mods' states on different actions, the mod
off mid-move.
