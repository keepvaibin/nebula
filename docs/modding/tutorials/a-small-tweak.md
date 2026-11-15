---
title: A small tweak: six health
shortTitle: A small tweak
intro: Change one number of the game by its name, so Mario starts each level with six health.
contentType: tutorials
---

## About this tutorial

One number of the game changed by its name: Mario starts each level with six health instead of three. When the mod
is switched off or taken out, the number is the game's again. The whole mod is the folder [`a-small-tweak`](../guides/a-small-tweak/) in the documentation.

## Step 1: Make the folder

```sh
lumalink new six-health --id six-health --name "Six Health" --summary "Mario starts each level with six health." --author you
```

## Step 2: Write the code

Put this in `six-health/source/main.lum`:

```lumascript
on start {
    set_number("Game/System/GameRules.cpp#cPlayerHealth", 6)
}
```

`set_number` changes one of the game's numbers by its name: the file of the game's source that holds it, `#`, and the
number's name. `cPlayerHealth` is the health Mario has when a level starts, and the size of his life meter. The game
keeps what the number was; when the mod stops, every number it set is put back.

## Step 3: Build and check

```sh
lumalink build six-health
lumalink check six-health
```

`check` ends `Errors: 0`. A name the game does not have is refused when the mod starts, with the name in the mod log.

## Step 4: Test it

The folder has two tests.

- `tests/six`: enters Good Egg Galaxy, reads Mario's health and its most (`MarioActor.mHealth`, `mMaxHealth`) and
  takes a picture. It passes when both are 6 and the life meter shows the green of six health.
- `tests/taken-out`: reads 6, takes the mod out while the game runs, enters the galaxy again and reads 3. It passes
  when the mod log says `its 1 numbers are the game's again (or another mod's)`.

```sh
lumalink test six-health
```

Each test runs the game hidden and silent and ends `pass` or `fail` with the run folder.

## Step 5: Play it

```sh
lumalink pack six-health
```

Drop `six-health.luma` on the game's window and switch it on in the Mods screen. The next level starts with six
health.

## See also

`language/language.md` (numbers), `format/rules.md`, `lumalink/test-harness.md`, `guides/first-mod.md`.

## Further reading

* [Tutorials](index.md)
* [LumaScript reference](../reference/lumascript/index.md)
* [Testing a mod](../how-tos/testing-a-mod.md)
