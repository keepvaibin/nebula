# Content: animations

## 1. Clips in a model's own file (layer 1)

Each glTF animation of a model's `.glb`/`.gltf` becomes clip files beside the model in its archive, named by the
animation's name (an unnamed one is `animation<N>`):

| What moves | glTF | Clip file |
|---|---|---|
| Joints | node `translation`, `rotation`, `scale` channels | `<name>.bck` |
| A material's texture transform | `KHR_animation_pointer` to `KHR_texture_transform` offset, scale, rotation | `<name>.btk` |
| A material's colour | `KHR_animation_pointer` to `baseColorFactor` / `emissiveFactor` | `<name>.bpk` |
| A material's texture | `extras.nebula.texture_swaps` (`material`, `keys` [time, texture]) | `<name>.btp` |

How a clip plays: `animations[i].extras.nebula.play`: `once`, `once_then_first`, `loop` (the default),
`there_and_back`, `loop_there_and_back`; anything else is refused (Cook.Clip.Plays).

## 2. An animation file (layer 2)

`layers/<l>/files/<archive>.arc/<any>.anim.glb` (or `.anim.gltf`) holds animations only. Each animation becomes a
clip of the archive's game model, by the animation's name, and replaces the game's clip of that name (the store
compares names without case: `Walk` replaces `walk.bck`). The game's other clips stay. The file's own name is free.

- The model. The archive's one model; if it has several, the one named as the archive (`Kuribo.arc/Kuribo.bdl`).
  Otherwise refused: Cook.Anim.WhichModel {archive, models}.
- The skeleton. The file's nodes carry the model's skeleton, as a Blender export of the armature does. Each of
  the game model's joints is the node of the same name (missing: Cook.Model.MissingJoint {joint, model}). Each such
  node hangs, through any nodes between, from the node of its game parent joint:
  - Cook.Anim.ParentDiffers {joint, parent, found}
  - Cook.Anim.ParentMissing {joint, parent}
  - Cook.Anim.RootDiffers {joint, found}

  A joint no channel moves keeps its node's rest place.
- Channels. Joint `translation`, `rotation` and `scale`, and material tracks (layer 4, below):
  - a channel on a node that is no joint: Cook.Anim.NotAJoint {animation, node, model};
  - morph weights, a pointer to anything but a material, or anything else: Cook.Anim.OnlyJoints {animation, path}.
- Materials (layer 4). The file's `materials` stand for the game model's materials of the same name (exact, else
  without case), as section 1's tracks do for a model's own: `KHR_animation_pointer` to `baseColorFactor` (`.bpk`) and
  to the base colour texture's `KHR_texture_transform` offset, scale, rotation (`.btk`), and
  `extras.nebula.texture_swaps` (`.btp`, slot 0), whose textures stand for the game model's textures by their image's
  `name`, else the image's file name less its last ending (the game's names have dots: `KuriboAll.2`). The file's
  material needs no texture of its own. Parts of a texture transform no channel moves keep the file material's still
  `KHR_texture_transform` (glTF's default without one), not the game material's. Refused by name:
  - a material an animation moves that the game model lacks: Cook.Anim.NoGameMaterial {material, model, materials};
  - a swapped-in texture it lacks: Cook.Anim.NoGameTexture {texture, model, textures};
  - a swapped-in image with neither name nor file name: Cook.Anim.TextureUnnamed {texture, model};
  - a store imported by an older Nebula, whose model this one cannot read for names: Cook.Anim.ModelUnread {model};
  - any other material pointer: Cook.Clip.Pointer; a material number the file lacks: Cook.Model.Invalid.

  An animation of materials only makes no joint clip: the game's joint clip of that name stays (one moving nothing
  is a still `.bck`, as before).
- Names. Every animation is named (Cook.Anim.NoName {animation}), and no two share a name
  (Cook.Anim.Twice {name}). A file with no animation: Cook.Anim.None {file}.
- Place. Only inside a game archive (Cook.Anim.NotInArchive {file}), and only with the game known: the cook
  numbers joints by the game's model (Cook.Anim.NoGame {file} in a LumaLink check without `--game`).
- The fact. `cook.model` with `other` = the game model's store path, `joints` = its joint count, `clips` = the
  clips made, `materials` = the game materials the file's animations move, and `meshes`, `vertices`, `textures` = 0.

## 2a. A clip's frame events (layer 3)

`layers/<l>/files/<archive>.arc/<clip>.anim.json` names events at frames of the archive's clip `<clip>`:

