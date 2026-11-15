---
title: Writing material shaders
shortTitle: Write material shaders
intro: Give a model's material a look of its own with a short HLSL function, with values your code can change while the game runs.
contentType: how-tos
---

## About material shaders

Most materials need no code: a glTF material's colors and maps are enough. For more information, see [Adding models](adding-models.md#materials).

A material shader is for a look that changes with time, position or view, such as moving stripes, a glow at the edges, or water. You write one HLSL function, `surface`, that sets the material's color, normal, roughness, metal, glow and alpha. Nebula does the rest: lighting, shadows and fog work on your material as on the game's own.

You write the shader once. Nebula builds it for every renderer: Direct3D 12, Vulkan and Metal.

## Prerequisites

* LumaLink and an installed Nebula. For more information, see [Installing LumaLink](../get-started/installing-lumalink.md).
* A mod with a model. For more information, see [Adding models](adding-models.md).

## Writing the shader

A shader is two files in the mod's assets, with the same name:

```text
layers/base/assets/shaders/Stripes.shader.json
layers/base/assets/shaders/Stripes.hlsl
```

1. Describe the shader in its `.shader.json` file: its values, its textures, and what it reads.

   ```json
   {
     "stage": "material",
     "params": [
       { "name": "speed", "type": "float", "default": 0.5 },
       { "name": "tint", "type": "float4", "default": [1.0, 0.2, 0.1, 1.0] }
     ],
     "textures": [ { "slot": 0, "name": "paint" } ],
     "reads": [ "time" ]
   }
   ```

1. Write the `surface` function in the `.hlsl` file:

   ```hlsl
   void surface(in SurfaceIn i, inout Surface s) {
       const float band = frac(i.uv0.x * 8.0 + i.time * speed);
       s.base = band < 0.5 ? tint.rgb : s.base * paint(i.uv0).rgb;
       s.roughness = 0.3;
   }
   ```

### The shader's description

| Field | What it holds |
|---|---|
| `stage` | Always `"material"`. |
| `params` | The shader's values. Each has a `name`, a `type` (`float` or `float4`) and a `default`. |
| `textures` | Up to 8 textures. Each has a `slot` from 0 to 7 and a `name`. |
| `reads` | What the shader reads besides texture coordinates and vertex color: `normal`, `world_position`, `view` and `time`. |

### What the function reads

`SurfaceIn i` holds:

| Field | What it is |
|---|---|
| `uv0`, `uv1` | The mesh's first and second texture coordinates. 0 where the mesh has none. |
| `color` | The mesh's vertex color. |
| `normal` | The surface's normal, in view space, unit length. Only if `reads` lists it. |
| `world_position` | The point's position in the world, in game units. Only if `reads` lists it. |
| `view` | The direction toward the camera, in view space, unit length. Only if `reads` lists it. |
| `time` | Time in seconds. Only if `reads` lists it. |

Each value in `params` is a global of its name: `speed` is a `float` and `tint` is a `float4`. Each texture is two functions of its name: `paint(uv)` reads it, and `paint_level(uv, level)` reads one level of its detail.

### What the function sets

`Surface s` starts as the glTF material would look, and the function changes what it needs:

| Field | Starts as |
|---|---|
| `base` | The material's base color factor, times the vertex color if the mesh has one. |
| `alpha` | The base color factor's alpha. |
| `normal` | The surface's normal, turned by the material's normal map if it has one. |
| `roughness`, `metal` | The material's factors, times its metallic-roughness map. |
| `emission` | The material's emissive factor: a glow that light doesn't change. |

Colors are 0 to 1, in the same color space as the game's colors. The glTF base color and emissive textures are not read for a material with a shader. Read what you need through `textures`.

### What a shader can't do

A shader is one function and its helpers. It can't bind anything of its own, so these words are refused, with the line they are on: `register`, `cbuffer`, `tbuffer`, `ConstantBuffer`, texture, sampler and buffer types, `groupshared`, `globallycoherent`, `#include`, `#pragma`, `#line` and `[[`. A shader file is at most 64 KB.

## Using the shader on a material

In your 3D tool, set the material's custom properties (glTF `extras`). Name the shader by its path in `assets/`, without the ending. Give values for its params, and a texture of the glTF file for each of its textures:

```json
"extras": {
  "nebula": {
    "shader": "shaders/Stripes",
    "params": { "speed": 1.0, "tint": [0.1, 0.4, 1.0, 1.0] },
    "textures": { "paint": 0 }
  }
}
```

A param you leave out takes its default. Every texture of the shader must be given. A material with `KHR_materials_unlit` can't use a shader.

Then check the mod:

```shell
lumalink check my-mod
```

LumaLink compiles the shader for Direct3D 12 with `dxc`, and for Vulkan and Metal with `glslang`. Each error names the file and line, and the renderer that refused it. A shader must compile for all of them: some HLSL, such as templates, compiles for Direct3D 12 and not for Vulkan.

## Changing a value while the game runs

Your code changes a param on one model it attached, through the `content.model` service. The value lasts until you set it again, or until an animation that moves that color plays.

```lumascript
use svc.content.model as cm

on stage "EggStarGalaxy" {
    guard let mario = MarioAccess::getPlayerActor() else {
        return
    }
    let panel = cm.attach(mario, model "Panel", cm.Follow.Joint, "HandR", none)
    cm.set_param(panel, "Stripes", "speed", vec4(2, 0, 0, 0))
}
```

`set_param` takes the material's name, the param's name and four numbers. A `float` param reads the first. A material or param the model doesn't have is a line in the mod log: `Content.MissingParam`.

### The range of a value

A param is stored in one of the material's color registers, so its values have a range:

* The first params, in the order the `.shader.json` file lists them, hold -4 to 4.
* The later ones hold 0 to 1.

A value outside its range is held to the range. A material has seven places in all, fewer if the material's own colors use some. More params than fit are refused, by name.

## Troubleshooting

| Message | What to do |
|---|---|
| Line LINE does not compile for RENDERER | Fix the code at that line. The message is the compiler's. |
| The shader was not compiled for RENDERER: no compiler was found | A note: the check found no compiler for that renderer. Install Nebula with Nebula Setup, which brings both. |
| A word the shader may not use | Remove it. Read values through `params` and textures through `textures`. |
| The shader has no surface() | Define `void surface(in SurfaceIn i, inout Surface s)`. |
| The material and the shader disagree | A param or texture of the material isn't in the shader's description, or one the shader needs is missing. |
| More parameters than fit | Use fewer params, or pack several values into one `float4`. |

## Further reading

* [Adding models](adding-models.md)
* [Services](../concepts/services.md)
* [Reading the mod log](reading-the-mod-log.md)
