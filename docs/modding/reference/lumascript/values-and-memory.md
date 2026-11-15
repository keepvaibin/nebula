---
title: Values, views and memory
shortTitle: Values, views and memory
intro: How LumaScript holds game objects, handles, values, scratch memory and module state.
contentType: reference
---

## About values, views and memory

This page describes how LumaScript holds game objects, handles, values and containers, temporary scratch memory, and module state.

## Views of game objects

A value of a game class type (`MarioActor`, `Kuribo`, `LiveActor`) is a view: it reaches the object's fields and
functions, and it is valid only while the code that received it runs (a hook's body, a statement holding a game
call's result). The compiler refuses to store a view in a variable that outlives that (a `var` at module level, a
struct field, a container, a closure, a task across a `wait`), naming the line. To keep an object, take a handle:
`let h = ref(self)`.

## Handles

`ref<C>` names an object and becomes empty when the object is gone. Using it gives a view or `none`:

```lumascript
var target: ref<Kuribo>? = none
...
if let k = target?.get { k.mVelocity = vec3 }   // nothing when it is gone
```

`get` on a handle known not to be empty (`target!.get`) is a script error if it is empty. The run-time support
learns that an object is gone from the destructor rows of its class (kernel: `LUMA_TELL` on `NameObj::~NameObj` and
every derived destructor); there is no list kept by the game for it, and no cost when a mod holds no handle.

## Values and containers

Every value is copied when assigned or passed. Containers and `text` and `bytes` are shared underneath until one
copy is written (copy on write; counted, never cyclic, since a container holds values and never refers to another
container), so copying is cheap and never visible. They are freed when the last copy ends. Nothing in the language
can make a cycle, so nothing leaks and nothing waits for a collector.

## Scratch

Text made only to hand to the game for one call (a `const char *` argument, a log line) is made in the frame's
scratch area, which the run-time support empties at the end of each frame. The script never sees this; it is how
the compiler avoids an allocation per call.

## Module state

`var` and `let` at the top of a module live for the mod's whole life (from `on start` until the mod is stopped).
`let` at the top must be a constant expression or the result of `on start`.

## Further reading

* [LumaScript reference](index.md)
