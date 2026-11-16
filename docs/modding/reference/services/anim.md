---
title: anim service
shortTitle: anim
intro: Play animations on models and actors, and receive their marks.
contentType: reference
---

## About the anim service

The `anim` service plays a clip of an actor's model or of a model your mod attached, the way the game plays its own. Your mod can read the clip's frame, mark frames, and get an event at each mark and at the end.

| Service | Version | Permission | C import | LumaScript |
|---|---|---|---|---|
| `anim` | 1.1 | None | `LUMA_SERVICE(slot, "anim", "1.1")` | `use svc.anim` |

The header is `native/include/luma_anim.h`. In LumaScript, a service is called by the last part of its name, such as `anim.` for `anim`. Each function below shows its LumaScript form first and its C form second. For how the forms are made, see [Calling services](calling-services.md).

### Details from the header

LumaCore's anim service: a clip of an actor's model played as the game plays its own (its joint, texture, color, texture-swap and visibility clips of one name started together), from a frame, at a rate, once or looping, blended from the pose it had; its frame read; frames of it marked, each mark an event as the clip passes it; its end an event. The clip is the model's own (a mod model's glTF animations, a game model's clips, or the mod's replacement of one). Import: `LUMA_SERVICE(slot, "anim", "1.0")`.

## Limits and constants

| Name | Value | Description |
|---|---|---|
| `LUMA_ANIM_MOST_PLAYS` | `64` | Plays one mod has going at once, marks one play carries, and event rows one clip's events give. |
| `LUMA_ANIM_MOST_MARKS` | `16` |  |
| `LUMA_ANIM_MOST_EVENT_ROWS` | `64` |  |
| `LUMA_ANIM_EVENT_NAME_MOST` | `63` | An event row's name: 1 to 63 bytes of A-Z a-z 0-9 _. (the cook's rule). |
| `LUMA_ANIM_MOST_RATE` | `16.0f` | The fastest rate: frames of the clip per frame of the game (60 a second). |
| `LUMA_ANIM_BLEND_CLIP` | `0` | As the game starts the clip: its model's clip table (.banmt), else none. |
| `LUMA_ANIM_BLEND_NONE` | `(-1)` | The clip's pose at once. |

## Types

| Name | Declaration | Description |
|---|---|---|
| `LumaAnimPlay` | `typedef LumaHandle LumaAnimPlay;` | A play: owned by the mod, ended by its end, stop, another play of a kind it plays on its actor, the actor's own code starting another clip, the actor's death, its scene's end, or the mod's. |

## Enumerations

### Other constants

The events a play gives (`LumaEvent`.kind; handle: the play).

| Name | Description |
|---|---|
| `LUMA_ANIM_EVENT_END = 1` | Status `LUMA_OK`: a once clip reached its last frame; `LUMA_E_CANCELLED`: it ended first. value: a `LumaAnimWhy`. Last of its play's events. |
| `LUMA_ANIM_EVENT_MARK = 2` | Status `LUMA_OK`; value: below `LumaAnimHow`.mark_count the mark's index in its marks; 1.1: from mark_count up, mark_count + the row of the clip's own events (the cooked <clip>.events beside it, animation 7.3), data the row's name (UTF-8 text; data_size bytes, no NUL, valid during the call). A clip without events gives only the mod's marks. |

### `LumaAnimWhy`

Why a play ended (`LUMA_ANIM_EVENT_END` value).

