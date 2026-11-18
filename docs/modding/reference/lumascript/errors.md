---
title: Errors
shortTitle: Errors
intro: Errors LumaLink reports when it builds a mod, and script errors while the game runs.
contentType: reference
---

## About errors

LumaLink reports some mistakes when it builds a mod. Others can only show while the game runs, and Nebula reports them as script errors in the mod log.

## When building

Every compile error names the file, the line, the column, what was found and what was expected, in the language of
the person building (LumaLink's 14 languages), with a code `S<number>`. [Build codes](build-codes.md) lists every
code with its message. The first error does not stop the check; all are listed.

## While the game runs

A script error does this, and only this:

1. The statement stops; the hook, task or event handler it is in ends as described above.
2. The mod log on screen and the game's log get one line: the mod, `file.lum:line`, and what happened
   (`index 7 outside a list of 5`, `handle to Kuribo is empty`, `division by zero`, `i32 overflow in a + b`).
3. The same line repeated is counted, not repeated.
4. The mod keeps running. A script error writes no memory the script does not own, so it is not a fault.
   The guard is for faults in C code (a mod's own C, a library's, and the C LumaLink writes), which turn
   the mod off. A fault inside the game's own code, in a game function a script called, ends the game:
   see [When a mod fails](../../concepts/when-a-mod-fails.md).

Generated C carries `#line` markers, so a debugger, a crash report and a profile show `file.lum:line`.

## Further reading

* [LumaScript reference](index.md)
