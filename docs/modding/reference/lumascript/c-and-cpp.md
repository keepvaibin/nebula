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
```

The C side of that mod, in its `source/` folder:

<!-- sample file: source/fastmath.h -->
```c
float fast_noise(float x, float y);
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
```

- Types across: integers, floats and `bool`, and `text` and `bytes` as (pointer, length) for the call's duration.
  A result is a number or a `bool`. Vectors, `color`, structs (`c struct` too), `ref<C>` and containers do not
  cross yet: LumaLink says S0800.
- The C side links the same `luma.h` and may hook, call and read by name itself; its faults go to the guard.

## Further reading

* [LumaScript reference](index.md)
