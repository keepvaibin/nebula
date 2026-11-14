# Changing the screen: hide the HUD

The star, coin and life counters and the life meter taken off the screen, with an option on the Mods screen to bring
them back. The whole mod is the folder `hide-the-hud/` beside this page.

## 1. Make the folder

```sh
lumalink new hide-the-hud --id hide-the-hud --name "Hide the HUD" --summary "Takes the counters and the life meter off the screen." --author you
lumalink add option hide hide-the-hud --kind toggle --default true --name "Hide the HUD"
```

`add option` puts a switch named "Hide the HUD" on the mod's line in the Mods screen, on at first.

## 2. Write the code

Put this in `hide-the-hud/source/main.lum`:

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

- `option hide: bool` is the switch; the code reads it as a variable.
- `on frame` runs every frame. In a level (the HUD's holder exists) with the HUD up, it calls the game's own
  `MR::deactivateDefaultGameLayout`, which puts every counter and the meter away.
- `hook before ... activateDefaultGameLayout()` with `skip` stops the game's own call that brings the HUD back.
- `on option hide` runs when the player flips the switch; turned off outside a cutscene, the HUD comes back at once.

## 3. Build and check

```sh
lumalink build hide-the-hud
lumalink check hide-the-hud
```

`check` ends `Errors: 0`.

## 4. Test it

- `tests/hidden`: enters Good Egg Galaxy, asks the game `MR::isActiveDefaultGameLayout` and takes a picture. It
  passes when the answer is false and the corners where the life meter and the coin counter stand show the sky.
- `tests/shown`: the same with the option off. It passes when the HUD is up and the meter is drawn.

```sh
lumalink test hide-the-hud
```

Not yet: the test harness does not yet start a mod with an option set, so `tests/shown` ends `harness-error`.

## 5. Play it

```sh
lumalink pack hide-the-hud
```

Drop `hide-the-hud.luma` on the game's window and switch it on. In the Mods screen, "Hide the HUD" turned off brings
the HUD back without a restart.

## See also

`language/language.md` (options, hooks, `skip`), `format/luma.md` (options), `lumalink/test-harness.md`.
