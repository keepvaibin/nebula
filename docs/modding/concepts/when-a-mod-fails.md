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
* The fault happened outside mod code, or outside a guarded call.
* A second fault happened while the first one was being handled.
* The mod called the game and the game called the mod again, so returning would skip game code that is half done.

## Code time

The guard also measures how long each mod's code runs in each frame. The mod's page on the **Mods** page shows the average and the worst time a frame over the last 120 frames. In a test, the fact `kernel.guard_time` gives the same two values.

## Further reading

* [Reading the mod log](../how-tos/reading-the-mod-log.md)
* [How Nebula loads mods](how-nebula-loads-mods.md)
