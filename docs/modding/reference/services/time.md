---
title: time service
shortTitle: time
intro: The time service gives your mod the game's frames and world steps, real clocks, timers, and control of the world clock's speed.
contentType: reference
---

## About the time service

The game counts time in two ways:

* A frame is one picture presented on the screen. Frames go on at the display's rate whatever happens in the game.
* A step is one update of the world: the stage, its objects, the camera, the controls the stage reads and its sound. With no mod changing the world clock, a stage runs one step a frame.

Each mod can set its own speed for the world clock and can hold it. The world's speed is the product of every mod's speed, so the order of the **Mods** list never changes it. While any mod holds the world, it takes no steps. A mod that holds the world alone can step it frame by frame.

The world clock never moves the frame itself, the system's screens (the settings menu, the HOME Menu, error windows, Nebula's own screens), events, or `on frame` handlers. A held world is drawn every frame.

| | |
|---|---|
| Name | `time` |
| Version | 1.0 |
| Header | `luma_time.h` |
| Permission | None |
| C import | `LUMA_SERVICE(slot, "time", "1.0")` |
| LumaScript | `use svc.time` |

The player has the last word. The mod's page on the **Mods** page shows each mod's clock, such as `Time: held` or `Time: 2x`. **Let go** clears that mod's speed, hold and owed steps from the next frame, and writes a note to the mod log. Your mod sees it in `time.now()` and may hold again later.

## Limits

| C constant | LumaScript | Value | Meaning |
|---|---|---|---|
| `LUMA_TIME_REAL` | `time.REAL` | 1000 | The world clock's speed for real time, in thousandths. |
| `LUMA_TIME_RATE_LEAST` | `time.RATE_LEAST` | 1 | The slowest speed a mod may set. |
| `LUMA_TIME_RATE_MOST` | `time.RATE_MOST` | 16000 | The fastest speed a mod may set. |
| `LUMA_TIME_STEPS_MOST` | `time.STEPS_MOST` | 16 | The most steps one frame runs, and the most one `step` call asks for. |
| `LUMA_TIME_TIMERS_MOST` | `time.TIMERS_MOST` | 64 | The timers one mod has at once. |

## How the world clock works

* Each mod's speed starts at 1000. `set_rate`, `hold`, `release` and `step` apply from the next frame. Nothing changes inside a frame.
* The world's speed is the exact product of every mod's speed, divided by 1000 once for each mod after the first, rounded down once, and kept within 1 to 16000. Two mods at 500 and 4000 give 2000.
* At a speed below 1000, some frames take no step. At 500 the frames take 0, 1, 0, 1 steps. At 1500 they take 1, 2, 1, 2. A frame runs at most 16 steps.
* The buttons pressed in a frame that takes no step because of a slow speed are carried to the next step, so no press is lost. A frame that takes no step because the world is held carries none.
* Sound follows the speed as a tape does. From 125 to 4000, the game's music, sequences and voices play at that speed and pitch. Held, or outside 125 to 4000, the world's sound is held as in the pause menu. Movie sound never changes.
* The world clock is 1000 and not held before the first stage, while a stage loads, while a movie plays, and while the cutscene skip runs. What mods set applies again when that ends.
* While the game holds the stage itself (the HOME Menu, the settings menu, a system warning, the skip question), no step runs.
* Everything is whole numbers, so the same mods, list and inputs give the same steps on every machine.
* When your mod stops, its speed, hold and owed steps go, from the next frame.

## Functions

### now

Fills in the frame and its world step as the game sees them.

```c
int32_t (*now)(const LumaMod* mod, LumaTimeNow* out);
```

```lumascript
time.now() -> time.Now
```

Game thread. Every field holds for the whole frame, except `step` and `step_in_frame`, which an `on step` handler sees for its own step.

| Returns | When |
|---|---|
| `LUMA_OK` | Always, for a valid call. |
| `LUMA_E_ARGUMENT` (reported) | `out` is null, or `out->size` is below 64, the size of the 1.0 struct (`Service.StructSize`). |

### monotonic_ns

Reads the monotonic clock.

```c
int64_t (*monotonic_ns)(const LumaMod* mod);
```

