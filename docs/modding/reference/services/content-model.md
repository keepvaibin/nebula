---
title: content.model service
shortTitle: content-model
intro: Attach models to actors, change their materials, and show or hide their parts.
contentType: reference
---

## About the content.model service

The `content.model` service puts your mod's cooked models on actors and changes models' materials and parts. For how models are made, see [Adding models](../../how-tos/adding-models.md).

| Service | Version | Permission | C import | LumaScript |
|---|---|---|---|---|
| `content.model` | 1.1 | None | `LUMA_SERVICE(slot, "content.model", "1.1")` | `use svc.content.model` |

The header is `native/include/luma_content_model.h`. In LumaScript, a service is called by the last part of its name, such as `model.` for `content.model`. Each function below shows its LumaScript form first and its C form second. For how the forms are made, see [Calling services](calling-services.md).

### Details from the header

LumaCore's content.model service: a model drawn with an actor of the game, following its base, one of its joints, or its whole skeleton by joint name; shown, hidden, detached; the player's parts listed. Import: `LUMA_SERVICE(slot, "content.model", "1.0")`.

## Limits and constants

| Name | Value | Description |
|---|---|---|
| `LUMA_CONTENT_MODEL_MOST` | `64` | Models one mod has attached and not yet detached, at once. |

## Types

| Name | Declaration | Description |
|---|---|---|
| `LumaContentModel` | `typedef LumaHandle LumaContentModel;` | A model a mod attached: owned by the mod, ended by detach, by its actor's death, by its scene's end, or by the mod's. |

## Enumerations

### `LumaContentModelFollow`

What an attached model follows.

| Name | Description |
|---|---|
| `LUMA_CONTENT_MODEL_ACTOR = 0` | The actor's base (its position, rotation and scale). |
| `LUMA_CONTENT_MODEL_JOINT = 1` | One joint (or socket) of the actor, named by `joint`. |
| `LUMA_CONTENT_MODEL_SKELETON = 2` | Each joint of the model takes the actor's joint of its name, every frame; a joint with no such name on the actor moves with its own parent. |

## Structures

### `LumaPlayerPart`

One of the player's parts: a model of its own the game draws at a joint of the body.

Its size in version 1.0 is 24 bytes.

| Name | Description |
|---|---|
| `uint32_t size` | Sizeof(`LumaPlayerPart`) as the mod was built. |
| `const char* name` | "MarioHandR": the model, ObjectData/<name>.arc. |
| `const char* joint` | The body's joint it is drawn at ("HandR"); "" for one drawn at the base. |

## `content.model` functions

In C, each function takes your mod (`const LumaMod* mod`) first. LumaScript passes it for you.

### `attach`

```lumascript
model.attach(actor: LiveActor, model: model, follow: model.Follow, joint: text?, place: content.Place?) -> model.Model
```

```c
int32_t attach(const LumaMod* mod, LumaActor actor, LumaModelName model, LumaContentModelFollow follow, const char* joint, const LumaPlace* place, LumaContentModel* out);
```

Call it on the game thread. The model `model` drawn with `actor`, following as `follow` says: the joint `joint` (null: the actor's base) for `LUMA_CONTENT_MODEL_JOINT`, offset and turned by `place` (null: none; ignored by `LUMA_CONTENT_MODEL_SKELETON`). It is drawn as the game draws its own models (the game's lights, shadows and draw order), hidden while the actor is hidden or clipped. It returns `LUMA_OK` and sets `*out`. A model in no store: the placeholder model, `LUMA_OK`, and `Content.MissingModel` in the mod log; a joint the actor's model lacks: its base, `LUMA_OK`, and `Content.MissingJoint`. A null or dead actor, an actor that is not one or has no model, an empty name, a follow outside the enum, a null out: `LUMA_E_ARGUMENT` (reported); a place below 1.0's size: `LUMA_E_ARGUMENT` (reported) (`Service.StructSize`); `LUMA_CONTENT_MODEL_MOST` attached: `LUMA_E_LIMIT` (reported). Off the game thread: `LUMA_E_THREAD` (reported); while the game takes the mod out: `LUMA_E_CANCELLED`.

### `detach`

```lumascript
model.detach(model: model.Model)
```

```c
int32_t detach(const LumaMod* mod, LumaContentModel model);
```

Call it on the game thread. Stops drawing it; the handle ends. `LUMA_OK`; an ended handle: `LUMA_E_HANDLE` (reported); off the game thread: `LUMA_E_THREAD` (reported); while the game takes the mod out: `LUMA_E_CANCELLED`.

### `show`

```lumascript
model.show(model: model.Model, on: bool)
```

```c
int32_t show(const LumaMod* mod, LumaContentModel model, bool on);
```

Call it on the game thread. Shown (`on` true) or hidden (false); hidden with its actor as well. `LUMA_OK`; an ended handle: `LUMA_E_HANDLE` (reported); off the game thread: `LUMA_E_THREAD` (reported); while the game takes the mod out: `LUMA_E_CANCELLED`.

### `player_parts`

```c
int32_t player_parts(const LumaMod* mod, LumaPlayerPart* parts, uint32_t count, uint32_t* total);
```

Not callable from LumaScript.

You can call it on any thread. The player's parts, the first `count` into `parts` (each one's size set by the mod; null when count is 0), and their number in *total (null: not wanted). `LUMA_OK`; a part's size below 1.0's, or a null parts with a count: `LUMA_E_ARGUMENT` (reported) (`Service.StructSize`).

### `set_param`

```lumascript
model.set_param(model: model.Model, material: text, param: text, rgba: vec4) -> bool
```

```c
int32_t set_param(const LumaMod* mod, LumaContentModel model, const char* material, const char* param, const float* rgba);
```

Needs `content.model` 1.1 or later.

Call it on the game thread. A parameter of a material shader: the attached model's material `material` takes `rgba` (four floats; a float parameter reads the first) as its parameter `param`, on this model only, until it is set again or a clip that moves that color plays. The cook names each parameter's place in <model>.params beside the model: places 0 to 2 hold -4 to 4, places 3 to 6 hold 0 to 1, both in 255ths; a value outside is held to them. `LUMA_OK`. A material or parameter the model's parameters lack, or a model that shows nothing: `LUMA_E_NOT_FOUND`, `Content.MissingParam` in the mod log. An ended handle: `LUMA_E_HANDLE` (reported); a null material, param or rgba, or a value that is not a number: `LUMA_E_ARGUMENT` (reported). Off the game thread: `LUMA_E_THREAD` (reported); while the game takes the mod out: `LUMA_E_CANCELLED`.

## Further reading

* [Calling services](calling-services.md)
* [Services reference](index.md)
* [About services](../../concepts/services.md)
* [Permissions](../../concepts/permissions.md)
