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

## Further reading

* [About LumaKit](../../concepts/lumakit.md)
* [LumaScript reference](../lumascript/index.md)