```lumascript
time.monotonic_ns() -> i64
```

Any thread. Returns nanoseconds, at least 1, never decreasing. In a test run's deterministic mode, this clock follows the frames, so every run gives the same values. Returns `LUMA_E_ARGUMENT` for a null or unknown mod, and `LUMA_E_CANCELLED` for a mod being taken out.

### wall

Reads the wall clock and the player's local time.

```c
int32_t (*wall)(const LumaMod* mod, LumaTimeWall* out);
```

```lumascript
time.wall() -> time.Wall
```

Any thread. In deterministic mode, the wall clock follows the frames, starting from the year 2000 with an offset of 0.

| Returns | When |
|---|---|
| `LUMA_OK` | Always, for a valid call. |
| `LUMA_E_ARGUMENT` (reported) | `out` is null, or `out->size` is below 32, the size of the 1.0 struct. |

### set_rate

Sets your mod's factor of the world clock's speed, from the next frame.

```c
int32_t (*set_rate)(const LumaMod* mod, uint32_t rate);
```

```lumascript
time.set_rate(rate: u32)
```

| Parameter | Type | Description |
|---|---|---|
| `rate` | `u32` | Thousandths of real time, 1 to 16000. 1000 takes your factor back. 500 is half speed. |

Game thread. Returns `LUMA_OK`. A rate outside 1 to 16000 is `LUMA_E_ARGUMENT`, reported as `Service.Argument` naming `rate`. In LumaScript, a literal outside the range is a compile error.

### hold

Holds the world from the next frame. Holding twice is the same as holding once.

```c
int32_t (*hold)(const LumaMod* mod);
```

```lumascript
time.hold()
```

Game thread. Returns `LUMA_OK`.

### release

Lets the world go from the next frame. Steps your mod asked for that have not run are dropped. Releasing when you don't hold does nothing.

```c
int32_t (*release)(const LumaMod* mod);
```

```lumascript
time.release()
```

Game thread. Returns `LUMA_OK`.

### step

Runs more world steps in the next frame while your mod holds the world.

```c
int32_t (*step)(const LumaMod* mod, uint32_t count);
```

```lumascript
time.step(count: u32) -> bool
```

| Parameter | Type | Description |
|---|---|---|
| `count` | `u32` | Steps to add, 1 to 16. At most 16 can be owed. |

Game thread.

| Returns | LumaScript | When |
|---|---|---|
| `LUMA_OK` | `true` | The steps run in the next frame. |
| `LUMA_E_BUSY` | `false` | Another mod holds the world too, or the game is at boot, loading, a movie or the cutscene skip. This is an answer and is not reported. |
| `LUMA_E_ARGUMENT` (reported) | `false` | Your mod does not hold the world (`Time.NotHeld`), or `count` is 0 or over 16. |
| `LUMA_E_LIMIT` (reported) | `false` | More than 16 steps would be owed. |

### timer

Starts a timer on one of three clocks.

```c
int32_t (*timer)(const LumaMod* mod, LumaTimeClock clock, uint64_t after, uint64_t every, LumaEventFn fn,
                 void* user, LumaTimeTimer* out);
```

```lumascript
time.timer(clock: time.Clock, after: u64, every: u64, fn(e: time.TimerEvent)) -> time.Timer?
```

| Parameter | Type | Description |
|---|---|---|
| `clock` | `time.Clock` | `Frames`, `Steps` or `Nanoseconds` (the monotonic clock). |
| `after` | `u64` | Units from now until the first event. At least 1. |
| `every` | `u64` | Units between later events. 0 fires once. |
| `fn` | closure | Runs for each `TIMER` event. |

Game thread. A timer counts its clock from the call. A `Steps` timer does not move while the world is held. `Frames` and `Nanoseconds` timers do. A timer that fires once is closed after its event.

| Returns | LumaScript | When |
|---|---|---|
| `LUMA_PENDING` | The timer | The timer runs. Its events go to `fn`. |
| `LUMA_E_ARGUMENT` (reported) | `none` | `fn` or `out` is null, `after` is 0, or `clock` is unknown. |
| `LUMA_E_LIMIT` (reported) | `none` | The mod has 64 timers already. |

