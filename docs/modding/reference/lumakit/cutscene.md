---
title: kit.cutscene
shortTitle: cutscene
intro: "Scripted scenes of shots and lines, waited on, skippable."
contentType: reference
---

## About kit.cutscene

The `kit.cutscene` module of LumaKit gives you scripted scenes of shots and lines, waited on, skippable.

To use the module, add `lumakit` to the `needs` of your mod's `luma.json` and write:

```lumascript
use kit.cutscene
```

### How the module works

Scripted scenes of shots and lines, waited on, skippable. A scene is a task: it takes the main camera (kit.camera take: the newest holds it), turns the player's control off, shows the game's own letterbox (CinemaFrame, as the game's demos do), and runs its shots in order. The eye is placed each frame through kit.camera's aim (a FIXED rig of svc.draw.view), which the frame draws even while the world is held, so camera shots move over a held world. Look the camera `frames` long at `from_offset` from the actor in world axes, looking at it; the shot ends early when the actor is gone or killed (the scene goes on) Move the eye from one place to another over `frames`, eased at both ends (eye, aim, up and field of view) Line the speaker says the line in the game's balloon (kit.talk); with no speaker, or a speaker gone, a message screen (kit.screen) Action the actor starts the named action of its own (MR::tryStartAction) and the shot lasts until it ends Wait frames Call the mod's function, once The world: with holds_world the world is held (svc.time) during Look, Move, Wait and Call; it runs during a Line (the balloon is the scene's own layout) and an Action (an animation needs the world's steps), and until the letterbox's bars stand (they are a layout of the scene too). The player's control is off for the whole scene either way. Skipping: the game's pause button (MR::isPressedPause), read every frame of a skippable scene; a skipped scene ends at once and play gives false. The game's pause menu does not open while a scene plays. Every way out (its end, a skip, a newer scene, its mod stopping) puts everything back: the camera, the letterbox, the world, the player's control, the music. One scene plays at a time for all mods: a newer one ends the one before (false to its waiter). Data: kit/cutscene/<id>.json of the calling mod, read by find(); actors by placement name (the first live object of that name), eyes as numbers.

The module's source is `lumakit/source/cutscene.lum`.

## Declarations

| Name | Kind | Description |
|---|---|---|
| [`Shot`](#shot) | Enum |  |
| [`Scene`](#scene) | Struct |  |
| [`play`](#play) | Task | The scene played for the calling mod. true: it ran to its end; false: it was skipped, a newer scene began, or its mod stopped. |
| [`find`](#find) | Function | The scene of the calling mod's kit/cutscene/<id>.json, its actors found now by their placement names; none when the mod has no such file, it does not read as a scene, or an actor a Look or an Action names is not in the stage. (A Line's speaker that is not there is said as a message.) Call it in the stage the scene plays in. |
| [`active`](#active) | Function | Whether a scene plays now (for a mod that holds its own HUD or input back while one does). |

### `Shot`

```lumascript
pub enum Shot { Look(at: ref<LiveActor>, from_offset: vec3, frames: u32), Move(from: view.Eye, to: view.Eye, frames: u32),
                Line(speaker: ref<LiveActor>?, line: text), Action(who: ref<LiveActor>, action: text),
                Wait(frames: u32), Call(f: fn()) }
```

### `Scene`

```lumascript
pub struct Scene { shots: list<Shot>, letterbox: bool = true, skippable: bool = true, holds_world: bool = true,
                   music: sound? = none }
```

### `play`

```lumascript
pub task play(s: Scene) -> bool
```

The scene played for the calling mod. true: it ran to its end; false: it was skipped, a newer scene began, or its mod stopped.

### `find`

```lumascript
pub fn find(id: text) -> Scene?
```

The scene of the calling mod's kit/cutscene/<id>.json, its actors found now by their placement names; none when the mod has no such file, it does not read as a scene, or an actor a Look or an Action names is not in the stage. (A Line's speaker that is not there is said as a message.) Call it in the stage the scene plays in.

### `active`

```lumascript
pub fn active() -> bool
```

Whether a scene plays now (for a mod that holds its own HUD or input back while one does).

## Further reading

* [LumaKit reference](index.md)
* [About LumaKit](../../concepts/lumakit.md)
