---
title: What the compiler emits
shortTitle: What the compiler emits
intro: A summary of the C code LumaLink writes from LumaScript.
contentType: reference
---

## About the compiler output

This page summarizes what LumaLink builds from LumaScript. For the details, see [The compiled form](compiled-form.md).

- One C file per module, with `#line` markers; one generated `imports.c` holding the `LumaImport` list built from
  every game name used, with signatures from the self-description; the mod's `luma_mod` with `start` and `stop`.
- The run-time support library is linked statically into the mod: containers, text, bytes, scratch,
  handles, tasks, the error report. Freestanding (no C library), the same source on every platform.
- No construct produces code that runs outside the game thread unless a service says it does.

## Further reading

* [LumaScript reference](index.md)
