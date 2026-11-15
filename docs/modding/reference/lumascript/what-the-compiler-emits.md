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

| To | What the language needs |
|---|---|
| kernel / luma.h | A way to report a script error that the on-screen mod log shows with its file and line (`LumaCore.report(mod, file, line, code, text)` or a `mods` service call); destructor rows on every class (done: `LUMA_TELL`); a mod with no imports |
| self-description | Overloads, `const`, which classes are derivable, which functions are virtual and their overrides (B3), enums and their values, game structs' layouts, constant data names, per version |
| `time` | A frame event at the frame's start, game time that stops when the game pauses, the load and pause state |
| `mods` | The mod's options and their change events |
| `K.derive` | Fields stored beside a derived object; overrides as hooks limited to the class |
| `luma.json` | `source/` layout (`main.lum`), options declared once in `luma.json` and read by the compiler |
| validator | The same checks of option names, content names and permissions the compiler makes, when a `.luma` is installed |

## Further reading

* [LumaScript reference](index.md)
