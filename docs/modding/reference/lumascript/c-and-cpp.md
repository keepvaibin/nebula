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
export c fn ball_count -> i32 { return balls.len as i32 }
```

- Types across: integers, floats, `bool`, `vec*`, `mat34`, `color`, structs marked `c struct` (C layout), `text` and
  `bytes` as (pointer, length) for the call's duration, and `ref<C>` as an opaque handle with C functions to get the
  object (`lumart_ref_get`). No container crosses.
- The C side links the same `luma.h` and may hook, call and read by name itself; its faults go to the guard.

## Further reading

* [LumaScript reference](index.md)
