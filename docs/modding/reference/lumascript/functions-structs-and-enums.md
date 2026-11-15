---
title: Functions, structs and enums
shortTitle: Functions, structs and enums
intro: Declaring functions, structs and enums in LumaScript.
contentType: reference
---

## About functions, structs and enums

This page describes how to declare functions, structs with their own functions, and enums.

## Functions

```lumascript
fn area(w: f32, h: f32) -> f32 { return w * h }
pub fn greet(name: text, times: int = 1) { for i in 0..times { log "hi {name}" } }
```

Parameters are values: a function receives copies (containers are shared until written). A parameter
marked `var` is passed so the function may change the caller's variable: `fn grow(var l: list<int>)`, called
`grow(var mine)`.

## Structs and their functions

```lumascript
struct Ball {
    pos: vec3
    vel: vec3 = vec3
    life: int = 120
    fn step(var self) { self.pos += self.vel; self.life -= 1 }
    fn dead(self) -> bool { return self.life <= 0 }
}
let b = Ball { pos: vec3 }
```

## Enums

```lumascript
enum Phase { Idle, Flying(speed: f32), Done }
```

## Further reading

* [LumaScript reference](index.md)
