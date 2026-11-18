---
title: Playing as a new character
shortTitle: A new character
intro: Make a rigged model from any 3D tool into a character the game's animations move, and play it in Mario's place with its own moves, voice and jump.
contentType: tutorials
example: a-new-character
---

## Introduction

In this tutorial, you make Robo, a robot the player plays in Mario's place. Robo has its own model, does a somersault where Mario jumps, has a voice of its own, and jumps a quarter higher. Everything else Mario does, Robo does with Mario's own animations, because its model is bound to Mario's skeleton.

You learn how to:

* Bind a rigged model from any 3D tool to Mario's skeleton with `lumalink add character`.
* Define a character as data with LumaKit's `kit.player.character`: its look, the animations it plays for the game's actions, its voice and its constants.
* Play the character from code.
* Test a character in the game.

## Prerequisites

* Nebula, installed with Nebula Setup.
* LumaLink. For more information, see [Installing LumaLink](../get-started/installing-lumalink.md).
* The [Adding a new power-up](a-new-power-up.md) tutorial.

## Mod folder layout

```text
a-new-character/
  luma.json
  kit/player.character/robo.json          the character
  layers/base/assets/Robot.glb            the model, bound to Mario's skeleton
  layers/base/assets/voice/*.wav          the voice, with a .sound.json beside each
  source/main.lum
  source/Robot.glb                        the model as the 3D tool made it
  source/make_robot.py                    makes source/Robot.glb and the voice
  tests/play/test.json
  tests/probe/                            a mod for a test: the animation and voice played, as facts
```

## Step 1: Make the model

Make the character in any 3D tool, rigged on a skeleton, standing in a T-pose, and export it as glTF 2.0 binary (`.glb`). The joint names can be the ones your tool uses: Mixamo's, VRM's, Rigify's and Unreal's are all known. Robo's source model, `source/Robot.glb`, uses Mixamo's names: `Hips`, `Spine`, `LeftArm`, `LeftForeArm`, `LeftUpLeg` and so on.

## Step 2: Bind it to Mario's skeleton

```shell
lumalink add character source/Robot.glb a-new-character --replaces mario --as Robot
```

LumaLink matches each of Mario's joints to one of the model's, by name or by the part of the body it is, scales the model to Mario's height, and binds it to Mario's skeleton, so the game's animations move it. It prints what it matched:

```text
  LegR1 = RightUpLeg (by the part it is)
  HandL = LeftHand (by name)
  Spine2: no part of the model follows it
  Neck: kept as an extra joint (moves with its parent, no animation of its own)
Stands in for ObjectData/Mario.arc/mario.bdl; scaled by 0.717; 384 vertices rebound
```

A joint the model doesn't have, such as Mario's cap joints, is left out. A joint Mario doesn't have, such as a neck or a tail, stays and moves with its parent. To match a joint by hand, add `--joint GAME=SOURCE`.

With `--as Robot`, the result is a new model of the mod, `layers/base/assets/Robot.glb`, and Mario's own model stays as it is. Without `--as`, the model replaces Mario's for everyone. The full report is written beside the mod folder.

## Step 3: Define the character

A character is a file in `kit/player.character/`, read by LumaKit when the game starts:

```json
{
  "id": "robo",
  "name": "Robo",
  "look": "Robot",
  "actions": { "ジャンプ": "空中一回転", "着地": "着地幅とび" },
  "voice": {
    "声小ジャンプ": "voice/Hup", "声中ジャンプ": "voice/Hup", "声大ジャンプ": "voice/HupHigh",
    "声スピン": "voice/Spin", "声小ダメージ": "voice/Ouch"
  },
  "tuning": { "mJumpHeight": 1.25 }
}
```

* `look` is the model, by its path in `assets/` without `.glb`. While Robo plays, it is drawn on Mario's skeleton and Mario's own body is not drawn.
* `actions` maps the game's name for an action to the animation played for it. The game names Mario's actions in Japanese: `ジャンプ` is the jump, `着地` the landing. Here Robo plays Mario's somersault (`空中一回転`) for a jump, and the long-jump landing (`着地幅とび`) for a landing.
* `voice` maps the game's voice lines to the mod's sounds. `声小ジャンプ` is the first jump's voice, `声スピン` the spin's.
* `tuning` multiplies the player's constants: `mJumpHeight` 1.25 is a jump a quarter higher.

The manifest says the mod has this kind of file:

```json
"needs": [{ "id": "lumakit", "version": ">=1.0.0" }],
"kit": { "lumakit.player.character": 1 }
```

For every field, see [kit.player.character](../reference/lumakit/player-character.md).

## Step 4: Add the voice

Each voice line is a sound file with a `.sound.json` beside it, on the `voice` bus, so it follows the player's voice volume:

```json
{ "kind": "effect", "file": "Hup.wav", "bus": "voice", "distance": [300, 3000] }
```

The sound's name is its path in `assets/` without `.sound.json`: `voice/Hup`. For more information, see [Adding sounds and music](../how-tos/adding-sounds-and-music.md).

## Step 5: Play it

A defined character plays only when code asks. Robo plays from the start:

<!-- sample: examples/a-new-character/source/main.lum -->
```lumascript
// Robo: the player plays Robo in Mario's place from the start. Robo itself is data, kit/player.character/robo.json:
// its model (assets/Robot.glb, made from source/Robot.glb by `lumalink add character`), the clips it plays for some
// of Mario's actions, its voice and its higher jump. When the mod is switched off, Mario comes back.
use kit.player.character as pc

on start {
    guard let robo = pc.find("robo") else {
        log "robo: kit/player.character/robo.json was not read"
        return
    }
    robo.play()
}
```

`pc.mario().play()` gives Mario back. When the mod is switched off, LumaKit gives Mario back by itself, with his own constants.

## Step 6: Test it in the game

The test reads the player's jump constant with Robo and after the mod is taken out, and takes a picture of Robo, of Robo in the air and of Mario again:

```json
"steps": [
  { "at": 10, "call": "getMarioConstant", "signature": "f32(s32, const char *, s32)", "args": [0, "mJumpHeight", 0], "fact": "jump_robo" },
  { "at": 20, "picture": "robo" },
  { "at": 30, "hold": ["A"], "frames": 12 },
  { "at": 40, "picture": "jump" },
  { "at": 100, "take_out": "robo" },
  { "at": 110, "call": "getMarioConstant", "signature": "f32(s32, const char *, s32)", "args": [0, "mJumpHeight", 0], "fact": "jump_mario" },
  { "at": 120, "picture": "mario" },
  { "at": 122, "end": true }
]
```

The expected facts: the jump constant is 27.5 with Robo and 22 after the mod is taken out, and no script error. For more information, see [Test files](../reference/test-files.md).

```shell
lumalink test a-new-character
```

## Further reading

* [kit.player.character](../reference/lumakit/player-character.md)
* [Adding models](../how-tos/adding-models.md)
* [Adding animations](../how-tos/adding-animations.md)
* [lumalink add](../reference/lumalink-commands/add.md)