In LumaScript, `on time.timer` is a compile error. A timer's events go to the closure you give `time.timer`. To wait a number of frames or seconds in a task, use `wait N frames` or `wait N seconds`, which need no timer. For more information, see [Tasks](../lumascript/tasks.md).

### cancel

Ends a timer. No event for it is delivered after this returns.

```c
int32_t (*cancel)(const LumaMod* mod, LumaTimeTimer timer);
```

```lumascript
time.cancel(timer: time.Timer)
```

Game thread. Returns `LUMA_OK`. A handle that is not one of your mod's open timers is `LUMA_E_HANDLE`, reported.

### on_frame

Sets your mod's frame handler, from the next frame. A null `fn` removes it.

```c
int32_t (*on_frame)(const LumaMod* mod, LumaTimeFn fn, void* user);
```

Game thread. Returns `LUMA_OK`. The handler runs once each frame, after every mod's events, in the order of the **Mods** list. It runs while the world is held.

In LumaScript, write `on frame { }` instead. Calling `time.on_frame` is a compile error.

### on_step

Sets your mod's step handler, from the next step. A null `fn` removes it.

```c
int32_t (*on_step)(const LumaMod* mod, LumaTimeFn fn, void* user);
```

Game thread. Returns `LUMA_OK`. The handler runs once for each world step, before the stage's update, in the order of the **Mods** list. It runs 0 times in a held frame and up to 16 times in a fast one.

In LumaScript, write `on step { }` or `on step(now: time.Now) { }` instead. Calling `time.on_step` is a compile error. `on step` cannot wait, but it can start a task.

`LumaTimeFn` is `typedef void (*LumaTimeFn)(void* user, const LumaTimeNow* now);`. `now` is valid during the call only.

## Events

| Kind | Value | LumaScript | Description |
|---|---|---|---|
| `LUMA_TIME_EVENT_TIMER` | 1 | `time.TimerEvent` | A timer came due. `handle` is the timer. `value` is how many times it came due since its last event, more than 1 when its period is shorter than a frame. `status` is `LUMA_OK`. No data. |

## Structs

### LumaTimeNow

`time.Now` in LumaScript.

| Field | C type | LumaScript | Description |
|---|---|---|---|
| `size` | `uint32_t` | | `sizeof(LumaTimeNow)` as the mod was built. |
| `state` | `LumaTimeState` | `time.State` | What the game is doing. |
| `frame` | `uint64_t` | `u64` | Frames presented since the game started. The first is 1. |
| `step` | `uint64_t` | `u64` | World steps since the game started. |
| `steps` | `uint32_t` | `u32` | World steps this frame runs: 0 held, 1 real time, up to 16. |
| `step_in_frame` | `uint32_t` | `u32` | In `on step`, this step's place in the frame, from 0. Elsewhere, the same as `steps`. |
| `rate` | `uint32_t` | `u32` | The world's speed now, in thousandths. 1000 while loading. |
| `holders` | `uint32_t` | `u32` | How many mods hold the world now. 0 when it is not held. |
| `sound_rate` | `uint32_t` | `u32` | The speed the game's sound follows now, in thousandths. 0 when the world's sound is held. |
| `reserved` | `uint32_t` | | 0. |
| `frame_start_ns` | `int64_t` | `i64` | The monotonic clock at this frame's start. |
| `frame_ns` | `int64_t` | `i64` | Real time from the previous frame's start to this one's. |

The first stage update after a load runs even in a frame whose `steps` is 0, and counts as a step. So `step` can grow by 1 in such a frame.

### LumaTimeWall

`time.Wall` in LumaScript.

