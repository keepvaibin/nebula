---
title: Nerves
shortTitle: Nerves
intro: Every nerve of the game's objects, with the code that runs in it.
contentType: reference
---

## About the game's nerves

This page explains nerves and links to the lists of every nerve of the game, grouped by the first letter of the class.

The game's objects, screens and the states objects hand their work to run one nerve at a time: Kuribo walks in
`KuriboNrvWalk`, and that nerve's code (`Kuribo::exeWalk`) runs every frame until another nerve is set.

A mod hooks a nerve's code by the name in the Code column, as any function of the game. In LumaScript:
`hook before Kuribo::exeWalk() { ... }` runs first and may skip the game's frame of it, `hook after` runs last,
`hook wrap` runs around it (see [Hooks](lumascript/hooks.md)). In C the same kinds are `LUMA_BEFORE`,
`LUMA_AFTER` and `LUMA_WRAP` (luma.h).
The On leaving column is the code that runs as an object leaves the nerve. Nerves of a state (a class that does
an object's work for a while, such as `WalkerStateWander`) name the state's class.

## Nerves by class

* [Classes A to F](nerves-a-f.md): 185 classes
* [Classes G to M](nerves-g-m.md): 174 classes
* [Classes N to Z](nerves-n-z.md): 270 classes

## Further reading

* [Hooks](lumascript/hooks.md)
* [State machines](lumascript/state-machines.md)