```json
{ "events": [ { "frame": 12, "event": "step_left" }, { "frame": 30, "event": "step_right" } ] }
```

The cook writes `<archive>.arc/<clip>.events`: a table (NTBL) of the columns `frame` (int) and `event` (text), one row
per event, sorted by frame (rows of one frame in the file's order). The anim service gives each row to a play of the
clip as a mark after the play's own (anim 1.1: MARK value >= mark count is an event row; `anim.event_name`).
Code: Nebula `crates/nebula-import/src/cook/clip_events.rs`, `cook/overlay.rs` (`modern_files`).

- Keys. `events` only; each event `frame` and `event` only (Cook.Effect.UnknownPart {where, key, expected});
  missing: Cook.Effect.Missing {where, key}.
- Frame. A whole number from 0 to 65,534 (Cook.Effect.Kind, Cook.Effect.Range). With the game known and its
  archive holding the clip, the frame is checked against the lead clip (the first present of `.bck`, `.btk`, `.brk`,
  `.bpk`, `.btp`, `.bva` of that name, without case, as the anim service picks it): a frame at or past its frame
  count is Cook.Events.PastEnd {where, frame, clip, last} (a play's frame stays under the count: a loop wraps there, a
  once clip ends).
- Event. 1 to 63 of `A-Z a-z 0-9 _ .`: Cook.Events.Name {where, name, max}. The same event twice at one frame:
  Cook.Events.Twice {where, event, frame}. Two events at one frame are two rows.
- Count. At least one (Cook.Events.None {file}), at most 64 (Cook.Events.TooMany {file, count, max}).
- Place. Only in a game archive (Cook.Events.NotInArchive {file}); the clip's name (the file name before
  `.anim.json`) is 1 to 127 bytes (Cook.Events.ClipName {file, max}).
- Not checked, said. The game's archive has no clip of that name (a clip the mod itself brings, from a model's
  file or an animation file): Note Cook.Events.ClipUnknown {file, clip, archive}; no game given:
  Note Cook.Events.GameUnknown {file}. The frames are then checked only against 65,534.
