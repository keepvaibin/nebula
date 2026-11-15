---
title: About the game description
shortTitle: Game description
intro: Every build of Nebula describes itself in one file that lists every name a mod can use. LumaLink checks your mod against it.
contentType: concepts
---

## About the game description

The game description is a file named `smg_game.luma-self.json`. Every build of the game module writes it from the game's source. It lists everything a mod can name:

* Functions, with their signatures and which hooks they take.
* Classes, structs and enums, with their fields, sizes and values.
* Globals and the game's data in code.
* The game's data names: nerves, messages, and the keys of its tables.

Nebula Setup installs the file beside the game. LumaLink, the LumaScript compiler and the editor support read it. Nothing else is a source of names.

## Why it matters for your mod

When you build a mod, LumaLink checks every name your code uses against the game description. A name the game does not have, or a function called with the wrong signature, is a build error with the name and the place in your source. You find the mistake before a player does.

When the game loads your mod, it binds each name again against the running game. A field of a game class is reached through the offset the running game gives, not an offset stored in your mod, so one mod works on every platform even when a class is laid out differently.

## Stable names

A name and signature in a released game description keeps its meaning in later versions. A mod built against one version of Nebula keeps working with later versions, unless it uses something a later version removed. In that case the mod does not start, and the **Mods** page names what is missing.

## Finding names

To find the name of a function, a class or a table key, use one of these:

* The reference pages for [classes](../reference/classes.md), [events](../reference/events.md), [nerves](../reference/nerves.md) and [tables](../reference/tables.md).
* `lumalink list tables`, `models`, `messages` or `files`, which lists those in your imported game, with a filter such as `Kuribo`. For more information, see [lumalink list](../reference/lumalink-commands/list.md).
* An editor with LumaLink's language server, which completes game names as you type. For more information, see [Setting up an editor](../how-tos/setting-up-an-editor.md).

## Using another game description

By default, LumaLink uses the game description of the installed Nebula. To build against another one, such as a newer build, pass its folder with `--names`.

## Further reading

* [About LumaScript](lumascript.md)
* [Validator rules](../reference/validator-rules.md)
