---
title: save, save.global and save.game services
shortTitle: save
intro: The save services keep your mod's values with the player's game file or across all files, and read or offer the game's own save files.
contentType: reference
---

## About the save services

| | `save` | `save.global` | `save.game` |
|---|---|---|---|
| What it keeps | Values kept with the game file the player has loaded | Values kept for the player across all files | The game's own save files, out and in |
| Version | 1.1 | 1.0 | 1.0 |
| Header | `luma_save.h` | `luma_save.h` | `luma_save.h` |
| Permission | None | None | `save.game` |
| LumaScript | `use svc.save` | `use svc.save.global`, then `global.get(...)` | `use svc.save.game`, then `game.export_file(...)` |

### Values kept with a game file

`save` values live inside the game's own save file, beside the player's stars. So a value follows the file exactly as the game's progress does:

* It is written when the game saves, at the game's own save points. Your call never writes it.
* It is copied when the player copies the file on the file-select screen.
* It is cleared when the file is erased or started new.
* It is lost when the player quits without saving, as a star would be.
* Mario's and Luigi's game data of one file keep their own values.

A file holds the values of every mod. When your mod is removed, its values stay in the file, unused, and are never given to another mod. If the player installs your mod again, it reads them again. The **Mods** page lists removed mods' data and lets the player delete it.

### Values kept across files

`save.global` values are kept in `luma/<mod id>.bin` beside the game's save file, so a backup of the saves has them. The file is written whole at the end of the frame in which you set a value, at most once a frame. It is read once, as your mod begins.

### The game's own files

`save.game` gives your mod a user file as the game would write it now, and offers a file in your mod's data folder to replace one of the player's files. A replacement always goes through Nebula's offer screen, where the player picks a file and confirms. The old file is kept in the save folder first.

## Keys

A key is 1 to 64 characters of `a` to `z`, `0` to `9`, `_`, `-` and `.`. A key outside this rule is `LUMA_E_ARGUMENT`, reported.

## Limits

| C constant | LumaScript | Value | Meaning |
|---|---|---|---|
| `LUMA_SAVE_KEY_MOST` | `save.KEY_MOST` | 64 | Bytes of a key's name. |
| `LUMA_SAVE_VALUE_MOST` | `save.VALUE_MOST` | 65536 | Bytes of a value. |
| `LUMA_SAVE_MOD_MOST` | `save.MOD_MOST` | 262144 | One mod's entries in one file, keys and values with their lengths. |
| `LUMA_SAVE_GLOBAL_MOST` | `save.GLOBAL_MOST` | 1048576 | One mod's global file. |

The values of all mods together in one game file are at most 1 MiB. A set that goes past any of these limits is `LUMA_E_LIMIT`, reported.

## save functions

### get

Reads the value of `key` in the file the game has loaded now.

```c
int64_t (*get)(const LumaMod* mod, const char* key, void* buf, uint64_t size);
```

```lumascript
save.get(key: text) -> bytes?
```

Game thread. In C, `get` copies `min(length, size)` bytes into `buf` and returns the value's whole length, also when it is over `size`, so you can ask again with room. `buf` may be null with `size` 0 to ask the length. LumaScript asks the length for you.

| Returns | LumaScript | When |
|---|---|---|
| The length, 0 or more | The value | Found. |
| `LUMA_E_NOT_FOUND` | `none` | The key is not set. |
| `LUMA_E_ABSENT` | `none` | No file is loaded: the title screen or the file-select screen. |
| `LUMA_E_ARGUMENT` (reported) | `none` | A key outside the rule, or a null `buf` with a `size` above 0. |

### set

Sets the value of `key`. The game's next save of the file writes it.

```c
int32_t (*set)(const LumaMod* mod, const char* key, const void* value, uint64_t bytes);
```

```lumascript
save.set(key: text, value: bytes) -> bool
```

Game thread. `value` is 0 to 65536 bytes.

| Returns | LumaScript | When |
|---|---|---|
| `LUMA_OK` | `true` | Set. |
| `LUMA_E_ABSENT` | `false` | No file is loaded. |
| `LUMA_E_LIMIT` (reported) | `false` | Past 64 KiB for the value, 256 KiB for your mod's entries in the file, or 1 MiB for all mods' entries in the file. |
| `LUMA_E_ARGUMENT` (reported) | `false` | A key outside the rule, or a null value with `bytes` above 0. |

### remove

Removes the value of `key`.

```c
int32_t (*remove)(const LumaMod* mod, const char* key);
```

```lumascript
save.remove(key: text) -> bool
```

Game thread.

| Returns | LumaScript | When |
|---|---|---|
| `LUMA_OK` | `true` | Removed. |
| `LUMA_E_NOT_FOUND` | `false` | The key is not set. |
| `LUMA_E_ABSENT` | `false` | No file is loaded. |
| `LUMA_E_ARGUMENT` (reported) | `false` | A key outside the rule. |

### file

Gives the loaded file's number and character.

```c
int32_t (*file)(const LumaMod* mod, uint32_t* number, LumaSaveCharacter* character);
```

