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
the person building (LumaLink's 14 languages), with a code `S<number>` and a page in the language book. The
first error does not stop the check; all are listed.

## While the game runs

A script error does this, and only this:

1. The statement stops; the hook, task or event handler it is in ends as described above.
2. The mod log on screen and the game's log get one line: the mod, `file.lum:line`, and what happened
   (`index 7 outside a list of 5`, `handle to Kuribo is empty`, `division by zero`, `i32 overflow in a + b`).
3. The same line repeated is counted, not repeated.
4. The mod keeps running. A script error cannot damage memory, so it is not a fault; the guard is for
   faults in C code, which do take the mod's hooks out.

Generated C carries `#line` markers, so a debugger, a crash report and a profile show `file.lum:line`.

## Further reading

* [LumaScript reference](index.md)
