---
title: mods, mods.ids and mods.install services
shortTitle: mods
intro: The mods services list the mods of the session, read your mod's options and other mods' kit data, give your names numbers that never clash, and ask the player to install a mod.
contentType: reference
---

## About the mods services

| | `mods` | `mods.ids` | `mods.install` |
|---|---|---|---|
| What it does | The session's list of mods, your mod's options, kit data, and news of other mods stopping | Numbers for your names in the game's numbered spaces | Asks the player to install a `.luma` |
| Version | 1.1 | 1.0 | 1.0 |
| Header | `luma_mods.h` | `luma_mods.h` | `luma_mods.h` |
| Permission | None | None | None |
| LumaScript | `use svc.mods` | `use svc.mods.ids`, then `ids.get(...)` | `use svc.mods.install`, then `install.request(...)` |

`mods.install` needs no permission because nothing installs without the player's answer on the import screen.

Calling another mod's functions is not part of these services. A mod calls a library mod through its `needs`. For more information, see [Modules and imports](../lumascript/modules.md).

## The session's list

The list is the mods as this session started them, in the order of the **Mods** list. It is read once, at the first call, and is fixed for the session, `state` included. A mod taken out later still reads as running. To learn that another mod stopped, use `watch_mods`.

A mod can read these facts of another mod: its ID, its name as the **Mods** page shows it, its version, its place in the list, its kind, whether it has code for this platform, and its state. A mod cannot read another mod's options, files, permissions or the player's answers.

## mods functions

### count

The number of mods in the session's list.

```c
uint32_t (*count)(const LumaMod* mod);
```

```lumascript
mods.count() -> u32
```

Game thread.

### at

The mod at `index` in the list.

```c
int32_t (*at)(const LumaMod* mod, uint32_t index, LumaModsInfo* out);
```

```lumascript
mods.at(index: u32) -> mods.Info?
```

| Parameter | Type | Description |
|---|---|---|
| `index` | `u32` | 0 is the top of the list. |

Game thread.

| Returns | LumaScript | When |
|---|---|---|
| `LUMA_OK` | The mod | Found. |
| `LUMA_E_NOT_FOUND` | `none` | Past the end of the list. |
| `LUMA_E_ARGUMENT` (reported) | `none` | A null `out`, or an `out->size` below 260, the 1.0 struct. |

### find

The mod with the ID `id`.

```c
int32_t (*find)(const LumaMod* mod, const char* id, LumaModsInfo* out);
```

```lumascript
mods.find(id: text) -> mods.Info?
```

Game thread. Returns as `at`. `LUMA_E_NOT_FOUND` when no mod has that ID. A null `id` is `LUMA_E_ARGUMENT`, reported.

### option

Your mod's own option `key`, as the player has it now, as text.

```c
int64_t (*option)(const LumaMod* mod, const char* key, char* buf, uint64_t size);
```

```lumascript
mods.option(key: text) -> text?
```

Game thread. `key` is the `key` of one of your `options` in `luma.json`. The text depends on the option's kind:

| Kind | Text |
|---|---|
| `choice` | The chosen value. `off` when the player turned an optional choice off. |
| `number` | The number as the **Mods** page shows it, in its shortest decimal form, such as `0.5` or `12`. |
| `toggle` | `1` for on, `0` for off. |
| `text` | The player's text, or the option's `default` until the player types one. |
| `key` | The control's name, or the option's `default` until the player binds another. |

In C, the text and a NUL are written into `buf`, and the call returns the text's length without the NUL. A null `buf` with `size` 0 asks the length.

| Returns | LumaScript | When |
|---|---|---|
| The length | The text | Read. |
| `LUMA_E_NOT_FOUND` (reported) | `none` | A null key, or a key that is not one of your mod's options (`Mods.OptionUnknown`). |
| `LUMA_E_DENIED` (reported) | `none` | A key naming another mod's option, `<mod id>/<key>` (`Mods.OptionNotOwn`). A mod reads only its own options. |
| `LUMA_E_TOO_LARGE` (reported) | `none` | `buf` is too short for the text and its NUL (`Mods.OptionBufferShort`). |

In LumaScript, you usually read options with `option` declarations instead. For more information, see [Mod structure and lifecycle](../lumascript/lifecycle.md).

