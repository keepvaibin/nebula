---
title: content.fx service
shortTitle: content-fx
intro: Play particle effects on actors and in the world.
contentType: reference
---

## About the content.fx service

The `content.fx` service plays your mod's cooked effects and the game's. For how effects are made, see [Adding particle effects](../../how-tos/adding-particle-effects.md). The shared types `LumaActor`, `LumaPlace`, `LumaEffectName` and `LumaModelName` are in `luma_content.h`: an actor is valid while its scene lasts, and a place is an offset in game units and a turn in degrees in a joint's space.

| Service | Version | Permission | C import | LumaScript |
|---|---|---|---|---|
| `content.fx` | 1.0 | None | `LUMA_SERVICE(slot, "content.fx", "1.0")` | `use svc.content.fx` |

The header is `native/include/luma_content_fx.h`. In LumaScript, a service is called by the last part of its name, such as `fx.` for `content.fx`. Each function below shows its LumaScript form first and its C form second. For how the forms are made, see [Calling services](calling-services.md).

### Details from the header

LumaCore's content.fx service: a mod's particle effects, or the game's, started on an actor or a model the mod attached, stopped and tinted. Import: `LUMA_SERVICE(slot, "content.fx", "1.0")`.

## Limits and constants

| Name | Value | Description |
|---|---|---|
| `LUMA_CONTENT_FX_MOST` | `256` | Effects one mod has started and not yet ended, at once. |

## Types

| Name | Declaration | Description |
|---|---|---|
| `LumaContentFxEffect` | `typedef LumaHandle LumaContentFxEffect;` | An effect a mod started: owned by the mod, ended by stop, by its actor's death or scene's end, or by the mod's. |

## `content.fx` functions

In C, each function takes your mod (`const LumaMod* mod`) first. LumaScript passes it for you.

### `start`

```lumascript
fx.start(actor: LiveActor, name: effect, joint: text?, place: content.Place?, follow: bool) -> fx.Effect
```

```c
int32_t start(const LumaMod* mod, LumaActor actor, LumaEffectName name, const char* joint, const LumaPlace* place, bool follow, LumaContentFxEffect* out);
```

Call it on the game thread. The effect `name` on `actor`: at the joint `joint` (null: the actor's base), offset and turned by `place` (null: none), followed each frame (`follow` true) or left where it started (false). It returns `LUMA_OK` and sets `*out`. A name in no store: the placeholder effect, `LUMA_OK`, and `Content.MissingEffect` in the mod log; a joint the actor's model lacks: the base, `LUMA_OK`, and `Content.MissingJoint`. A null or dead actor, an actor that is not one, an empty name, a null out: `LUMA_E_ARGUMENT` (reported); a place below 1.0's size: `LUMA_E_ARGUMENT` (reported) (`Service.StructSize`); `LUMA_CONTENT_FX_MOST` open: `LUMA_E_LIMIT` (reported). Off the game thread: `LUMA_E_THREAD` (reported); while the game takes the mod out: `LUMA_E_CANCELLED`.

### `start_on_model`

```lumascript
fx.start_on_model(model: content.model.Model, name: effect, socket: text?, place: content.Place?, follow: bool) -> fx.Effect
```

```c
int32_t start_on_model(const LumaMod* mod, LumaContentModel model, LumaEffectName name, const char* socket, const LumaPlace* place, bool follow, LumaContentFxEffect* out);
```

Call it on the game thread. As start, on a model the mod attached at its joint or socket `socket` (null: the model's root), offset and turned by `place` (null: none): `LUMA_OK` and `*out`, `LUMA_E_ARGUMENT`, `LUMA_E_LIMIT`, `LUMA_E_THREAD` and `LUMA_E_CANCELLED` as start; an unknown socket: the root, `LUMA_OK`, and `Content.MissingJoint`; an ended model handle: `LUMA_E_HANDLE` (reported). The effect ends ("model") when the model's handle ends.

### `stop`

```lumascript
fx.stop(effect: fx.Effect, now: bool)
```

```c
int32_t stop(const LumaMod* mod, LumaContentFxEffect effect, bool now);
```

Call it on the game thread. Stops making particles: those alive fade as the effect says (`now` false), or all go at once (true). The handle ends. `LUMA_OK`; an ended handle: `LUMA_E_HANDLE` (reported); off the game thread: `LUMA_E_THREAD` (reported); while the game takes the mod out: `LUMA_E_CANCELLED`.

### `tint`

```lumascript
fx.tint(effect: fx.Effect, scale: f32, rgba: color)
```

```c
int32_t tint(const LumaMod* mod, LumaContentFxEffect effect, float scale, LumaColor rgba);
```

Call it on the game thread. Scale and color on top of the effect's own (1 and 0xFFFFFFFF: none), as the game's setEffectBaseScale and setEffectPrmColor. `LUMA_OK`; a scale not above 0 or not finite: `LUMA_E_ARGUMENT` (reported); an ended handle: `LUMA_E_HANDLE` (reported); off the game thread: `LUMA_E_THREAD` (reported); while the game takes the mod out: `LUMA_E_CANCELLED`.

## Further reading

* [Calling services](calling-services.md)
* [Services reference](index.md)
* [About services](../../concepts/services.md)
* [Permissions](../../concepts/permissions.md)
