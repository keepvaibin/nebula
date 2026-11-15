---
title: draw.post service
shortTitle: draw-post
intro: Run full-screen pixel programs over the frame.
contentType: reference
---

## About the draw.post service

The `draw.post` service runs your mod's full-screen pixel programs over the picture. You write a program in `assets/post/<name>.hlsl` with a `post/<name>.json`, and LumaLink cooks it into `<name>.post`.

| Service | Version | Permission | C import | LumaScript |
|---|---|---|---|---|
| `draw.post` | 1.0 | None | `LUMA_SERVICE(slot, "draw.post", "1.0")` | `use svc.draw.post` |

The header is `native/include/luma_draw_post.h`. In LumaScript, a service is called by the last part of its name, such as `post.` for `draw.post`. Each function below shows its LumaScript form first and its C form second. For how the forms are made, see [Calling services](calling-services.md).

### Details from the header

LumaCore's draw.post service: a mod's full-screen pixel programs over the frame. Import: `LUMA_SERVICE(slot, "draw.post", "1.0")`. No permission. A post program is cooked from the mod's assets/post/<name>.hlsl and post/<name>.json into <name>.post (LumaLink's `post` kind). The file defines `float4 post(PostIn i)`; it reads the frame so far with scene(uv) and scene_texel(pixel), and, when its json `reads` lists them, depth(uv) (the distance from the eye along the view axis, game units) and motion(uv) (where that surface was the frame before, minus uv). Its parameters are globals of their names, set from the stage's params (16 floats). Stages: WORLD, after the 3-D scene and the game's bloom, before 2-D; FRAME, after the HUD, before the present point. A mod has one program in each (its slot). In a stage the mods' programs run in Mods-list order, bottom first, each reading the one before. Every picture the host draws runs them, interpolated pictures too.

## Limits and constants

| Name | Value | Description |
|---|---|---|
| `LUMA_DRAW_POST_PROGRAMS_MOST` | `8` | Programs one mod holds at once. |
| `LUMA_DRAW_POST_PARAMS_MOST` | `16` | A stage's parameter floats (four rows of four). |
| `LUMA_DRAW_POST_STAGES` | `2` |  |
| `LUMA_DRAW_POST_FILE_MAGIC` | `"NBPOST01"` | <name>.post in a mod's store, written by LumaLink's cook: little-endian, unaligned. `LUMA_DRAW_POST_FILE_MAGIC`, then u32 version (1), u32 reads (`LUMA_DRAW_POST_READS_`*), u32 param count, u32 text bytes, then each param (`LUMA_DRAW_POST_PARAM_BYTES`: char name[32] NUL-padded, u32 floats 1 to 4, u32 row 0 to 3, u32 column 0 to 3, float default[4]), then the text (UTF-8, the part LumaLink writes: the params mapped onto post_params and the mod's code; the host adds its prelude and entry). |
| `LUMA_DRAW_POST_FILE_VERSION` | `1` |  |
| `LUMA_DRAW_POST_HEADER_BYTES` | `24` |  |
| `LUMA_DRAW_POST_PARAM_BYTES` | `60` |  |
| `LUMA_DRAW_POST_READS_DEPTH` | `1` |  |
| `LUMA_DRAW_POST_READS_MOTION` | `2` |  |

## Types

| Name | Declaration | Description |
|---|---|---|
| `LumaDrawPostProgram` | `typedef LumaHandle LumaDrawPostProgram;` | A program: the mod's until program_free or its end. |

## Enumerations

### `LumaDrawPostStage`

| Name | Description |
|---|---|
| `LUMA_DRAW_POST_WORLD = 0` | After the 3-D scene and the game's bloom, before 2-D. |
| `LUMA_DRAW_POST_FRAME = 1` | After the HUD (draw point hud.after), before the present point. |

## `draw.post` functions

In C, each function takes your mod (`const LumaMod* mod`) first. LumaScript passes it for you.

### `program`

```lumascript
post.program(name: text) -> post.Program?
```

```c
int32_t program(const LumaMod* mod, const char* name, LumaDrawPostProgram* out);
```

Call it on the game thread. The mod's cooked post program `name` (<name>.post in its store), compiled by the host for this device. It returns `LUMA_OK` and sets `*out`. A null or empty name or out: `LUMA_E_ARGUMENT` (reported); no such program: `LUMA_E_NOT_FOUND`, `Draw.Post.Missing` (540); the device's compiler refused it: `LUMA_E_ARGUMENT`, `Draw.Post.Compile` (541) with the compiler's first error; it reads motion and this frame has no motion vectors (temporal upscaling off): `LUMA_E_ABSENT`, `Draw.Post.NoMotion` (542); `LUMA_DRAW_POST_PROGRAMS_MOST` held: `LUMA_E_LIMIT` (reported).

### `set`

```lumascript
post.set(stage: post.Stage, program: post.Program)
```

```c
int32_t set(const LumaMod* mod, LumaDrawPostStage stage, LumaDrawPostProgram program);
```

Call it on the game thread. The mod's slot in `stage` runs `program` from the next frame on (0: the slot empty). Its params are the program's defaults until `params`. `LUMA_OK`. A stage past FRAME: `LUMA_E_ARGUMENT` (reported); a program that is not the mod's: `LUMA_E_HANDLE` (reported).

### `params`

```lumascript
post.params(stage: post.Stage, values: list<f32>) -> bool
```

```c
int32_t params(const LumaMod* mod, LumaDrawPostStage stage, const float* values, uint32_t count);
```

Call it on the game thread. The slot's parameters from the next frame on: `count` floats (0 to `LUMA_DRAW_POST_PARAMS_MOST`) in the program's params order (four rows of four, a param never crossing a row); the rest keep theirs. `LUMA_OK`. A count past 16, a null values with a count, a value not finite: `LUMA_E_ARGUMENT` (reported); an empty slot: `LUMA_E_STATE`, `Draw.Post.Empty` (543).

### `program_free`

```lumascript
post.program_free(program: post.Program)
```

```c
int32_t program_free(const LumaMod* mod, LumaDrawPostProgram program);
```

Call it on the game thread. The program freed (a slot running it empties). `LUMA_OK`; a program that is not the mod's: `LUMA_E_HANDLE` (reported).

## Further reading

* [Calling services](calling-services.md)
* [Services reference](index.md)
* [About services](../../concepts/services.md)
* [Permissions](../../concepts/permissions.md)