### watch_options

Gets an `OPTION` event each time the player changes one of your options while your mod runs.

```c
int32_t (*watch_options)(const LumaMod* mod, LumaEventFn fn, void* user);
```

```lumascript
mods.watch_options(fn: fn(e: mods.Event)?)
```

Game thread. Returns `LUMA_OK`. One event arrives for each option changed, in `luma.json` order, at the start of a frame after the **Mods** page applied the change. `option` called in the event gives the new value. A later call replaces `fn`. A null `fn`, or `none`, stops the events.

Layers chosen by options still change at the next start: the event says the value changed, not that content moved.

In LumaScript, `on option KEY { }` runs when one option changes.

### watch_mods

Gets a `STOPPED` event each time another mod stops while yours runs, and a `FORGOTTEN` event each time the player deletes a removed mod's data.

```c
int32_t (*watch_mods)(const LumaMod* mod, LumaEventFn fn, void* user);
```

```lumascript
mods.watch_mods(fn: fn(e: mods.Event)?)
```

Game thread. Returns `LUMA_OK`.

* A mod stops when the player turns it off on the **Mods** page, removes it, reloads it, or when the guard stops it. The event arrives after its stop and before its code is unloaded. A library mod removes what it made for that mod.
* A mod's data is forgotten when the player uses **Forget** on its line under the **Mods** page's removed mods. The game drops its numbers, its values in the game files and every value a mod keeps for it under `<id>/` keys, its global file and its data folder. A library drops anything else it holds for that ID.

A later call replaces `fn`. A null `fn`, or `none`, stops the events.

In a library mod written in LumaScript, `on mods.stopped(gone) { }` runs when a mod that called the library's functions stops. For more information, see [Library mods](../lumascript/modules.md#library-mods).

### kit_names

The names of the `kit/<module>/<name>.json` files in another mod.

```c
int32_t (*kit_names)(const LumaMod* mod, uint64_t of, const char* module, char* buf, uint64_t size,
                     uint64_t* length);
```

```lumascript
mods.kit_names(of: u64, module: text) -> list<text>?
```

Kit data is the data files a mod ships for a library mod to read, such as LumaKit's `kit/actor/` files. A library mod reads them for each mod that needs it.

| Parameter | Type | Description |
|---|---|---|
| `of` | `u64` | A mod, as LumaScript's `caller()` gives it. |
| `module` | `text` | 1 to 64 of `a-z 0-9 _ - .`, not starting with a dot, with no `..`. |

Game thread. In C, the names are written into `buf` sorted byte for byte, each followed by a 0 byte, and `*length` is the bytes they take. A null `buf` with `size` 0 asks the length. A folder mod reads like a `.luma`.

| Returns | LumaScript | When |
|---|---|---|
| `LUMA_OK` | The names | Read. An empty list when there are none. |
| `LUMA_E_TOO_LARGE` | `none` | `size` is below `*length`. Nothing is written. |
| `LUMA_E_DENIED` (reported) | `none` | `of` is not running, is not a mod, or is neither your mod nor a mod that names yours in its `needs` (`Mods.KitNotNeeded`). |
| `LUMA_E_ARGUMENT` (reported) | `none` | A `module` outside the rule, a null `length`, or a null `buf` with a `size` above 0. |

### kit_read

The bytes of `kit/<module>/<name>.json` in another mod.

```c
int32_t (*kit_read)(const LumaMod* mod, uint64_t of, const char* module, const char* name, void* buf,
                    uint64_t size, uint64_t* length);
```

```lumascript
mods.kit_read(of: u64, module: text, name: text) -> bytes?
```

Game thread. The file is read once and kept until `of` stops. `of`, `module` and `name` follow the rules of `kit_names`.

| Returns | LumaScript | When |
|---|---|---|
| `LUMA_OK` | The bytes | Read. |
| `LUMA_E_NOT_FOUND` | `none` | No such file. |
| `LUMA_E_TOO_LARGE` | `none` | `size` is below `*length`. Nothing is written. |
| `LUMA_E_FORMAT` (reported) | `none` | The file is over 4 MiB (`Mods.KitTooLarge`). |
| `LUMA_E_IO` | `none` | It could not be read. |
| `LUMA_E_DENIED` (reported) | `none` | As `kit_names`. |

