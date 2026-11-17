---
title: events service
shortTitle: events
intro: The events service tells your mod what happened in the game (a coin, a star, a stage starting, an object killed) at the start of the next frame.
contentType: reference
---

## About the events service

The game raises its events in the middle of its own work: a coin while a pickup is counted, a kill while an object is being taken down. Code that runs there and changes the game breaks the code around it. So the events service keeps each event and tells your mod at the start of the next frame, where your code may do anything: make and kill objects, move the player, change counts, open screens.

To act at the moment itself, such as cancelling a hit, changing a count or playing another sound, hook the function that raises the event. [Events](../events.md) names it for each event.

| | |
|---|---|
| Name | `events` |
| Version | 1.0 |
| Header | `luma_events.h` |
| Permission | None |
| C import | `LUMA_SERVICE(slot, "events", "1.0")` |
| LumaScript | `use svc.events` |

## events functions

### watch

Gets the game's events of `kinds`.

```c
int32_t (*watch)(const LumaMod* mod, uint32_t kinds, LumaEventFn fn, void* user);
```

```lumascript
events.watch(kinds: u32, fn: fn(e: events.Event)?)
```

Game thread. Returns `LUMA_OK`.

* `kinds` has the bit `1 << kind` for each event your mod wants (the kinds below). `LUMA_EVENTS_ALL` is every one.
* At the start of a frame, your function gets every event of those kinds that happened since the last frame's start, in the order they happened, all in that frame's start. It is never called inside `watch`.
* A later call replaces `kinds` and `fn`. `kinds` 0, or a null `fn`, stops the events; those not yet given are dropped.
* A bit outside `LUMA_EVENTS_ALL` (bit 0, or 29 to 31): `LUMA_E_ARGUMENT`, reported (`Service.Argument`).

An event nothing watches costs the game one load and one branch. While a mod watches it, each raise copies the event once for each watching mod.

## events limits

| C constant | LumaScript | Value | Meaning |
|---|---|---|---|
| `LUMA_EVENTS_NAME_MOST` | `events.NAME_MOST` | 128 | The bytes of an event's name a `LumaEventsGame` holds, its NUL included. |
| `LUMA_EVENTS_ALL` | `events.ALL` | 0x1FFFFFFE | Every kind `watch` takes. |
| `LUMA_EVENTS_OTHER_THREAD` | `events.OTHER_THREAD` | 1 | `flags`: raised off the game thread. |

## events events

Each event's `data` is a `LumaEventsGame`, valid during the call. `value` is the same number as the record's `value`. `handle` is 0. `status` is `LUMA_OK`, or `LUMA_E_MEMORY` when events between the previous one and this one were lost because there was no memory to keep them.

