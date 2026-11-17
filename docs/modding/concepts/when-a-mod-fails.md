---
title: When a mod fails
shortTitle: When a mod fails
intro: If a mod's code crashes while the game runs, Nebula turns that mod off and the game continues.
contentType: concepts
---

## About the guard

Nebula runs every piece of mod code inside a guard: hooks, starts and stops, service events, timers, and the functions of classes a mod adds. When the code fails with one of the faults below, the guard stops that call, turns the mod off, and returns to the game as if the code had done nothing.

* An access violation: the code read or wrote memory it does not own.
* An illegal instruction.
* An integer division by zero, or an integer division that overflows.

Nothing of the guard runs when no mod with code is on.

## A fault in another mod's code

Mods call each other: a mod calls the functions a library mod exports, and a library calls functions that mods gave it (callbacks). When the code that fails belongs to another mod than the one whose call the guard stopped, the guard decides by need which mod to turn off:

1. It starts at the mod whose code failed.
2. While the mod that called it needs it (lists it in its `needs`, directly or through another library), it moves to that caller.
3. The mod it stops at is turned off.

A mod that calls a library takes the library's faults as its own, because it chose to need it. The library stays on. For example, when mod A calls library L1, L1 calls library L2 and L2's code fails, A is turned off and both libraries stay on.

A library never needs the mods that use it. When a library calls a function a mod gave it, the call goes through a boundary of that mod (`LumaCore.call_mod` in C; every call of a function value of another mod in LumaScript). When that function fails, the mod that gave it is turned off, the call returns with nothing done, and the library goes on to its next user in the same frame.

The log names three things: the mod turned off, the mod whose code failed, and the place as the mod's file and offset, for example `guard-lib.dll+0x1a2b`.

### Code cut short

The mods whose code was running between the fault and the stopped call, and that stay on, had their calls cut short. Each one is told: its `cut` function (in C, `LumaModInfo.cut`; in LumaScript, `on mods.cut(c)`) runs once, after the fault, with the function that was cut, the mod that called it, the mod turned off and the mod whose code failed. It can put right what the call left half done. A mod without a `cut` function stays on, and the log says that what the call was changing may be half done.

Nebula itself holds nothing across a cut: no kernel code runs between a guarded call and the mod code under it, and the kernel's per-call values (the running mod, the nesting depth, a start or stop in progress) are put back by the guarded call itself.

### A library that fails under two mods

The guard counts, for each mod, the faults in its code that turned another mod off. When a library's code fails under the calls of two different mods, the log says the fault is likely the library's own. The **Mods** page shows it, and offers to turn the library off, or to turn the two mods on again.

In a test, the fact `guard.under.ID` gives the count and the first two mods (`count`, `first`, `second`).

## What happens after a fault

The mod is off for the rest of the session. None of its code runs again, including its stop function, until the next start. Nebula takes the mod out at the next frame, and also takes out every mod that needs it.

The call that failed ends as if the mod had done nothing:

* A `before` hook does not skip the game's function.
* A `wrap`, a `replace` or an override that had not yet called the game's function calls it.
* A mod whose start fails does not start.
* A service event that fails is canceled.

The game's log and the mod log record the fault with the mod's name, where it happened, the kind of fault and the address. The player sees a notice in their language. For more information, see [Reading the mod log](../how-tos/reading-the-mod-log.md).

> [!WARNING]
> What the mod was changing when it failed can be half done. For example, a hook that had set half of an object's values leaves the other half as they were.

## When the game ends instead

The guard returns to the game only when it is certain the game's state is whole. In these cases the game ends, and the crash report names the mod:

* The fault is a stack overflow.
* The fault happened outside mods' code (in the game, the host or the system), or outside a guarded call. The crash report still names the mod whose code called the code that failed, and the place in it.
* A second fault happened while the first one was being handled.
* The mod called the game and the game called the mod again, so returning would skip game code that is half done.

## The next start after a crash

When the game ends with a crash while mods are on, the next start reads what the crash left:

* When the crash names a mod, that mod is off for the next start. The other mods stay on. The **Mods** page asks what to do with it: **Keep it off**, **Turn it on again** or **Remove**. Unless the player keeps it off, the mod starts again at the start after.
* When a mod crashed the game while the game started, before the first frame of the first scene, at two of its starts in a row, it stays off until the player turns it on. Crashes during play never count toward this.
* When the crash names no mod and mods' code was running, every mod's code is off for the next start. Mods without code stay on.
* When the crash names no mod and no mod's code was running, every mod is off for the next start, and the **Mods** page says why.

In dev mode, the mod you are working on stays on after a crash. For more information, see [Reloading a mod while playing](../how-tos/reloading-a-mod-while-playing.md).

In a test, the question is the fact `crash.shown.ID` (`twice`, `enabled`, `text`, `focus`) and the player's answer is `crash.answer.ID` (`off`, `on`, `remove` or `closed`). To test the start after a crash, end one test with `"ends": "crash"` and continue from it. For more information, see [Test files](../reference/test-files.md).

## Code time

The guard also measures how long each mod's code runs in each frame. The mod's page on the **Mods** page shows the average and the worst time a frame over the last 120 frames. In a test, the fact `kernel.guard_time` gives the same two values.

## Further reading

* [Reading the mod log](../how-tos/reading-the-mod-log.md)
* [How Nebula loads mods](how-nebula-loads-mods.md)
