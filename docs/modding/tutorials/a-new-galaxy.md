---
title: Adding a new galaxy
shortTitle: A new galaxy
intro: Add a galaxy of your own, with its stage, enemies, a boss with its own cutscene and music, and text in every language.
contentType: tutorials
example: a-new-galaxy
---

## Introduction

In this tutorial, you add Orb King Galaxy, a galaxy the game never had. It is a small mossy planet with three Spikerolls that roll toward Mario, and the Orb King, a boss that wakes with its own cutscene, shows a life meter and plays its own music. Stomping the Orb King three times makes the galaxy's star appear.

Every model, sound and table in this mod is made for it. Nothing is copied from the game.

You learn how to:

* Generate models and music with a script.
* Build a stage from the game's tables.
* Define a galaxy with LumaKit's `stage` module, and enemies and a boss with the `actor` module.
* Play a cutscene with the `cutscene` module, and show a meter with the `hud` module.

## Prerequisites

* Nebula, installed with Nebula Setup.
* LumaLink. For more information, see [Installing LumaLink](../get-started/installing-lumalink.md).
* Python 3, to run the script that generates the mod's models and sounds. The script needs no extra packages.

## Creating the mod

Create the mod folder, and add LumaKit to the mod's needs.

```sh
lumalink new orb-galaxy --id orb-galaxy --name "Orb King Galaxy" --summary "A new galaxy: a mossy planet, rolling Spikerolls and the Orb King, who guards its star." --author YOUR-NAME
lumalink add need lumakit orb-galaxy
```

The galaxy, the enemies, the cutscene and the meter use LumaKit's `kit.stage`, `kit.actor`, `kit.cutscene` and `kit.hud` modules.

## Generating the content

The finished mod includes `source/content/make_content.py`, which generates every model and sound from code. Copy it into your mod, then run it from the mod folder:

```sh
python source/content/make_content.py
```

The script writes these files to `layers/base/assets/`:

| File | Description |
|---|---|
| `NebulaPlanet.glb` | The planet: a sphere 20 m across with gentle bumps, normals, UVs and a generated moss texture. |
| `NebulaPlanet.kcl.glb` | The ground Mario walks on: the same surface at a lower resolution. A `.kcl.glb` file is converted into collision. |
| `Spikeroll.glb` | The enemy: a purple ball with gold spikes and eyes, skinned to a two-joint skeleton, with the animations `Wait`, `Roll` and `Down`. |
| `OrbKing.glb` | The boss: a blue sphere with red eyes and a gold crown on its own joint, with the animations `Wait`, `Slam`, `Dizzy`, `Hurt` and `Down`. |
| `NebulaTheme.wav`, `BossTheme.wav` | The music: a calm loop in a major key and a driving loop in a minor key, at 32 kHz stereo. |
| `NebulaTheme.sound.json` | The galaxy's music as a stream. A stream is read from disk as it plays, so it can be several minutes long: `{"kind": "stream", "loop": true, ...}`. |
| `BossTheme.sound.json` | The boss music as a sound held in memory, which can be at most 60 seconds long: `{"kind": "effect", "bus": "music", "loop": true, ...}`. |

glTF lengths are in meters, and the game uses 100 of its units for each meter. An animation's name is the name of the action your code starts. The game converts each file when the player imports the mod.

## Building the stage

A galaxy is a stage made of tables in `layers/base/files/StageData/`:

| File | Contents |
|---|---|
| `OrbKingGalaxyScenario.arc/zonelist.bcsv.json` | The galaxy's zones. This galaxy has one zone, the galaxy itself. |
| `OrbKingGalaxyScenario.arc/scenariodata.bcsv.json` | The galaxy's single scenario, with the star's name. |
| `OrbKingGalaxy.arc/jmp/start/common/startinfo.json` | Where Mario starts: 1150 units above the center, on top of the planet. |
| `OrbKingGalaxy.arc/jmp/placement/common/planetobjinfo.json` | A `GlobalPointGravity` at the center, so everything falls toward the planet. |
| `OrbKingGalaxy.arc/jmp/placement/common/objinfo.json` | Empty. The code places the planet, the enemies and the boss. |
| `OrbKingGalaxy.arc/camera/cameraparam.bcam.json` | The cameras. The galaxy has none of its own, so the game uses its default camera. |
| The other tables in `jmp/` | Empty. `path/commonpathinfo` has no rail, so the stage has no opening flight and no `ScenarioStarter`, which needs a rail named by its `CommonPath_ID`. |

