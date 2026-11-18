---
title: The game by name
shortTitle: The game by name
intro: How LumaScript names the game's functions, fields, classes and enums, and checks them.
contentType: reference
---

## About the game by name

Every function, field, class, enum and global of the game is in scope by its own name. This page describes how names are written, which types cross between your code and the game, and how LumaLink checks each use.

## Names

- Every function in the self-description: `MR::getPlayerPos`, `GameDataFunction::setGameEventValue("Name", 3)`. An
  overloaded name with several signatures is written with its parameter types when the arguments do not choose:
  `MR::startAction(self, "Walk")` is chosen by its arguments; `` `MR::isNear(const LiveActor *, const Vec3f &, f32)`
  `` names one exactly. An ambiguous call is a compile error listing each signature.
- On a view: fields `self.mVelocity`, functions `self.exeWander`, `self.getSensor("body")`. Base class members are
  reached through the view as in C++. In a `class` body or a hook on a member function, the class's own members may be
  written without `self.` (`kill`, `makeActorAppeared`).
- Globals: `game.sInstance` style statics by their qualified name.
- Enums of the game: `MarioStatus_Swim`, `ACTMES_SPIN_ATTACK`; constant game data in code by name:
  `Mario::kReactionDecay` (read only: the game's code folds a `const` at every use, so nothing written at run time
  reaches it; change what reads it with a hook). Game data the game reads at run time, a global that is not `const`
  (`gIsLuigi`) or a table (`set_table`), is written by name.
- `set_table(name, steps..., value) -> bool` writes one place of a game table for the mod: the table's name as
  `luma_numbers.json` lists it, then each field by its name (text) or element by its index, then the value in the
  place's type. The name and each field are written as text in the call, not passed in a variable (S0151). It gives
  `true` when the place was written. The game's own bytes come back when the mod is taken out.
  `table(name, steps...)` reads the place as it is now, in the place's type (or `none`).
- `as_class<T>(x)` gives `x` as the game class or mod class `T`, or `none` when the object is not a `T`. An upcast
  needs no call: a `Kuribo` is a `LiveActor` where one is wanted.

```lumascript
on start {
    if set_table("Game/Player/MarioConst.cpp#gMarioConstDefault", "mJumpHeight", 0, 30.0) {
        if let h = table("Game/Player/MarioConst.cpp#gMarioConstDefault", "mJumpHeight", 0) {
            log "jump height {h}"
        }
    }
}

fn is_goomba(a: LiveActor) -> bool {
    return as_class<Kuribo>(a) != none
}
```

## Types across the boundary

| Game type | In LumaScript |
|---|---|
| `bool`, integer and float types, `s32 u16 f32` ... | the same |
| `TVec3f`, `Vec3f`, `TVec2f`, `TPos3f`, `Mtx` by value or `const &` | `vec3`, `vec2`, `mat34` (copied in and out) |
| `T &` / `T *` to one of those, written by the game | `var` argument: `MR::calcGravityVector(self, var g)` |
| A result `Vec3f *`, `const TVec3f &` (`MR::getPlayerPos`) | a `vec3` copy, taken when the call returns |
| `const char *` | `text` |
| `s32 *`, `f32 *` out parameters | `var` argument |
| A game class pointer or reference (`LiveActor *`, `const Kuribo &`) | a view; `none` for null where the game may give null (`LiveActor?`) |
| Anything else (a raw buffer, a function pointer, a `void *`) | not callable from LumaScript; compile error naming the parameter. Reach it from C in the same mod |

## Optional game functions

`optional` before a call of a game function makes it a function your mod can do without:

```lumascript
on player ready {
    if let dead = optional MR::isPlayerDead() {      // none on a game without it
        log "dead: {dead}"
    }
    if optional MR::incPlayerLeft() {                // true when the game has it, and it ran
        log "one more life"
    }
}
```

* On a game without the function, the mod still loads. The call runs nothing, its arguments included, and gives
  `none`, or `false` for a function with no result.
* On a game with it, it is the call: its result as an option, or `true` for a function with no result.
* One call of the same function without `optional`, anywhere in the mod, makes the mod need it.
* `optional` goes before a game function's call only (`MR::f(..)`, `obj.f(..)`, `obj?.f(..)`); before anything
  else it is S0162.

## Checks

Each game use is bound by name and signature when the mod loads (kernel loader: whole or not at all). LumaLink
checks every use against the self-description of the game it builds against: the installed game's, or the one
`LUMA_NAMES` names. A name that description lacks is a compile error naming it. A game that lacks a function the
mod calls refuses the mod and names the function, unless every call of it is `optional`.

## Further reading

* [LumaScript reference](index.md)
