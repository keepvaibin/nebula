---
title: C and C++ in the same mod
shortTitle: C and C++ in the same mod
intro: Mixing LumaScript with C and C++ code.
contentType: reference
---

## About C and C++ in the same mod

A mod can mix LumaScript with C and C++ code. This page lists how the two call each other and which values cross between them.

```lumascript
use c "fastmath.h"                         // declarations from the mod's source/ C or C++ files
extern c fn fast_noise(x: f32, y: f32) -> f32
var balls: list<vec3> = []
export c fn ball_count() -> i32 { return balls.len() as i32 }

c struct Spot { at: vec3, radius: f32, lit: bool }   // C's layout, under its own name, in lm_exports.h
extern c fn spot_glow(s: Spot, from: vec3) -> color
```

The C side of that mod, in its `source/` folder:

<!-- sample file: source/fastmath.h -->
```c
#include "lm_exports.h"   /* Spot, lm_vec3, lm_color, ball_count */

float fast_noise(float x, float y);
lm_color spot_glow(Spot s, lm_vec3 from);
```

<!-- sample file: source/fastmath.c -->
```c
#include "fastmath.h"

/* Noise from 0 to 1 at (x, y): a hash of the whole cell, with no table and no maths library. */
float fast_noise(float x, float y) {
    unsigned int h = (unsigned int)(int)x * 374761393u + (unsigned int)(int)y * 668265263u;
    h = (h ^ (h >> 13)) * 1274126177u;
    return (float)(h ^ (h >> 16)) / 4294967296.0f;
}

/* The spot's light, seen from `from`: none when the spot is off or `from` is outside it. */
lm_color spot_glow(Spot s, lm_vec3 from) {
    float dx = s.at.x - from.x, dy = s.at.y - from.y, dz = s.at.z - from.z;
    int inside = dx * dx + dy * dy + dz * dz < s.radius * s.radius;
    lm_color c = {1.0f, 0.9f, 0.5f, s.lit && inside ? 1.0f : 0.0f};
    return c;
}
```

- Types across, by value: integers, floats, `bool`, `vec2` to `vec4`, `quat`, `mat34` and `color` (`lumart.h`'s
  `lm_vec3` and the rest), a `c struct` (the C struct of its name, in `lm_exports.h`; its fields any of these),
  and `ref<C>` (a `lumart_ref`, which C may keep; `lm_ref_get_C(r)` gives the object, or null once it is gone, on
  the game thread). `text`, `bytes` and a `list` of any of the by-value types cross as (pointer, length) for the
  call's duration (`const int32_t*, uint64_t`; a `list<bool>` as `const bool*`); an `export c fn` gets its own
  copy. A result is any of the by-value types. A map, a set and a list of texts or lists do not cross: LumaLink
  says S0800.
- A `var` parameter of a by-value type crosses as a pointer to the variable (`int32_t*`, `lm_vec3*`, `bool*`). An
  `extern c fn` writes the script's variable (called `f(var n)`); an `export c fn` writes the C's, and a null
  pointer runs nothing and gives the result's zero.
- The C side links the same `luma.h` and may hook, call and read by name itself; its faults go to the guard.

## Further reading

* [LumaScript reference](index.md)
