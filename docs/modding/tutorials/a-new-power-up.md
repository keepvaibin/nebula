---
title: Adding a new power-up
shortTitle: A new power-up
intro: Add a Gold Flower that turns Mario gold and lets him throw bouncing gold balls.
contentType: tutorials
example: a-new-power-up
---

## Introduction

In this tutorial, you add a power-up called the Gold Flower in front of the starting point of Good Egg Galaxy. When Mario touches it, he turns gold and glows. When the player spins, Mario throws a gold ball that bounces along the ground. An enemy that the ball hits flashes, drops three coins and disappears. Taking damage ends the form.

You learn how to:

* Add a character model that uses Mario's skeleton.
* Add particle effects and sounds.
* Define a new form for Mario with LumaKit's `player.form` module, and the item that gives it with the `item` module.
* Throw projectiles and decide what happens when they hit something.

The finished mod uses simple placeholder models. To use your own, replace the `.glb` files with models of the same names.

## Prerequisites

* Nebula, installed with Nebula Setup.
* LumaLink. For more information, see [Installing LumaLink](../get-started/installing-lumalink.md).
* A character model in glTF format (`.glb`) for Gold Mario, or the placeholder model from the finished mod.

## Mod folder layout

When you finish, the mod folder contains these files:

```text
a-new-power-up/
  luma.json
  source/main.lum
  layers/base/assets/GoldFlower.glb        the flower
  layers/base/assets/GoldBall.glb          the ball
  layers/base/assets/GoldMario.glb         gold Mario, bound to Mario's skeleton
  layers/base/assets/effects.json          the glow, the shine, the trail and the burst
  layers/base/assets/GetGold.wav, GetGold.sound.json
  layers/base/assets/ThrowGold.wav, ThrowGold.sound.json
  layers/base/patches/StageData/EggStarGalaxy.arc/jmp/placement/common/objinfo.json
  tests/gold/test.json
```

## Creating the mod

1. Create the mod folder, add LumaKit to the mod's needs, and add the mod's options.

   ```sh
   lumalink new a-new-power-up --id gold-flower --name "Gold Flower" --summary "A flower that turns Mario gold; a spin throws a gold ball." --author YOUR-NAME
   lumalink add need lumakit a-new-power-up
   lumalink add option balls_at_once a-new-power-up --kind number --default 2 --min 1 --max 4 --step 1 --name "Balls at once"
   lumalink add option flash_frames a-new-power-up --kind number --default 20 --min 5 --max 60 --step 5 --name "Flash frames"
   ```

