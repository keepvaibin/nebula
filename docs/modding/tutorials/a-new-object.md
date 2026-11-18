---
title: Adding a new object to a stage
shortTitle: A new object
intro: Make an object of your own, with its own model, ground Mario stands on and behaviour, and place it in one of the game's stages.
contentType: tutorials
example: a-new-object
---

## Introduction

In this tutorial, you add the Sky Lift to the Good Egg Galaxy: a platform floating above the first planet. When Mario stands on it, it rises, up to 300 units, and holds there. When he leaves, it sinks back to its place.

You learn how to:

* Make a model and its ground: the part Mario stands on.
* Define a kind of object with LumaKit's `kit.actor`, with code that runs each step.
* Place the object in a stage with a placement patch.
* Move the object, and Mario with it.
* Test an object in the game.

## Prerequisites

* Nebula, installed with Nebula Setup.
* LumaLink. For more information, see [Installing LumaLink](../get-started/installing-lumalink.md).
* The [Changing a stage](a-stage-edit.md) tutorial, for placement tables.

## Mod folder layout

```text
a-new-object/
  luma.json
  layers/base/assets/SkyLift.glb          the look
  layers/base/assets/SkyLift.kcl.glb      the ground
  layers/base/patches/StageData/EggStarGalaxy.arc/jmp/placement/common/objinfo.json
  source/main.lum
  source/make_lift.py                     writes both models
  tests/ride/test.json
```

## Step 1: Make the model and its ground

Make the model in any 3D tool and export it as glTF 2.0 binary (`.glb`), in metres. The Sky Lift is a slab 3 metres square, with its top at the model's origin, so the lift stands where it is placed and Mario stands on its placed point.

The ground is a second model, `SkyLift.kcl.glb`, beside the first. It is never drawn: it is what Mario stands on and runs into. Each of its materials is one kind of ground, set in the material's custom properties:

```json
"extras": { "Floor_code": 0, "Wall_code": 0, "Sound_code": 1, "Camera_id": -1, "Camera_through": 0 }
```

`Floor_code` 0 is plain floor, and `Sound_code` 1 the game's plain footsteps. For more information, see [Adding models](../how-tos/adding-models.md).

The finished mod makes both models with a short script, `source/make_lift.py`, so you can change the size and build it again.

## Step 2: Define the object

`actor.define` makes a kind of object. Its `name` is the name a stage places it by, after the mod's id: `sky-lift/SkyLift`.

```lumascript
use kit.actor

on start {
    actor.define(actor.Kind {
        name: "SkyLift"
        look: model "SkyLift"
        collision: "SkyLift"
        on_ground: false
        shadow: false
        sensors: [actor.Sensor.Body(150.0)]
        on_init: placed
        on_frame: step
    })
}
```

* `collision` names the ground model, `SkyLift.kcl.glb`. It moves with the object, and Mario standing on it moves with it.
* `on_ground: false` keeps the object where it is placed. Without it, the object falls onto the ground below, as the game's walkers do.
* `on_init` runs once for each lift the stage makes, and `on_frame` runs each step.

## Step 3: Make it rise

Each lift keeps the place the stage gave it and how far it has risen:

```lumascript
let rise_most: f32 = 300.0
let speed: f32 = 2.0

var lifts: list<ref<LiveActor>> = []
var homes: list<vec3> = []
var risen: list<f32> = []

fn placed(a: LiveActor) {
    lifts.push(ref(a))
    homes.push(a.mPosition)
    risen.push(0.0)
}
```

A `ref` doesn't keep the object alive: when the stage ends, `get()` gives `none`. Mario rides a lift when he is on the ground over its top:

```lumascript
fn ridden(a: LiveActor, up: vec3) -> bool {
    guard holds("player"), let feet = MR::getPlayerPos() else { return false }
    if not MR::isOnGroundPlayer() {
        return false
    }
    var front = vec3(0.0, 0.0, 1.0)
    MR::calcFrontVec(var front, a)
    let side = cross(up, front)
    let d = feet - a.mPosition
    let above = dot(d, up)
    return above > -5.0 and above < 40.0 and abs(dot(d, front)) < 160.0 and abs(dot(d, side)) < 160.0
}
```

Each step, the lift moves 2 units up while ridden and 2 down while not, along its own up:

```lumascript
fn step(a: LiveActor) {
    guard holds("stage"), let i = index_of(a) else { return }
    var up = vec3(0.0, 1.0, 0.0)
    MR::calcUpVec(var up, a)
    let h = risen[i]
    let next = if ridden(a, up) { min(h + speed, rise_most) } else { max(h - speed, 0.0) }
    risen[i] = next
    a.mPosition = homes[i] + up * next
}

on stage ended {
    lifts = []
    homes = []
    risen = []
}
```

`index_of` finds the lift in the list with `same(l, a)`, which compares two objects themselves rather than their fields. The finished `main.lum` has it.

## Step 4: Place it

A placement patch adds the lift to the galaxy's table, 150 units over the first planet's ground, near Mario's start:

```json
{
  "patch": 1,
  "key": ["l_id"],
  "add": [
    { "name": "sky-lift/SkyLift", "l_id": 9801, "pos_x": -3200.0, "pos_y": -12950.0, "pos_z": -15950.0,
      "dir_x": 0.0, "dir_y": 0.0, "dir_z": 0.0 }
  ]
}
```

The mod defines its kind in `on start`, before any stage loads, so the stage finds `sky-lift/SkyLift` when it makes its objects. For more information, see [Placing objects in stages](../how-tos/placing-objects-in-stages.md).

## Step 5: Test it in the game

The test puts Mario 100 units over the lift, lets him land, waits while it carries him, then runs him off:

```json
"player": { "position": [-3200.0, -12850.0, -15950.0] },
"steps": [
  { "at": 20, "call": "MR::isOnGroundPlayer", "signature": "bool()", "args": [], "fact": "landed" },
  { "at": 20, "call": "MR::getPlayerPos", "signature": "Vec3f *()", "args": [], "fact": "on_top" },
  { "at": 200, "call": "MR::getPlayerPos", "signature": "Vec3f *()", "args": [], "fact": "carried" },
  { "at": 210, "stick": [0, 1], "frames": 90 },
  { "at": 420, "count": "object:LumaObj", "near": [-3200.0, -12950.0, -15950.0], "within": 5, "fact": "lift_home" },
  { "at": 422, "end": true }
]
```

The expected facts: he landed on the lift's top, not 150 units lower on the planet (it starts rising as he lands, so by frame 20 it has risen a few units); it carried him exactly 300 units up; and after he left, one object stands within 5 units of the lift's place. A LumaKit object's name in the game is `LumaObj`. For more information, see [Test files](../reference/test-files.md).

```shell
lumalink test a-new-object
```

## Further reading

* [kit.actor](../reference/lumakit/actor.md)
* [Adding models](../how-tos/adding-models.md)
* [Placing objects in stages](../how-tos/placing-objects-in-stages.md)
* [Changing a stage](a-stage-edit.md)
