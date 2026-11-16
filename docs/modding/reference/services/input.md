---
title: input service
shortTitle: input
intro: Read your mod's own actions and what the game reads from the controller, ask for text, rumble the controller, inject input, and open raw devices.
contentType: reference
---

## About the input service

The `input` service gives your mod its own actions. An action is a `key` option in `luma.json`. The player binds it on the **Controls** page, and until they do, it follows the game control named in its `default`. Your mod reads an action's state once a frame with `read`.

The service also tells you what the game read from each controller this frame, which kind of device the player uses, and lets you ask the player for a line of text. `input.inject` lets a mod with the `input.inject` permission replace what the game reads. `input.dev` lets a mod with the `input.devices` permission open raw HID devices (Windows only).

While **Settings** or the HOME Menu is open, every action reads up, and nothing pressed there reaches your mod after the menu closes.

An action in `luma.json` looks like this:

```json
{"key": "jump2", "kind": "key", "name": "Second jump", "default": "a"}
```

The key is the action's id: 1 to 64 characters of `a-z`, `0-9`, `.`, `_` and `-`. A mod can declare at most 64 actions.

| Service | Version | Permission | C import | LumaScript |
|---|---|---|---|---|
| `input` | 1.3 | None | `LUMA_SERVICE(slot, "input", "1.3")` | `use svc.input` |
| `input.inject` | 1.0 | `input.inject` | `LUMA_SERVICE(slot, "input.inject", "1.0")` | `use svc.input.inject` |
| `input.dev` | 1.0 | `input.devices` | `LUMA_SERVICE(slot, "input.dev", "1.0")` | `use svc.input.dev` |

The header is `native/include/luma_input.h`. In LumaScript, a service is called by the last part of its name, such as `input.` for `input`. Each function below shows its LumaScript form first and its C form second. For how the forms are made, see [Calling services](calling-services.md).

### Details from the header

LumaCore's input services. `input` 1.3, no permission: `LUMA_SERVICE(slot, "input", "1.3")`. - The mod's own actions (1.0), which the player binds in Controls. An action is a `key` option of the mod's luma.json: its `key` is the action's id, its `name` what Controls shows, its `default` the game control it follows until the player binds it (one of input_bindings.cpp's kTargetIds: "c" follows the game's C wherever the player has put it). The bindings live only in the game's settings (`modbind.` lines), kept while the mod is off or removed. - What the game gets from a player's controller this frame, the kind of device the player uses and an event when it changes, text typed with the system keyboard, and rumble (1.1). - What aims the main player's pointer (1.2). - The actions of a mod that needs this one, found by their ids (1.3). `input.inject` 1.0, permission "input.inject" (the **Mods** page says the mod moves the player): the mod supplies a player's controller. `input.dev` 1.0, permission "input.devices": the computer's HID devices, listed and read raw.

## Limits and constants

