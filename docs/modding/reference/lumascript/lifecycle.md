---
title: Lifecycle, options, frames and content
shortTitle: Lifecycle, options, frames and content
intro: The on handlers, options and content names of a LumaScript mod.
contentType: reference
---

## About lifecycle, options, frames and content

This page describes the `on` handlers that run at points of your mod's life, the options the player sets, and how code names content.

```lumascript
option speed: f32           // declared in luma.json "options"; the compiler checks the name and type
option colour: text

on start { log "ready, speed {speed}" }         // after every import is bound
on stop { }                                     // before a reload or when the mod is turned off
on frame { }                                    // once per game frame, at the frame's start (service time)
on option speed { }                             // an option changed on the Mods screen
on load { gIsLuigi = true }                     // game data read at run time, before the stage that uses it loads
on stage "EggStarGalaxy" { }                    // a stage became ready (service time / LumaKit stage)
on stage ready { }                              // any stage became ready
on player ready { }                             // the ready stage's player is there
on play { }                                     // every frame of play
on stage ended { }                              // the ready stage is gone or going
on draw draw.Point.HudAfter { }                 // a drawing point of the frame (service draw); 2D or 3D by point
on layers -> list<text> { return [] }           // which `when.code` layers are on; once a start, before content
```

- `on layers` names which of the mod's layers with `"when": {"code": true}` are on. It runs once a start, after every
  mod is loaded and before any `on start`: it can read the mod's options and its own assets, and no mod has started.
  A name that is not one of those layers is left out and said in the mod log. The game prepares its files with the
  answer; when the answer differs from the one the files were prepared with (the first time, or after an option
  changed it), the game starts again once, by itself, before it shows anything. If the answer changes again in that
  start, the prepared layers stay until the next start and the mod log says so. A fault in `on layers` turns the mod
  off, as one in `on start` does. It is not run again when the mod is reloaded while the game runs. A mod without
  `on layers` has none of these layers on.

- What of the game exists when each event runs:

  | Event | Runs | What exists |
  |---|---|---|
  | `on layers` | once a start, after every mod is loaded, before any `on start` | the mod's options and its own files; nothing of the game |
  | `on load` | at the mod's start, just before its `on start` | the same as `on start` |
  | `on start` | at the first line of the game's start | the mod's options and files, and every service; not the game's messages, archives, layouts, scene or objects |
  | `on frame` | every frame from the first, the boot frames too | the game's messages a few frames in, once it has read its system files; a scene, but no stage until one loads |
  | `on stage "NAME"` | once a stage, when the stage NAME is ready (its scene is made and its opening is over) | the set `stage` |
  | `on stage ready` | once a stage, when any stage is ready, as `on stage "NAME"` | the set `stage` |
  | `on player ready` | once a stage, the first frame after it is ready that the player is there | the set `player` |
  | `on play` | every frame of play (not the pause menu, a movie or a skip) on which the player is there | the set `player` |
  | `on stage ended` | once, the first frame after a ready stage on which its scene is gone or going | the set `system` |

  The sets are lists of what game functions need, in the game's `luma_functions.json` (`guarantees`): `system` the
  save files and the comet scheduler (read some frames after boot, never undone); `stage` that, the scene and the scene
  objects every game scene makes; `player` that and the player's actor. The game checks each need of a set before it
  runs a handler of it, so a handler never runs without them.

  In `on start` a mod defines things (kinds, galaxies, hooks, its tables), reads its options and data and starts its
  tasks. A `text "Key"` made there reads as the game's line when it is used later. Read before the game's messages
  exist (logged, compared, built into another text, given to the game), it reads as its key there, and the mod log
  says so once. Code that needs the running game (its objects, its scene, the player) belongs in `on stage`, or in a
  hook on the game function that makes the thing it needs.

- Many game functions need something to be there: the scene, a scene object, the player, the save files
  (`luma_functions.json` `needs`; the function's reference page lists them). LumaLink refuses a call (S0154) unless
  each need is proved where the call is, and names the needs and what would prove them. Proofs:

  - a handler's set, in its body up to its first `wait` (after a wait the stage may have ended);
  - a hook's body proves what its game function needs (the game is calling it), an `override fn` what the virtual it
    replaces needs and the set `stage` (the game makes a mod's object while a stage is made, and calls it while it
    lives);
  - `if can(F) { }` proves what F needs in its block; `guard can(F) else { return }` after it; `if not can(F) { return }`
    after the `if`; `and` joins them. `can(F)` asks the game whether F's needs are there now (`LumaCore.can_call`);
    `can` of a quoted row (`` can(`MR::f(int)`) ``) asks for that row, else for every row of the name;
  - `if holds("player") { }` proves a set's needs as `can` does a function's: `"system"`, `"stage"` or `"player"`
    (`LumaCore.holds`);
  - `if let x = G() { }` proves what G's result is null without (`null_when`);
  - the right side of `a and b` has what `a` proves (`holds("player") and MR::isOnGroundPlayer()`), of `a or b` what
    `not a` proves;
  - a call proves what it makes after it (`makes`: `MR::declareCoin` makes the CoinHolder `MR::appearCoinPop` needs);
  - a function of the mod proves nothing of its own: what its body needs and does not prove is its callers' to prove.
    A function named as a value, a closure that runs later, a task, an export and a state's body have no caller the
    build sees, so each proves its own. A closure a built-in runs at once (`any`, `map`, `filter` ...) keeps what is
    proved around it.

  A loop that waits proves nothing it did not prove itself the next time round. `on frame`, `on start`, `on step`,
  the events and a task prove nothing: test `holds` or `can` there. The game still checks each call when it is made: a call whose
  needs are missing is not made, gives its type's empty value (0, `none`, an empty record), and the mod log says
  `Call.NotNow` once, naming the function and what it needs. C mods have `LumaCore.can_call` and `LumaCore.holds`.
  LumaKit is safe at any time for the same reason: in the boot frames and the title screen its calls return empty values.

- Content of the mod is named by its path in `layers/` or its key, checked when building against the mod's own
  files and resolved when the mod loads: `model "GoldBall"`, `effect "Burst"`, `sound "Throw"`,
  `layout "Timer"`, `text "Timer.Title"`, `font "Lyrics"`, `movie "Trailer"`, `data "lyrics.json"` (a data file of
  the mod, its bytes; `json.decode` reads it). A name that is not there is a compile error naming it.
- `sound_named(name)`, `model_named(name)`, ...: the same lookup with a name made while running; `none` for an empty
  text. The build cannot check such a name: one the mod does not have is the game's placeholder where it is used.
- `log "..."` writes to the game's log as the mod; `log fact "key" value` writes a fact for the harness.

## Further reading

* [LumaScript reference](index.md)
