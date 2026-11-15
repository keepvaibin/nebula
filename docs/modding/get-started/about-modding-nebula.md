---
title: About modding Nebula
shortTitle: About modding
intro: Learn what a Nebula mod is, what it can change, and which tools you use to make one.
contentType: get-started
---

## About mods

A mod is a folder with a `luma.json` file that describes it. LumaLink packs the folder into one `.luma` file. Players import the `.luma` file on the **Mods** page in Nebula, or drop it on the game's window.

One `.luma` file works on every platform Nebula runs on: Windows, Linux, macOS, Android and iOS. It holds the mod's content as sources, which Nebula prepares on the player's device, and the mod's code built for each platform.

## What a mod can change

A mod can bring content, code, or both.

* Content: models, animations, materials, particle effects, sounds, music, movies, screens, fonts, text and stages. A mod can replace a file of the game or add a new one.
* Table patches: changes to single rows and values of the game's tables, such as an enemy's speed or a galaxy's music. Two mods can change different rows of the same table.
* Code: functions that run before, after or instead of the game's own functions, new kinds of objects, and calls to Nebula's services for input, saves, files, the network, audio, drawing and time.

Mod code runs as native machine code. There is no interpreter. You write it in LumaScript or in C, and LumaLink compiles it for each platform.

## Tools

* LumaLink creates, checks, builds, tests and packs mods. It has a window and a command line. For more information, see [Installing LumaLink](installing-lumalink.md).
* LumaScript is the language for mod code. It reads like a small scripting language and compiles to native code. For more information, see [About LumaScript](../concepts/lumascript.md).
* LumaKit is a library mod that ships with Nebula. It gives your code ready-made parts: new objects, power-ups, cutscenes, menus, the HUD, cameras and stages. For more information, see [About LumaKit](../concepts/lumakit.md).

## Safety for players

Nebula checks every mod when the player imports it and again at every start, with the same rules LumaLink uses. A mod that breaks a rule does not start, and the **Mods** page says why in the player's language.

A mod that wants to use the internet, files outside its own folder, or another sensitive service lists it as a permission. The player sees each permission and can turn it off. For more information, see [About permissions](../concepts/permissions.md).

If a mod's code fails while the game runs, Nebula turns that mod off and the game continues. For more information, see [When a mod fails](../concepts/when-a-mod-fails.md).

## Next steps

* [Installing LumaLink](installing-lumalink.md)
* [Quickstart for Nebula mods](quickstart.md)
