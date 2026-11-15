---
title: Creating a mod
shortTitle: Create a mod
intro: Create a mod folder with LumaLink, then add what the mod needs, its text in other languages, its options, its permissions and its files.
contentType: how-tos
---

## Prerequisites

* LumaLink. For more information, see [Installing LumaLink](../get-started/installing-lumalink.md).
* Nebula, installed with Nebula Setup, with your game imported. LumaLink checks your mod against the installed game.

## Creating the mod folder

1. In a terminal, go to the folder where you keep your mods.
1. Create the mod.

   ```shell
   lumalink new MOD-FOLDER --id MOD-ID --name "MOD NAME" --summary "ONE LINE ON WHAT IT DOES" --author YOUR-NAME
   ```

   Replace these values:

   * MOD-FOLDER: the folder to create. It must not exist yet.
   * MOD-ID: the mod's ID, 3 to 64 characters of `a`-`z`, `0`-`9`, `-` and `.`, starting with a letter. The ID never changes once players have your mod.
   * MOD NAME and ONE LINE ON WHAT IT DOES: what players see on the **Mods** page.
   * YOUR-NAME: your name. To list several authors, repeat `--author`.

LumaLink writes `luma.json` and an empty `base` layer.

## Adding to the mod

Each `lumalink add` command changes `luma.json` or copies a file into the right place, and checks the result. The form is always `lumalink add ITEM VALUE MOD-FOLDER`.

| To add | Run |
|---|---|
| A mod your mod needs, such as LumaKit | `lumalink add need lumakit MOD-FOLDER` |
| The name and summary in another language | `lumalink add text EuGerman MOD-FOLDER --name "NAME" --summary "SUMMARY"` |
| An option the player sets | `lumalink add option speed MOD-FOLDER --kind number --default 2 --min 1 --max 5 --name Speed` |
| A permission | `lumalink add permission net.internet MOD-FOLDER --host api.example.org --why "Uploads your best times."` |
| A file at a game path | `lumalink add file title.arc MOD-FOLDER --at LayoutData/Title.arc` |
| A new asset of the mod's own | `lumalink add asset ball.glb MOD-FOLDER` |
| A replacement for a game texture | `lumalink add texture CoinBody.png MOD-FOLDER --at Coin/CoinBody` |
| A source file for code | `lumalink add script main.lum MOD-FOLDER` |
| An icon for the **Mods** page | `lumalink add icon icon.png MOD-FOLDER` |
| A mod yours cannot run beside | `lumalink add conflict OTHER-ID MOD-FOLDER --why "Both change the Fire Flower."` |

For every item and option, see [lumalink add](../reference/lumalink-commands/add.md).

## Writing code

Put LumaScript files in `source/`. The mod's entry is `source/main.lum`. For a first example, see [Quickstart for Nebula mods](../get-started/quickstart.md).

## Checking and building

1. Check the mod with the same rules Nebula uses.

   ```shell
   lumalink check MOD-FOLDER
   ```

   The check ends with the number of errors and notes. Fix every error. For what a code means, run `lumalink explain CODE`.

1. If the mod has code, build it for every platform.

   ```shell
   lumalink build MOD-FOLDER
   ```

   LumaLink writes the libraries to `code/`.

## Trying it in the game

To start Nebula with your mod from its folder, run:

```shell
lumalink run MOD-FOLDER
```

In the LumaLink window, **Play in Nebula** does the same.

### Changing code while you play

When the mod folder has code, the game starts in dev mode. Each time you save a change to a file in `source/`, LumaLink builds the mod and the game reloads its code while you play. LumaLink shows whether the reload worked. Changes to `luma.json` or to content show the next time the game starts. To stop, close the game.

In dev mode, your mod's code and the permissions it declares are allowed without asking, and a crash doesn't turn its code off. Dev mode is available on Windows, Linux, and macOS.

To keep values across a reload, call `keep(value)` in your mod's `on stop` and `kept<T>()` in its `on start` (in C, LumaCore's `keep` in `stop` and `kept` in `start`).

For more information, see [lumalink run](../reference/lumalink-commands/run.md).

## Next steps

* [Testing a mod](testing-a-mod.md)
* [Packing and sharing a mod](packing-and-sharing-a-mod.md)
