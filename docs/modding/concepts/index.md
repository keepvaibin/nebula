---
title: Concepts
shortTitle: Concepts
intro: Learn how Nebula mods, LumaScript, LumaKit and services work.
contentType: landing
---

## About concepts

These articles explain how the parts of a mod fit together.

## Articles

| Article | Description |
|---|---|
| [About game content in mods](game-content.md) | Learn which kinds of content a mod can bring, which source formats it uses, and how Nebula turns them into the game's own data. |
| [How Nebula loads mods](how-nebula-loads-mods.md) | Learn what Nebula checks and prepares at each start, in which order mods load, and what happens when two mods change the same thing. |
| [About LumaKit](lumakit.md) | LumaKit is a library mod that ships with Nebula. It gives your mod ready-made parts such as new enemies, power-ups, cutscenes, HUD elements, menus and galaxies. |
| [About LumaScript](lumascript.md) | LumaScript is the language for Nebula mod code. It is simple to write, it is checked against the game when you build, and it compiles to native machine code. |
| [About mod files and layers](mod-files-and-layers.md) | Learn how a mod folder is laid out, how layers group a mod's content, and how a mod replaces, patches and adds the game's files. |
| [About permissions](permissions.md) | Mods that run code ask the player before they start, and list every sensitive service they use as a permission the player can allow or refuse. |
| [About services](services.md) | Services are the functions Nebula gives mod code for input, saves, files, the network, audio, drawing, time and more. |
| [About the game description](the-game-description.md) | Every build of Nebula describes itself in one file that lists every name a mod can use. LumaLink checks your mod against it. |
| [When a mod fails](when-a-mod-fails.md) | If a mod's code crashes while the game runs, Nebula turns that mod off and the game continues. |
