---
title: Closures
shortTitle: Closures
intro: Functions as values in LumaScript.
contentType: reference
---

## About closures

A closure is a function written as a value. You pass closures to services and LumaKit to receive events and answers.

`fn(x: int) -> int { return x + step }` makes a closure; it copies what it uses from around it when made (it
never refers to a local that may end). A closure may be stored and called later. It may not capture a view of a
game object; it captures a `ref` instead.

## Further reading

* [LumaScript reference](index.md)
