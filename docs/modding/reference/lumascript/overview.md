---
title: LumaScript overview
shortTitle: LumaScript overview
intro: What LumaScript is for and the rules it keeps.
contentType: reference
---

## About LumaScript

LumaScript is the language for Nebula mods. LumaLink compiles it to C and then to machine code inside your mod's own library. This page lists the rules the language keeps.

A mod written by a person who is not a C++ programmer, compiled by LumaLink to C and then by the platform's compiler
to machine code inside the mod's own library. The game contains nothing of LumaScript.

1. Native. Every construct has a fixed translation to C that a reader of the generated file can follow. No
   interpreter, no bytecode, no reflection at run time, no code made while the game runs.
2. Safe by construction. No raw pointers, no manual freeing, no reading a game object after it is gone. Each
   mistake the compiler cannot rule out (an index outside a list, an empty handle, division by zero, an overflow) is
   a script error: reported with the mod, the file and the line, and the game goes on with its own behavior.
3. No garbage collector. Values, owned containers freed when their owner ends, and a scratch area emptied each
   frame. Nothing pauses.
4. The game by name. Every function, field, class, enum and global of the game's self-description is in scope
   by its own name; the compiler checks each use against the installed game's description and writes the mod's
   import list from what the script uses.
5. One way to do each thing. Where LumaKit gives a convenience, the language does not add a second.

## Further reading

* [LumaScript reference](index.md)
