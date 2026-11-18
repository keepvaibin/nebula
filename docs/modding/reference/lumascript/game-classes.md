---
title: Classes derived from the game's
shortTitle: Classes derived from the game's
intro: Declaring classes that derive from the game's classes in LumaScript.
contentType: reference
---

## About classes derived from the game's

A LumaScript class can derive from one of the game's classes and override its functions. This page describes how to declare one.

```lumascript
class GoldBall : Kuribo {
    var age: int = 0
    override fn control() { age += 1; if age > 300 { kill() } else { base.control() } }
}
```

- An override's parameter types may be left out when its name has one virtual in the base (`override fn
  receiveMsgPlayerAttack(msg, other, mine)`); they are then the virtual's.
- The base is any game class the self-description lists as derivable. The game's factory makes it; it can be placed
  in a stage by its name (`GoldBall`) like a game class.
- Fields declared in the class are the mod's (stored beside the game's object as the kernel provides); the base's
  fields and functions are reached by name as on any game object.
- `override` replaces a virtual of the base for this class only; `base.control` calls the base's version. Only
  virtuals the self-description lists can be overridden; anything else is a compile error naming it.
- A class's own `fn` takes `self` first (`fn bump(self, by: int) -> int`): it runs on one object, reached as
  `ball.bump(1)`, `self.bump(1)` or `bump(1)` inside the class. One without `self` is called by the class's name
  (`GoldBall.count()`).
- A hook written inside a class runs only for that class's objects. For any other object the game's function runs as
  if the hook were not there. `self` is the object, with its fields.

```lumascript
class GoldBall : Kuribo {
    var hits: int = 0
    fn bump(self, by: int) -> int { hits += by; return hits }
    hook before Kuribo::exeWander() { bump(1) }      // only a GoldBall's wandering
}
```

## Further reading

* [LumaScript reference](index.md)