1. Optionally, add the mod's name, summary and option names in each of the game's languages. To add the name and summary for one language, run `lumalink add text LANGUAGE a-new-power-up --name NAME --summary SUMMARY`. Add the option names to the `text` block by hand. The finished `luma.json` looks like this:

   ```json luma.json
   {
     "format": "1.0",
     "id": "gold-flower",
     "version": "1.0.0",
     "name": "Gold Flower",
     "summary": "A flower that turns Mario gold; a spin throws a gold ball.",
     "text": {
       "UsEnglish": {"name": "Gold Flower", "summary": "A flower that turns Mario gold; a spin throws a gold ball.", "options": {"balls_at_once": {"name": "Balls at once"}, "flash_frames": {"name": "Flash frames"}}},
       "EuEnglish": {"name": "Gold Flower", "summary": "A flower that turns Mario gold; a spin throws a gold ball.", "options": {"balls_at_once": {"name": "Balls at once"}, "flash_frames": {"name": "Flash frames"}}},
       "UsFrench": {"name": "Fleur d'or", "summary": "Une fleur qui rend Mario doré ; une toupie lance une boule d'or.", "options": {"balls_at_once": {"name": "Boules à la fois"}, "flash_frames": {"name": "Durée de l'éclat (images)"}}},
       "EuFrench": {"name": "Fleur d'or", "summary": "Une fleur qui rend Mario doré ; une toupie lance une boule d'or.", "options": {"balls_at_once": {"name": "Boules à la fois"}, "flash_frames": {"name": "Durée de l'éclat (images)"}}},
       "UsSpanish": {"name": "Flor dorada", "summary": "Una flor que vuelve dorado a Mario; un giro lanza una bola de oro.", "options": {"balls_at_once": {"name": "Bolas a la vez"}, "flash_frames": {"name": "Duración del destello (fotogramas)"}}},
       "EuSpanish": {"name": "Flor dorada", "summary": "Una flor que vuelve dorado a Mario; un giro lanza una bola de oro.", "options": {"balls_at_once": {"name": "Bolas a la vez"}, "flash_frames": {"name": "Duración del destello (fotogramas)"}}},
       "EuGerman": {"name": "Goldblume", "summary": "Eine Blume, die Mario golden macht; eine Drehung wirft eine Goldkugel.", "options": {"balls_at_once": {"name": "Kugeln gleichzeitig"}, "flash_frames": {"name": "Blitzdauer (Bilder)"}}},
       "EuItalian": {"name": "Fiore d'oro", "summary": "Un fiore che rende Mario d'oro; una piroetta lancia una palla d'oro.", "options": {"balls_at_once": {"name": "Palle alla volta"}, "flash_frames": {"name": "Durata del lampo (fotogrammi)"}}},
       "EuDutch": {"name": "Goudbloem", "summary": "Een bloem die Mario goud maakt; een draai gooit een gouden bal.", "options": {"balls_at_once": {"name": "Ballen tegelijk"}, "flash_frames": {"name": "Flitsduur (beelden)"}}},
       "UsPortuguese": {"name": "Flor Dourada", "summary": "Uma flor que deixa Mario dourado; um giro lança uma bola de ouro.", "options": {"balls_at_once": {"name": "Bolas ao mesmo tempo"}, "flash_frames": {"name": "Duração do brilho (quadros)"}}},
       "JpJapanese": {"name": "ゴールドフラワー", "summary": "マリオが金色になる花。スピンで金のボールを投げます。", "options": {"balls_at_once": {"name": "同時に出せるボール"}, "flash_frames": {"name": "光る長さ(フレーム)"}}},
       "KrKorean": {"name": "골드 플라워", "summary": "마리오를 금색으로 바꾸는 꽃. 스핀으로 금 공을 던집니다.", "options": {"balls_at_once": {"name": "동시에 던지는 공"}, "flash_frames": {"name": "번쩍임 길이(프레임)"}}},
       "CnSimpChinese": {"name": "黄金花", "summary": "让马力欧变成金色的花，旋转可投出金球。", "options": {"balls_at_once": {"name": "同时存在的球数"}, "flash_frames": {"name": "闪光时长（帧）"}}},
       "TwTradChinese": {"name": "黃金花", "summary": "讓瑪利歐變成金色的花，旋轉可投出金球。", "options": {"balls_at_once": {"name": "同時存在的球數"}, "flash_frames": {"name": "閃光時長（格）"}}}
     },
     "authors": ["you"],
     "game": { "id": "RMG" },
     "options": [
       {"key": "balls_at_once", "kind": "number", "name": "Balls at once", "min": 1, "max": 4, "step": 1, "default": 2},
       {"key": "flash_frames", "kind": "number", "name": "Flash frames", "min": 5, "max": 60, "step": 5, "default": 20}
     ],
     "needs": [{ "id": "lumakit", "version": ">=1.0.0" }],
     "layers": [{ "name": "base" }],
     "code": { "platforms": ["windows-x64", "windows-arm64", "linux-x64", "linux-arm64", "macos-arm64", "macos-x64", "android-arm64", "android-x64", "ios-arm64"]}
   }
   ```

## Adding the models

Gold Mario is a figure bound to Mario's skeleton. Give LumaLink your model and the name to store it under:

