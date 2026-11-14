# LumaKit: cutscene

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
