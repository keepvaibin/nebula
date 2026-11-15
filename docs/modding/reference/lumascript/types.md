---
title: Types
shortTitle: Types
intro: The types of LumaScript values.
contentType: reference
---

## About types

This page lists the types of LumaScript values.

| Type | What | In C |
|---|---|---|
| `bool` | | `uint8_t` 0/1 |
| `i8 i16 i32 i64 u8 u16 u32 u64` | integers; `int` is `i32` | the same |
| `f32 f64`; `float` is `f32` (the game's) | | the same |
| `vec2 vec3 vec4 quat` | `f32` components `x y z w`; `vec3` is laid out as the game's `TVec3f`/`Vec3f` | struct of `float` |
| `mat34` | the game's 3x4 matrix (`TPos3f`, `Mtx`) | `float[3][4]` |
| `color` | RGBA, `f32` 0..1 each; from `#RRGGBB(AA)` | struct |
| `text` | immutable UTF-8; `len` in bytes, `chars` by code point | runtime handle |
| `bytes` | an owned byte buffer | runtime handle |
| `list<T>` `map<K, V>` `set<T>` | owned containers; `K` is an integer, `text`, `bool` or an enum without data | runtime handles |
| `T?` | `T` or `none` | tagged struct |
| `(A, B, ...)` | a tuple: values by position, `.0`, `.1`; `let (a, b) = pair` | C struct |
| `struct S { ... }` | named fields, values | C struct |
| `enum E { A, B(int), C { x: f32 } }` | a choice, each case with or without data | tagged union |
| `fn(A, B) -> R` | a function, or a closure | function pointer + owned environment |
| `ref<C>` | a handle to a game object of class `C` (or a mod's own derived class): empty once the object is gone | generation-checked handle |
| game classes `C` | a view of a live game object, valid during the statement or hook that has it | pointer, not storable |
| game enums, game structs by value | as the self-description gives them | the game's own layout |

- Inference. `let x = 3` is `int`; `let v = vec3`; a function's parameter and result types are always
  written, its locals never need to be. An integer literal takes the type its place needs if it fits, else is an
  error naming the type and the value.
- Conversions are written. `x as f32`, `n as u8` (an integer that does not fit is a script error; `wrap(n as
  u8)` keeps the low bits on purpose). No implicit conversion between integer and float, or between sizes, except
  that a literal adapts as above.
- Equality and order. `==` on every type but functions, views and service handles (for those, `same(a, b)`:
  the same object or the same stream); `<` on numbers and `text`.
- Operators. `+ - * / %` (integer: overflow and division by zero are script errors; float: IEEE), `color * f32`
  and `color + color` (each channel, kept in 0..1), `and or not`,
  `& | ^ << >>` on integers, `vec * float`, `vec + vec`, `dot cross length normal` as functions, `x?.y` (none when x
  is none), `x ?? y` (y when x is none), `a..b` and `a..=b` ranges.

## Further reading

* [LumaScript reference](index.md)
