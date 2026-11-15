---
title: Adding animations
shortTitle: Add animations
intro: Replace the game's animations, add animations to your own models, name events at frames of an animation, and play animations from code.
contentType: how-tos
---

## About animations in a mod

Animations are glTF 2.0 animations, made in Blender or any other 3D tool. Nebula cooks each one into the game's own animation files, so the game plays it like its own: with its sounds and effects, and at no extra cost.

An animation can move:

| What moves | In glTF |
|---|---|
| Joints | The `translation`, `rotation` and `scale` of joint nodes. |
| A material's color | `KHR_animation_pointer` to `baseColorFactor`. |
| A material's texture position | `KHR_animation_pointer` to the base color texture's `KHR_texture_transform` offset, scale or rotation. |
| A material's texture | `extras.nebula.texture_swaps`. For more information, see [Swapping textures](#swapping-textures). |

There are three ways to add animations:

* In a model's own file. Every animation in a model's `.glb` comes with the model. See [Adding models](adding-models.md).
* In an animation file. A file with animations only, for a game model that you don't replace. For example, a new walk for the Goomba.
* From code. Your code plays any animation of a model, at any speed, and is told when it ends.

## Prerequisites

* LumaLink and an installed Nebula. For more information, see [Installing LumaLink](../get-started/installing-lumalink.md).
* A mod folder. For more information, see [Creating a mod](creating-a-mod.md).

## Replacing a game animation

To change how a game model moves without replacing the model, add an animation file to the model's archive.

1. Find the model and its animations. To list the models whose path contains `Kuribo`, with how many animations each has:

   ```shell
   lumalink list models Kuribo
   ```

1. In your 3D tool, make the model's skeleton: the same joints, with the same names and the same parents as the game's model. Animate it.
1. Name each animation after the game animation it replaces, for example `Walk`. Case doesn't matter: `Walk` replaces `walk`.
1. Export the animations only, as `NAME.anim.glb`. The name before `.anim.glb` is yours to choose.
1. Add the file to the model's archive:

   ```shell
   lumalink add file GoombaWalk.anim.glb my-mod --at ObjectData/Kuribo.arc/GoombaWalk.anim.glb
   ```

1. Check the mod:

   ```shell
   lumalink check my-mod
   ```

Each animation in the file replaces the game's animation with the same name. The game's other animations stay. A joint your animation doesn't move keeps its rest pose from your file.

An animation file is checked against the game's model, so the game must be known. If LumaLink finds no installed Nebula, give the game with `--game`.

### Which model an animation file moves

An animation file moves the one model of its archive. If the archive has several models, it moves the one named like the archive, such as `Kuribo.bdl` in `Kuribo.arc`. If none has the archive's name, the file is refused.

### Materials in an animation file

An animation file can also change the game model's materials. Give the file's material the same name as the game's material, and animate its color, texture position or texture. A swapped-in texture is the game's texture with the same name as your image. Name the image after the game's texture, for example `KuriboAll.2`.

## How an animation plays

By default an animation loops. To make it play another way, set its custom properties (glTF `extras`):

```json
"extras": { "nebula": { "play": "once" } }
```

| Value | How it plays |
|---|---|
| `once` | Once, then stays on its last frame. |
| `once_then_first` | Once, then goes back to its first frame. |
| `loop` | Over and over. The default. |
| `there_and_back` | To the end, then back to the start, once. |
| `loop_there_and_back` | To the end and back, over and over. |

The game runs at 60 frames a second. An animation lasts at most 65,535 frames, about 18 minutes.

## Swapping textures

To show different textures over time, such as eyes that blink, list the swaps in the animation's custom properties. Each swap names a material by its number in the file, and a list of keys. Each key is a time in seconds and a texture by its number in the file. Each texture shows from its time until the next key.

```json
"extras": {
  "nebula": {
    "texture_swaps": [
      { "material": 0, "keys": [[0.0, 1], [2.5, 2], [2.6, 1]] }
    ]
  }
}
```

The material must have a base color texture.

## Naming events at frames

Your code can be told when an animation reaches a frame, for example to play a footstep sound. To name events at frames of an animation, add a file named `CLIP.anim.json` to the model's archive, where `CLIP` is the animation's name:

```json
{
  "events": [
    { "frame": 12, "event": "step_left" },
    { "frame": 30, "event": "step_right" }
  ]
}
```

```shell
lumalink add file Walk.anim.json my-mod --at ObjectData/Kuribo.arc/Walk.anim.json
```

* `frame` is a whole number from 0 to the animation's last frame.
* `event` is 1 to 63 of the letters `A-Z` and `a-z`, the digits `0-9`, `_` and `.`.
* A file has 1 to 64 events. Two events can share a frame.

When your code plays the animation, each event comes to it with its name. For more information, see [Playing an animation from code](#playing-an-animation-from-code).

## Playing an animation from code

The `anim` service plays an animation on any object with a model. It needs no permission.

```lumascript
use svc.anim

var steps: u32 = 0

on stage "EggStarGalaxy" {
    guard let mario = MarioAccess::getPlayerActor() else {
        return
    }
    let how = anim.How { rate: 0.5, blend: 0, loop: 2, start: 0.0, marks: [10.0] }
    let play = anim.play(mario, "Walk", how, fn(e: anim.Event) {
        match e {
            anim.Event.Mark(socket, value, data) => { steps = steps + 1 }
            anim.Event.End(socket, status, value) => { log "the walk ended" }
        }
    })
}
```

`anim.How` says how it plays:

| Field | What it does |
|---|---|
| `rate` | Frames of the animation for each frame of the game. 1 is normal speed, 0.5 half speed, up to 16. 0 is 1. |
| `blend` | For joints: frames to blend from the pose before. 0 blends as the game does for that animation. -1 doesn't blend. |
| `loop` | 0 plays as the animation says, 1 plays once, 2 loops. |
| `start` | The frame it starts from. |
| `marks` | Up to 16 frames. Each gives a `Mark` event every time the animation passes it. |

The function you give gets each event at the start of a frame:

* `Mark` when the animation passes one of your marks, or an event of its `.anim.json` file. For a file's event, `data` is the event's name.
* `End` when the play ends: a once animation reached its end, your code called `anim.stop`, another animation replaced it, or the object or stage is gone.

To play an animation on a model your mod attached, use `anim.play_on_model`. To stop a play where it is, use `anim.stop(play)`. To read its frame, use `anim.frame(play)`.

A play starts every kind of animation of that name together: joints, colors and textures, as the game starts its own.

## Troubleshooting

| Message | What to do |
|---|---|
| Joint NAME hangs from FOUND here but from PARENT in the game's model | Put the joint under the same parent as in the game's model. |
| Animation NAME moves NODE, which is no joint of the game's model | Remove the animation of that node, or name it as a joint of the game's model. |
| An animation file goes in the archive of the model it moves | Put the file inside `ObjectData/NAME.arc/`. |
| An animation file is made from the game's model, which is not known here | Install Nebula, or give the game with `--game`. |
| Two animations are named NAME | Give each animation its own name. |
| Frame FRAME is past the end of clip CLIP | Use a frame from 0 to the last frame the message names. |

In the game, an animation your code plays that the model doesn't have is a line in the mod log: `Content.MissingAnimation`. For more information, see [Reading the mod log](reading-the-mod-log.md).

## Further reading

* [Adding models](adding-models.md)
* [Adding sounds and music](adding-sounds-and-music.md)
* [Services](../concepts/services.md)
