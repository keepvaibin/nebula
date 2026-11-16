---
title: Making a small tweak
shortTitle: A small tweak
intro: Change one of the game's numbers by name, so that Mario starts each level with six health instead of three.
contentType: tutorials
example: a-small-tweak
---

## Introduction

In this tutorial, you make a mod that changes a single value in the game: the health Mario has when a level starts. The mod sets the value when the game starts, and Nebula restores the original value when the mod is turned off or removed.

This is the smallest kind of code mod. It needs no content files and no LumaKit modules.

## Prerequisites

* Nebula, installed with Nebula Setup.
* LumaLink. For more information, see [Installing LumaLink](../get-started/installing-lumalink.md).

## Creating the mod

Create the mod folder.

```sh
lumalink new six-health --id six-health --name "Six Health" --summary "Mario starts each level with six health." --author YOUR-NAME
```

Replace YOUR-NAME with your name.

## Writing the code

Create `six-health/source/main.lum` with this code:

```lumascript
on start {
    set_number("Game/System/GameRules.cpp#cPlayerHealth", 6)
}
```

`set_number` changes one of the game's numbers by name. The name is the source file that defines the number, followed by `#` and the number's name. `cPlayerHealth` is the health Mario has when a level starts, and also the size of his life meter.

Nebula records the original value before the mod changes it. When the mod stops, every number it set returns to its original value.

## Building and checking the mod

```sh
lumalink build six-health
lumalink check six-health
```

The last line of the `check` output is `Errors: 0`. If a name does not exist in the game, the mod does not start, and the mod log names the value it could not find.

## Testing the mod

The finished mod includes two tests:

* `tests/six` enters Good Egg Galaxy, reads Mario's health and maximum health (`MarioActor.mHealth` and `mMaxHealth`), and takes a picture. The test passes when both values are 6 and the life meter shows the color for six health.
* `tests/taken-out` reads 6, removes the mod while the game runs, enters the galaxy again, and reads 3. The test passes when the mod log reports that the mod's numbers are restored.

To run the tests, copy the `tests` folder from the finished mod into `six-health`, then run:

```sh
lumalink test six-health
```

Each test runs the game hidden and silent, and ends with `pass` or `fail` and the path of its run folder.

## Playing the mod

1. Pack the mod.

   ```sh
   lumalink pack six-health
   ```

   LumaLink writes the `.luma` file beside the mod folder.

1. Drag the `.luma` file onto the game's window, and turn the mod on in the **Mods** page.
1. Enter any level. Mario starts with six health.

## Next steps

* [Hiding the HUD](hide-the-hud.md)
* [About LumaScript](../concepts/lumascript.md)
* [Testing a mod](../how-tos/testing-a-mod.md)