### needing

The running mods with code whose `needs` names your mod, in the order of the **Mods** list.

```c
int32_t (*needing)(const LumaMod* mod, uint64_t* out, uint32_t capacity, uint32_t* count);
```

```lumascript
mods.needing() -> list<u64>
```

Game thread. Each mod is a `caller()` number. In C, up to `capacity` numbers are written into `out`, and `*count` is how many there are, which may be more than `capacity`. Returns `LUMA_OK`. A null `count`, or a null `out` with a `capacity` above 0, is `LUMA_E_ARGUMENT`, reported.

### of_mod

The mod a `caller()` number names.

```c
int32_t (*of_mod)(const LumaMod* mod, uint64_t which, LumaModsInfo* out);
```

```lumascript
mods.of_mod(which: u64) -> mods.Info?
```

Game thread. A library uses it to put what it holds for each mod in the **Mods** list's order, by `order`.

| Returns | LumaScript | When |
|---|---|---|
| `LUMA_OK` | The mod | Found. |
| `LUMA_E_NOT_FOUND` | `none` | `which` is no mod the game runs now: 0, any other number, or a mod that stopped. |
| `LUMA_E_ARGUMENT` (reported) | `none` | A null `out`, or an `out->size` below 260. |

## mods limits

| C constant | LumaScript | Value | Meaning |
|---|---|---|---|
| `LUMA_MODS_KIT_FILE_MOST` | `mods.KIT_FILE_MOST` | 4194304 | The largest kit file `kit_read` gives, in bytes (4 MiB). |

## mods events

`handle` is 0.

| Kind | Value | LumaScript case | Description |
|---|---|---|---|
| `LUMA_MODS_EVENT_OPTION` | 1 | `mods.Event.Option(value)` | To `watch_options`' function. `value` is the option's place in `options` in `luma.json`, 0 for the first. No data. |
| `LUMA_MODS_EVENT_STOPPED` | 2 | `mods.Event.Stopped(data)` | To `watch_mods`' function. `data` is the stopped mod's ID as UTF-8 text, valid during the call. `value` is 0. |
| `LUMA_MODS_EVENT_FORGOTTEN` | 3 | `mods.Event.Forgotten(data)` | To `watch_mods`' function. `data` is the forgotten mod's ID. `value` is 0. |

Each event's status is `LUMA_OK`.

## mods structs and enums

### LumaModsInfo

`mods.Info` in LumaScript.

| Field | C type | LumaScript | Description |
|---|---|---|---|
| `size` | `uint32_t` | | `sizeof(LumaModsInfo)` as the mod was built. |
| `order` | `uint32_t` | `u32` | Its place in the list from the top, from 1. |
| `kind` | `LumaModsKind` | `mods.Kind` | What kind of mod it is. |
| `state` | `LumaModsState` | `mods.State` | Its state when the list was read. |
| `has_code` | `uint32_t` | `u32` | 1 when it has code for this platform, 0 when not. |
| `reserved` | `uint32_t` | | 0. |
| `id` | `char[68]` | `text` | Its ID. |
| `name` | `char[132]` | `text` | Its name as the **Mods** page shows it: its name in `luma.json`'s `text` for the player's language, else its `name`, else its ID. |
| `version` | `char[36]` | `text` | Its `version` in `luma.json`. Empty when it has none. |

### LumaModsKind

`mods.Kind` in LumaScript.

| C | LumaScript | Value | Meaning |
|---|---|---|---|
| `LUMA_MODS_LUMA` | `mods.Kind.Luma` | 0 | A `.luma` mod. |
| `LUMA_MODS_RIIVOLUTION` | `mods.Kind.Riivolution` | 1 | A Riivolution mod. |
| `LUMA_MODS_TEXTURES` | `mods.Kind.Textures` | 2 | A texture pack. |
| `LUMA_MODS_FILES` | `mods.Kind.Files` | 3 | Loose files. |

### LumaModsState

`mods.State` in LumaScript.