Each row names its columns: `name`, `l_id`, `pos_x` to `pos_z`, `dir_x` to `dir_z`, `scale_x` to `scale_z`, and `Obj_arg0` to `Obj_arg7`. To see how the game's own stages are built, extract one in the same form with `lumalink extract --game GAME-PAK -o OUTPUT-FOLDER StageData/GALAXY-NAME`. For more information, see [lumalink extract](../reference/lumalink-commands/extract.md).

The galaxy's name and the star's name, in all 14 languages, are in `layers/base/merge/MessageData/messages.json`.

## Writing the code

Create `orb-galaxy/source/main.lum` with this code:

```lumascript
use kit.stage
use kit.actor
use kit.cutscene
use kit.hud
use kit.item
use kit.gravity
use svc.audio

let galaxy = "OrbKingGalaxy"
let king_most: i32 = 3

// The Orb King's fight: its health, the meter showing it, what it is doing and for how many frames.
var health: i32 = 3
var meter: hud.Meter? = none
var awake = false
var mood = "Wait"
var mood_frames: u32 = 0

on start {
    stage.define_galaxy(stage.Galaxy { name: galaxy, title: text "OrbKingGalaxy", dome: stage.Dome.Terrace,
        stars: [stage.Star { scenario: 1, title: text "OrbKingGalaxy_Star1" }] })
    stage.music(galaxy, none, sound "NebulaTheme")
    actor.define(actor.Kind { name: "Spikeroll", look: model "Spikeroll", idle_action: "Wait",
        sensors: [actor.Sensor.Body(70.0), actor.Sensor.Attack(85.0)],
        defeated_by: [actor.Attack.Spin, actor.Attack.Trample, actor.Attack.HipDrop, actor.Attack.StarBit],
        drops: item.Gives.Coins(3), on_frame: roll })
    actor.define(actor.Kind { name: "OrbKing", look: model "OrbKing", idle_action: "Wait",
        sensors: [actor.Sensor.Body(240.0), actor.Sensor.Attack(250.0)],
        defeated_by: [actor.Attack.Trample, actor.Attack.HipDrop], on_frame: king_step, on_attacked: king_hit })
    stage.build(galaxy, fn(b: stage.Builder) {
        stage.planet(b, model "NebulaPlanet", model "NebulaPlanet", vec3(0.0, 0.0, 0.0))
        stage.field(b, gravity.Field.Point { at: vec3(0.0, 0.0, 0.0) })   // everything falls toward the planet's centre
        stage.place(b, "orb-galaxy/Spikeroll", vec3(800.0, 600.0, 0.0))
        stage.place(b, "orb-galaxy/Spikeroll", vec3(-800.0, 600.0, 0.0))
        stage.place(b, "orb-galaxy/Spikeroll", vec3(0.0, 600.0, -800.0))
        stage.place(b, "orb-galaxy/OrbKing", vec3(0.0, -1000.0, 0.0), vec3(180.0, 0.0, 0.0))
    })
}

// A Spikeroll rolls at Mario once he is near.
fn roll(a: LiveActor) {
    if actor.near_player(a, 900.0) {
        MR::tryStartAction(a, "Roll")
        actor.move_toward_player(a, 0.4)
    } else {
        MR::tryStartAction(a, "Wait")
    }
}

fn set_mood(k: LiveActor, to: text) {
    mood = to
    mood_frames = 0
    MR::tryStartAction(k, to)
}

// The Orb King wakes when Mario comes near: its scene, then the fight with its own music and meter.
task wake(k: ref<LiveActor>) {
    log "the Orb King wakes"
    let watched = wait cutscene.play(cutscene.Scene { shots: [
        cutscene.Shot.Look(k, vec3(0.0, 500.0, 1400.0), 90),
        cutscene.Shot.Action(k, "Slam"),
        cutscene.Shot.Wait(40)] })
    let m = hud.meter(hud.Corner.TopRight, king_most)
    hud.set_meter(m, health)
    meter = m
    audio.music("BossTheme", 500)
    mood_frames = 0
}

// It walks toward Mario, charges at him, then sits dizzy: the time to stomp it.
fn king_step(k: LiveActor) {
    if not awake {
        if actor.near_player(k, 1500.0) {
            awake = true
            start wake(ref(k))
        }
        return
    }
    if cutscene.active() {
        return
    }
    mood_frames += 1
    if mood == "Wait" {
        actor.move_toward_player(k, 0.3)
        if mood_frames >= 180 {
            set_mood(k, "Slam")
        }
    } else if mood == "Slam" {
        actor.move_toward_player(k, 1.6)
        if mood_frames >= 60 {
            set_mood(k, "Dizzy")
        }
    } else if mood == "Dizzy" {
        if mood_frames >= 150 {
            set_mood(k, "Wait")
        }
    } else if mood_frames >= 40 {
        set_mood(k, "Wait")
    }
}

// A stomp while it is dizzy costs it one health; the third beats it and its star appears.
fn king_hit(k: LiveActor, attack: actor.Attack) -> bool {
    if mood != "Dizzy" {
        return true
    }
    health -= 1
    log fact "king_health" health
    if let m = meter {
        hud.set_meter(m, health)
    }
    if health > 0 {
        set_mood(k, "Hurt")
        return true
    }
    if let m = meter {
        hud.remove_meter(m)
    }
    meter = none
    audio.music("NebulaTheme", 1000)
    stage.appear_star(k.mPosition, 1)
    log "the Orb King is beaten"
    return false
}
```

