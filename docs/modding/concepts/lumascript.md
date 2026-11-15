---
title: About LumaScript
shortTitle: LumaScript
intro: LumaScript is the language for Nebula mod code. It is simple to write, it is checked against the game when you build, and it compiles to native machine code.
contentType: concepts
---

## About LumaScript

LumaScript is a small language made for game mods. You write it in `.lum` files in your mod's `source` folder. LumaLink translates it to C, and the platform's compiler turns the C into machine code inside your mod's library. The game contains nothing of LumaScript, and nothing interprets it while the game runs.

```lumascript
use kit.hud

var jumps: i64 = 0

hook before Mario::tryJump() {
    jumps += 1
    log "jump {jumps}"
}
```

## The game by name

Every function, field, class, enum and global of the game is in scope by its own name, such as `MR::getPlayerPos` or `Kuribo::exeWander`. You do not import them. When you build, LumaLink checks each name you use against the game's description of itself, and writes your mod's import list from what your code uses. A name the game does not have is a compile error, not a crash in the game.

For more information, see [About the game description](the-game-description.md).

## Memory and errors

LumaScript has no raw pointers and no manual freeing, and it cannot read a game object after the object is gone. When a mistake can only show while the game runs, such as an index outside a list, an empty handle, a division by zero or an overflow, it is a script error. Nebula reports the mod, the file and the line, and the game goes on with its own behavior.

LumaScript has no garbage collector. Values belong to their owner and are freed when the owner ends, and temporary values are freed every frame. Nothing pauses the game.

## What you can write

* Hooks run your code before, after or instead of a game function: `hook before`, `hook after`, `hook wrap` and `hook replace`.
* Events run your code at moments such as the start, each frame, or a service's answer: `on start`, `on frame`, `on stop`.
* Tasks run over several frames and wait for time, for a condition or for an answer: `task`, `wait`, `frames`, `seconds`.
* Classes derive from the game's own classes and override their functions, to make new kinds of objects.
* States describe what an object is doing, frame by frame.
* Services are reached with `use svc.NAME`, such as `use svc.net.local` or `use svc.save`.
* Modules split a mod into several files, and `use kit.NAME` brings in a LumaKit module.

## LumaScript and C

Part of a mod can be written in C or C++, beside its LumaScript. LumaScript calls your C functions through `use c "FILE.h"`, and your C calls LumaScript functions marked `export c fn`. For more information, see [Writing code in C](../how-tos/writing-code-in-c.md).

## Further reading

* [LumaScript reference](../reference/lumascript/index.md)
* [Example programs](../reference/lumascript/example-programs.md)
* [Quickstart for Nebula mods](../get-started/quickstart.md)
