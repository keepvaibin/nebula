---
title: draw.light service
shortTitle: draw-light
intro: Add lights to the scene.
contentType: reference
---

## About the draw.light service

The `draw.light` service adds your mod's own lights to the world. A light has a place, a color and an intensity in candela.

| Service | Version | Permission | C import | LumaScript |
|---|---|---|---|---|
| `draw.light` | 1.0 | None | `LUMA_SERVICE(slot, "draw.light", "1.0")` | `use svc.draw.light` |

The header is `native/include/luma_draw_light.h`. In LumaScript, a service is called by the last part of its name, such as `light.` for `draw.light`. Each function below shows its LumaScript form first and its C form second. For how the forms are made, see [Calling services](calling-services.md).

### Details from the header

LumaCore's draw.light service: a mod's own lights in the world. A light is a glowing body at a place, of a color and an intensity in candela. Traced (ray tracing on), it is an emitter like the game's lamps: it lights what it sees, casts shadows where `casts` says, and is seen in reflections. Not traced, it joins the game's own light set as the game's lamps do (a Luma's glow, a fireball's): the game lights with one such lamp at a time, the one nearest the player, blending to it over half a second. A light lives until remove or the mod's end; it is in whatever scene runs, and in none while the game loads one. Import: `LUMA_SERVICE(slot, "draw.light", "1.0")`. Units: the game's world units (centimetres) and axes; color 0 to 1 as the game's own lights give it (sRGB).

## Limits and constants

| Name | Value | Description |
|---|---|---|
| `LUMA_DRAW_LIGHTS_MOST` | `16` | Lights one mod has at once, and all mods together. |
| `LUMA_DRAW_LIGHTS_ALL_MOST` | `256` |  |
| `LUMA_DRAW_LIGHT_CANDELA_MOST` | `5000.0f` | A light's intensity, candela: above 0, at most this (a bright lamp at 1 m; the sun is no draw.light). |
| `LUMA_DRAW_LIGHT_RADIUS_DEFAULT` | `25.0f` | A light's body, world units: 0 takes `LUMA_DRAW_LIGHT_RADIUS_DEFAULT`; else from LEAST to MOST. |
| `LUMA_DRAW_LIGHT_RADIUS_LEAST` | `5.0f` |  |
| `LUMA_DRAW_LIGHT_RADIUS_MOST` | `400.0f` |  |
| `LUMA_DRAW_LIGHT_FAR` | `10000000.0f` | A light's center: each coordinate within this of the world's origin (the game's worlds lie well inside it). |

## Types

| Name | Declaration | Description |
|---|---|---|
| `LumaDrawLight` | `typedef LumaHandle LumaDrawLight;` | A light: owned by the mod until remove or its end. |

## Enumerations

### Other constants

`LumaDrawLightDesc`.flags.

| Name | Description |
|---|---|
| `LUMA_DRAW_LIGHT_CASTS = 1u << 0` | Traced: what it lights is shadowed by what stands between (else it lights through); not traced the game's lamp casts none either way. |

## Structures

### `LumaDrawLightDesc`

| Name | Description |
|---|---|
| `uint32_t size` | Sizeof(`LumaDrawLightDesc`) as the mod was built. |
| `float position[3]` | Its center in the world: each within `LUMA_DRAW_LIGHT_FAR`. |
| `float colour[3]` | 0 to 1 each (sRGB, as the game's lights); all 0 is dark. |
| `float candela` | Its intensity: above 0, at most `LUMA_DRAW_LIGHT_CANDELA_MOST`. |
| `float radius` | Its glowing body's radius: 0 for the default, else LEAST to MOST. |
| `uint32_t flags` | `LUMA_DRAW_LIGHT_`*. |

## `draw.light` functions

In C, each function takes your mod (`const LumaMod* mod`) first. LumaScript passes it for you.

### `add`

```lumascript
light.add(desc: light.Desc) -> light.Light
```

```c
int32_t add(const LumaMod* mod, const LumaDrawLightDesc* desc, LumaDrawLight* out);
```

Call it on the game thread. A light as `desc` says, lit from the next frame the game draws. It returns `LUMA_OK` and sets `*out`. A null desc or out, a value outside its range or not finite (the argument named: position, color, candela, radius, flags: a bit this version does not know): `LUMA_E_ARGUMENT` (reported); a desc's size under sizeof(`LumaDrawLightDesc`) 1.0: `LUMA_E_ARGUMENT`, `Service.StructSize` in the mod log (a larger one, a later version's, is read up to 1.0's fields); `LUMA_DRAW_LIGHTS_MOST` of the mod's, or `LUMA_DRAW_LIGHTS_ALL_MOST` of all mods': `LUMA_E_LIMIT` (reported). Off the game thread: `LUMA_E_THREAD` (reported); while the game takes the mod out: `LUMA_E_CANCELLED`.

### `move`

```lumascript
light.move(light: light.Light, position: vec3)
```

```c
int32_t move(const LumaMod* mod, LumaDrawLight light, const float position[3]);
```

Call it on the game thread. The light's center to `position` (3 floats, each within `LUMA_DRAW_LIGHT_FAR`), from the next frame drawn. `LUMA_OK`; an ended light or another mod's: `LUMA_E_HANDLE` (reported); null, not finite or farther: `LUMA_E_ARGUMENT` (reported). Thread and taking out as add.

### `set`

```lumascript
light.set(light: light.Light, desc: light.Desc)
```

```c
int32_t set(const LumaMod* mod, LumaDrawLight light, const LumaDrawLightDesc* desc);
```

Call it on the game thread. Everything of the light as `desc` says (its handle stays). `LUMA_OK`; answers as add, and as move for the handle.

### `remove`

```lumascript
light.remove(light: light.Light)
```

```c
int32_t remove(const LumaMod* mod, LumaDrawLight light);
```

Call it on the game thread. The light is gone from the next frame drawn (not traced, the game's lamp fades out from it as from any lamp that goes). `LUMA_OK`; an ended light or another mod's: `LUMA_E_HANDLE` (reported). Thread and taking out as add.

## Further reading

* [Calling services](calling-services.md)
* [Services reference](index.md)
* [About services](../../concepts/services.md)
* [Permissions](../../concepts/permissions.md)
