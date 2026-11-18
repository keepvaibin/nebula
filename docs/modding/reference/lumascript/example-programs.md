---
title: Example programs
shortTitle: Example programs
intro: Complete LumaScript mods you can build and test.
contentType: reference
---

## About the example programs

Each tutorial builds one complete mod, all but one in LumaScript. You can download each finished mod, with its tests, from its tutorial.

| Program | What it shows |
|---|---|
| [Quickstart for Nebula mods](../../get-started/quickstart.md) | `use kit.hud`, a hook on the player's jump, a test file. |
| [Making a small tweak](../../tutorials/a-small-tweak.md) | Changing one number of the game by its name. |
| [Hiding the HUD](../../tutorials/hide-the-hud.md) | `option`, `on frame`, `hook before` with `skip`, `on option`. |
| [Changing a stage](../../tutorials/a-stage-edit.md) | Table patches alone: no code. |
| [Adding a level timer to the HUD](../../tutorials/a-level-timer.md) | A screen of its own with `svc.ui`, `on step`, and `text` rows in every language. |
| [Adding a new object to a stage](../../tutorials/a-new-object.md) | An object with its own model, ground and behaviour with `kit.actor`. |
| [Adding a new character to talk to](../../tutorials/a-new-npc.md) | A character with a model, materials and dialogue with `kit.talk`. |
| [Adding a new power-up](../../tutorials/a-new-power-up.md) | A form with `kit.player.form`, an item and a projectile. |
| [Playing as a new character](../../tutorials/a-new-character.md) | A rigged model played in Mario's place with `kit.player.character`. |
| [Playing in first person](../../tutorials/first-person.md) | `kit.camera`, `svc.input`, `option` and `on option`. |
| [Adding a new galaxy](../../tutorials/a-new-galaxy.md) | A stage, placements, models, enemies and a boss with `kit.stage`. |
| [Playing a video inside a galaxy](../../tutorials/a-video-in-a-galaxy.md) | The `movie` service. |
| [Making a game mode: Balloon World](../../tutorials/balloon-world.md) | A game mode with menus, the HUD and `net.local` and `net.internet`. |
| [Racing a ghost: a time trial with a replay viewer](../../tutorials/ghost-time-trial.md) | `kit.replay`, files and a menu of saved runs. |
| [Connecting to LiveSplit](../../tutorials/livesplit-link.md) | A TCP connection with `net.local`. |
| [Adding an online leaderboard](../../tutorials/online-leaderboard.md) | `net.internet` as an optional service, menus and `save.global`. |

## Further reading

* [Tutorials](../../tutorials/index.md)
* [LumaScript reference](index.md)
