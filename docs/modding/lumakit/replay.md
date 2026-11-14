# LumaKit: replay

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
pub fn puppet(look: model) -> Puppet                  // lit as the player (up to 15)
pub fn play_inputs(t: Track) -> pending               // What.Inputs, each frame injected
pub fn seed(n: u32)                                   // the game's random numbers from n (MR::getRandom)
pub struct Rewind { fn back(self, frames: u32) -> u32; fn end(self) }
pub fn rewind(what: list<What>, seconds: f32) -> Rewind
```

Built on: `K.field`, `K.hook` (`MR::getRandom`, the player's pose), `K.call`, `input` (inject), `time.ctl`† (step),
`draw.inst`† (puppet opacity), `C.skin`, `C.anim`. Conformance: a recorded run played back pose for pose (difference
0 on the same build), a track decoded on another platform, 15 puppets, inputs replayed giving the same end position,
rewind of the player 5 s.


Recording, playback and rewinds take only the steps in which the game plays (time state Play): a pause menu or the
galaxy map adds nothing to a track. With no player (the title, the file select, a movie) a recorder of What.Player
records nothing, put_player does nothing and current_pose(What.Player) is a script error.

A puppet shows a model of the mod: a ghost draws the mod's ghost model, and set_pose plays the pose's action only when
that model has an animation of that name. A puppet with the player's own model and animations is not in this version.