```sh
lumalink add character figure.glb a-new-power-up --as GoldMario
```

LumaLink matches the figure's joints to Mario's, scales it to his height, and writes `layers/base/assets/GoldMario.glb`. A glTF meter is 100 game units. The game's own Mario model does not change: while the form lasts, the mod's code draws the figure on Mario's skeleton.

`GoldFlower.glb` and `GoldBall.glb` are ordinary models. Copy them to `layers/base/assets/`.

## Adding effects and sounds

Each effect starts from one of the game's particle effects (`like`) and changes its color (`primary`, as RRGGBBAA):

```json
{
  "effects": {
    "gold_aura": { "particles": [ { "like": "CoinFlowerGlow00", "shape": { "primary": "ffd040ff" } } ] },
    "gold_flower_shine": { "particles": [ { "like": "CommonGlow00", "shape": { "primary": "ffe070ff" } } ] },
    "gold_ball_trail": { "particles": [ { "like": "FireBall00", "shape": { "primary": "ffc830ff" } } ] },
    "gold_ball_burst": { "particles": [ { "like": "FireBallBreak00", "shape": { "primary": "ffd040ff" } } ] }
  }
}
```

A sound is a `.wav` file with a `.sound.json` file beside it. The flower's sound plays at full volume wherever Mario is:

```json
{ "kind": "effect", "file": "GetGold.wav", "volume": 0.9, "positioned": false }
```

The throw sound plays from the position where the ball starts, with a small random change of pitch each time:

```json
{ "kind": "effect", "file": "ThrowGold.wav", "volume": 0.8, "pitch": [0.95, 1.05], "distance": [300, 3000] }
```

For more information, see [Adding particle effects](../how-tos/adding-particle-effects.md) and [Adding sounds and music](../how-tos/adding-sounds-and-music.md).

## Placing the flower

Create `layers/base/patches/StageData/EggStarGalaxy.arc/jmp/placement/common/objinfo.json`. This patch adds the flower to Good Egg Galaxy's object table, under the mod's ID, a few steps in front of the starting point.

```json
{
  "patch": 1,
  "key": ["l_id"],
  "add": [
    {
      "name": "gold-flower/GoldFlower",
      "l_id": 9411,
      "pos_x": -3439.8, "pos_y": -13081.3, "pos_z": -15575.5,
      "dir_x": 0.0, "dir_y": 0.0, "dir_z": 0.0,
      "scale_x": 1.0, "scale_y": 1.0, "scale_z": 1.0,
      "Obj_arg0": -1, "Obj_arg1": -1, "Obj_arg2": -1, "Obj_arg3": -1,
      "Obj_arg4": -1, "Obj_arg5": -1, "Obj_arg6": -1, "Obj_arg7": -1
    }
  ]
}
```

## Writing the code

Create `source/main.lum` with this code:

