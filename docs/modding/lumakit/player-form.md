# LumaKit: player.form

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
pub fn game(f: GameForm) -> FormId            // the game's own forms, given like a mod's
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
holds, the older returns when it ends), the mod turned off while its form is on (Normal again, no crash).