```lumascript
save.file() -> (u32, save.Character)?
```

Game thread. `number` is 1 to 6.

| Returns | LumaScript | When |
|---|---|---|
| `LUMA_OK` | The number and the character | A file is loaded. |
| `LUMA_E_ABSENT` | `none` | No file is loaded. |
| `LUMA_E_ARGUMENT` (reported) | `none` | A null out. |

## save.global functions

`save.global` has `get`, `set` and `remove`, with the same parameters as the `save` functions:

```c
typedef struct LumaSaveGlobalService {
    LumaServiceTable base;
    int64_t (*get)(const LumaMod* mod, const char* key, void* buf, uint64_t size);
    int32_t (*set)(const LumaMod* mod, const char* key, const void* value, uint64_t bytes);
    int32_t (*remove)(const LumaMod* mod, const char* key);
} LumaSaveGlobalService;
```

```lumascript
global.get(key: text) -> bytes?
global.set(key: text, value: bytes) -> bool
global.remove(key: text) -> bool
```

The differences:

* There is no `LUMA_E_ABSENT`. The values exist whether or not a file is loaded.
* `set` and `remove` write the mod's global file whole on the file thread at the end of the frame. Your mod's stop writes at once.
* `set` is `LUMA_E_LIMIT` past 64 KiB for a value or 1 MiB for the mod's file.
* A first call before the file's read ends waits for it.
* When the file could not be read, every call returns `LUMA_E_IO`, and nothing is written over the file. With no memory for the values, every call returns `LUMA_E_MEMORY`.
* A file that does not parse is copied to `luma/<mod id>.bin.broken`, the mod log gets the error `Save.GlobalBroken`, and the values start empty. If the copy fails, every call returns `LUMA_E_IO` and the file is never written over.
* A write the system refuses is tried again after a wait that doubles from 60 frames to 3600 frames. The mod log gets one `Save.GlobalWriteFailed` error for each run of refusals.

| Function | Answers |
|---|---|
| `get` | `LUMA_E_NOT_FOUND`, `LUMA_E_IO`, `LUMA_E_MEMORY` |
| `set` | `LUMA_E_IO`, `LUMA_E_MEMORY` |
| `remove` | `LUMA_E_NOT_FOUND`, `LUMA_E_IO`, `LUMA_E_MEMORY` |

## save.game functions

### export_file

Asks for a user file as the game would write it now.

```c
int32_t (*export_file)(const LumaMod* mod, uint32_t file, LumaEventFn fn, void* user, LumaHandle* out);
```

```lumascript
game.export_file(file: u32, fn(e: game.ExportEvent)) -> game.Request?
wait game.export_file(file: u32) -> bytes?
```

| Parameter | Type | Description |
|---|---|---|
| `file` | `u32` | The user file, 1 to 6. |

Game thread. Returns `LUMA_PENDING` with the request. An `EXPORT` event follows at the next frame's start, and the request is closed after it. During the boot, the event waits for the first frame's start after the game has read its user files, so a mod may ask from its start. The mod log gets the note `Save.Exported`.

A `file` outside 1 to 6, or a null `fn` or `out`, is `LUMA_E_ARGUMENT`. A fifth open request is `LUMA_E_LIMIT`. Both are reported.

The bytes are one framed file of the whole user file:

| Part | Content |
|---|---|
| Header | `NSAV`, then a `u16` version 1 and a `u16` part count 3, little-endian |
| Part 1 | Mario's game data: a `u32` length and its bytes |
| Part 2 | Luigi's game data: a `u32` length and its bytes |
| Part 3 | The file's settings: a `u32` length and its bytes |

Each part is exactly as the game's save file holds it. A part with no stored bytes has length 0. The loaded file's own game data and settings are made at the event's frame start as the game's next save would write them.

### take

Copies the exported file inside the `EXPORT` event's call. C only: LumaScript takes the data for you.

```c
int64_t (*take)(const LumaMod* mod, LumaHandle handle, void* buf, uint64_t size);
```

Call it once, inside the `EXPORT` event's call for that handle. It copies `min(value, size)` bytes into `buf` and returns `value`, the file's whole length. Outside the call, for another handle, or a second time, it returns `LUMA_E_HANDLE`, reported. A null `buf` with a `size` above 0 is `LUMA_E_ARGUMENT`, reported.

### offer

Offers a file from your mod's data folder to replace one of the player's user files.

```c
int32_t (*offer)(const LumaMod* mod, const char* path, LumaEventFn fn, void* user, LumaHandle* out);
```

```lumascript
game.offer(path: text, fn(e: game.OfferEvent)) -> game.Request?
wait game.offer(path: text) -> u64?
```

| Parameter | Type | Description |
|---|---|---|
| `path` | `text` | A file in your mod's data folder, in the form `export_file` gives. At most 4 MiB. |

Game thread. The file is read and checked before any screen opens. Then Nebula's offer screen opens over the game, which pauses. It shows the offered file's character, stars and last save at the top, and the player's six files below it. The loaded file is shown as in play and cannot be picked. The player picks a file, then confirms **Replace** or **Keep**.

