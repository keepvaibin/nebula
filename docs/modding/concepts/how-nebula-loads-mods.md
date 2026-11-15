---
title: How Nebula loads mods
shortTitle: How mods load
intro: Learn what Nebula checks and prepares at each start, in which order mods load, and what happens when two mods change the same thing.
contentType: concepts
---

## About a start with mods

Every time the game starts with at least one mod turned on, Nebula checks and prepares the mods before the first scene. With no mod turned on, none of these steps runs.

At the end of a start, every mod is either fully on or off. A mod that is on has its content prepared and its code running. A mod that is off has its reasons on its page on the **Mods** page and in the mod log. Nothing of a mod that is off is used.

## The steps of a start

1. **Nebula's own parts.** Nebula checks that the parts it needs for mods are present: the mod importer, the shader compiler, its placeholder files, and enough free space for prepared mod files. If one is missing, every mod is off for this start and the title screen says which part is missing.
1. **Answers.** A mod with code whose answers no longer hold is off for this start. This happens when an update changed its code or its permissions. The question opens at the title screen. For more information, see [About permissions](permissions.md).
1. **The start check.** Nebula checks each mod with the same rules LumaLink uses. A mod that fails a start rule is off. The start rules cover the game region, a newer file format, a missing mod it needs, a conflict with another mod, and a graphics feature this device does not have.
1. **Preparing content.** Nebula prepares the content of the mods that are still on: models, textures, sounds, shaders and table patches. If a file of a mod fails, that mod is off, each failed file is named in the mod log, and the content is prepared again without it.
1. **References.** Nebula checks the names that mods refer to, such as a model or a sound in another mod. A missing one is drawn or played as a placeholder.
1. **Code.** Nebula loads the code of the mods that are still on, and binds their hooks. A mod whose code cannot bind is off. Nothing it prepared in step 4 is used.
1. **The title screen.** The title screen shows a short notice when a mod has errors or warnings.

Prepared content is kept between starts. A start with the same mods and files prepares nothing again.

## Load order

The **Mods** page lists mods in load order, from top to bottom. When two mods change the same thing, the mod higher in the list wins:

| What | Which mod wins |
|---|---|
| Files | When two mods place a file at the same game path, the higher mod's file is used. |
| Table patches | When two mods set the same value, the higher mod's value is used. Patches to different rows or values of the same table both apply. |
| Replacing a function | Only one mod can replace a given function. The higher mod's replacement is used, and the lower mod does not start. |
| Before hooks | The higher mod's hook runs first. |
| After hooks | The higher mod's hook runs last, so its result stands. |
| Wrap hooks | The higher mod's wrap is outermost. It sees the call first and the result last. |

Within one mod, `before` hooks run in the order the code declares them, `after` hooks in the reverse order, and the first `wrap` is outermost.

A mod can ask to load after or before other mods with the `load` key in `luma.json`. A mod that needs another mod, such as LumaKit, always loads after it. For more information, see [luma.json](../reference/luma-json.md).

## A mod's code library

A mod with code carries one library for each platform in its `code` folder. Nebula loads the library for the device it runs on. A mod with code but no library for the device is off on that device, with the reason on the **Mods** page. By default, `lumalink build` builds all nine platforms.

When Nebula refuses a library, the reason is one of the validator's codes, shown in the player's language on the **Mods** page and in English in the game's log. For the list of codes, see [Validator rules](../reference/validator-rules.md).

## Further reading

* [About permissions](permissions.md)
* [When a mod fails](when-a-mod-fails.md)
* [Reading the mod log](../how-tos/reading-the-mod-log.md)
