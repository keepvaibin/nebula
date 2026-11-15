---
title: Adding particle effects
shortTitle: Add particle effects
intro: Make new particle effects, change the game's own, and start effects on objects from code.
contentType: how-tos
---

## About particle effects

Sparkles, smoke, glows and trails are particle effects. An effect is one or more emitters, and each emitter makes particles: small pictures that move, grow, fade and change color.

A mod's effects are written in one JSON file. Nebula cooks them into the game's own particle data, so the game's particle code draws them, on every renderer, at no extra cost.

A mod can:

* Make a new effect, starting from one of the game's particles or from nothing.
* Change one of the game's particles. Every place the game starts it shows your version.
* Start an effect from code, on any object, at any joint.

## Prerequisites

* LumaLink and an installed Nebula. For more information, see [Installing LumaLink](../get-started/installing-lumalink.md).
* A mod folder. For more information, see [Creating a mod](creating-a-mod.md).

## Looking at the game's particles

Every particle of the game has a name, such as `CommonGlow00`, and the same set of values. To read them all, extract them:

```shell
lumalink extract ParticleData/Effect.arc/Particles.jpc --output game-particles
```

LumaLink writes `Particles.jpc.json` beside the game's file. It holds every particle under its name, with every value, in the form your mod writes. Copy a particle from it to start your own.

## Writing the effects file

A mod's effects are in `layers/base/assets/effects.json`:

```json
{
  "textures": { "Spark": "textures/spark.png" },
  "effects": {
    "sparkle": {
      "draw": "3D",
      "particles": [
        {
          "like": "CommonGlow00",
          "textures": { "sprite": "Spark" },
          "emitter": { "rate": 4.0, "life": 40 },
          "shape": { "primary": "ffd040ff" }
        }
      ]
    }
  },
  "replace": {
    "CommonGlow00": { "shape": { "primary": "80c0ffff" } }
  }
}
```

The file has three parts. Each is optional.

| Part | What it holds |
|---|---|
| `textures` | The mod's own textures: a name for each, and its PNG or JPEG file, by its path in `assets/`. |
| `effects` | The mod's new effects, by name. |
| `replace` | Changes to the game's particles, by the game's particle name. |

### New effects

Each new effect has a name of lowercase letters, digits and `_`, starting with a letter. Its parts:

* `particles`: its emitters, 1 to 16, in order. Each is a particle, as described below.
* `draw`: where it is drawn. One of:

| Value | Where |
|---|---|
| `3D` | In the scene. Pauses with the game. The default. |
| `PAUSE_IGNORE` | In the scene, and keeps moving while the game is paused. |
| `INDIRECT` | In the screen-warp pass, like heat haze. |
| `AFTER_INDIRECT` | After the screen-warp pass, over warped water. |
| `BLOOM_EFFECT` | Into the bloom, so it glows. |
| `AFTER_IMAGE_EFFECT` | Into the after-image pass. |

The game knows the effect as `MOD-ID:NAME`, so it never collides with the game's effects or another mod's.

### A particle

A particle starts from a game particle named by `like`, or from a plain glowing dot. Then each value you give replaces that value only:

```json
{
  "like": "CommonGlow00",
  "emitter": { "rate": 4.0, "life": 40 },
  "shape": { "primary": "ffd040ff", "size": [5.0, 5.0] }
}
```

The values are grouped as in the extracted file:

| Group | What it controls |
|---|---|
| `emitter` | How particles are made: how many (`rate`), how long they live (`life`), where (`volume`), their speed and spread. |
| `shape` | How each particle looks: its size, colors (`primary`, `environment`), blending, facing and texture animation. |
| `extra` | How each particle changes over its life: fade in and out, grow, spin. |
| `child` | Particles each particle makes in turn. |
| `second_texture` | A second texture over the first, and a screen-warp texture. |
| `fields` | Forces on the particles. Each has a `type`: `gravity`, `air`, `magnet`, `newton`, `vortex`, `random`, `drag`, `convection` or `spin`. |
| `keys` | Values that change over time. Each track names a `value`, its `keys`, and whether it loops. |
| `primary_colors`, `environment_colors` | Colors over a particle's life. |

