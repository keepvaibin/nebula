---
title: kit.projectile
shortTitle: projectile
intro: "Things thrown or shot, with bounces, life, trail, burst and a hit callback."
contentType: reference
---

## About kit.projectile

The `kit.projectile` module of LumaKit gives you things thrown or shot, with bounces, life, trail, burst and a hit callback.

To use the module, add `lumakit` to the `needs` of your mod's `luma.json` and write:

```lumascript
use kit.projectile
```

### How the module works

Things thrown or shot, with bounces, life, trail, burst and a hit callback. A shot is an object of the kit's class KitShot: made by std make the first time, and a dead one of a stage made again for the next throw (the game's actors stay for their stage, so the pool is the most shots ever alive at once). Each frame, before the game moves it (base.movement: its sensors, then position += velocity): gravity added when it `falls` (the game's gravity at its place, MR::calcGravity through onCalcGravity), then the map along this frame's step asked (MR::getFirstPolyNormalOnLineToMap) when `hits_map`: a hit there is the callback's. A sensor touching the shot's (the game's sensor contact, attackSensor) is the callback's too, but for the thrower's group: every actor the shot touches in its first two steps (the one throwing it, standing where it starts) is passed for the shot's life without a call, as the game's FireMarioBall passes the player. The callback's answer: Bounce (velocity mirrored on the map's normal, or away from the touched sensor's center; the bounces left counted down, none left is a Burst), Burst (the burst effect and the hit sound, then gone), Pass (on through), Stick (stops there, still living until its life ends). A life's end: gone without a burst. Damage is the callback's to send (its two HitSensor arguments, the shot's and the touched one, are the game's, for MR::sendMsg*; views, valid in the call, so not fields of Hit: language). Every shot belongs to the mod that threw it: when it stops, its shots are killed and its callbacks never run again. throw gives `ref<LiveActor>?`, none when the stage cannot make a shot (as item.spawn). on_hit is fn(Hit, HitSensor, HitSensor) -> Then and Hit is { at, map }, since a struct holds no view; a HitSensor written in a type is its view.

The module's source is `lumakit/source/projectile.lum`.

## Declarations

| Name | Kind | Description |
|---|---|---|
| [`Shot`](#shot) | Struct |  |
| [`Hit`](#hit) | Struct | Where a shot touched: the place, and whether it was the map (the callback's sensors are the shot's own then). |
| [`Then`](#then) | Enum |  |
| [`burst_always`](#burst_always) | Function | The default answer: every hit bursts. |
| [`throw`](#throw) | Function | Throws `s` now: its object at `from`, flying at `velocity`; its handle (empty once it is gone), none when the stage cannot make one. |

### `Shot`

```lumascript
pub struct Shot {
    look: model? = none                       // none: only its trail
    from: vec3
    velocity: vec3                            // game units per frame
    falls: bool = false                       // pulled by gravity
    bounces: u32 = 0
    life_frames: u32 = 180
    radius: f32 = 30.0
    trail: effect? = none
    burst: effect? = none
    sound_throw: sound? = none
    sound_hit: sound? = none
    hits_map: bool = true
    on_hit: fn(Hit, HitSensor, HitSensor) -> Then = burst_always
}
```

### `Hit`

```lumascript
pub struct Hit { at: vec3, map: bool }
```

Where a shot touched: the place, and whether it was the map (the callback's sensors are the shot's own then).

### `Then`

```lumascript
pub enum Then { Bounce, Burst, Pass, Stick }
```

### `burst_always`

```lumascript
pub fn burst_always(h: Hit, mine: HitSensor, other: HitSensor) -> Then
```

The default answer: every hit bursts.

### `throw`

```lumascript
pub fn throw(s: Shot) -> ref<LiveActor>?
```

Throws `s` now: its object at `from`, flying at `velocity`; its handle (empty once it is gone), none when the stage cannot make one.

## Further reading

* [LumaKit reference](index.md)
* [About LumaKit](../../concepts/lumakit.md)
