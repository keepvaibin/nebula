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
  | `on stage "NAME"` | when the stage NAME is ready | everything: messages, layouts, the scene, the player and the stage's objects |

  In `on start` a mod defines things (kinds, galaxies, hooks, its tables), reads its options and data and starts its
  tasks. A `text "Key"` made there reads as the game's line when it is used later. Read before the game's messages
  exist (logged, compared, built into another text, given to the game), it reads as its key there, and the mod log
  says so once. Code that needs the running game (its objects, its scene, the player) belongs in `on stage`, or in a
  hook on the game function that makes the thing it needs.

- Content of the mod is named by its path in `layers/` or its key, checked when building against the mod's own
  files and resolved when the mod loads: `model "GoldBall"`, `effect "Burst"`, `sound "Throw"`,
  `layout "Timer"`, `text "Timer.Title"`, `font "Lyrics"`, `movie "Trailer"`, `data "lyrics.json"` (a data file of
  the mod, its bytes; `json.decode` reads it). A name that is not there is a compile error naming it.
- `sound_named(name)`, `model_named(name)`, ...: the same lookup with a name made while running; `none` when the mod
  has no such content. The only content names the build cannot check, so each use is listed in the build's report.
- `log "..."` writes to the game's log as the mod; `log fact "key" value` writes a fact for the harness
 .

## Further reading

* [LumaScript reference](index.md)
