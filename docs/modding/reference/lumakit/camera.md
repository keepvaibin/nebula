---
title: kit.camera
shortTitle: camera
intro: "The main camera taken for a time, camera rigs for views (follow, eye, side, above), mirror, field of view. A rig is a `draw.ViewCamera` (svc.draw), so the same rig drives the main camera or a `draw.view`."
contentType: reference
---

## About kit.camera

The `kit.camera` module of LumaKit gives you the main camera taken for a time, camera rigs for views (follow, eye, side, above), mirror, field of view. A rig is a `draw.ViewCamera` (svc.draw), so the same rig drives the main camera or a `draw.view`.

To use the module, add `lumakit` to the `needs` of your mod's `luma.json` and write:

```lumascript
use kit.camera
```

### How the module works

The main camera taken for a time, camera rigs for views (follow, eye, side, above), mirror, field of view, shake. A rig is a svc.draw.view Camera, so the same rig drives the main camera (take) or a mod's draw.view (view.rig). The main camera: after CameraDirector::calcPose each frame, the newest live control's eye replaces the game's pose (its place, the point it looks at, its up), through walls, and the game's interpolation is cut while it holds; the field of view replaces the camera man's while createViewMtx reads it. When the last control ends, the game's own camera is back at once (its own pose was computed all along). The newest control holds; ending it gives the one before. Every control, the mirror, the field of view and a shake are recorded under caller(); `on mods.stopped` takes a stopped mod's away. The main camera is also held through draw.view's main_eye (the same eye each frame, and at once on take, aim and end), so it holds while the world is held (kit.cutscene's aim each frame of a hold). Mirror: the stick's x flipped (MR::getPlayerStickX), and the 3-D picture flipped by LumaKit's post program kit_mirror at draw.post's WORLD stage (the HUD stays readable). `mirror`'s parameter is `enabled` (`on` is a keyword). The handle's functions are also module functions (`end`, `alive`). The free camera turns with the stick, not the pointer: scripts cannot read the game's pointer position (MR::getPointerPos).

The module's source is `lumakit/source/camera.lum`.

## Declarations

| Name | Kind | Description |
|---|---|---|
| [`Control`](#control) | Struct |  |
| [`take`](#take) | Function | The main camera follows the rig until end(); the newest control holds. |
| [`free`](#free) | Function | A free camera from where the game's camera is: the move stick moves it along its look (up and down) and turns it (left and right), buttons 1 and 2 raise and lower it; `speed` units a frame at a full stick. |
| [`aim`](#aim) | Function | The control re-set as a fixed rig at `e` (kit.cutscene calls it each frame); the main camera is there at once when the control holds, the world held or not (settle). |
| [`end`](#end) | Function |  |
| [`alive`](#alive) | Function |  |
| [`follow`](#follow) | Function | A camera `distance` behind the target and `height` above it, looking at it, turning with it. |
| [`eye`](#eye) | Function | First person: the eye at the actor's joint, looking along its front, the near plane `near` (the player's own model hidden while the main camera holds it). |
| [`side`](#side) | Function | A side camera for 2D sections: `distance` from the player along `normal` (world axes), looking at the player. Called while a scene has a player (a rig's actor cannot be empty: a service struct's field cannot be none). |
| [`stereo`](#stereo) | Function | Two rigs `eye_distance` apart across the rig's look (left, right), for a pair of views (VR). A fixed rig moves its eye and the point it looks at; a follow or world rig its offset (a follow rig's x is the actor's side). A joint rig's pair is the joint's one eye (draw.view has no side offset for a joint). |
| [`mirror`](#mirror) | Function | The picture and the stick's x flipped, or not. |
| [`fov`](#fov) | Function | The main camera's vertical field of view in degrees (1 to 179); none: the game's own (or a control's rig's). |
| [`shake`](#shake) | Function | The main camera shaken `strength` game units, fading out over `frames`. |
| [`now`](#now) | Function | Where the main camera is this frame. |

### `Control`

```lumascript
pub struct Control {
    n: u64
    fn end(self) { end(self) }
    fn alive(self) -> bool { return alive(self) }
}
```

### `take`

```lumascript
pub fn take(rig: view.Camera) -> Control
```

The main camera follows the rig until end(); the newest control holds.

### `free`

```lumascript
pub fn free(speed: f32 = 40.0) -> Control
```

A free camera from where the game's camera is: the move stick moves it along its look (up and down) and turns it (left and right), buttons 1 and 2 raise and lower it; `speed` units a frame at a full stick.

### `aim`

```lumascript
pub fn aim(c: Control, e: view.Eye)
```

The control re-set as a fixed rig at `e` (kit.cutscene calls it each frame); the main camera is there at once when the control holds, the world held or not (settle).

### `end`

```lumascript
pub fn end(c: Control)
```

### `alive`

```lumascript
pub fn alive(c: Control) -> bool
```

### `follow`

```lumascript
pub fn follow(target: LiveActor, distance: f32, height: f32 = 300.0) -> view.Camera
```

A camera `distance` behind the target and `height` above it, looking at it, turning with it.

### `eye`

```lumascript
pub fn eye(of: LiveActor, joint: text = "Head", near: f32 = 5.0) -> view.Camera
```

First person: the eye at the actor's joint, looking along its front, the near plane `near` (the player's own model hidden while the main camera holds it).

### `side`

```lumascript
pub fn side(normal: vec3, distance: f32) -> view.Camera
```

A side camera for 2D sections: `distance` from the player along `normal` (world axes), looking at the player. Called while a scene has a player (a rig's actor cannot be empty: a service struct's field cannot be none).

### `stereo`

```lumascript
pub fn stereo(rig: view.Camera, eye_distance: f32) -> (view.Camera, view.Camera)
```

Two rigs `eye_distance` apart across the rig's look (left, right), for a pair of views (VR). A fixed rig moves its eye and the point it looks at; a follow or world rig its offset (a follow rig's x is the actor's side). A joint rig's pair is the joint's one eye (draw.view has no side offset for a joint).

### `mirror`

```lumascript
pub fn mirror(enabled: bool)
```

The picture and the stick's x flipped, or not.

### `fov`

```lumascript
pub fn fov(degrees: f32?)
```

The main camera's vertical field of view in degrees (1 to 179); none: the game's own (or a control's rig's).

### `shake`

```lumascript
pub fn shake(strength: f32, frames: u32)
```

The main camera shaken `strength` game units, fading out over `frames`.

### `now`

```lumascript
pub fn now() -> view.Eye
```

Where the main camera is this frame.

## Further reading

* [LumaKit reference](index.md)
* [About LumaKit](../../concepts/lumakit.md)