Colors are written `rrggbbaa`, such as `ffd040ff`. To take away the `extra`, `child` or `second_texture` group, set it to `null`. A value that isn't one of the group's names is refused, with the list of names it can be.

### Textures

Name a particle's textures by their role:

```json
"textures": { "sprite": "Spark", "second": "mr_noise00_i" }
```

| Role | What it is |
|---|---|
| `sprite` | The particle's picture. |
| `child` | The picture of its child particles. |
| `second` | The second texture, drawn over the first. Turn it on with `second_texture.second_on`. |
| `indirect` | The screen-warp texture. Turn it on with `second_texture.indirect_on`. |

A texture is one of the file's own, by its name in `textures`, or one of the game's particle textures, by its name. Over `like`, a role you name replaces only that role.

To animate the picture, list its frames. The sprite starts on the first:

```json
"texture_frames": ["Spark0", "Spark1", "Spark2"]
```

## Changing a game particle

Under `replace`, name the game's particle and give the values to change:

```json
"replace": {
  "CommonGlow00": { "shape": { "primary": "80c0ffff" } }
}
```

Each place the game starts that particle shows your version. The particle keeps its number, so the game's own tables still start it. A replacement starts from the game's particle, not from another mod's version. When two mods replace the same particle, the mod higher in the load order wins, and the mod log names both.

## Starting an effect from code

The `content.fx` service starts an effect on an object. It needs no permission. Name a mod's effect with `effect "NAME"`; the build checks that the effects file has it.

```lumascript
use svc.content.fx

on stage "EggStarGalaxy" {
    guard let mario = MarioAccess::getPlayerActor() else {
        return
    }
    let sparkle = fx.start(mario, effect "sparkle", "HandR", none, true)
    fx.tint(sparkle, 1.5, #FFFFFFFF)
}
```

`fx.start` takes:

1. The object.
1. The effect: a mod's effect, or one of the game's effects by its name.
1. The joint to start it at, or `none` for the object's base.
1. A place, `none` or a `LumaPlace` with an offset in game units and a turn in degrees, in the joint's space.
1. `true` to follow the joint every frame, `false` to stay where it started.

It returns a handle. The effect stops at `fx.stop(effect, now)`, or when the object is gone, the stage changes, or your mod stops. With `now` set to `false`, particles that are alive fade out as the effect says. With `true`, they go at once.

`fx.tint(effect, scale, colour)` scales and colors the effect on top of its own values.

To start an effect at a joint or socket of a model your mod attached, use `fx.start_on_model`. For more information on sockets, see [Adding models](adding-models.md#sockets).

LumaKit starts effects for you on its objects, items and forms: give the effect as their `aura`, `shine` or `trail`. For more information, see [About LumaKit](../concepts/lumakit.md).

## Troubleshooting

`lumalink check` refuses the whole file at the first problem, and names the effect and emitter by the value's place in the file, such as `/effects/sparkle/particles/0/emitter/rate`.

| Message | What to do |
|---|---|
| A value that is not one of its group's names | Use a name from the list the message gives, or from the extracted file. |
| A value out of its range, or not one of its choices | Use a value in the range, or one of the choices listed. |
| A color that is not `rrggbbaa` | Write the color as eight hex digits. |
| A texture that is neither the file's nor the game's | Add it under `textures`, or check the game texture's name. |
| A game particle to start from, or to replace, that the game doesn't have | Check the name in the extracted file. |
| A role is named but the record does not draw it | A note: turn on `second_texture.second_on` or `indirect_on`, or remove the role. |

While the game runs, the mod log says when an effect started but showed no particle for a second (`Content.EffectEmpty`), when an effect name is in no store (`Content.MissingEffect`), and when a joint isn't on the object (`Content.MissingJoint`). For more information, see [Reading the mod log](reading-the-mod-log.md).

## Further reading

* [Adding models](adding-models.md)
* [About LumaKit](../concepts/lumakit.md)
* [Services](../concepts/services.md)
