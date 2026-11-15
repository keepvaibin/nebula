---
title: LumaScript reference
shortTitle: LumaScript reference
intro: Look up the rules of the LumaScript language.
contentType: reference
---

## About the LumaScript reference

LumaScript is the language for Nebula mods. LumaLink compiles it to C and then to machine code. For an introduction, see [About LumaScript](../../concepts/lumascript.md).

## Pages

| Page | What it covers |
|---|---|
| [LumaScript overview](overview.md) | What LumaScript is for and the rules it keeps. |
| [Files and modules](modules.md) | How LumaScript source files, modules and use declarations work. |
| [Lexical structure](lexical-structure.md) | Comments, identifiers, keywords and literals in LumaScript. |
| [Types](types.md) | The types of LumaScript values. |
| [Statements and expressions](statements-and-expressions.md) | Declarations, control flow and operators in LumaScript. |
| [Functions, structs and enums](functions-structs-and-enums.md) | Declaring functions, structs and enums in LumaScript. |
| [Closures](closures.md) | Functions as values in LumaScript. |
| [Classes derived from the game's](game-classes.md) | Declaring classes that derive from the game's classes in LumaScript. |
| [Values, views and memory](values-and-memory.md) | How LumaScript holds game objects, handles, values, scratch memory and module state. |
| [The game by name](the-game-by-name.md) | How LumaScript names the game's functions, fields, classes and enums, and checks them. |
| [Hooks](hooks.md) | Running code before, after, around or instead of a game function. |
| [Errors](errors.md) | Errors LumaLink reports when it builds a mod, and script errors while the game runs. |
| [Lifecycle, options, frames and content](lifecycle.md) | The on handlers, options and content names of a LumaScript mod. |
| [Tasks](tasks.md) | Code that waits across frames. |
| [State machines](state-machines.md) | States and nerves in LumaScript. |
| [Services, events and LumaKit](services.md) | Calling services and LumaKit from LumaScript. |
| [C and C++ in the same mod](c-and-cpp.md) | Mixing LumaScript with C and C++ code. |
| [Tests in the mod](tests.md) | Writing tests inside a LumaScript mod. |
| [What the compiler emits](what-the-compiler-emits.md) | A summary of the C code LumaLink writes from LumaScript. |
| [Standard modules](standard-modules.md) | The modules every LumaScript mod can use. |
| [Not in LumaScript](not-in-lumascript.md) | Features LumaScript does not have. |
| [Detailed rules](detailed-rules.md) | The detailed rules of LumaScript that complete the other reference pages. |
| [Defining classes](defining-classes.md) | How a mod defines classes the game can place and run. |
| [References to game objects](references.md) | How a mod keeps references to game objects safely. |
| [Text and service values](text-and-service-values.md) | How a script reads a service's structs, enums and constants, shows them in text, and uses the mod's own text rows. |
| [Message tags](message-tags.md) | The tags for waits, pages, pictures, sizes, colors and the player's name in game text. |
| [The compiled form](compiled-form.md) | What LumaLink builds from a mod's LumaScript, in detail. |
| [Example programs](example-programs.md) | Complete mods written in LumaScript. |

## Further reading

* [Services reference](../services/index.md)
* [LumaKit reference](../lumakit/index.md)
* [Tutorials](../../tutorials/index.md)
