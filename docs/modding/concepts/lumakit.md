---
title: About LumaKit
shortTitle: LumaKit
intro: LumaKit is a library mod that ships with Nebula. It gives your mod ready-made parts such as new enemies, power-ups, cutscenes, HUD elements, menus and galaxies.
contentType: concepts
---

## About LumaKit

LumaKit is a mod with the ID `lumakit`, written in LumaScript and built like any other mod. Nebula installs it as `lumakit.luma`, and LumaLink installs its source so your builds find it.

LumaKit loads only when a mod that is on needs it. One copy serves every mod that uses it, and a mod pays only for the modules it uses.

## Using LumaKit

1. Add LumaKit to your mod's needs.

   ```shell
   lumalink add need lumakit MOD-FOLDER --version ^1.0.0
   ```

   Replace MOD-FOLDER with your mod's folder. This adds `{ "id": "lumakit", "version": "^1.0.0" }` to `needs` in `luma.json`: any LumaKit 1 from 1.0.0 on. Without `--version` the need takes any version (`*`).

1. In your code, bring in each module you use with `use kit.MODULE`.

   ```lumascript
   use kit.hud
   use kit.actor
   ```

A call to a LumaKit function is a direct native call into LumaKit's library, bound when the game loads your mod.

## Modules

| Module | What it gives you |
|---|---|
| `actor` | New kinds of objects and enemies, with sensors, attacks, defeats and drops. |
| `area` | Areas of a stage with their own effects, such as slow motion, wind or a side camera. |
| `camera` | Taking the camera: following, first person, side view, mirror, field of view and shake. |
| `cutscene` | Scenes of camera shots, lines and actions, with the game's letterbox, skippable with the pause button. |
| `gravity` | Gravity fields: parallel, toward a point, away from a point, and shapes of your own. |
| `hud` | Text, counters, meters, timers and a popup in the screen's corners. |
| `item` | New items and what they give: coins, lives, forms and items of your own. |
| `menu` | Entries in the pause menu. |
| `net.kit` | Ordered messages between players, peers joining and leaving, a shared clock, and web pages served by your mod. |
| `player.character` | New playable characters with their own models, animations and sounds. |
| `player.form` | New forms for the player, like the game's Fire Mario or Bee Mario. |
| `player.state` | New moves and actions, such as a double jump, a glide or a dive. |
| `pointer` | The Wii Remote pointer: what it throws, grabs and points at. |
| `progress` | The player's stars and flags, beyond the game's 120 stars. |
| `projectile` | Things that fly, bounce and hit: balls, shots and thrown objects. |
| `replay` | Recording and playing back the player's run, for ghosts and replays. |
| `ride` | Objects the player can ride. |
| `screen` | Whole screens: messages, choices, lists and loading screens. |
| `shop` | Shops where the player buys items. |
| `stage` | New galaxies and stages, built from data or from code, with their own music and stars. |
| `talk` | Lines and choices in the game's speech balloon, and dialogue trees. |
| `ui.kit` | Widgets for your own panels: buttons, sliders, toggles, color pickers, lists and text fields. |

## Rules every module keeps

* Everything belongs to the mod that made it. A form, a kind of object, a HUD element or a menu entry is recorded under your mod. When your mod stops, LumaKit undoes all of it, newest first, so nothing of your mod is left behind.
* One owner, many mods. When two mods ask for the same single thing, such as the player's form or the main camera, the newest request holds it. When the newer request ends, the earlier one holds it again.
* Names never clash. LumaKit names your kinds, items and forms `MOD-ID/NAME` to the game, so two mods can use the same short name.
* Permissions are yours. A module function that uses the network, files or the camera says so. Your mod declares the permission, and the player sees it on your mod.
* Failures are named. A wrong call is a script error at your line, naming the module, the function and what it expected. A function that can fail to do what you asked returns `none` or `false`, as its reference says.
* Game thread only. Call module functions from your mod's code on the game thread. A call from another thread is a script error.

## Data or code

Many things a module defines can be written as data in `kit/MODULE/` files of your mod, with no code at all. A mod of content and data alone can add a form, an item, a galaxy or a dialogue. LumaLink checks those files when you check or pack the mod.

## Further reading

* [LumaKit reference](../reference/lumakit/index.md)
* [A new power-up](../tutorials/a-new-power-up.md)
* [A new galaxy](../tutorials/a-new-galaxy.md)
