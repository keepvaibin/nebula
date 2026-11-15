---
title: Files and modules
shortTitle: Files and modules
intro: How LumaScript source files, modules and use declarations work.
contentType: reference
---

## About files and modules

Each `.lum` file in your mod's `source` folder is one module. This page describes how modules, `use` lines and library mods work.

- A source file is UTF-8 text with the extension `.lum`. One file is one module; its name is its path in the mod's
  `source/` folder without the extension, with `/` between parts (`source/ball/fly.lum` is `ball/fly`).
- `use` brings a module into scope:

  | Form | What |
  |---|---|
  | `use ./ball/fly` | another module of the same mod (path from `source/`) |
  | `use kit.actor` | a LumaKit module (or any library mod's module, `use <mod id>.<module>`, which must be a `needs` of the mod) |
  | `use svc.net.local` | a service: its functions and events |
  | `use c "helpers.h"` | C declarations from the mod's own C sources |

  The game is always in scope; it is not `use`d. `use x as y` renames; `use x.{a, b}` takes names.
- A name declared `pub` is visible to modules that `use` this one; anything else is private to its file.
- The mod's entry is the module `main` (`source/main.lum`). Its top-level `on start`, hooks and states are the mod's
  own; other modules' top-level hooks are the mod's too (a hook is never private).

## Library mods

A library mod offers modules to other mods. A mod uses one with `use <mod id>.<module>`, and the library must be in the `needs` of the mod's `luma.json`. LumaKit is such a library: its modules are used as `use kit.<module>`.

* A name the library declares `pub` is visible to the mods that use the module.
* A library's `pub task` is exported. The caller gets the task's handle, and `wait` copies the result into the caller's memory.
* The library's service calls are its own. A request made inside an export is answered to the library.
* `on mods.stopped(gone)`, with its parameter untyped or `u64`, runs in the library when a mod that called its exports stops. Use it to end what that mod started. The `mods` service's own STOPPED event is written `on mods.stopped(e: mods.StoppedEvent)`.

## Further reading

* [LumaScript reference](index.md)