| Field | C type | LumaScript | Description |
|---|---|---|---|
| `size` | `uint32_t` | | `sizeof(LumaTimeWall)` as the mod was built. |
| `utc_offset_s` | `int32_t` | `i32` | Local time minus UTC, in seconds: the player's time zone and daylight saving now. |
| `utc_ns` | `int64_t` | `i64` | Nanoseconds since 1970-01-01 00:00 UTC. |
| `year` | `int32_t` | `i32` | Local year, such as 2026. |
| `month` | `uint8_t` | `u8` | 1 to 12. |
| `day` | `uint8_t` | `u8` | 1 to 31. |
| `weekday` | `uint8_t` | `u8` | 0 Sunday to 6 Saturday. |
| `hour` | `uint8_t` | `u8` | 0 to 23. |
| `minute` | `uint8_t` | `u8` | 0 to 59. |
| `second` | `uint8_t` | `u8` | 0 to 59. |
| `millisecond` | `uint16_t` | `u16` | 0 to 999. |

## Enums

### LumaTimeState

`time.State` in LumaScript. What the game is doing. The game checks the states in the order of this table, and the first that holds is the state.

| C | LumaScript | Value | Meaning |
|---|---|---|---|
| `LUMA_TIME_BOOT` | `time.State.Boot` | 0 | Before the first stage: the boot scene and the system's files. |
| `LUMA_TIME_LOADING` | `time.State.Loading` | 1 | A stage is being built or taken down. |
| `LUMA_TIME_MOVIE` | `time.State.Movie` | 4 | A movie plays. |
| `LUMA_TIME_SKIP` | `time.State.Skip` | 5 | The cutscene skip runs the stage unseen. |
| `LUMA_TIME_MENU` | `time.State.Menu` | 3 | A stage is up and the game holds it itself: its pause menu, the galaxy map, the HOME Menu, the settings menu, an error window, or the skip's question. |
| `LUMA_TIME_PLAY` | `time.State.Play` | 2 | A stage runs. |

The stage's own pause menu and galaxy map count as `Menu`, but they run inside the stage, so they follow the world clock.

### LumaTimeClock

`time.Clock` in LumaScript. The clock a timer counts.

| C | LumaScript | Value | Meaning |
|---|---|---|---|
| `LUMA_TIME_FRAMES` | `time.Clock.Frames` | 0 | Frames presented. |
| `LUMA_TIME_STEPS` | `time.Clock.Steps` | 1 | World steps. |
| `LUMA_TIME_NANOSECONDS` | `time.Clock.Nanoseconds` | 2 | The monotonic clock. |

### LumaTimeTimer

`time.Timer` in LumaScript. A timer handle that only `cancel` takes.

## Example

This mod freezes the world for half a second of real time every 600 frames.

```lumascript
use svc.time

var held_until: i64 = 0

on frame {
    let now = time.now()
    if now.state != time.State.Play {
        return
    }
    if held_until == 0 and now.frame % 600 == 0 {
        time.hold()
        held_until = time.monotonic_ns() + 500_000_000
    } else if held_until != 0 and time.monotonic_ns() >= held_until {
        time.release()
        held_until = 0
    }
}

on step(now: time.Now) {
    if now.step % 600 == 0 {
        log "ten seconds of world time at step {now.step}"
    }
}
```

Waiting in a task needs no timer:

```lumascript
use svc.time

task pauses() {
    wait 30 frames
    wait 2 seconds
    let end = time.monotonic_ns() + 1_000_000_000
    wait until time.monotonic_ns() >= end
    log "done"
}
```

`wait 30 frames` counts frames, also while the world is held. `wait 2 seconds` counts world steps at 60 a second. `wait until` checks its condition once a frame, so the last wait is one second of real time.

## Mod log messages

| Code | ID | Level | Message |
|---|---|---|---|
| 120 | `Time.NotHeld` | Error | `{mod}: {where} works only while the mod holds the game; it did nothing.` |
| 121 | `Time.LetGo` | Note | `{mod}: you set the game back to its normal speed.` |

The service also uses the common messages: `Service.Argument` (`rate`, `count`, `clock`, `after`, `fn`, `out`), `Service.Limit` (64 timers, 16 owed steps), `Service.Handle`, `Service.Thread` and `Service.StructSize` (`LumaTimeNow` 64 bytes, `LumaTimeWall` 32). For more information, see [Calling services](calling-services.md#mod-log-messages).

## Further reading

* [Calling services](calling-services.md)
* [Tasks](../lumascript/tasks.md)
* [Events and handlers](../lumascript/lifecycle.md)