| C | LumaScript | Value | Meaning |
|---|---|---|---|
| `LUMA_MODS_RUNNING` | `mods.State.Running` | 0 | Its code runs, or it has no code and its content is in. |
| `LUMA_MODS_OFF` | `mods.State.Off` | 1 | The player turned it off. |
| `LUMA_MODS_REFUSED` | `mods.State.Refused` | 2 | The check refused it. Its page on the **Mods** page says why. |
| `LUMA_MODS_NOT_STARTED` | `mods.State.NotStarted` | 3 | On, but its code did not start: the player's answer is waiting, or a mod it needs is not running. |
| `LUMA_MODS_TAKEN_OUT` | `mods.State.TakenOut` | 4 | Taken out while the game runs. |

The list is fixed for the session, so no call gives `TakenOut`.

## mods.ids

The game identifies some things by number. Two mods that each pick a number could pick the same one. `mods.ids` gives each of your names a number that no other mod's name has, and the same number in every session.

### Spaces

| Space | C constant | Mods' numbers | What a number is for |
|---|---|---|---|
| `message` | `LUMA_MODS_SPACE_MESSAGE` | 0x01000000 to 0x01FFFFFF | A message one mod's actors send and another mod's actors receive. It is above every attack number, so it reaches a receiver's `receiveOtherMsg`, and the game's own actors ignore it. |
| `sensor` | `LUMA_MODS_SPACE_SENSOR` | 0x01000000 to 0x01FFFFFF | A sensor type of a mod's actors. The game takes it as a character's sensor, so it meets the player, rides, eyes, map objects and other characters. |

A space is added by Nebula, never by a mod. Stage switches, event flags, effect names and sound names are not numbered spaces: use the game's functions, the `save` service, and your mod's content for them.

### How numbers are given

* The key is `<your mod's id>/<name>`. A name is 1 to 64 of `a-z 0-9 _ - .`.
* The first request gives a number. Every later request of the same key gives the same number, in this session, the next, and after a reinstall.
* The first number tried is derived from the key, then the next free number upward. So every machine with the same mods gives the same numbers, whatever order they asked in, unless two keys meet. When two keys meet, the one that asked first on that machine keeps the number, and the mod log gets the note `Mods.IdMoved`.
* To send a number to another machine, as in co-op play, send the key and ask `get` or `of` on each side. Never send a raw number.
* Numbers are kept in `mod-ids.json` in Nebula's settings folder. A number is never given to another key, also after its mod is removed. A renamed mod with `takes_over` keeps its old ID's numbers.

### get

The number for `<your mod's id>/<name>` in `space`.

```c
int32_t (*get)(const LumaMod* mod, const char* space, const char* name, uint32_t* out);
```

```lumascript
ids.get(space: text, name: text) -> u32
```

Game thread. Returns `LUMA_OK` and the number. An unknown space, a name outside the rule, or a null `out` is `LUMA_E_ARGUMENT`. A full space is `LUMA_E_LIMIT`. Both are reported, and LumaScript then gives 0.

### of

Another mod's number, read only.

```c
int32_t (*of)(const LumaMod* mod, const char* space, const char* mod_id, const char* name, uint32_t* out);
```

```lumascript
ids.of(space: text, mod_id: text, name: text) -> u32?
```

Game thread.

| Returns | LumaScript | When |
|---|---|---|
| `LUMA_OK` | The number | That mod asked for `<mod_id>/<name>` in this or an earlier session. |
| `LUMA_E_NOT_FOUND` | `none` | It has not. |
| `LUMA_E_ARGUMENT` (reported) | `none` | A bad space, mod ID or name, or a null `out`. |

## mods.install

### request

Asks the player to install a `.luma` from your mod's data folder.

```c
int32_t (*request)(const LumaMod* mod, const char* path, LumaEventFn fn, void* user, LumaHandle* out);
```

```lumascript
install.request(path: text, fn(e: install.DoneEvent)) -> install.Request?
wait install.request(path: text) -> u64?
```

| Parameter | Type | Description |
|---|---|---|
| `path` | `text` | A file ending in `.luma` in your mod's data folder, written there with `fs.write`. |

Game thread. The import screen opens and shows the mod. It has three choices: **Install**, **Not now**, and **Don't ask again**. When the same mod is installed at an older version, the button reads **Update** and the screen names both versions. An installed mod starts the next time Nebula starts. The mod log gets the note `Mods.InstallAsked`, naming the file's name but never its path.

