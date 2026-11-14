# Your first mod

From an empty folder to a mod that counts Mario's jumps in the top left corner of the screen, with a test that
checks it. The whole mod is the folder `first-mod/` beside this page.

## 1. Make the folder

```sh
lumalink new jump-counter --id jump-counter --name "Jump Counter" --summary "Counts Mario's jumps in the top left corner." --author you
lumalink add need lumakit jump-counter
lumalink add text EuGerman jump-counter --name "Sprungzähler" --summary "Zählt Marios Sprünge oben links."
```

`new` writes `jump-counter/luma.json` (the mod's name, id and version) and an empty base layer. `add need` adds
LumaKit to `luma.json`: the counter on the screen is LumaKit's `kit.hud`. `add text` gives the name and summary in
one more language; the folder beside this page has all 14.

## 2. Write the code

Put this in `jump-counter/source/main.lum`:

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

- `on start` runs once, when the game starts with the mod on. It puts a counter showing 0 in the top left corner.
- `hook before Mario::tryJump()` runs each time the game is about to try a jump. The mod adds one, shows the new
  number and writes `jump 1`, `jump 2`, ... to the mod log.

## 3. Build and check

```sh
lumalink build jump-counter
lumalink check jump-counter
```

`build` turns `source/` into native code for every platform in `code/<platform>/`. A mistake in the code stops it
with the file, line and column. `check` reads the folder as the game will and prints every finding with its code;
it ends `Errors: 0`.

## 4. Test it

Put this in `jump-counter/tests/jump/test.json`:

```json
{
  "harness": 1,
  "name": "jump-counter",
  "about": "One jump in a galaxy shows 1 in the top left corner",
  "proves": ["script.build", "kit.hud"],
  "mods": ["../../../lumakit", "../.."],
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

```sh
lumalink test jump-counter
```

`test` starts the game hidden and silent with LumaKit and your mod, enters Good Egg Galaxy, presses A once and takes
a picture before and after. It passes when the mod log has `jump 1`. The pictures are in the run folder it names.

## 5. Play it

```sh
lumalink pack jump-counter
```

`pack` writes `jump-counter.luma`. Drop it on the game's window and switch it on in the Mods screen. Each jump adds
one to the counter.

Not yet: a LumaKit HUD element made in `on start` stops the game at its first screen; until that is fixed, this mod
builds and checks but its test does not pass.

## See also

`format/luma.md`, `format/rules.md`, `language/language.md`, `lumakit/hud.md`, `lumalink/test-harness.md`.
