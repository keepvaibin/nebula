---
title: draw.inst service
shortTitle: draw-inst
intro: Change how one actor's model draws, without changing other actors of the same model.
contentType: reference
---

## About the draw.inst service

The `draw.inst` service puts overrides on one actor's drawn model: a color over every material, one material's color, a hidden part, or a scale. Other actors of the same model draw as the game made them.

| Service | Version | Permission | C import | LumaScript |
|---|---|---|---|---|
| `draw.inst` | 1.0 | None | `LUMA_SERVICE(slot, "draw.inst", "1.0")` | `use svc.draw.inst` |

The header is `native/include/luma_draw_inst.h`. In LumaScript, a service is called by the last part of its name, such as `inst.` for `draw.inst`. Each function below shows its LumaScript form first and its C form second. For how the forms are made, see [Calling services](calling-services.md).

### Details from the header

LumaCore's draw.inst service: per-instance overrides on an actor's drawn model, seen on that one actor and on no other of its model. A color over every material, a material's color by name, a part hidden, a scale: the Gold Flower's gold Mario is this (a color on Mario), not a feature. An override lives until release, the mod's end, the actor's death or its scene's end; then the model draws as the game made it again. Import: `LUMA_SERVICE(slot, "draw.inst", "1.0")`. The actor's own model (the one its game code draws: MR::getModel), not its attached parts (content.model has its own). Applied each frame after the game's own animation of the model (color animations, poses), before its draw.

## Limits and constants

| Name | Value | Description |
|---|---|---|
| `LUMA_DRAW_INSTS_MOST` | `64` | Overrides one mod has at once, and all mods together. One actor has at most one of each mod. |
| `LUMA_DRAW_INSTS_ALL_MOST` | `512` |  |
| `LUMA_DRAW_INST_SCALE_MOST` | `100.0f` | A scale's component: above 0, at most this. |
| `LUMA_DRAW_INST_PARAM_MATERIAL0` | `"material0"` | Set_material_param's names: a material's colors as its data has them (GX's): the material and ambient colors of channels 0 and 1, the four constant colors (0 to 1 each, RGBA), and the four color registers (-4 to 4 each). |
| `LUMA_DRAW_INST_PARAM_MATERIAL1` | `"material1"` |  |
| `LUMA_DRAW_INST_PARAM_AMBIENT0` | `"ambient0"` |  |
| `LUMA_DRAW_INST_PARAM_AMBIENT1` | `"ambient1"` |  |

## Types

| Name | Declaration | Description |
|---|---|---|
| `LumaDrawInst` | `typedef LumaHandle LumaDrawInst;` | An override of one actor's model: owned by the mod until release or its end. |

## `draw.inst` functions

"constant0".. "constant3", "register0".. "register3" alike.

In C, each function takes your mod (`const LumaMod* mod`) first. LumaScript passes it for you.

### `of`

```lumascript
inst.of(actor: LiveActor) -> inst.Inst
```

```c
int32_t of(const LumaMod* mod, LumaActor actor, LumaDrawInst* out);
```

Call it on the game thread. The override of `actor`'s model for this mod: a new one, or the one the mod has on it already (the same handle). Nothing changes until a set_ call. It returns `LUMA_OK` and sets `*out`. A null, dead or non-actor `actor`, one with no model, a null out: `LUMA_E_ARGUMENT` (reported); `LUMA_DRAW_INSTS_MOST` of the mod's or ALL_MOST of all: `LUMA_E_LIMIT` (reported). Off the game thread: `LUMA_E_THREAD`; while the game takes the mod out: `LUMA_E_CANCELLED`.

### `set_colour`

```lumascript
inst.set_colour(inst: inst.Inst, colour: color)
```

```c
int32_t set_colour(const LumaMod* mod, LumaDrawInst inst, LumaColor colour);
```

Call it on the game thread. Every material's colors times `colour` (0xRRGGBBAA; 0xFFFFFFFF: as the game draws them): the material and ambient colors of both channels, the constant colors and the color registers, alpha with them. `LUMA_OK`; an ended override or another mod's: `LUMA_E_HANDLE` (reported). Thread and taking out as of.

### `set_material_param`

```lumascript
inst.set_material_param(inst: inst.Inst, material: text?, name: text, value: vec4) -> bool
```

```c
int32_t set_material_param(const LumaMod* mod, LumaDrawInst inst, const char* material, const char* name, const float value[4]);
```

Call it on the game thread. The color `name` (`LUMA_DRAW_INST_PARAM_`*) of the material named `material` (the model data's name; null: every material) set to `value` (4 floats, RGBA), in place of the game's, from the next frame; set_colour multiplies over it. A name not of the list, a value out of its range or not finite: `LUMA_E_ARGUMENT` (reported); no material of that name: `LUMA_E_NOT_FOUND` (reported). Handle as set_colour.

### `set_texture`

```lumascript
inst.set_texture(inst: inst.Inst, material: text, map: u32, texture: draw.Texture) -> bool
```

```c
int32_t set_texture(const LumaMod* mod, LumaDrawInst inst, const char* material, uint32_t map, LumaDrawTexture texture);
```

Call it on the game thread. Not in 1.0's game: a texture of the draw service on a material's map. `LUMA_E_ABSENT` after the handle's checks (the call is the contract's; a later 1.x fills it).

### `set_visible`

```lumascript
inst.set_visible(inst: inst.Inst, part: text, on: i32) -> bool
```

```c
int32_t set_visible(const LumaMod* mod, LumaDrawInst inst, const char* part, int32_t on);
```

Call it on the game thread. The joint named `part` and every joint under it shown (`on` 1) or hidden (0); null `part`: the whole model. Hidden by the mod is hidden whatever the game shows; shown gives the game its own choice back. No joint of that name: `LUMA_E_NOT_FOUND` (reported). Handle as set_colour.

### `set_scale`

```lumascript
inst.set_scale(inst: inst.Inst, scale: vec3)
```

```c
int32_t set_scale(const LumaMod* mod, LumaDrawInst inst, const float scale[3]);
```

Call it on the game thread. The drawn model scaled by `scale` (3 floats, x y z of the actor's own axes, above 0 and at most `LUMA_DRAW_INST_SCALE_MOST`) about the actor's base, after the game's own scale; 1, 1, 1: as the game. Only the picture: the actor's collision and logic stay the game's. Out of range: `LUMA_E_ARGUMENT` (reported). Handle as set_colour.

### `release`

```lumascript
inst.release(inst: inst.Inst)
```

```c
int32_t release(const LumaMod* mod, LumaDrawInst inst);
```

Call it on the game thread. The override ended: the model draws as the game made it from the next frame. `LUMA_OK`; an ended override or another mod's: `LUMA_E_HANDLE` (reported).

## Further reading

* [Calling services](calling-services.md)
* [Services reference](index.md)
* [About services](../../concepts/services.md)
* [Permissions](../../concepts/permissions.md)
