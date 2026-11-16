---
title: Modding glossary
shortTitle: Glossary
intro: Look up the terms used throughout the Nebula modding documentation.
contentType: get-started
---

## About the glossary

This glossary defines the terms you see in the Nebula modding documentation, in LumaLink's output and on the **Mods** page.

## .luma file

A packed mod: a ZIP archive of a mod folder, written by `lumalink pack`. One `.luma` file works on every platform Nebula runs on. Players import it on the **Mods** page or drag it onto the game's window. For more information, see [The .luma file format](../reference/luma-file-format.md).

## Asset

A file in a layer's `assets/` folder that belongs to the mod alone, such as a model or a sound. The game refers to an asset as `MOD-ID:PATH`, so it never collides with the game's files or another mod's. For more information, see [About mod files and layers](../concepts/mod-files-and-layers.md).

## Finding

One problem that the validator reports, at one place in a mod. Each finding has a code, such as `L203`, a level (**Error** or **Note**), the file and, where there is one, the line. For more information, see [Validator rules](../reference/validator-rules.md).

## Game description

The file `smg_game.luma-self.json`, which lists every function, class, field, enum, global, nerve, message and table key that a mod can name. LumaLink checks your mod against it when you build. For more information, see [About the game description](../concepts/the-game-description.md).

## Guard

The part of Nebula that runs every piece of mod code. If the code fails, for example by reading memory it does not own, the guard stops the call, turns the mod off, and returns to the game. For more information, see [When a mod fails](../concepts/when-a-mod-fails.md).

## Hook

Code that runs before, after, around or instead of one of the game's functions. In LumaScript, you write `hook before`, `hook after`, `hook wrap` or `hook replace`. For more information, see [Hooks](../reference/lumascript/hooks.md).

## Layer

A folder of content under a mod's `layers/` folder. Most mods have one layer, `base`. A mod can turn a layer on or off with a condition, such as an option, the game's region or language, or the platform. For more information, see [About mod files and layers](../concepts/mod-files-and-layers.md).

## Load order

The order of mods on the **Mods** page, from top to bottom. When two mods change the same thing, the mod higher in the list wins. For more information, see [How Nebula loads mods](../concepts/how-nebula-loads-mods.md).

## luma.json

The manifest of a mod: its ID, name, version, summary, needs, options, permissions, layers and code platforms. For more information, see [luma.json](../reference/luma-json.md).

## LumaCore

The interface that Nebula gives mod code: a table of functions for finding and hooking the game's functions, reading fields, defining classes and reaching services. C code uses it through `luma.h`, and LumaScript code uses it through the code that LumaLink writes. For more information, see [LumaCore C API](../reference/lumacore-c-api.md).

## LumaKit

A library mod that ships with Nebula. It gives your mod ready-made parts, such as new kinds of objects, power-ups, cutscenes, HUD elements, menus and galaxies. For more information, see [About LumaKit](../concepts/lumakit.md).

## LumaLink

The tool that creates, checks, builds, tests and packs mods. It has a window and the `lumalink` command-line tool. For more information, see [Installing LumaLink](installing-lumalink.md).

## LumaScript

The language for Nebula mod code. LumaLink compiles it to C, and then to native machine code for each platform. For more information, see [About LumaScript](../concepts/lumascript.md).

## Mod log

The record of what each mod did and every problem Nebula found with it, in the player's language. For more information, see [Reading the mod log](../how-tos/reading-the-mod-log.md).

## Need

Another mod that a mod requires, such as LumaKit, listed in `needs` in `luma.json` with a version range. A mod always loads after the mods it needs.

## Nerve

The state that one of the game's objects is in, such as `KuriboNrvWalk`. The nerve's code, such as `Kuribo::exeWalk`, runs every frame until another nerve is set. For more information, see [Nerves](../reference/nerves.md).

## Option

A setting that a mod adds to its entry on the **Mods** page, such as a toggle or a number. The mod's code reads it like a variable. For more information, see [Lifecycle, options, frames and content](../reference/lumascript/lifecycle.md).

## Patch

A file in a layer's `patches/` folder that changes, adds or removes rows in one of the game's files that has rows, such as a table, a stage placement or a message file. Two mods can patch different rows of the same file. For more information, see [Patching game tables](../how-tos/patching-game-tables.md).

## Permission

A sensitive service that a mod's code uses, such as the internet or the player's files, declared in `luma.json` with a reason. The player can allow or refuse each permission. For more information, see [About permissions](../concepts/permissions.md).

## Platform

A system that a mod's code is built for, such as `windows-x64` or `android-arm64`. By default, `lumalink build` builds all nine platforms.

## Script error

A mistake in LumaScript code that can only show while the game runs, such as an index outside a list or a division by zero. Nebula reports the mod, the file and the line, and the game continues with its own behavior. For more information, see [Errors](../reference/lumascript/errors.md).

## Service

A set of functions that Nebula gives mod code, such as `save`, `audio` or `net.internet`. Each service has a name and a version. For more information, see [About services](../concepts/services.md).

## Task

LumaScript code that runs over several frames, and waits for time, a condition or an answer. For more information, see [Tasks](../reference/lumascript/tasks.md).

## Test harness

The part of LumaLink that runs a mod's tests. It starts the game hidden and silent, plays the steps of each test, and checks what happened. For more information, see [Testing a mod](../how-tos/testing-a-mod.md).

## Validator

The set of rules that LumaLink and Nebula check every mod against, when you check or pack a mod, when a player imports it, and at each start. For more information, see [Validator rules](../reference/validator-rules.md).

## World step

One step of the game world, 60 per second. World steps stop while the game is paused, so a time measured in world steps is the same on any computer.
