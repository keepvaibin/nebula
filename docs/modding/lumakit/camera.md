# LumaKit: camera

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
(image and controls), a rig in a `draw.view` region, two mods taking the camera (rule 7).
As built (FB, 11-12): data rigs (`view.Camera { rig: view.Rig.Fixed|Follow|Joint|World, ... }`)
replace R-7's `Look(fn)`; a computed camera calls `aim(c, e: view.Eye)` each frame (draw.view `main_eye`, held while
the world is held). `end(c)` and `alive(c)` are module functions too.
