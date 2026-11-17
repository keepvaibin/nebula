---
title: Reloading a mod while playing
shortTitle: Reload while playing
intro: Play your mod in dev mode so that each code change you save is built and reloaded in the running game.
contentType: how-tos
---

## About dev mode

Dev mode is for the person who makes a mod. When you play a mod folder that has code, LumaLink starts the game in dev mode for that mod. In dev mode:

* Your mod's code and every permission it declares are allowed without the question players see. Nothing is saved, so the next start without dev mode asks as usual.
* A crash doesn't turn your mod off for the next start, even when the crash names it. Other mods follow the usual rules. For more information, see [When a mod fails](../concepts/when-a-mod-fails.md#the-next-start-after-a-crash).
* Each time you save a change to a file in `source/`, LumaLink builds the mod and the game reloads its code while you play.

Dev mode is available on Windows, Linux, and macOS. Nebula for Android and iOS has no dev mode.

## Prerequisites

* Nebula, installed with Nebula Setup.
* LumaLink. For more information, see [Installing LumaLink](../get-started/installing-lumalink.md).
* A mod folder with code in `source/`. For more information, see [Creating a mod](creating-a-mod.md).

## Starting the game in dev mode

Use the LumaLink window or the command line.

### Starting from the LumaLink window

1. In LumaLink, select your mod folder.
1. Click **Play in Nebula**.

LumaLink builds the mod, adds it to the game's mods, and starts the game. The window shows "Playing MOD-ID in dev mode."

### Starting from the command line

1. Open a terminal in your mod folder.
1. Run `lumalink run`:

   ```shell
   lumalink run .
   ```

   To pass options to the game, put them after `--`. This example ends the game after 10 minutes:

   ```shell
   lumalink run . -- --seconds 600
   ```

LumaLink keeps running until you close the game.

## Changing code while you play

1. With the game running in dev mode, edit a file in your mod's `source/` folder.
1. Save the file.

LumaLink shows each step:

| Message | Meaning |
| ------- | ------- |
| "The code changed. Building it." | LumaLink saw the change and is building the mod. |
| "Built. The game is reloading it (N)." | The build passed. The game takes out the mod and loads the new build. N counts the reloads. |
| "Reloaded in the game." | The new code is running. |
| "The game did not reload it: REASON" | The game refused the new build. The mod stays off until a build that passes. |
| "Changes other than code show the next time the game starts." | You changed `luma.json` or content. Restart the game to see it. |
| "The game was closed." | Dev mode ended. |

When the build fails, LumaLink shows the errors and the game keeps the code it has. Fix the errors and save again.

When you save while a reload is still running, LumaLink waits for the game's answer, then builds again.

## Keeping values across a reload

A reload ends the old code and starts the new code from nothing. Each service your mod uses starts fresh, and every value in your mod's code is reset. To carry a value into the new code, keep it when the mod stops and read it back when the mod starts.

1. In your mod's `on stop`, call `keep` with the value. The value must be JSON data: `bool`, a number, `text`, a vector, a list, map, or optional of JSON data, or a struct of your mod whose fields are JSON data.
1. In your mod's `on start`, before its first `wait`, call `kept` with the value's type. `kept` returns `none` when nothing was kept, for example on the first start, or when the kept value no longer matches the type because you changed the struct.

This mod keeps a coin count and a best time across reloads:

```lumascript
struct Saved { coins: int, best: u32 }

var coins = 0
var best_time: u32 = 0

on start {
    if let saved = kept<Saved>() {
        coins = saved.coins
        best_time = saved.best
    }
}

on stop {
    keep(Saved { coins: coins, best: best_time })
}
```

A kept value can be at most 1 MiB once encoded. `keep` returns `false` for a larger value and writes a line to the mod log. Kept values last until the game closes.

In C, use LumaCore's `keep(mod, bytes, size)` in `stop` and `kept(mod, &size)` in `start`. For more information, see the comments in `luma.h`.

## How a reload works

The game checks for a reload request every 30 frames, at the start of a frame. When it finds one:

1. The game takes out the mods that use your mod, then your mod. Each mod's `on stop` runs, the game frees the memory the mod still holds, and the mod's library closes. The game keeps running while it waits.
1. The game loads your mod from its installed folder, then the mods that use it, in their start order. Each load is checked as at a start.
1. The game answers with the result, which LumaLink shows.

If the mods are not taken out within 600 frames, the reload ends and is not done.

On Windows, the game runs each mod's code from a copy, so LumaLink can replace the installed files while the game runs.

## Troubleshooting

* "This version of the game does not give the mod's ID, so changes are not reloaded." Your Nebula is older than LumaLink. Update Nebula.
* The game did not reload the mod. The reason names the check that failed, for example a service or permission the new build needs that the mod did not have at its start. Restart the game to load the mod with its new needs.
* A mod that uses yours stays off after a reload. It starts only when your mod starts. Fix the build of your mod, then save again.

## Testing a reload

A test can reload a running mod with the step `{ "reload": "MOD-ID" }`. The next step waits until the reload ends, up to 600 frames. Replace MOD-ID with your mod's `id` from `luma.json`. For more information, see [Test files](../reference/test-files.md).

## Further reading

* [lumalink run](../reference/lumalink-commands/run.md)
* [Testing a mod](testing-a-mod.md)
* [How Nebula loads mods](../concepts/how-nebula-loads-mods.md)