| Returns | LumaScript | When |
|---|---|---|
| `LUMA_PENDING` | The request | The screen opens. A `DONE` event follows, never inside this call. The request is closed after it. |
| `LUMA_E_BUSY` | `none` | A request of your mod is open; less than 60 seconds have passed since the player last said no; any screen waiting on the player is up; or the game is held by a mod or in a cutscene the game marks unskippable. |
| `LUMA_E_NOT_FOUND` | `none` | No file at `path`. |
| `LUMA_E_DENIED` | `none` | The player chose **Don't ask again** for your mod before. No screen opens. The player can change this on your mod's page, under "May ask to install mods". |
| `LUMA_E_ABSENT` | `none` | This game has no install screen. |
| `LUMA_E_ARGUMENT` (reported) | `none` | A path outside your data folder or not ending in `.luma`, or a null `fn` or `out`. |

### cancel

Ends a request. The screen closes as **Not now** when still open. No event is delivered after this returns.

```c
int32_t (*cancel)(const LumaMod* mod, LumaHandle handle);
```

```lumascript
install.cancel(handle: install.Request)
```

Returns `LUMA_OK`. A handle that is not your mod's open request is `LUMA_E_HANDLE`, reported.

### Events

| Kind | Value | LumaScript record | Statuses |
|---|---|---|---|
| `LUMA_MODS_INSTALL_EVENT_DONE` | 1 | `install.DoneEvent` | `LUMA_OK` installed, on or off as the player chose; `LUMA_E_DENIED` the player said no, or closed the screen; `LUMA_E_FORMAT` the check refused the mod (the screen shows why), or the file changed while it was read; `LUMA_E_EXISTS` that ID is installed at the same or a newer version. `value` is 0. No data. |

No `mods.install` event carries `LUMA_E_CANCELLED`. `cancel`, your mod's stop and the game's end send nothing.

## Removed mods

A mod is removed when its folder is gone. Turning a mod off, or taking it out, does not remove it. A removed mod's numbers and saved values are kept, so they work again when it returns. The **Mods** page lists each removed mod with what it keeps, and **Forget** deletes all of it after the player confirms.

## Example

This library-style code reads each needing mod's kit files.

```lumascript
use svc.mods

fn load_all() {
    for m in mods.needing() {
        guard let info = mods.of_mod(m) else {
            continue
        }
        for name in mods.kit_names(m, "shapes") ?? [] {
            if let data = mods.kit_read(m, "shapes", name) {
                log "{info.id}: kit/shapes/{name}.json is {data.len()} bytes"
            }
        }
    }
}

on start {
    log "{mods.count()} mods in this session"
    if let me = mods.find("my-mod") {
        log "my place in the list: {me.order}"
    }
    load_all()
}
```

## Mod log messages

| Code | ID | Level | Message |
|---|---|---|---|
| 160 | `Mods.IdMoved` | Note | `{key} was given {number} in {space}; {other} already held its first choice.` |
| 161 | `Mods.IdsUnreadable` | Error | `The list of mod numbers could not be read. It was saved as mod-ids.json.bad and a new list was started.` |
| 162 | `Mods.Forgotten` | Note | `{mod}'s saved data was deleted.` |
| 163 | `Mods.InstallAsked` | Note | `{mod} asked to install {file}.` |
| 164 | `Mods.TookOver` | Note | `{mod} now uses the saved data of {old}.` |
| 165 | `Mods.OptionUnknown` | Error | `{mod} asked for an option it does not have: {key}.` |
| 166 | `Mods.OptionNotOwn` | Error | `{mod} asked for {key}, another mod's option; a mod reads only its own.` |
| 167 | `Mods.OptionBufferShort` | Error | `{mod} gave {size} bytes for its option {key}, which needs {needed}.` |

The services also use `Mods.KitNotNeeded` and `Mods.KitTooLarge`, and the common messages. For more information, see [Calling services](calling-services.md#mod-log-messages).

## Further reading

* [LumaKit](../lumakit/index.md), which reads kit data with these functions
* [save service](save.md)
* [luma.json](../luma-json.md)