| Name | Description |
|---|---|
| `LUMA_ANIM_ENDED = 0` | A once clip at its last frame. |
| `LUMA_ANIM_STOPPED = 1` | The mod's stop. |
| `LUMA_ANIM_REPLACED = 2` | Another play of a kind it plays on its actor, or the actor's own code playing another clip of its first kind. |
| `LUMA_ANIM_ACTOR = 3` | The actor died (a model's: it was detached). |
| `LUMA_ANIM_SCENE = 4` | The actor's scene ended. |
| `LUMA_ANIM_MOD = 5` | The mod stopped or was taken out (no event comes then). |

### `LumaAnimLoop`

How a clip plays (`LumaAnimHow`.loop).

| Name | Description |
|---|---|
| `LUMA_ANIM_AS_CLIP = 0` | As the clip says (its glTF extras.nebula.play, or the game's clip). |
| `LUMA_ANIM_ONCE = 1` | Once, then it stays on its last frame. |
| `LUMA_ANIM_LOOP = 2` | From its first frame again after its last, never ending. |

## Structures

### `LumaAnimHow`

How a play goes: zeroed (or a null pointer) is the clip as the game plays it, from its first frame.

Its size in version 1.0 is 32 bytes.

| Name | Description |
|---|---|
| `uint32_t size` | Sizeof(`LumaAnimHow`) as the mod was built. |
| `float rate` | Frames of the clip per frame of the game; 0: 1. Above 0, at most `LUMA_ANIM_MOST_RATE`. |
| `int32_t blend` | Joint clips: frames from the pose it had to the clip's; `LUMA_ANIM_BLEND_CLIP` or _NONE. |
| `uint32_t loop` | A `LumaAnimLoop`: 0 to 2. |
| `float start` | The frame it starts from: 0 to the clip's last. |
| `uint32_t mark_count` | At most `LUMA_ANIM_MOST_MARKS`. |
| `const float* marks` | Frames (copied) that each give `LUMA_ANIM_EVENT_MARK` as the clip passes them, every time it does (each loop; either way on a clip that goes there and back). |

## `anim` functions

In C, each function takes your mod (`const LumaMod* mod`) first. LumaScript passes it for you.

### `play`

```lumascript
anim.play(actor: LiveActor, clip: text, how: anim.How, fn: fn(e: anim.Event)?) -> anim.Play?
```

```c
int32_t play(const LumaMod* mod, LumaActor actor, const char* clip, const LumaAnimHow* how, LumaEventFn fn, void* user, LumaAnimPlay* out);
```

Call it on the game thread. The clip `clip` of `actor`'s model played: every kind of clip of that name its model has (joints.bck, texture moves.btk, colors.brk and.bpk, texture swaps.btp, visibility.bva), the first of them in that order leading (its frame is the play's; its end, its marks; 1.1: its events' rows, as marks after the mod's own, the first `LUMA_ANIM_MOST_EVENT_ROWS` of them). An earlier play on the actor of a kind this one plays ends (`LUMA_ANIM_REPLACED`). Played as `how` says (null: the clip as the game plays it, from its first frame). The play's events come to fn(user, event) at a frame's start (`fn` null: none; `user` null: given back null). It returns `LUMA_OK` and sets `*out`. No clip of that name: `LUMA_E_NOT_FOUND`, `Content.MissingAnimation` in the mod log. A null or dead actor, one that is not one or has no model, an empty name, a rate below 0, not a number or above `LUMA_ANIM_MOST_RATE`, a blend below -1, a loop outside the enum, a start outside the clip, more than `LUMA_ANIM_MOST_MARKS` marks, null marks with a count, a null out: `LUMA_E_ARGUMENT` (reported); a how below 1.0's size: `LUMA_E_ARGUMENT` (reported) (`Service.StructSize`); `LUMA_ANIM_MOST_PLAYS` going: `LUMA_E_LIMIT` (reported). Off the game thread: `LUMA_E_THREAD` (reported); while the game takes the mod out: `LUMA_E_CANCELLED`.

Events: `END(socket, status, value)`, `MARK(socket, value, data)`.

### `play_on_model`

```lumascript
anim.play_on_model(model: content.model.Model, clip: text, how: anim.How, fn: fn(e: anim.Event)?) -> anim.Play?
```

```c
int32_t play_on_model(const LumaMod* mod, LumaContentModel model, const char* clip, const LumaAnimHow* how, LumaEventFn fn, void* user, LumaAnimPlay* out);
```

Call it on the game thread. As play, on the model `model` the mod attached (content.model, models 7): its own clips. A skeleton-follow model's joints stay its actor's; its other kinds play. `how`, `fn` and `user` as play. It returns `LUMA_OK` and sets `*out`. No clip of that name, or a model that shows nothing (neither it nor the placeholder in a store): `LUMA_E_NOT_FOUND`, `Content.MissingAnimation` in the mod log. An ended model: `LUMA_E_HANDLE` (reported). A clip, how or out play refuses: `LUMA_E_ARGUMENT` (reported); `LUMA_ANIM_MOST_PLAYS` going: `LUMA_E_LIMIT` (reported). Off the game thread: `LUMA_E_THREAD` (reported); while the game takes the mod out: `LUMA_E_CANCELLED`.

Events: `END(socket, status, value)`, `MARK(socket, value, data)`.

### `stop`

```lumascript
anim.stop(play: anim.Play)
```

```c
int32_t stop(const LumaMod* mod, LumaAnimPlay play);
```

Call it on the game thread. The play stops where it is: every kind of it stays on its frame until its actor's code or another play starts a clip. Its end: `LUMA_E_CANCELLED`, `LUMA_ANIM_STOPPED`. `LUMA_OK`; an ended play: `LUMA_E_HANDLE` (reported); off the game thread: `LUMA_E_THREAD` (reported); while the game takes the mod out: `LUMA_E_CANCELLED`.

### `frame`

```lumascript
anim.frame(play: anim.Play) -> (f32, f32)
```

```c
int32_t frame(const LumaMod* mod, LumaAnimPlay play, float* frame, float* last);
```

Call it on the game thread. The play's frame now into *frame (null: not wanted) and its clip's last frame into *last (null: not wanted). `LUMA_OK`; an ended play: `LUMA_E_HANDLE` (reported); off the game thread: `LUMA_E_THREAD` (reported); while the game takes the mod out: `LUMA_E_CANCELLED`.

## Further reading

* [Calling services](calling-services.md)
* [Services reference](index.md)
* [About services](../../concepts/services.md)
* [Permissions](../../concepts/permissions.md)