```lumascript
use kit.player.form
use kit.item
use kit.projectile
use svc.draw.inst as inst

option balls_at_once: int
option flash_frames: int

let BALL_SPEED: f32 = 35.0          // game units per frame
let BALL_LIFE: u32 = 180            // frames
let BALL_RADIUS: f32 = 20.0         // the ball model is 40 units across

var balls: list<ref<LiveActor>> = []

fn became_gold(player: MarioActor) {
    log fact "gold" true
}

fn ended(player: MarioActor, why: form.Ended) {
    log fact "gold" false
}

fn throw_ball(player: MarioActor) {
    balls = balls.filter(fn(b: ref<LiveActor>) -> bool { return b.get() != none })
    if balls.len() >= balls_at_once { return }
    var front = vec3(0.0, 0.0, 0.0)
    MR::getPlayerFrontVec(var front)
    let up = player.mGravity * -1.0
    let ball = projectile.throw(projectile.Shot {
        look: model "GoldBall"
        from: player.mPosition + front * 80.0 + up * 60.0
        velocity: front * BALL_SPEED
        falls: true                              // it bounces along the ground, as a fireball does
        bounces: 3
        life_frames: BALL_LIFE
        radius: BALL_RADIUS
        trail: effect "gold_ball_trail"
        burst: effect "gold_ball_burst"
        sound_throw: sound "ThrowGold"
        on_hit: hit
    })
    if let b = ball { balls.push(b) }
    log fact "balls_out" balls.len()
}

fn hit(h: projectile.Hit, mine: HitSensor, other: HitSensor) -> projectile.Then {
    if h.map { return projectile.Then.Bounce }
    if MR::isSensorPlayer(other) { return projectile.Then.Pass }   // the ball starts beside the player: pass by
    if not MR::sendArbitraryMsg(ACTMES_FIREBALL_ATTACK as u32, other, mine) {   // what Fire Mario's ball sends
        return projectile.Then.Pass                       // what a fireball does not hurt: flies on through
    }
    if let enemy = other.mHost {
        start flash(ref(enemy))
        MR::appearCoinPop(enemy, enemy.mPosition, 3)
        log fact "defeated" true
    }
    return projectile.Then.Burst
}

task flash(e: ref<LiveActor>) {
    for f in 0..flash_frames {
        let fade = 1.0 - f as f32 / flash_frames as f32
        if let a = e.get() {
            let i = inst.of(a)
            inst.set_colour(i, color(1.0, 1.0 - 0.16 * fade, 1.0 - fade, 1.0))
            inst.release(i)
        } else { return }
        wait 1 frames
    }
}

on start {
    let gold = form.define(form.Form {
        id: "gold"
        look: model "GoldMario"                  // drawn on Mario's skeleton
        aura: effect "gold_aura"
        ends_on_damage: true
        on_spin: throw_ball
        on_start: became_gold
        on_end: ended
    })
    item.define(item.Item {
        id: "GoldFlower"                         // placed in a stage as "gold-flower/GoldFlower"
        look: model "GoldFlower"
        shine: effect "gold_flower_shine"
        sound_get: sound "GetGold"
        gives: item.Gives.Form(gold)
    })
}
```

* `form.define` defines the gold form: the figure drawn on Mario's skeleton, the glow, what a spin does (`throw_ball`), and two functions that log when the form starts and ends. `ends_on_damage` ends the form when Mario is hurt.
* `item.define` defines the flower. Touching it gives Mario the form.
* `throw_ball` keeps at most `balls_at_once` balls out at a time. Each ball starts in front of Mario, a little above him, and `falls: true` makes it drop and bounce along the ground.
* `hit` runs each time a ball touches something. The ball bounces off the map and passes through the player. Anything else receives the same message that Fire Mario's fireball sends. An enemy that the ball hurts flashes gold for `flash_frames` frames and drops three coins. The ball continues through anything it does not hurt.
* `log fact` writes values that the mod's test reads.

## Building, testing and packing the mod

```sh
lumalink check a-new-power-up
lumalink build a-new-power-up
lumalink test a-new-power-up
lumalink pack a-new-power-up
```

* `lumalink build` builds the code for every platform listed in `luma.json`.
* `lumalink test` starts the game hidden with your mod and LumaKit, and runs `tests/gold/test.json`. The test walks Mario into the flower, places a Goomba ahead of him, and spins so that a ball hits it. Three more spins throw more balls, never more than two at once. Then a second Goomba walks into Mario and ends the form. The command prints `pass` or the check that failed.
* `lumalink pack` writes `gold-flower-1.0.0.luma` beside the mod folder.

To play the mod, drag the `.luma` file onto the game's window, turn the mod on in the **Mods** page, and enter Good Egg Galaxy.

## Next steps

* [Making a game mode: Balloon World](balloon-world.md)
* [LumaKit: player.form](../reference/lumakit/player-form.md)
* [LumaKit: item](../reference/lumakit/item.md)
* [LumaKit: projectile](../reference/lumakit/projectile.md)