Before replacing, the old file's bytes are written to `luma/replaced/<date>-file<n>.bin` in the save folder. Then the game saves. An `OFFER` event follows, and the request is closed after it.

| Returns | LumaScript | When |
|---|---|---|
| `LUMA_PENDING` | The request | The file is offered. |
| `LUMA_E_BUSY` | `none` | Another offer is open (any mod's), a screen waits on the player, a mod holds the world, a cutscene plays, or the game has not yet read its user files (the first frames of the boot). |
| `LUMA_E_NOT_FOUND` | `none` | No file at `path`. |
| `LUMA_E_IO` | `none` | The file could not be read. |
| `LUMA_E_ABSENT` | `none` | This game has no offer screen. |
| `LUMA_E_ARGUMENT` (reported) | `none` | A path outside the data folder, or a null `fn` or `out`. |

### cancel

Ends a request. No event for it is delivered after this returns. An open question closes as **Keep**, and nothing is replaced. A replacement the player already confirmed still goes through.

```c
int32_t (*cancel)(const LumaMod* mod, LumaHandle handle);
```

```lumascript
game.cancel(handle: game.Request)
```

Returns `LUMA_OK`. A handle that is not your mod's open request is `LUMA_E_HANDLE`, reported.

## Events

`save.game` events. `handle` is the request's. Neither carries `data` of its own.

| Kind | Value | LumaScript record | `value` | Statuses |
|---|---|---|---|---|
| `LUMA_SAVE_GAME_EVENT_EXPORT` | 1 | `game.ExportEvent`, with `data: bytes` | The file's length. | `LUMA_OK`; `LUMA_E_NOT_FOUND` the user file is empty; `LUMA_E_MEMORY`. |
| `LUMA_SAVE_GAME_EVENT_OFFER` | 2 | `game.OfferEvent` | The user file replaced, 1 to 6, else 0. | `LUMA_OK`; `LUMA_E_DENIED` the player said no or the screen closed, or the file was loaded meanwhile; `LUMA_E_FORMAT` not a game file of this disc's form (no screen opens); `LUMA_E_IO` the old file could not be kept or saves are off, so nothing was replaced; `LUMA_E_MEMORY`. |

No `save.game` event carries `LUMA_E_CANCELLED`. `cancel`, a mod's stop and the game's end send nothing.

## Enums

### LumaSaveCharacter

`save.Character` in LumaScript.

| C | LumaScript | Value |
|---|---|---|
| `LUMA_SAVE_MARIO` | `save.Character.Mario` | 0 |
| `LUMA_SAVE_LUIGI` | `save.Character.Luigi` | 1 |

## Renamed mods

A mod renamed with `takes_over` in `luma.json` takes over the values of its old ID when the player installs it and the old mod is not installed. At its first start, its entries in every file, its global file and its mod numbers move to the new ID, and the mod log gets the note `Mods.TookOver`. For more information, see [luma.json](../luma-json.md).

## Example

This mod counts the player's visits to Good Egg Galaxy in the loaded file, and in all files together.

```lumascript
use svc.save
use svc.save.global

fn count(value: bytes?) -> u32 {
    if let v = value {
        if v.len() == 4 {
            return v.read_u32(0, big: false)
        }
    }
    return 0
}

fn as_bytes(n: u32) -> bytes {
    var b = b"\x00\x00\x00\x00"
    b.write_u32(0, n, big: false)
    return b
}

on stage "EggStarGalaxy" {
    let here = count(save.get("visits")) + 1
    save.set("visits", as_bytes(here))
    let all = count(global.get("visits")) + 1
    global.set("visits", as_bytes(all))
    log "visits in this file: {here}, in all files: {all}"
}
```

The count kept with the file is written at the game's next save, so it is lost if the player quits without saving. The count across files is written at the end of the frame.

## Mod log messages

| Code | ID | Level | Message |
|---|---|---|---|
| 180 | `Save.ChunkBroken` | Error | `Mods' data in file {file} could not be read; the file's game data loaded.` |
| 181 | `Save.Exported` | Note | `{mod} read your file {file}.` |
| 182 | `Save.Offered` | Note | `{mod} offered a file to replace file {file}.` |
| 183 | `Save.Replaced` | Note | `File {file} was replaced with the one {mod} offered.` |
| 184 | `Save.ChunkTooBig` | Error | `Mods' data did not fit in file {file}; the file was saved without it.` |
| | `Save.GlobalBroken` | Error | `This mod's data for all save files could not be read. It starts empty.` |
| | `Save.GlobalWriteFailed` | Error | `This mod's data for all save files could not be saved. Nebula will try again soon.` |

The services also use the common messages. For more information, see [Calling services](calling-services.md#mod-log-messages).

## Further reading

* [fs service](fs.md), for files of any size in your mod's data folder
* [mods service](mods.md), for removed mods and mod numbers
* [About permissions](../../concepts/permissions.md)