| Name | Value | Description |
|---|---|---|
| `LUMA_INPUT_ACTIONS_MOST` | `64` | The most actions one mod has (its key options; luma.json allows 64 options of every kind). |
| `LUMA_INPUT_PLAYERS` | `2` | The players a call names: 0 the main player, 1 the co-star (the second player's pointer). |
| `LUMA_INPUT_TEXT_MOST` | `4096` | The longest text a text request takes, in code points. |
| `LUMA_INPUT_DEVICES_MOST` | `64` | The most devices `list` gives and the most a mod has open. |
| `LUMA_INPUT_DEV_OPEN_MOST` | `8` |  |
| `LUMA_INPUT_REPORT_MOST` | `1024` | The longest report kept; a longer one is cut (flags 1 on its event). |

## Types

| Name | Declaration | Description |
|---|---|---|
| `LumaInputAction` | `typedef LumaHandle LumaInputAction;` | One of the mod's actions, as `action` gives it. Never 0; the same for the session. |
| `LumaInputInject` | `typedef LumaHandle LumaInputInject;` | One mod's supply of one player's controller (begin to end). |

## Enumerations

### `LumaInputPad`

A controller port (1.2; `LUMA_INPUT_PLAYERS` of them). 4 bytes, a C enum.

| Name | Description |
|---|---|
| `LUMA_INPUT_PAD_MAIN = 0` | The main player's. |
| `LUMA_INPUT_PAD_CO_STAR = 1` | The co-star's, the second controller. |

### `LumaInputPointerSource`

What aims a player's pointer (1.2, pointer_source).

| Name | Description |
|---|---|
| `LUMA_INPUT_POINTER_SOURCE_PLAYER = 0` | The player's own controls: the source that moved last (the default). |
| `LUMA_INPUT_POINTER_SOURCE_INFRARED = 1` | A Wii Remote's camera, as the player aims it. |
| `LUMA_INPUT_POINTER_SOURCE_GYRO = 2` | A controller's gyro, turned `sensitivity` times as far as the setting. |
| `LUMA_INPUT_POINTER_SOURCE_MOUSE = 3` | The mouse, where it points on the picture. |
| `LUMA_INPUT_POINTER_SOURCE_STICK = 4` | The pointer stick (the right stick, as bound in Controls). |

### Other constants

The game's buttons, as `LumaInputGame`.buttons holds them (the Wii Remote's and the Nunchuk's).

| Name | Description |
|---|---|
| `LUMA_INPUT_BUTTON_A = 1u << 0` |  |
| `LUMA_INPUT_BUTTON_B = 1u << 1` |  |
| `LUMA_INPUT_BUTTON_1 = 1u << 2` |  |
| `LUMA_INPUT_BUTTON_2 = 1u << 3` |  |
| `LUMA_INPUT_BUTTON_PLUS = 1u << 4` |  |
| `LUMA_INPUT_BUTTON_MINUS = 1u << 5` |  |
| `LUMA_INPUT_BUTTON_HOME = 1u << 6` |  |
| `LUMA_INPUT_BUTTON_UP = 1u << 7` |  |
| `LUMA_INPUT_BUTTON_DOWN = 1u << 8` |  |
| `LUMA_INPUT_BUTTON_LEFT = 1u << 9` |  |
| `LUMA_INPUT_BUTTON_RIGHT = 1u << 10` |  |
| `LUMA_INPUT_BUTTON_C = 1u << 11` |  |
| `LUMA_INPUT_BUTTON_Z = 1u << 12` |  |
| `LUMA_INPUT_BUTTONS_ALL = (1u << 13) - 1u` |  |

### Other constants

The kind of device a player plays with.

| Name | Description |
|---|---|
| `LUMA_INPUT_DEVICE_NONE = 0` | Nobody plays this player, or the host samples no controls. |
| `LUMA_INPUT_DEVICE_KEYBOARD_MOUSE = 1` |  |
| `LUMA_INPUT_DEVICE_PAD = 2` | A controller: Xbox, PlayStation, Switch, Steam, a generic pad. |
| `LUMA_INPUT_DEVICE_TOUCH = 3` | A phone's on-screen controls. |
| `LUMA_INPUT_DEVICE_MOTION = 4` | A Wii Remote. |

### Other constants

What a text request takes.

| Name | Description |
|---|---|
| `LUMA_INPUT_TEXT_ANY = 0` |  |
| `LUMA_INPUT_TEXT_NUMBER = 1` | Digits, one leading minus and one point. |
| `LUMA_INPUT_TEXT_NAME = 2` | No line breaks, no tabs, no control characters (a file or a player's name). |

### Other constants

The input events (`LumaEvent`.kind).

| Name | Description |
|---|---|
| `LUMA_INPUT_EVENT_DEVICE = 1` | A player's device kind changed (watch_device): `value` the new `LUMA_INPUT_DEVICE_`*, `flags` the player. |
| `LUMA_INPUT_EVENT_TEXT = 2` | A text request ended (text_request), `handle` the request: status `LUMA_OK` with `data` the UTF-8 text (no 0 at its end; `data_size` its bytes, `value` its code points), or `LUMA_E_CANCELLED` (the player canceled it, the mod did, another request took the keyboard, or the window lost the keyboard) with no data. |
| `LUMA_INPUT_EVENT_REPORT = 3` | A raw report from an open device (input.dev open): `handle` the device, `data` the report as the device sent it (its report id first), `value` the reports dropped before it (0 normally), `flags` 1 when it was cut to `LUMA_INPUT_REPORT_MOST`; or status `LUMA_E_CLOSED` once when the device went away (no event after). |

## Structures

### `LumaInputState`

An action this game frame. `pressed` and `released` hold for the one frame the action went down or up; a press and a release inside one frame give both, with `down` as the frame ends.

Its size in version 1.0 is 24 bytes.

| Name | Description |
|---|---|
| `uint32_t size` | Sizeof(`LumaInputState`) as the mod was built. |
| `uint8_t down` | 1 while held. |
| `uint8_t pressed` | 1 the frame it went down. |
| `uint8_t released` | 1 the frame it went up. |
| `uint8_t bound` | 1: the player bound it in Controls; 0: it follows its game control. |
| `float value` | 0 to 1: a trigger or stick bound to it pressed this far; 1 or 0 for a key or button. |
| `uint64_t frames` | Game frames it has been down (0 while up). |

### `LumaInputGame`

A player's controller in one game frame, as the game reads it.

Its size in version 1.1 is 28 bytes.

| Name | Description |
|---|---|
| `uint32_t size` | Sizeof(`LumaInputGame`) as the mod was built. |
| `uint32_t buttons` | `LUMA_INPUT_BUTTON_`* held. |
| `float stick_x` | The move stick, -1 to 1: right. |
| `float stick_y` | -1 to 1: up. |
| `float pointer_x` | Where the player points, -1 to 1 across the picture: right (pointer_on). |
| `float pointer_y` | -1 to 1: down. |
| `uint8_t pointer_on` | 1: the player points at the picture. |
| `uint8_t shake` | 1: a spin this read (a shake of either hand). |
| `uint8_t device` | `LUMA_INPUT_DEVICE_`*; read only (inject ignores it). |
| `uint8_t injected` | 1: a mod supplied it (input.inject); read only. |

### `LumaInputTextRequest`

Its size in version 1.1 is 24 bytes.

| Name | Description |
|---|---|
| `uint32_t size` | Sizeof(`LumaInputTextRequest`) as the mod was built. |
| `uint32_t kind` | `LUMA_INPUT_TEXT_`*. |
| `uint32_t max_length` | Code points, 1 to `LUMA_INPUT_TEXT_MOST`. |
| `uint32_t reserved` |  |
| `const char* initial` | UTF-8 the field starts with (cut to max_length); null: empty. |

### `LumaInputDeviceInfo`

Its size in version 1.0 is 400 bytes.

| Name | Description |
|---|---|
| `uint32_t size` | Sizeof(`LumaInputDeviceInfo`) as the mod was built. |
| `uint16_t vendor` | USB vendor id. |
| `uint16_t product` | USB product id. |
| `uint16_t usage_page` | Its top collection's HID usage page and usage (1/5: a game pad). |
| `uint16_t usage` |  |
| `uint16_t version` | Release number. |
| `uint16_t input_bytes` | Its longest input report, with the report id byte. |
| `char name[128]` | The product's name, UTF-8 (may be empty). |
| `char path[256]` | What `open` takes: valid while it stays plugged in. |

## `input` functions

In C, each function takes your mod (`const LumaMod* mod`) first. LumaScript passes it for you.

### `action`

```lumascript
input.action(id: text) -> input.Action?
```

```c
int32_t action(const LumaMod* mod, const char* id, LumaInputAction* out);
```

Call it on the game thread. The mod's action `id` (its key option's key) into `*out`. `LUMA_OK`; `LUMA_E_NOT_FOUND` when the mod's luma.json has no key option of that key (an answer, not reported); a null id or out: `LUMA_E_ARGUMENT` (reported) (`Service.Argument`).

### `read`

```lumascript
input.read(action: input.Action) -> input.State
```

```c
int32_t read(const LumaMod* mod, LumaInputAction action, LumaInputState* out);
```

Call it on the game thread. The action's state this game frame into `*out`. `LUMA_OK`; an action that is neither this mod's nor one of a mod that needs it (a library reads its users' actions): `LUMA_E_HANDLE` (reported) (`Service.Handle`); a null out: `LUMA_E_ARGUMENT`, a size below 1.0's: `LUMA_E_ARGUMENT` (`Service.StructSize`), both reported. While the player is in a menu of the host (Settings, the HOME menu) every action reads up.

### `game`

```lumascript
input.game(player: u32) -> input.Game?
```

```c
int32_t game(const LumaMod* mod, uint32_t player, LumaInputGame* out);
```

Needs `input` 1.1 or later.

Call it on the game thread. What the game got from `player`'s controller at its last read, after the player's bindings and any injection, into `*out` (`device` NONE: no device gives it, as in the headless host). `LUMA_OK`; `LUMA_E_ABSENT` when the game has not read that player (the co-star while nobody plays it): `*out` zeroed but its size; a player past `LUMA_INPUT_PLAYERS`, a null out or a size below 1.1's: `LUMA_E_ARGUMENT` (reported).

### `game_raw`

```lumascript
input.game_raw(player: u32) -> input.Game
```

```c
int32_t game_raw(const LumaMod* mod, uint32_t player, LumaInputGame* out);
```

Needs `input` 1.1 or later.

Call it on the game thread. As `game`, but the player's own controller while a mod injects (what the player does, which the game does not get); the same as `game` otherwise.

### `device`

```lumascript
input.device(player: u32) -> u32
```

```c
int32_t device(const LumaMod* mod, uint32_t player, uint32_t* out);
```

Needs `input` 1.1 or later.

Call it on the game thread. The kind of device `player` plays with into `*out` (`LUMA_INPUT_DEVICE_`*; NONE when nobody does or the host samples no controls). `LUMA_OK`; a bad player or a null out: `LUMA_E_ARGUMENT` (reported).

### `watch_device`

```lumascript
input.watch_device(fn: fn(e: input.Event)?)
```

```c
int32_t watch_device(const LumaMod* mod, LumaEventFn fn, void* user);
```

Needs `input` 1.1 or later.

Call it on the game thread. fn(user, event) at a frame's start each time a player's device kind changes (`LUMA_INPUT_EVENT_DEVICE`, the latest kind when it changed twice between frames); a null fn stops it. One watch a mod: a second replaces the first. `LUMA_OK`.

Events: `DEVICE(value, flags)`.

### `text_request`

```lumascript
input.text_request(request: input.TextRequest, fn(e: input.TextEvent)) -> input.Request?
wait input.text_request(request: input.TextRequest) -> u64?
```

```c
int32_t text_request(const LumaMod* mod, const LumaInputTextRequest* request, LumaEventFn fn, void* user, LumaHandle* out);
```

Needs `input` 1.1 or later.

Call it on the game thread. Opens the system keyboard (and its input method: composition, candidates) for the player to type; the game's controls are held while it is open (the game reads a controller at rest, every action reads up). The text arrives as `LUMA_INPUT_EVENT_TEXT` through fn when the player confirms (Enter) or cancels (Escape). `*out`: the request. `LUMA_PENDING`; a TEXT event follows (to fn), closed after its event. One request open at once in the game: a new one ends the open one Canceled. `LUMA_E_ABSENT` where the host has no text entry ; a null request, fn or out, a size below 1.1's, a bad kind, max_length 0 or past `LUMA_INPUT_TEXT_MOST`, or an initial that is not UTF-8: `LUMA_E_ARGUMENT` (reported).

In a task, `wait` gives the `TEXT` event's result instead of calling a closure.

### `text_peek`

```lumascript
input.text_peek(request: input.Request) -> (text, u32)
```

```c
int32_t text_peek(const LumaMod* mod, LumaHandle request, char* buffer, uint32_t size, uint32_t* length, uint32_t* caret);
```

Needs `input` 1.1 or later.

Call it on the game thread. The open request's text so far (for the mod to draw the field): up to size - 1 bytes of UTF-8 and a 0 into buffer (cut on a code point), its whole length in bytes into *length and the caret in code points into *caret (each may be null). `LUMA_OK`; a request that is not this mod's open one: `LUMA_E_HANDLE` (reported); a null buffer with size above 0: `LUMA_E_ARGUMENT` (reported).

### `text_cancel`

```lumascript
input.text_cancel(request: input.Request)
```

```c
int32_t text_cancel(const LumaMod* mod, LumaHandle request);
```

Needs `input` 1.1 or later.

Call it on the game thread. Ends the mod's open request: its event comes Canceled. `LUMA_OK`; one that is not its open one: `LUMA_E_HANDLE` (reported).

### `rumble`

```lumascript
input.rumble(player: u32, strength: f32, seconds: f32) -> bool
```

```c
int32_t rumble(const LumaMod* mod, uint32_t player, float strength, float seconds);
```

Needs `input` 1.1 or later.

Call it on the game thread. Shakes `player`'s controller at `strength` (0 to 1) for `seconds` (at most 10) of game time, beside the game's own rumble (the stronger wins); a later call replaces the mod's earlier one, strength 0 stops it. The player's Vibration setting scales it as it does the game's. `LUMA_OK`; `LUMA_E_ABSENT` when the player's device has no motor (a keyboard, a phone's screen) or the host drives none; a bad player, a strength or seconds not a number, below 0 or past their most: `LUMA_E_ARGUMENT` (reported).

### `pointer_source`

```lumascript
input.pointer_source(pad: input.Pad, kind: input.PointerSource, sensitivity: f32) -> bool
```

```c
int32_t pointer_source(const LumaMod* mod, LumaInputPad pad, LumaInputPointerSource kind, float sensitivity);
```

Needs `input` 1.2 or later.

Call it on the game thread. What aims `pad`'s pointer from the next read: only `kind` moves it (the others are not read for it), until a mod sets another, `LUMA_INPUT_POINTER_SOURCE_PLAYER` gives it back to the player's controls, or the mod that set it stops. `sensitivity` (above 0, at most 8) for GYRO, else ignored. The player's settings (invert, speeds, recenter) still apply. `LUMA_OK`; `LUMA_E_ABSENT` when the player has no such device now (no Wii Remote for INFRARED, no controller with a gyro for GYRO, no mouse for MOUSE, no controller for STICK), or for pad 1 (the co-star's pointer follows its own controller); a pad past 1, an unknown kind or a GYRO sensitivity out of range: `LUMA_E_ARGUMENT` (reported).

### `user_action`

```lumascript
input.user_action(of: u64, id: text) -> input.Action?
```

```c
int32_t user_action(const LumaMod* mod, uint64_t of, const char* id, LumaInputAction* out);
```

Needs `input` 1.3 or later.

Call it on the game thread. As `action`, the action `id` of the mod `of` (a mod as LumaScript's caller() gives it: its `LumaMod` pointer as a number), so a library starts what a mod using it defines by that mod's own keys; `read` reads it. `of` must be running and be `mod` itself or name `mod` in its luma.json `needs`. `LUMA_OK`; `LUMA_E_NOT_FOUND` when `of` has no key option of that key (an answer, not reported); any other `of`: `LUMA_E_DENIED` (reported) (`Mods.KitNotNeeded`); a null id or out: `LUMA_E_ARGUMENT` (reported).

## `input.inject` functions

In C, each function takes your mod (`const LumaMod* mod`) first. LumaScript passes it for you.

### `begin`

```lumascript
inject.begin(player: u32) -> inject.Inject?
```

```c
int32_t begin(const LumaMod* mod, uint32_t player, LumaInputInject* out);
```

Call it on the game thread. From the game's next read on, the mod supplies `player`'s controller: `*out` the injection. `LUMA_OK`; `LUMA_E_BUSY` when a mod injects that player already (an answer); a bad player or a null out: `LUMA_E_ARGUMENT` (reported).

### `set`

```lumascript
inject.set(injection: inject.Inject, state: input.Game)
```

```c
int32_t set(const LumaMod* mod, LumaInputInject injection, const LumaInputGame* state);
```

Call it on the game thread. What the game reads at its next read of that player: buttons (bits past `LUMA_INPUT_BUTTONS_ALL` are dropped), stick, pointer and shake; `device` and `injected` are not read. A read the mod set nothing for gets a controller at rest, not the last one (a mod that stops calling holds no button). A stick or pointer value that is not a number reads 0 and one past 1 reads 1, said once a call (`Service.Argument`), the call still `LUMA_OK`. `LUMA_OK`; an injection that is not this mod's open one: `LUMA_E_HANDLE`; a null state or a size below 1.1's `LumaInputGame`: `LUMA_E_ARGUMENT`; both reported.

### `end`

```lumascript
inject.end(injection: inject.Inject)
```

```c
int32_t end(const LumaMod* mod, LumaInputInject injection);
```

Call it on the game thread. The player's own controller again from the next read. `LUMA_OK`; `LUMA_E_HANDLE` as set. A mod taken out ends its injections.

## `input.dev` functions

In C, each function takes your mod (`const LumaMod* mod`) first. LumaScript passes it for you.

### `list`

```c
int32_t list(const LumaMod* mod, LumaInputDeviceInfo* out, uint32_t capacity, uint32_t* found);
```

Not callable from LumaScript.

Call it on the game thread. The HID devices plugged in now, up to `capacity` into out[] (each with its size set by the mod), and how many there are into *found (may exceed capacity; at most `LUMA_INPUT_DEVICES_MOST`). The system's keyboards and mice are not listed (it keeps them). `LUMA_OK`; `LUMA_E_ABSENT` on a platform or host that gives no raw devices ; a null found, a null out with capacity above 0, or a size below 1.0's in an entry: `LUMA_E_ARGUMENT` (reported).

### `open`

```lumascript
dev.open(path: text, fn(e: dev.Event)) -> dev.Request?
```

```c
int32_t open(const LumaMod* mod, const char* path, LumaEventFn fn, void* user, LumaHandle* out);
```

Call it on the game thread. Opens the device at `path` (from list) to read: each input report it sends arrives through fn as `LUMA_INPUT_EVENT_REPORT` at a frame's start, in order (64 kept between frames; past that the oldest is dropped and counted on the next event). `*out` the device. `LUMA_OK`; `LUMA_E_NOT_FOUND` (unplugged, or not a HID path), `LUMA_E_BUSY` (another program holds it); `LUMA_E_LIMIT` (`LUMA_INPUT_DEV_OPEN_MOST` open) (reported); `LUMA_E_ABSENT` as list; a null or empty path, fn or out: `LUMA_E_ARGUMENT` (reported).

Events: `REPORT(socket, status, data, value, flags)`.

### `write`

```lumascript
dev.write(device: dev.Request, data: bytes) -> bool
```

```c
int32_t write(const LumaMod* mod, LumaHandle device, const void* data, uint32_t size);
```

Call it on the game thread. Sends an output report (its report id first, 0 when the device numbers none). `LUMA_OK`; `LUMA_E_IO` when the device refused it; `LUMA_E_HANDLE` (not this mod's open device), a null data, size 0 or past `LUMA_INPUT_REPORT_MOST`: `LUMA_E_ARGUMENT`; both reported.

### `close`

```lumascript
dev.close(device: dev.Request)
```

```c
int32_t close(const LumaMod* mod, LumaHandle device);
```

Call it on the game thread. Closes it; no event comes after. `LUMA_OK`; `LUMA_E_HANDLE` as write. A mod taken out closes its own.

## Further reading

* [Calling services](calling-services.md)
* [Services reference](index.md)
* [About services](../../concepts/services.md)
* [Permissions](../../concepts/permissions.md)
