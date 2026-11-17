---
title: kit.replay
shortTitle: replay
intro: "Recording and playing back poses, puppets posed from data, input files played frame-exactly, rewind of the player and chosen actors."
contentType: reference
---

## About kit.replay

The `kit.replay` module of LumaKit gives you recording and playing back poses, puppets posed from data, input files played frame-exactly, rewind of the player and chosen actors.

To use the module, add `lumakit` to the `needs` of your mod's `luma.json` and write:

```lumascript
use kit.replay
```

### How the module works

Poses and inputs recorded a world step at a time, tracks written as bytes that read the same on every platform, puppets posed from a track, an input track played back step for step, the game's random numbers seeded, and rewind of the player and chosen actors. Handles are small structs; their functions are also module functions (`replay.stop_recording(r)`, `replay.pose_at(t, n)`). A track's data lives in LumaKit, counted for the mod that made it, and goes with that mod or at free(); a Track value is its number and its length. What is recorded, each world step (`on step`: the game's own steps, so a track is frame-exact at any speed): What.Player the player's place, turn (from the player's own front and up), animation name and frame, and form What.Actor an actor's place, turn (its rotation), animation frame What.Inputs what the game read from player 0's controls that step (svc.input game), and the player's pose at the first step: the state the reads moved the player from Bytes: "LKT1", little-endian, floats as IEEE 754 single: the same file on every platform. A pose alone is the same form with one pose. Playing inputs back: svc.input.inject (the calling mod needs the permission input.inject:), one step's read set before each step; the player put at the track's first pose (place, turn, animation and its frame) in the step the first read is taken, so the reads move the player from the state they were recorded from (the frame of the animation steers the player's walk: the same reads from another frame drift apart); the track's end, the mod stopping or stop() on the pending ends the injection. Rewind keeps the last `seconds` of steps of each thing asked for (at most 60 s) and back(n) puts each n steps back: the player by place and matrix, an actor by place and rotation; the world around them is not rewound.

The module's source is `lumakit/source/replay.lum`.

## Declarations

| Name | Kind | Description |
|---|---|---|
| [`What`](#what) | Enum |  |
| [`Pose`](#pose) | Struct |  |
| [`Track`](#track) | Struct |  |
| [`Recorder`](#recorder) | Struct |  |
| [`Puppet`](#puppet) | Struct |  |
| [`Rewind`](#rewind) | Struct |  |
| [`current_pose`](#current_pose) | Function | The pose of `w` now; `none` with no player (the boot, the title, a load, a movie), for an actor that is gone, and for What.Inputs (inputs have no pose: record them and read the track). |
| [`pose_at`](#pose_at) | Function | The pose of step `frame` of a pose track; none past its end, for an input track or a freed one. |
| [`free`](#free) | Function | The track's data given up now (it goes with its mod otherwise). |
| [`pose_encode`](#pose_encode) | Function | A pose as bytes (a track of one pose), and back; none when the bytes are no pose. |
| [`pose_decode`](#pose_decode) | Function |  |
| [`encode`](#encode) | Function | A track as bytes; empty for a track that is not the calling mod's any more. |
| [`decode`](#decode) | Function | A track from bytes, counted for the calling mod; none when they are no track (wrong head, cut short, a number out of range, more than an hour of steps). |
| [`record`](#record) | Function | Recording of `w` from the next world step, for the calling mod, until stop or discard (or an hour). |
| [`stop_recording`](#stop_recording) | Function | The recording ends: its track (`stop` is a word of the language, so the function is stop_recording). A recorder stopped or discarded before gives an empty track. |
| [`discard`](#discard) | Function | The recording ends and its steps are thrown away. |
| [`puppet`](#puppet) | Function | A puppet showing `look`, for the calling mod; at most 15 at once for all mods: past that, and when the stage cannot make one, an empty handle whose alive() is false and whose other calls do nothing. |
| [`set_pose`](#set_pose) | Function | The puppet placed as the pose says: its place, its turn, its animation at the pose's frame (started when the name changes and the puppet's model has it). |
| [`pose`](#pose) | Function | The puppet at a track's step; hidden past the track's end (shown again by a step inside it). |
| [`view`](#view) | Function | The puppet's object, for a mod that draws on it (draw.inst opacity) or reads it; `none` after remove(), once its scene ended, or for an empty handle or another mod's. |
| [`alive`](#alive) | Function |  |
| [`remove`](#remove) | Function | The puppet killed with the game's own kill; may be called again. |
| [`play_inputs`](#play_inputs) | Task | An input track played back for the calling mod: each world step reads the track's next step instead of the player's controls. Ends at the track's end; an input track playing before ends first. The calling mod needs the permission input.inject. |
| [`seed`](#seed) | Function | The game's random numbers from `n` from now on, for as long as the calling mod is on: MR::getRandom answers a 32-bit generator of the kit's own (xorshift), the same on every platform, so a run of recorded inputs meets the same numbers again. |
| [`rewind`](#rewind) | Function | The last `seconds` (at most 60) of each of `what` kept from now on, for the calling mod. What.Inputs is not kept. |
| [`put_player`](#put_player) | Function | The player put at a pose: place, turn and animation (a recorded start, a rewind). Nothing where no player exists. |
| [`back`](#back) | Function | Each thing put back `frames` steps, or as far as is kept: the steps gone back. What is kept after that point is dropped (the future is recorded anew). A step's pose is kept as the step begins, so the newest kept is one step before now: `frames` back is the kept pose that many from the end. |
| [`end`](#end) | Function | Nothing is kept any more; may be called again. |

### `What`

```lumascript
pub enum What { Player, Actor(a: ref<LiveActor>), Inputs }
```

### `Pose`

```lumascript
pub struct Pose { at: vec3, rotation: quat, action: text, frame: f32, form: text? = none }
```

### `Track`

```lumascript
pub struct Track {
    id: u32
    frames: u32
    fn pose_at(self, frame: u32) -> Pose? { return pose_at(self, frame) }
    fn encode(self) -> bytes { return encode(self) }
    fn free(self) { free(self) }
}
```

### `Recorder`

```lumascript
pub struct Recorder {
    id: u32
    fn stop_recording(self) -> Track { return stop_recording(self) }
    fn discard(self) { discard(self) }
}
```

### `Puppet`

```lumascript
pub struct Puppet {
    id: u32
    fn pose(self, t: Track, frame: u32) { pose(self, t, frame) }
    fn set_pose(self, p: Pose) { set_pose(self, p) }
    fn view(self) -> LiveActor? { return view(self) }
    fn remove(self) { remove(self) }
    fn alive(self) -> bool { return alive(self) }
}
```

### `Rewind`

```lumascript
pub struct Rewind {
    id: u32
    fn back(self, frames: u32) -> u32 { return back(self, frames) }
    fn end(self) { end(self) }
}
```

### `current_pose`

```lumascript
pub fn current_pose(w: What) -> Pose?
```

The pose of `w` now; `none` with no player (the boot, the title, a load, a movie), for an actor that is gone, and for What.Inputs (inputs have no pose: record them and read the track).

### `pose_at`

```lumascript
pub fn pose_at(t: Track, frame: u32) -> Pose?
```

The pose of step `frame` of a pose track; none past its end, for an input track or a freed one.

### `free`

```lumascript
pub fn free(t: Track)
```

The track's data given up now (it goes with its mod otherwise).

### `pose_encode`

```lumascript
pub fn pose_encode(p: Pose) -> bytes
```

A pose as bytes (a track of one pose), and back; none when the bytes are no pose.

### `pose_decode`

```lumascript
pub fn pose_decode(b: bytes) -> Pose?
```

### `encode`

```lumascript
pub fn encode(t: Track) -> bytes
```

A track as bytes; empty for a track that is not the calling mod's any more.

### `decode`

```lumascript
pub fn decode(b: bytes) -> Track?
```

A track from bytes, counted for the calling mod; none when they are no track (wrong head, cut short, a number out of range, more than an hour of steps).

### `record`

```lumascript
pub fn record(w: What) -> Recorder
```

Recording of `w` from the next world step, for the calling mod, until stop or discard (or an hour).

### `stop_recording`

```lumascript
pub fn stop_recording(r: Recorder) -> Track
```

The recording ends: its track (`stop` is a word of the language, so the function is stop_recording). A recorder stopped or discarded before gives an empty track.

### `discard`

```lumascript
pub fn discard(r: Recorder)
```

The recording ends and its steps are thrown away.

### `puppet`

```lumascript
pub fn puppet(look: model) -> Puppet
```

A puppet showing `look`, for the calling mod; at most 15 at once for all mods: past that, and when the stage cannot make one, an empty handle whose alive() is false and whose other calls do nothing.

### `set_pose`

```lumascript
pub fn set_pose(p: Puppet, pose: Pose)
```

The puppet placed as the pose says: its place, its turn, its animation at the pose's frame (started when the name changes and the puppet's model has it).

### `pose`

```lumascript
pub fn pose(p: Puppet, t: Track, frame: u32)
```

The puppet at a track's step; hidden past the track's end (shown again by a step inside it).

### `view`

```lumascript
pub fn view(p: Puppet) -> LiveActor?
```

The puppet's object, for a mod that draws on it (draw.inst opacity) or reads it; `none` after remove(), once its scene ended, or for an empty handle or another mod's.

### `alive`

```lumascript
pub fn alive(p: Puppet) -> bool
```

### `remove`

```lumascript
pub fn remove(p: Puppet)
```

The puppet killed with the game's own kill; may be called again.

### `play_inputs`

```lumascript
pub task play_inputs(t: Track)
```

An input track played back for the calling mod: each world step reads the track's next step instead of the player's controls. Ends at the track's end; an input track playing before ends first. The calling mod needs the permission input.inject.

### `seed`

```lumascript
pub fn seed(n: u32)
```

The game's random numbers from `n` from now on, for as long as the calling mod is on: MR::getRandom answers a 32-bit generator of the kit's own (xorshift), the same on every platform, so a run of recorded inputs meets the same numbers again.

### `rewind`

```lumascript
pub fn rewind(what: list<What>, seconds: f32) -> Rewind
```

The last `seconds` (at most 60) of each of `what` kept from now on, for the calling mod. What.Inputs is not kept.

### `put_player`

```lumascript
pub fn put_player(p: Pose)
```

The player put at a pose: place, turn and animation (a recorded start, a rewind). Nothing where no player exists.

### `back`

```lumascript
pub fn back(r: Rewind, frames: u32) -> u32
```

Each thing put back `frames` steps, or as far as is kept: the steps gone back. What is kept after that point is dropped (the future is recorded anew). A step's pose is kept as the step begins, so the newest kept is one step before now: `frames` back is the kept pose that many from the end.

### `end`

```lumascript
pub fn end(r: Rewind)
```

Nothing is kept any more; may be called again.

## Further reading

* [LumaKit reference](index.md)
* [About LumaKit](../../concepts/lumakit.md)