- The fact. `cook.events` with `other` = the clip, then `rows` and `frames` (the game clip's frame count, `none`).

## 3. Every glTF animation (models and animation files)

- Every key, time and value, is a finite number; otherwise Cook.Anim.NotANumber {animation}.
- A clip lasts at most 65,535 frames (18 min 12 s at 60 a second, the 16-bit frame count of the game's clip files);
  otherwise Cook.Anim.TooLong {animation, seconds, max}, refused before any sample is taken.
- A clip of no length is one frame.
- The game counts 60 frames a second. Joints are sampled every other frame and on the last, rotations the shorter
  way.

## 4. Checks before a pack (L304, L804)

A source file under `files/` of a kind the cook makes is checked by the cook, not left "not checked" (L304):
- glTF (`.glb`, `.gltf`, and their `.anim.`, `.kcl.`, `.canm.` forms);
- `.png`;
- `.wav` and `.ogg` streams;
- `.bcsv.json`, `.bcam.json` and `.arc/jmp/.json` tables.
- `.anim.json` frame events.

The list is `luma_validate::cooked_source`; a nebula-import test holds it to `overlay::cook_source`. `lumalink check`
and `pack` with `--game` cook each such file against the game, and a refusal is L804 on that file, in the player's
language. The game's start cooks the same files and says the same.

## 5. Not built yet (later layers, PLANS-UNSTARTED 3.6)

- a texture swap on a slot other than 0 (a game material's second texture);
- (5) layers, masks, additive (the game's ActorAnimCtrl: one action starting a .bck, .btk, .brk, .bpk, .btp and .bva
  together, each with a start frame and a keep flag), and blends (the game's BckCtrl, `<model>.banmt`: name,
  interpole, play_frame, start_frame, end_frame, attribute). Today both are reachable as table patches by key
  (3.11);
- (6) morph weights;
- visibility (`KHR_node_visibility` to `.bva`).

## 6. Conformance

- 3601-walk-replace: ga-walk swaps the Goomba's Walk for a sway with a big head;
- 3602-walk-not-a-joint: ga-walk-bad is refused, naming Hat.
- 3603-walk-events, 3604-walk-events-past-end: ga-events, ga-events-bad (section 2a).
- 3605-blink-shut: ga-blink replaces the Goomba's blink (texture swaps on KuriboAll_v): eyes shut nearly always;
- 3606-blink-no-such-material: ga-blink-bad is refused, naming KuriboBody and KuriboAll_v.

Capabilities `anim.replace`, `anim.events`, `anim.materials`. Proven so far at the command line only,
not yet in a game run.

## 7. Run time: the `anim` service 1.0

Code:

- What plays. `play(actor, clip, how)` on a game actor, `play_on_model(model, clip, how)` on a model the mod
  attached (models 7). Every kind of clip of that name the model has starts together, as the game starts its own
  (`MR::startBck` and the material starts, so the actor's effect and sound keepers follow a joint clip). The first
  present of .bck, .btk, .brk, .bpk, .btp, .bva leads: its frame is the play's, its end the play's end, its frame the
  one marks are read on. A clip of a game model that a mod's animation file replaced (section 2) is the mod's clip.
- How (`LumaAnimHow`, null or zeroed: as the game plays the clip): `rate` (0 is 1; above 0, at most 16 frames a
  frame), `blend` (joint clips: 0 the model's clip table, -1 none, N frames), `loop` (0 as the clip says, 1 once,
  2 loop), `start` (0 to the clip's last frame), up to 16 `marks` (frames, copied).
- One play per kind and actor. A play ends an earlier play on its actor (any mod's) that plays a kind it plays.
  A colour clip and a joint clip of different names play side by side.
- Events at a frame's start, from the frame's movement before (a paused game moves no frame: no mark, no end):
  - `LUMA_ANIM_EVENT_MARK` (value: the mark's index) each time the lead's frame moves onto or past a mark: each loop
    (a loop's wrap counts the marks past its last frame and up to the new one), going back as well. A mark on the
    start frame comes at once.
  - `LUMA_ANIM_EVENT_END`, the play's last event: LUMA_OK when a once clip's control says it ended; else
    LUMA_E_CANCELLED with the why in `value`: 1 stop, 2 replaced (another play, or the actor's own code starting
    another clip of the lead's kind), 3 actor (it died; a model: it was detached), 4 scene. A mod that stops or is
    taken out gets no event (5 in the facts).
- stop leaves every kind of the play on its frame (rate 0) until the actor's code or another play starts a clip.
  The mod's own end leaves the clips running as the actor's own.
- frame reads the lead's frame and last frame.
- Refusals (each reported, no handle written): see the header. A clip the model lacks (or a model that shows
  nothing) is LUMA_E_NOT_FOUND and Content.MissingAnimation (627, {model}, {found}).
- Limits. 64 plays a mod, 16 marks a play, clip names under 128 bytes.
- Facts (harness). `anim.play.<h>` {model, clip, kinds, loops, frames, start, rate, on_model};
  `anim.mark.<h>.<i>` {frame}; `anim.frame.<h>` {frame, last} at each frame call; `anim.end.<h>` {why: end, stop,
  replaced, actor, scene, mod}.
- Cost with no mod: one load a frame and one at a scene's release.

Capabilities anim.play, anim.on_model, anim.how, anim.mark, anim.end, anim.frame, anim.refusals, anim.game_clip.

### 7.3 A clip's own events (anim 1.1; 3.6 layer 3)

Source: `layers/<l>/files/<archive>.arc/<clip>.anim.json` = `{"events": [{"frame": 12, "event": "step_left"}]}`;
cooked to `<clip>.events` in that archive, an NTBL table with columns `frame` (int, 0 to the clip's last frame) and
`event` (text, 1 to 63 of A-Z a-z 0-9 _ .), rows sorted by frame, at most 64. Run time: a play
looks for `<clip>.events` in its actor's model archive when it starts. Each row is a mark at its frame after the
mod's own: `LUMA_ANIM_EVENT_MARK` with value `mark_count + row` and `data` set to the row's name (`data_size` bytes,
no NUL, valid during the call). Rows are passed as the mod's marks are: each loop, on the start frame, and going
back. A file that is not an NTBL table, or one without both columns, gives no rows and one log line; more than
`LUMA_ANIM_MOST_EVENT_ROWS` (64) rows: the first 64, logged. The fact `anim.play.<h>` adds `event_rows`, and
`anim.mark.<h>.<i>` adds `event` for a row. A row past the lead's last frame (the cook checks only a game clip's, 2a) is reported at each start of such a play: `Content.EventPastEnd` (code 670, references 6). A clip without the file gives only the mod's marks, as in 1.0.
Conformance: 3619 (ja-events: the Goomba's Walk with rows start 0, step_left 6, step_right 12 and one own mark at
5), in suite-events.json until the cook is on the integration build. Capability anim.events.