* `stage.define_galaxy` makes the stage a galaxy with one star. `text "..."` refers to a line in `messages.json`, shown in the player's language. `stage.music` plays `NebulaTheme` in the stage instead of the game's music.
* `actor.define` defines a kind of object from a model: its sensors (`Body` for what touches it and `Attack` for what hurts Mario), the attacks that defeat it, what it drops, and a function that runs every frame. A kind is placed in a stage as `MOD-ID/NAME`.
* `stage.build` runs each time the stage loads. It places the planet with its model and ground, three Spikerolls on its upper half, and the Orb King upside down on the far side so that it stands on the planet. To place other objects when a stage starts, use `on stage "NAME" { }`. It runs once the stage is ready and Mario is at its starting point.
* The Orb King waits until Mario is within 1500 units. Then it plays its cutscene (the camera turns to it, it slams the ground, and there is a short pause), shows a meter of 3 at the top right of the screen, and switches the music to `BossTheme`. It then walks, charges and sits dizzy in turn. Only a stomp while it is dizzy counts. The third stomp makes the star appear where the Orb King stood, and the galaxy's music returns.

## Building and checking the mod

```sh
lumalink build orb-galaxy
lumalink check orb-galaxy
```

`lumalink check` reads every content file the way the game's importer does, including models, collision, sounds, tables and text. The last line of the output is `Errors: 0`.

## Testing the mod

The finished mod includes two tests:

* `tests/visit` enters the galaxy and takes a picture. The test passes when the stage is ready, the galaxy's music is playing as a stream, and the ground under Mario is the moss green of the planet.
* `tests/boss` starts Mario near the Orb King. The test passes when the Orb King wakes and, after its cutscene, its meter appears at the top right.

To run the tests, copy the `tests` folder from the finished mod into `orb-galaxy`, then run:

```sh
lumalink test orb-galaxy
```

## Playing the mod

1. Pack the mod.

   ```sh
   lumalink pack orb-galaxy
   ```

1. Drag the `.luma` file onto the game's window, and turn the mod on in the **Mods** page.

## Next steps

* [Playing a video inside a galaxy](a-video-in-a-galaxy.md)
* [LumaKit: stage](../reference/lumakit/stage.md)
* [LumaKit: cutscene](../reference/lumakit/cutscene.md)
* [Placing objects in stages](../how-tos/placing-objects-in-stages.md)
