---
title: LumaKit reference
shortTitle: LumaKit
intro: Look up every module of LumaKit, the library mod of ready-made parts.
contentType: reference
---

## About the LumaKit reference

LumaKit is one library mod with the id `lumakit`. A mod that uses a module adds `lumakit` to the `needs` of its `luma.json` and writes `use kit.<module>`. Each page lists the module's public declarations as its source declares them.

## Modules

| Module | Gives |
|---|---|
| [`kit.actor`](actor.md) | a new object kind in a few lines, and the helpers a mod's own `class` uses for the usual parts. |
| [`kit.area`](area.md) | new kinds of area, placed like the game's, with what happens inside. |
| [`kit.camera`](camera.md) | the main camera taken for a time, camera rigs for views (follow, eye, side, above), mirror, field of view. A rig is a `draw.ViewCamera` (svc.draw), so the same rig drives the main camera or a `draw.view`. |
| [`kit.cutscene`](cutscene.md) | scripted scenes of shots and lines, waited on, skippable. |
| [`kit.gravity`](gravity.md) | the player's gravity overridden, new gravity shapes placeable like the game's, the gravity at a point. |
| [`kit.hud`](hud.md) | counters, timers, meters, text, pictures in corners; text in 3D; popups; subtitles; the game's own HUD parts hidden. |
| [`kit.item`](item.md) | things picked up, placed in stages by name or made by code, in containers, carried in an inventory. |
| [`kit.menu`](menu.md) | entries in the game's pause menu. (A mod's settings are its `luma.json` options on the **Mods** page, read with `option` and `on option`: a setting is an option, not a menu entry.) |
| [`kit.net.kit`](net-kit.md) | reliable ordered messages and rooms over the `net` service's datagrams; a clock shared between peers; a small HTTP and WebSocket server for a page (a phone as a controller). |
| [`kit.player.character`](player-character.md) | a playable character (name, icon, model, animations, voice, constants), switching mid-level, a second player. |
| [`kit.player.form`](player-form.md) | a power-up: its look, aura, timer, music, what the player's actions do while it lasts, its own physics and scale, and the game's own forms by name to give from code. |
| [`kit.player.state`](player-state.md) | a new move (its trigger, animation, physics each frame, how it ends, whether it attacks) and the player's constants changed by name. |
| [`kit.pointer`](pointer.md) | the star pointer: where it points on screen and in the world, what B throws and what it costs, grabbing, the source that aims it (IR, gyro, mouse, stick), a co-star pointer. |
| [`kit.progress`](progress.md) | stars beyond 120 and new star kinds counted, comets added to any galaxy, per-file flags and counters, what a star gives, new-file rules. |
| [`kit.projectile`](projectile.md) | things thrown or shot, with bounces, life, trail, burst and a hit callback. |
| [`kit.replay`](replay.md) | recording and playing back poses, puppets posed from data, input files played frame-exactly, rewind of the player and chosen actors. |
| [`kit.ride`](ride.md) | mounts and vehicles: getting on and off, their moves, the player posed on them. |
| [`kit.screen`](screen.md) | whole screens: a message, a choice, a list, a loading screen, a layout of the mod's as a screen. |
| [`kit.shop`](shop.md) | buying things with coins or star bits. |
| [`kit.stage`](stage.md) | new galaxies with stars and comets in the domes, a new dome, music per galaxy, warping, stars appearing, the hub replaced, stages built in code. |
| [`kit.talk`](talk.md) | lines and choices from an actor in the game's balloon, dialogue trees, voice for lines. |
| [`kit.ui.kit`](ui-kit.md) | widgets for a mod's own panels: buttons, sliders, toggles, color pickers, lists, text fields, a wheel, a model view; pad, mouse and touch focus. |

## When a mod's code fails during a LumaKit call

A fault in a mod's code turns that mod off and never leaves LumaKit half done. For more information, see [When a mod fails](../../concepts/when-a-mod-fails.md).

LumaKit is written in LumaScript only. Its code leaves the kit in three ways, and none of them can cut a LumaKit call short while LumaKit stays on:

* **A function a mod gave it** (a callback, a handler, a shape's vector): the call goes through a boundary of the mod that gave it. When that code fails, that mod is turned off, the call returns with an empty result, and LumaKit goes on to its next user.
* **An event of a service it asked for for a mod**: the service calls LumaKit at a boundary of its own, and LumaKit calls the mod's function as above.
* **The game's code**: a fault there is outside the mods' code, so the game ends with a report instead of returning. When the game calls a mod's hook from inside such a call, that hook has its own boundary, and a fault in it turns off only that mod.

The kit needs no other library mod. A LumaKit call is cut only when LumaKit's own code fails. Its compiled code checks every access, division and conversion, so only a fault in the runtime itself can do that. LumaKit's runtime then puts back its calls in progress. The log also says that the module's state may be half done.

| Module | Functions it calls that a mod gave it |
|---|---|
| `kit.actor` | `on_init`, `on_frame`, `on_touch_player`, `on_attacked`, `on_talk` |
| `kit.area` | `on_enter`, `on_inside`, `on_leave` |
| `kit.camera` | none |
| `kit.cutscene` | a step's `Call` |
| `kit.gravity` | a shape's `vector` |
| `kit.hud` | none |
| `kit.item` | an item's `Call` |
| `kit.menu` | a pause entry's `on_choose` |
| `kit.net.kit` | the link's and page's receivers |
| `kit.player.character` | none |
| `kit.player.form` | `on_spin`, `on_act`, `on_jump`, `on_start`, `on_end` |
| `kit.player.state` | `motion`, `on_start`, `on_end` |
| `kit.pointer` | a throw's `shot`, `on_grab` |
| `kit.progress` | `on_star`, `on_new_file` |
| `kit.projectile` | a projectile's `answer` |
| `kit.replay` | none |
| `kit.ride` | `on_mount`, `on_dismount` |
| `kit.screen` | a list's `on_choose` |
| `kit.shop` | none |
| `kit.stage` | `build`, an unlock's `Code` check |
| `kit.talk` | none |
| `kit.ui.kit` | a widget's handlers |

## Further reading

* [About LumaKit](../../concepts/lumakit.md)
* [LumaScript reference](../lumascript/index.md)