| Kind | Value | LumaScript case | What happened |
|---|---|---|---|
| `LUMA_EVENTS_EVENT_STAGE_FRAME` | 1 | `events.Event.StageFrame(data)` | A frame of a stage ran. |
| `LUMA_EVENTS_EVENT_STAGE_START` | 2 | `events.Event.StageStart(data)` | A stage started. `name` the stage, `value` the mission. |
| `LUMA_EVENTS_EVENT_PLAYER_DAMAGE` | 3 | `events.Event.PlayerDamage(data)` | The player lost health. |
| `LUMA_EVENTS_EVENT_PLAYER_MISS` | 4 | `events.Event.PlayerMiss(data)` | The player lost a life. `value` the cause, 0 a fall out of the stage. |
| `LUMA_EVENTS_EVENT_POWER_UP` | 5 | `events.Event.PowerUp(data)` | The player's power-up changed. `value` the new one. |
| `LUMA_EVENTS_EVENT_COIN` | 6 | `events.Event.Coin(data)` | Coins were added. `value` how many. |
| `LUMA_EVENTS_EVENT_STAR_BIT` | 7 | `events.Event.StarBit(data)` | Star bits were added. `value` how many. |
| `LUMA_EVENTS_EVENT_STAR_GET` | 8 | `events.Event.StarGet(data)` | A star was collected. `name` the galaxy, `value` the star. |
| `LUMA_EVENTS_EVENT_CUTSCENE_START` | 9 | `events.Event.CutsceneStart(data)` | A cutscene started. `name` the cutscene. |
| `LUMA_EVENTS_EVENT_CUTSCENE_END` | 10 | `events.Event.CutsceneEnd(data)` | A cutscene ended. `name` the cutscene. |
| `LUMA_EVENTS_EVENT_ACTOR_APPEAR` | 11 | `events.Event.ActorAppear(data)` | An object appeared. `actor` the object, `name` its name. |
| `LUMA_EVENTS_EVENT_ACTOR_KILL` | 12 | `events.Event.ActorKill(data)` | An object was killed. `actor` the object, `name` its name. |
| `LUMA_EVENTS_EVENT_PLAYER_SPIN` | 13 | `events.Event.PlayerSpin(data)` | The player started a spin. |
| `LUMA_EVENTS_EVENT_PLAYER_JUMP` | 14 | `events.Event.PlayerJump(data)` | The player jumped off the ground. |
| `LUMA_EVENTS_EVENT_PLAYER_LAND` | 15 | `events.Event.PlayerLand(data)` | The player landed. |
| `LUMA_EVENTS_EVENT_PAUSE` | 16 | `events.Event.Pause(data)` | The pause menu opened (`value` 1) or closed (`value` 0). |
| `LUMA_EVENTS_EVENT_STAGE_END` | 17 | `events.Event.StageEnd(data)` | A stage ended. `name` the stage. |
| `LUMA_EVENTS_EVENT_GAME_SAVE` | 18 | `events.Event.GameSave(data)` | The game wrote the save. |
| `LUMA_EVENTS_EVENT_POWER_STAR_APPEAR` | 19 | `events.Event.PowerStarAppear(data)` | A power star appeared. `value` its number. |
| `LUMA_EVENTS_EVENT_TALK_LINE` | 20 | `events.Event.TalkLine(data)` | A line of talk opened. `actor` the speaker, `name` the message ID. |
| `LUMA_EVENTS_EVENT_TALK_CHOICE` | 21 | `events.Event.TalkChoice(data)` | The player answered a yes/no question. `value` 1 yes, 0 no. `actor` who asked, `name` the message ID. |
| `LUMA_EVENTS_EVENT_CUTSCENE_PART` | 22 | `events.Event.CutscenePart(data)` | A part of a cutscene started. `value` its index, `name` its name. |
| `LUMA_EVENTS_EVENT_CAMERA_CHANGE` | 23 | `events.Event.CameraChange(data)` | The camera changed. `value` 1 an event camera, 0 an area camera. `name` its ID. |
| `LUMA_EVENTS_EVENT_SOUND_START` | 24 | `events.Event.SoundStart(data)` | A sound effect started. `value` its number, `name` its name. |
| `LUMA_EVENTS_EVENT_EFFECT_START` | 25 | `events.Event.EffectStart(data)` | An object started an effect. `actor` the object, `name` the effect. |
| `LUMA_EVENTS_EVENT_SCREEN` | 26 | `events.Event.Screen(data)` | A screen layout was shown (`value` 1) or removed (`value` 0). `name` its name. |
| `LUMA_EVENTS_EVENT_ACTOR_MESSAGE` | 27 | `events.Event.ActorMessage(data)` | An object received a message. `value` the message, `actor` the receiver, `name` its name. |
| `LUMA_EVENTS_EVENT_PLAYER_ACTION` | 28 | `events.Event.PlayerAction(data)` | The player started an action. `value` the action. |

The numbers of messages and actions are listed in [Events](../events.md).

## events structs

### LumaEventsGame

`events.Game` in LumaScript.

| Field | C type | LumaScript | Description |
|---|---|---|---|
| `size` | `uint32_t` | | `sizeof(LumaEventsGame)` in the game that runs. |
| `value` | `int32_t` | `i32` | The kind's number. 0 where the kind has none. |
| `flags` | `uint32_t` | `u32` | `LUMA_EVENTS_OTHER_THREAD` when the event was raised off the game thread. The game raises its events on the game thread, stage loads included. |
| `name_size` | `uint32_t` | `u32` | The name's length in bytes without the NUL, even when `name` holds less. 0: no name. |
| `actor` | `uint64_t` | `u64` | The object's address (a `LiveActor *`). 0 where the kind has none, or once its scene is gone. |
| `name` | `char[128]` | `text` | The name as UTF-8, cut at 127 bytes. |

The game keeps its objects until their scene (a stage, the title) ends. So `actor`, when it is not 0, is the object the event was raised for, still in memory. It may be dead (killed objects stay until the scene ends). Once the scene ended before the event was given, `actor` is 0.

## Example

This mod counts the coins of each stage and says the count when the stage ends.

```c
#include <luma.h>
#include <luma_events.h>

static const LumaEventsService* events;
static int64_t coins;

static void told(void* user, const LumaEvent* event) {
    (void)user;
    const LumaEventsGame* game = (const LumaEventsGame*)event->data;
    if (event->kind == LUMA_EVENTS_EVENT_STAGE_START) {
        coins = 0;
    } else if (event->kind == LUMA_EVENTS_EVENT_COIN) {
        coins += game->value;
    }
}

static int32_t start(const LumaCore* core, const LumaMod* mod) {
    (void)core;
    return events->watch(mod, (1u << LUMA_EVENTS_EVENT_STAGE_START) | (1u << LUMA_EVENTS_EVENT_COIN), told, 0);
}

static const LumaImport imports[] = {
    LUMA_SERVICE(events, LUMA_EVENTS_NAME, "1.0"),
};

LUMA_MOD(imports, start, 0);
```

## Mod log messages

The service uses the common messages only. For more information, see [Calling services](calling-services.md#mod-log-messages).

## Further reading

* [Events](../events.md), with the function that raises each event
* [Hooks](../lumascript/hooks.md)
