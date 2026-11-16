---
title: Hiding the HUD
shortTitle: Hide the HUD
intro: Remove the star, coin and life counters and the life meter from the screen, with an option to show them again.
contentType: tutorials
example: hide-the-hud
---

## Introduction

In this tutorial, you make a mod that hides the game's HUD: the star, coin and life counters and the life meter. The mod adds an option to the **Mods** page, so players can show the HUD again without restarting the game.

You learn how to:

* Add an option to a mod and read it from code.
* Run code every frame with `on frame`.
* Stop one of the game's functions with a `hook before` and `skip`.
* React when the player changes an option with `on option`.

## Prerequisites

* Nebula, installed with Nebula Setup.
* LumaLink. For more information, see [Installing LumaLink](../get-started/installing-lumalink.md).
* We recommend that you complete the [Quickstart for Nebula mods](../get-started/quickstart.md) first.

## Creating the mod

Create the mod folder, and add an option named `hide`.

```sh
lumalink new hide-the-hud --id hide-the-hud --name "Hide the HUD" --summary "Takes the counters and the life meter off the screen." --author YOUR-NAME
lumalink add option hide hide-the-hud --kind toggle --default true --name "Hide the HUD"
```

`lumalink add option` adds a toggle named **Hide the HUD** to the mod's entry on the **Mods** page. It is on by default.

## Writing the code

Create `hide-the-hud/source/main.lum` with this code:

```lumascript
option hide: bool

// The game shows the HUD when a level starts. Put it away on the first frame it is up.
on frame {
    if hide and MR::isExistSceneObj(SceneObj_GameSceneLayoutHolder as i32) and MR::isActiveDefaultGameLayout() {
        MR::deactivateDefaultGameLayout()
    }
}

// The game brings the HUD back after each cutscene through this function.
hook before GameSceneLayoutHolder::activateDefaultGameLayout() {
    if hide {
        skip
    }
}

// The option turned off during play: the HUD comes back at once.
on option hide {
    if not hide and MR::isExistSceneObj(SceneObj_GameSceneLayoutHolder as i32) and not MR::isDemoActive() {
        MR::activateDefaultGameLayout()
    }
}
```

* `option hide: bool` declares the option. Your code reads it like a variable.
* `on frame` runs every frame. When a level is loaded (the HUD's holder exists) and the HUD is showing, it calls the game's own `MR::deactivateDefaultGameLayout`, which hides every counter and the meter.
* `hook before GameSceneLayoutHolder::activateDefaultGameLayout()` runs before the game shows the HUD again, for example after a cutscene. `skip` stops the game's call.
* `on option hide` runs when the player changes the option. When the player turns it off outside a cutscene, the HUD appears again immediately.

## Building and checking the mod

```sh
lumalink build hide-the-hud
lumalink check hide-the-hud
```

The last line of the `check` output is `Errors: 0`.

## Testing the mod

The finished mod includes two tests:

* `tests/hidden` enters Good Egg Galaxy, calls `MR::isActiveDefaultGameLayout`, and takes a picture. The test passes when the function returns `false` and the corners where the life meter and the coin counter would be show the sky.
* `tests/shown` does the same with the option turned off. The test passes when the HUD is showing and the life meter is drawn.

To run the tests, copy the `tests` folder from the finished mod into `hide-the-hud`, then run:

```sh
lumalink test hide-the-hud
```

> [!NOTE]
> In the current version, the test harness cannot start a mod with an option set, so `tests/shown` ends with `harness-error`.

## Playing the mod

1. Pack the mod.

   ```sh
   lumalink pack hide-the-hud
   ```

1. Drag the `.luma` file onto the game's window, and turn the mod on in the **Mods** page.
1. Enter a level. The HUD is hidden. To show it again, turn off **Hide the HUD** on the **Mods** page. You do not need to restart the game.

## Next steps

* [Adding a new character to talk to](a-new-npc.md)
* [Hooks](../reference/lumascript/hooks.md)
* [Lifecycle, options, frames and content](../reference/lumascript/lifecycle.md)
