---
title: Playing in first person
shortTitle: First person
intro: Put the camera in Mario's head, turn it with the mouse, the pointer, a gyro or a finger, and let Mario run where the player looks.
contentType: tutorials
example: first-person
---

## Introduction

In this tutorial, you make a mod that shows the game through Mario's eyes. The player looks around with whatever they look with on their device: the mouse, the Wii Remote's pointer, a gyro or a finger. The game already moves Mario with the stick along the camera it shows, so he runs where the player looks. A key switches back to the game's camera during play.

You learn how to:

* Take the game's camera with LumaKit's `kit.camera` and put it on one of Mario's joints.
* Turn the camera with the player's look.
* Give the player options and a key of their own.
* Hand the camera back when a stage ends.
* Test a camera in the game.

## Prerequisites

* Nebula, installed with Nebula Setup.
* LumaLink. For more information, see [Installing LumaLink](../get-started/installing-lumalink.md).
* The [Hiding the HUD](hide-the-hud.md) tutorial, for options.

## Mod folder layout

```text
first-person/
  luma.json
  source/main.lum
  tests/play/test.json
  tests/probe/            a mod for the test: the camera and the player, as facts
```

## Step 1: Write the manifest

The mod has three options: first person on or off, the field of view, and the key that switches the camera.

```json
{
  "format": "1.0",
  "id": "first-person",
  "version": "1.0.0",
  "name": "First Person",
  "summary": "Shows the game through Mario's eyes. Look around with the mouse, the pointer or a gyro; Mario runs where you look.",
  "authors": ["you"],
  "game": { "id": "RMG", "regions": ["RMGE01", "RMGP01", "RMGJ01"] },
  "options": [
    { "key": "first_person", "kind": "toggle", "name": "First person", "default": true },
    { "key": "fov", "kind": "number", "name": "Field of view", "min": 40, "max": 110, "step": 5, "default": 75 },
    { "key": "switch", "kind": "key", "name": "Switch camera", "default": "one" }
  ],
  "needs": [{ "id": "lumakit", "version": ">=1.0.0" }]
}
```

A `key` option is a key the player can bind in the mod's settings. The code reads it as an input action of the same name.

## Step 2: Take the camera

`camera.take` makes the mod's camera the one the game shows. `camera.eye` puts it on a joint of a model: Mario's `EyePosition` joint, which moves with his head. `camera.look` turns it with the player's look.

```lumascript
use kit.camera
use svc.input

option first_person: bool
option fov: int

var fp: camera.Control? = none

fn begin() {
    if fp != none {
        return
    }
    guard let p = MarioAccess::getPlayerActor() else { return }
    let c = camera.take(camera.eye(p, "EyePosition"))
    camera.look(c)
    camera.fov(fov as f32)
    fp = c
}
```

While the camera is in his head, LumaKit doesn't draw Mario's body, so it never fills the view. His shadow still shows.

## Step 3: Give it back

`camera.end` gives the game its own camera again, and `camera.fov(none)` its own field of view:

```lumascript
fn finish() {
    if let c = fp {
        camera.end(c)
    }
    fp = none
    camera.fov(none)
}

on player ready {
    if wanted {
        begin()
    }
}

on stage ended {
    finish()
}
```

Each stage has a new player, so the mod takes the camera on `player ready` and lets it go on `stage ended`. Between stages, on the file select and in loads, the game's camera shows.

## Step 4: Switch during play

```lumascript
var wanted = true
var switcher: input.Action? = none

on start {
    wanted = first_person
    switcher = input.action("switch")
}

on frame {
    guard let a = switcher else { return }
    if input.read(a).pressed != 0 and holds("player") {
        wanted = not wanted
        if wanted {
            begin()
        } else {
            finish()
        }
    }
}
```

`pressed` is 1 on the frame the key goes down, so holding the key switches once. The options take effect at once:

```lumascript
on option first_person {
    wanted = first_person
    if not wanted {
        finish()
    } else if holds("player") {
        begin()
    }
}

on option fov {
    if fp != none {
        camera.fov(fov as f32)
    }
}
```

## Step 5: Test it in the game

The test uses a second mod, the probe, that writes what the camera shows as facts: how high the eye is above Mario's feet, how far the view turned, and which way Mario runs compared with the view. A `look` step turns the view as the player would, in degrees each frame:

```json
"steps": [
  { "at": 100, "look": [2, 0], "frames": 45 },
  { "at": 170, "picture": "turned" },
  { "at": 180, "stick": [0, 1], "frames": 50 },
  { "at": 240, "hold": ["ONE"], "frames": 2 },
  { "at": 290, "picture": "given-back" },
  { "at": 300, "end": true }
]
```

The expected facts: the eye about 100 units above his feet with his body hidden, the view turned 90 degrees after 45 frames of 2 degrees, Mario running along the view, and, after the switch key, the game's camera with his body drawn. The probe measures each stretch of first person from its own start, because the galaxy's opening scene shows the game's camera first. For more information, see [Test files](../reference/test-files.md).

```shell
lumalink test first-person
```

## Further reading

* [kit.camera](../reference/lumakit/camera.md)
* [Hiding the HUD](hide-the-hud.md)
* [Test files](../reference/test-files.md)
