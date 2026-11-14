# A new galaxy

A galaxy the game never had: Orb King Galaxy, a small mossy planet with three Spikerolls that roll at Mario and the
Orb King, a boss that wakes with its own scene, shows a life meter, plays its own music and gives the galaxy's star
when it is stomped three times. Every model, sound and table is made for it; nothing is taken from the game. The
whole mod is the folder `a-new-galaxy/` beside this page.

## 1. Make the folder

```sh
lumalink new orb-galaxy --id orb-galaxy --name "Orb King Galaxy" --summary "A new galaxy: a mossy planet, rolling Spikerolls and the Orb King, who guards its star." --author you
lumalink add need lumakit orb-galaxy
```

The galaxy, the enemies, the scene and the meter are LumaKit's `kit.stage`, `kit.actor`, `kit.cutscene` and
`kit.hud`.

## 2. Make the content

`source/content/make_content.py` makes every model and sound from numbers (Python 3, nothing to install). Run it
from the mod's folder:

```sh
python source/content/make_content.py
```

It writes `layers/base/assets/`:

| File | What it is |
|---|---|
| `NebulaPlanet.glb` | the planet: a sphere 20 m across with gentle bumps, normals, UVs and a generated moss texture |
| `NebulaPlanet.kcl.glb` | the ground Mario walks on: the same surface, coarser; a `.kcl.glb` is cooked as collision |
| `Spikeroll.glb` | the enemy: a purple ball with gold spikes and eyes, skinned to a two-joint skeleton, clips `Wait`, `Roll`, `Down` |
| `OrbKing.glb` | the boss: a blue sphere with red eyes and a gold crown on its own joint, clips `Wait`, `Slam`, `Dizzy`, `Hurt`, `Down` |
| `NebulaTheme.wav`, `BossTheme.wav` | the music: a calm major loop and a driving minor loop, 32 kHz stereo |
| `*.sound.json` | each WAV as a looping sound on the music volume: `{"kind": "effect", "bus": "music", "loop": true, ...}` |

glTF lengths are metres; the game uses 100 of its units for each. A clip's name is the action the code starts. The
game cooks each file when the mod is imported.

## 3. The stage

A galaxy is a stage of tables under `layers/base/files/StageData/`:

| File | What it holds |
|---|---|
| `OrbKingGalaxyScenario.arc/zonelist.bcsv.json` | the zones of the galaxy: one, the galaxy itself |
| `OrbKingGalaxyScenario.arc/scenariodata.bcsv.json` | its one scenario, with the star's name |
| `OrbKingGalaxy.arc/jmp/start/common/startinfo.json` | where Mario starts: 1150 above the centre, on top of the planet |
| `OrbKingGalaxy.arc/jmp/placement/common/planetobjinfo.json` | `GlobalPointGravity` at the centre: everything falls toward the planet |
| `OrbKingGalaxy.arc/jmp/placement/common/objinfo.json` | empty: the planet, the enemies and the boss are placed by the code (section 4) |
| `OrbKingGalaxy.arc/camera/cameraparam.bcam.json` | the cameras: none of its own, so the game's default camera |
| the other tables of `jmp/` | empty; `path/commonpathinfo` holds no rail, so the stage has no opening flight and no `ScenarioStarter` (a starter needs a rail named by its `CommonPath_ID`) |

Rows name their columns: `name`, `l_id`, `pos_x` to `pos_z`, `dir_x` to `dir_z`, `scale_x` to `scale_z`,
`Obj_arg0` to `Obj_arg7`. `lumalink extract --game <game.pak> -o <folder> StageData/<a galaxy>` writes any of the
game's stages in the same form to read from. The galaxy's lines (its name and the star's, in 14 languages) are in
`layers/base/merge/MessageData/messages.json`.

## 4. Write the code

Put this in `orb-galaxy/source/main.lum`:

```lumascript
use kit.stage
use kit.actor
use kit.cutscene
use kit.hud
use kit.item
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

- `stage.define_galaxy` makes the stage a galaxy with one star; `text "..."` is a line of `messages.json` in the
  player's language. `stage.music` plays `NebulaTheme` over the stage instead of the game's music.
- `actor.define` makes a kind of object from a model: its sensors (Body: what touches, Attack: what hurts Mario),
  the attacks that defeat it, what it drops, and a function run every frame. A kind is placed as `<mod id>/<name>`.
- `stage.build` runs each time the stage loads: the planet with its model and ground, three Spikerolls on its upper
  half and the Orb King on the far side, upside down so it stands on the planet. Anything else a mod places when a
  stage starts goes in `on stage "Name" { }`, which runs once the stage stands and Mario is at its start (the
  stage's name is already the new one while it loads).
- The Orb King waits until Mario is within 1500 units, then plays its scene (the camera looks at it, it slams, a
  short wait), shows a meter of 3 at the top right and switches to `BossTheme`. It then walks, charges and sits
  dizzy in turn. Only a stomp while it is dizzy counts; the third makes its star appear where it stood and the
  galaxy's music comes back.

## 5. Build and check

```sh
lumalink build orb-galaxy
lumalink check orb-galaxy
```

`check` reads every content file as the game's import does (models, collision, sounds, tables, lines) and ends
`Errors: 0`.

## 6. Test it

- `tests/visit`: enters the galaxy and takes a picture. It passes when the stage is ready, the galaxy's music has
  started and the ground under Mario is the moss green of the planet.
- `tests/boss`: walks Mario round the planet. It passes when the Orb King wakes and its meter shows at the top right.

```sh
lumalink test orb-galaxy
```

Not yet: `text "..."` read in `on start` stops the game at its start, and `stage.build`'s planet stops the game as
it is made (its ground is cooked apart from its model). Until these are fixed the mod builds and checks, and its
tests do not pass. Its place in the Terrace dome's list has not been tried in a run.

## 7. Play it

```sh
lumalink pack orb-galaxy
```

Drop `orb-galaxy.luma` on the game's window and switch it on.

## See also

`lumakit/stage.md`, `lumakit/actor.md`, `lumakit/cutscene.md`, `lumakit/hud.md`, `content/models.md`,
`content/sound.md`, `content/stages.md`, `language/text-and-services.md` (text lines).
