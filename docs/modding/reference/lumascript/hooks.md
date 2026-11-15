---
title: Hooks
shortTitle: Hooks
intro: Running code before, after, around or instead of a game function.
contentType: reference
---

## About hooks

A hook runs your code before, after, around or instead of one of the game's functions.

```lumascript
hook before MarioActor::control {
    if frozen { skip }                    // a void function: skip it
}

hook after MR::getStarPieceNum -> n {  // the result, named
    result = n * 2                        // changes what the caller receives
}

wrap MR::addStarPiece(num) {             // the arguments, named; may be written
    num *= 2
    proceed                             // the next wrap, then the game's own code
}

replace Kuribo::exeWander {            // one mod only (a second is refused at load, naming both mods)
    self.mVelocity = vec3
}

hook before `MR::isNear(const LiveActor *, const Vec3f &, f32)`(actor, at, dist) -> bool {
    if dist < 0.0 { skip false }          // a non-void function: skip needs the result (else a compile error)
}
```

- `self` is the view of `this` for member functions.
- `before` may `skip` (with a result for a non-void function: the compiler requires it, so a skipped call always has
  one). `after` may change `result`. `wrap` calls `proceed` zero or one time per path;
  calling it twice is a compile error; a path that never proceeds must set `result` for a non-void function.
  `replace` never proceeds.
- A hook written in a `class` applies to that class's objects only (the compiler adds the `is_a` test).
- Hooks on constructors and destructors: `hook after Kuribo::Kuribo(name)`, `hook before Kuribo::~Kuribo`, before
  and after only (the kernel's rule).
- `hook ... every override of LiveActor::control` hooks the virtual and each override the self-description lists
  (kernel part `K.vhook`).
- Order: hooks of one mod run in the order written; between mods, in the player's load order (kernel).
- A script error inside a hook ends that hook's run: a `before` does not skip; a `wrap` that has not proceeded
  proceeds; an `after` leaves the result as it was. The game's own behavior goes on.

## Further reading

* [LumaScript reference](index.md)
