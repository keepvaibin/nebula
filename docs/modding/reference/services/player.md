---
title: player service
shortTitle: player
intro: Add extra players to the running scene.
contentType: reference
---

## About the player service

The `player` service adds extra players to the running scene. Each is one of the game's own player actors, driven by the second controller or by nothing. The game's own player keeps the camera, the HUD, the lives and the end of the level.

| Service | Version | Permission | C import | LumaScript |
|---|---|---|---|---|
| `player` | 1.1 | None | `LUMA_SERVICE(slot, "player", "1.1")` | `use svc.player` |

The header is `native/include/luma_player.h`. In LumaScript, a service is called by the last part of its name, such as `player.` for `player`. Each function below shows its LumaScript form first and its C form second. For how the forms are made, see [Calling services](calling-services.md). A C mod may import an earlier minor version when it calls only what that version has: each function says the version it needs when it is not 1.0.

### Details from the header

LumaCore's player service: extra players in the running scene, each a MarioActor of the game's own (the same brother as the game's player: the scene loaded only that one's files), driven by the second controller or by nothing. The game's own player stays the scene's: its camera, its HUD, its lives, the level's end. What of a player is drawn (1.1, set_drawn): a first-person camera's body.

## Limits and constants

| Name | Value | Description |
|---|---|---|
| `LUMA_PLAYER_MOST_EXTRA` | `3` | Extra players standing at once, across mods (the game's own player not counted). |

## Enumerations

### Other constants

1.1. What of a player set_drawn draws (bits).

| Name | Description |
|---|---|
| `LUMA_PLAYER_DRAWN_BODY = 1u << 0` | His model in every form, his hands, the Bee's fur and wings, the Rainbow's glow, what he carries, his silhouette behind walls, his blur. |
| `LUMA_PLAYER_DRAWN_SHADOW = 1u << 1` | His shadow on the ground. |
| `LUMA_PLAYER_DRAWN_ALL = (1u << 2) - 1u` |  |

### `LumaPlayerInput`

What drives an extra player (`LumaPlayerInit`.input).

| Name | Description |
|---|---|
| `LUMA_PLAYER_INPUT_PAD = 0` | The controller `pad`: its stick, buttons and shakes drive it as player 1's drive player 1. |
| `LUMA_PLAYER_INPUT_NONE = 1` | None: it stands, falls and is pushed as the game moves it; a mod moves it by the game's calls on it. |

## Structures

### `LumaPlayerInit`

An extra player as make makes it.

Its size in version 1.0 is 36 bytes.

| Name | Description |
|---|---|
| `uint32_t size` | `sizeof(LumaPlayerInit)` as the mod was built. |
| `LumaPlayerInput input` | 4 bytes, a C enum. |
| `uint32_t pad` | `LUMA_PLAYER_INPUT_PAD`: 1, the game's second controller (input's `LUMA_INPUT_PAD_CO_STAR`); NONE: 0. |
| `float pos[3]` | Where it stands, in the main stage's space; also where it comes back after a fall or a death. |
| `float rot[3]` | Its turn in degrees, as a placement's. |

## `player` functions

In C, each function takes your mod (`const LumaMod* mod`) first. LumaScript passes it for you.

### `make`

```lumascript
player.make(init: player.Init) -> LiveActor?
```

```c
int32_t make(const LumaMod* mod, const LumaPlayerInit* init, LumaActor* out);
```

Game thread, while a scene with a player runs. An extra player standing at `init`'s place: `LUMA_OK` and `*out`, the MarioActor (LumaCore's class_of and is_a, the game's calls and hooks take it; a hook on MarioActor's code runs for it too: `this` tells them apart). Its own reads of the controller take init's (none: nothing pressed). It is hurt, falls and dies as the game's player does, but its death ends nothing: it comes back at init's place with its life full. It goes when the mod removes it, when the mod goes, or with its scene. A null init or out: `LUMA_E_ARGUMENT` (reported); an init smaller than 1.0's: `LUMA_E_ARGUMENT` (reported) (`Service.StructSize`); an input outside the enum, a pad other than 1 for PAD or other than 0 for NONE, a place or turn that is not finite: `LUMA_E_ARGUMENT` (reported); `LUMA_PLAYER_MOST_EXTRA` standing: `LUMA_E_LIMIT` (reported); no scene with a player running: `LUMA_E_STATE`. Off the game thread: `LUMA_E_THREAD` (reported); while the game takes the mod out: `LUMA_E_CANCELLED`.

### `remove`

```lumascript
player.remove(player: LiveActor)
```

```c
int32_t remove(const LumaMod* mod, LumaActor player);
```

Call it on the game thread. The mod's extra player goes: it is no longer moved, drawn or touched (MR::isDead on it is true). `LUMA_OK`; not one of the mod's standing extra players: `LUMA_E_HANDLE` (reported). Off the game thread: `LUMA_E_THREAD` (reported); while the game takes the mod out: `LUMA_E_CANCELLED`.

### `set_drawn`

```lumascript
player.set_drawn(player: LiveActor, parts: u32)
```

```c
int32_t set_drawn(const LumaMod* mod, LumaActor player, uint32_t parts);
```

Needs `player` 1.1 or later.

Call it on the game thread. Of `player` (the game's own player, or any standing extra player) only `parts` (`LUMA_PLAYER_DRAWN_`*) drawn from the next frame; the rest is not drawn while everything else of him goes on: his animation and joints (a camera on his eye), sounds, effects, collision, his reflection in mirrors and water. A first-person camera's body: `LUMA_PLAYER_DRAWN_SHADOW`. The newest mod's call holds; `LUMA_PLAYER_DRAWN_ALL` gives him back whole. Until then, the mod's end, or his scene's end. `LUMA_OK`. A null `player` or one that is not a player standing in the scene, parts outside ALL: `LUMA_E_ARGUMENT` (reported). Off the game thread: `LUMA_E_THREAD` (reported); while the game takes the mod out: `LUMA_E_CANCELLED`.

## Further reading

* [Calling services](calling-services.md)
* [Services reference](index.md)
* [About services](../../concepts/services.md)
* [Permissions](../../concepts/permissions.md)
