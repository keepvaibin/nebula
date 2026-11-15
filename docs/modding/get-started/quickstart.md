---
title: Quickstart for Nebula mods
shortTitle: Quickstart
intro: Make a mod that counts Mario's jumps on the screen, test it in the game, and pack it into a .luma file.
contentType: get-started
---

## Introduction

In this quickstart, you make a mod that shows how many times Mario has jumped, in the top-left corner of the screen. You write its code in LumaScript, test it in the game, and pack it into a `.luma` file that anyone can import.

The finished mod is in the `first-mod` folder beside the tutorials.

## Prerequisites

* Nebula, installed with Nebula Setup.
* LumaLink. For more information, see [Installing LumaLink](installing-lumalink.md).

## Creating the mod

1. Create the mod folder.

   ```shell
   lumalink new jump-counter --id jump-counter --name "Jump Counter" --summary "Counts Mario's jumps in the top left corner." --author YOUR-NAME
   ```

   Replace YOUR-NAME with your name. LumaLink writes `jump-counter/luma.json` with the mod's name, ID and version, and an empty `base` layer.

1. Add LumaKit, which draws the counter.

   ```shell
   lumalink add need lumakit jump-counter
   ```

1. Optionally, add the name and summary in another language. This example adds German.

   ```shell
   lumalink add text EuGerman jump-counter --name "Sprungzähler" --summary "Zählt Marios Sprünge oben links."
   ```

## Writing the code

Create `jump-counter/source/main.lum` with this code:

```lumascript
use kit.hud

var jumps: i64 = 0
var counter: hud.Counter? = none

on start {
    counter = hud.counter(hud.Corner.TopLeft, 0)
}

hook before Mario::tryJump() {
    jumps += 1
    if let c = counter {
        hud.set_counter(c, jumps)
    }
    log "jump {jumps}"
}
```

* `on start` runs once, when the game starts with the mod on. It puts a counter showing 0 in the top-left corner.
* `hook before Mario::tryJump()` runs each time the game is about to try a jump. It adds one, shows the new number, and writes `jump 1`, `jump 2` and so on to the mod log.

## Building and checking the mod

1. Build the mod's code.

   ```shell
   lumalink build jump-counter
   ```

   LumaLink turns `source` into native code for every platform, in `code/PLATFORM`. A mistake in the code stops the build with the file, line and column.

1. Check the mod.

   ```shell
   lumalink check jump-counter
   ```

   LumaLink reads the folder the way the game will, and prints each finding with its code. The last line is `Errors: 0`.

## Testing the mod

1. Create `jump-counter/tests/jump/test.json` with this test:

   ```json
   {
     "harness": 1,
     "name": "jump-counter",
     "about": "One jump in a galaxy shows 1 in the top left corner",
     "proves": ["script.build", "kit.hud"],
     "mods": ["../.."],
     "stage": { "name": "EggStarGalaxy", "scenario": 1 },
     "steps": [
       { "at": 20, "picture": "before" },
       { "at": 30, "hold": ["A"], "frames": 8 },
       { "at": 120, "picture": "after" },
       { "at": 130, "end": true }
     ],
     "expect": [
       { "log": "jump 1" }
     ]
   }
   ```

1. Run the test.

   ```shell
   lumalink test jump-counter
   ```

LumaLink starts the game hidden and silent with your mod, enters Good Egg Galaxy, presses <kbd>A</kbd> once and takes a picture before and after. The test passes when the mod log has `jump 1`. The pictures are in the run folder that LumaLink names.

## Playing the mod

1. Pack the mod.

   ```shell
   lumalink pack jump-counter
   ```

   LumaLink writes `jump-counter.luma`.

1. Drag `jump-counter.luma` onto the game's window, and turn the mod on in the **MODS** page.
1. Play. Each jump adds one to the counter.

## Next steps

* [Making a small tweak](../tutorials/a-small-tweak.md)
* [About LumaScript](../concepts/lumascript.md)
* [Testing a mod](../how-tos/testing-a-mod.md)
