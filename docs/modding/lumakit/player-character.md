# LumaKit: player.character

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
pub fn mario() -> CharacterId                 // the game's own, to switch back
pub struct Second { look: model, pad: input.Pad, character: CharacterId? = none }
pub struct SecondPlayer { fn view(self) -> MarioActor; fn remove(self); fn alive(self) -> bool }
pub fn second_player(s: Second) -> SecondPlayer    // a second instance of the game's player
```

Data `kit/player.character/<id>.json`: all fields. Built on: `K.derive` and `K.hook` (the player object and the
globals it reads, the sm64coopdx method), `K.data`, `C.skin`, `C.anim`, `C.sound`, `C.text`, `assets` (a model made at
run time), `input`. Conformance: a character from data with its own clips and voice, switching mid-level and
back, Luigi's checks with `as_luigi`, a downloaded model (assets, checked by the validator) as a character, a second
player on pad 2 moving on a planet beside the first, the mod off while playing it (the game's Mario again).
