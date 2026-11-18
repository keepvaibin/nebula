---
title: fs and fs.exchange services
shortTitle: fs
intro: The fs service reads and writes files in your mod's own data folder and reads its installed files. The fs.exchange service reads and writes files in the Exchange folder that the player and other mods share.
contentType: reference
---

## About the fs services

Two services share one set of functions:

| | `fs` | `fs.exchange` |
|---|---|---|
| Version | 1.0 | 1.0 |
| Header | `luma_fs.h` | `luma_fs.h` |
| Permission | None | `fs.exchange` |
| C import | `LUMA_SERVICE(slot, "fs", "1.0")` | `LUMA_SERVICE(slot, "fs.exchange", "1.0")` |
| LumaScript | `use svc.fs` | `use svc.fs.exchange`, then `exchange.read(...)` |

They work with three folders:

| Folder | Where | Functions |
|---|---|---|
| Data folder | `mod-data/<id>` in Nebula's data folder. Your mod's own, read and written. Made at the first write. | `fs.read`, `write`, `remove`, `move`, `list` |
| Package folder | Your mod's installed folder in the Mods folder. Read only. | `fs.read_package`, `fs.list_package` |
| Exchange folder | `Exchange` in Nebula's data folder, shared by the player and every mod granted `fs.exchange`. A file there can change or go between two requests. | `fs.exchange.read`, `write`, `remove`, `move`, `list` |

Nebula's data folder is the one given with `--data`, or the system's folder for application data. A test run has its own. On Android, the data folder is private to the app, so the player cannot open the Exchange folder and the **Mods** page has no **Exchange folder** button. There, the Exchange folder is shared between mods only. On other systems, the **Exchange folder** button on the **Mods** page opens it.

Every call is a request. The service checks it on the game thread, does the work on one worker thread in the order the requests were made, and answers with one event at the start of a later frame. In a test run's deterministic mode, a request is answered at the start of the next frame, however fast the disk. Every request is closed after its event. A write is copied before the call returns, so a write followed by a read in the same frame reads what was written. When the player quits, the game does every write, remove and move already asked before it closes, also one asked in the last frame. Their events do not come.

## Paths

A path is the same on every platform, so a mod that works on one works on all:

* Relative to the folder, with `/` between parts.
* 1 to 255 bytes in all, and at most 16 parts.
* Each part is 1 to 64 bytes of ASCII letters, digits, space, `-`, `_` and `.`.
* No part starts or ends with a space or `.`. So `.`, `..` and hidden names are not paths.
* No Windows device name (`CON`, `PRN`, `AUX`, `NUL`), alone or before a dot, in any case.
* No part ending in `.tmp.` followed by digits and dots.

Letter case never makes two names. `Best.bin` and `best.bin` are the same file on every system. A write onto an existing file keeps the case the file has. A move that changes only the case renames the file.

No link is followed. A path through a symbolic link or a junction is `LUMA_E_DENIED`.

A path outside these rules, or the empty path `""` where the function does not allow it, is `LUMA_E_ARGUMENT`, reported.

## Limits

| C constant | LumaScript | Value | Meaning |
|---|---|---|---|
| `LUMA_FS_REQUESTS_MOST` | `fs.REQUESTS_MOST` | 32 | Requests one mod has open in `fs` and `fs.exchange` together. |
| `LUMA_FS_FILE_MOST` | `fs.FILE_MOST` | 67108864 | The largest file read or written, in bytes (64 MiB). |

| Limit | Value | Status |
|---|---|---|
| A mod's data folder | 256 MiB | An event with `LUMA_E_LIMIT`, reported once a session for each mod and folder |
| The Exchange folder | 1 GiB | An event with `LUMA_E_LIMIT`, reported once a session for each mod |
| A list | 65536 lines and 1 MiB | An event with `LUMA_E_TOO_LARGE` |

A folder's space is measured at each write, without the old bytes of the file the write replaces.

## Statuses of events

Every event carries one of these statuses:

| Status | LumaScript | When |
|---|---|---|
| `LUMA_OK` | `none` | Done. |
| `LUMA_E_NOT_FOUND` | `NotFound` | No such file or folder. |
| `LUMA_E_EXISTS` | `Exists` | A move whose new path is taken, or a write where a folder has that path. |
| `LUMA_E_TOO_LARGE` | `TooLarge` | A file over 64 MiB, or a list over 1 MiB or 65536 entries. |
| `LUMA_E_LIMIT` | `Limit` | The folder's space is full: 256 MiB for a data folder, 1 GiB for the Exchange folder. |
| `LUMA_E_DENIED` | `Denied` | A path through a link or junction, or a file the system refuses to open. |
| `LUMA_E_IO` | `Io` | The system failed to read or write it. |
| `LUMA_E_ABSENT` | `Absent` | The folder cannot be: this platform has no data folder, or the mod has no installed folder for `read_package` and `list_package`. |
| `LUMA_E_MEMORY` | `Memory` | No memory to queue the request. |

## fs functions

Every function runs on the game thread. Every request function returns `LUMA_PENDING` and the request in `*out`, and answers `LUMA_E_ARGUMENT` (reported) for a path outside the rules, a null path, `fn` or `out`, and `LUMA_E_LIMIT` (reported) for a 33rd open request.

In LumaScript, each request can be called with a closure, or waited in a task without one.

### read

Reads the whole file at `path` in the mod's data folder.

```c
int32_t (*read)(const LumaMod* mod, const char* path, LumaEventFn fn, void* user, LumaHandle* out);
```

```lumascript
fs.read(path: text, fn(e: fs.ReadEvent)) -> fs.Request?
wait fs.read(path: text) -> bytes?
```

A `READ` event follows. Its `value` is the file's length in bytes, and its data is the file. `""` is `LUMA_E_ARGUMENT`.

### read_package

As `read`, from your mod's own installed files. Read only.

```c
int32_t (*read_package)(const LumaMod* mod, const char* path, LumaEventFn fn, void* user, LumaHandle* out);
```

```lumascript
fs.read_package(path: text, fn(e: fs.ReadEvent)) -> fs.Request?
wait fs.read_package(path: text) -> bytes?
```

### take_read

Copies the file inside the `READ` event's call. C only: LumaScript takes the data for you.

```c
int64_t (*take_read)(const LumaMod* mod, LumaHandle request, void* buf, uint64_t size);
```

Call it once, inside the `READ` event's call for that handle. It copies `min(value, size)` bytes into `buf` and returns `value`, the whole file's length. Pass a null `buf` with `size` 0 to ask the length first. Asking does not count as the take.

| Returns | When |
|---|---|
| The file's length | Copied. |
| `LUMA_E_HANDLE` (reported) | Outside the event's call, another handle, or a second take. |
| `LUMA_E_ARGUMENT` (reported) | A null `buf` with a `size` above 0. |

The request is closed after the event's call, whether or not you called `take_read`.

### write

Writes `data` as the whole file at `path` in the mod's data folder, making its folders.

```c
int32_t (*write)(const LumaMod* mod, const char* path, const void* data, uint64_t bytes, LumaEventFn fn,
                 void* user, LumaHandle* out);
```

```lumascript
fs.write(path: text, data: bytes, fn(e: fs.WriteEvent)) -> fs.Request?
wait fs.write(path: text, data: bytes) -> u64?
```

The data is copied before the call returns. The file is replaced whole or not at all. A `WRITE` event follows, with `value` the bytes written. A null `data` with `bytes` above 0 is `LUMA_E_ARGUMENT`. Data over 64 MiB is `LUMA_E_TOO_LARGE`, reported. `""` is `LUMA_E_ARGUMENT`.

There is no append. Write the whole file each time.

### remove

Removes the file at `path`, or the folder and everything in it. `""` empties the data folder.

```c
int32_t (*remove)(const LumaMod* mod, const char* path, LumaEventFn fn, void* user, LumaHandle* out);
```

```lumascript
fs.remove(path: text, fn(e: fs.RemoveEvent)) -> fs.Request?
wait fs.remove(path: text) -> u64?
```

A `REMOVE` event follows, with `value` the files removed. A link inside a removed folder is removed as itself.

### move

Gives the file or folder at `from` the path `to`, making `to`'s folders. A `to` that is taken is not replaced.

```c
int32_t (*move)(const LumaMod* mod, const char* from, const char* to, LumaEventFn fn, void* user, LumaHandle* out);
```

```lumascript
fs.move(from: text, to: text, fn(e: fs.MoveEvent)) -> fs.Request?
wait fs.move(from: text, to: text) -> u64?
```

A `MOVE` event follows, with `value` 0, or `LUMA_E_EXISTS` when `to` is taken. `""`, or a `to` inside `from`, is `LUMA_E_ARGUMENT`.

### list

Lists the folder at `path` (`""` for the data folder itself).

```c
int32_t (*list)(const LumaMod* mod, const char* path, uint32_t recursive, LumaEventFn fn, void* user,
                LumaHandle* out);
```

```lumascript
fs.list(path: text, recursive: u32, fn(e: fs.ListEvent)) -> fs.Request?
wait fs.list(path: text, recursive: u32) -> text?
```

| Parameter | Type | Description |
|---|---|---|
| `path` | `text` | The folder to list. |
| `recursive` | `u32` | 0 lists the folder's own entries. 1 lists everything under it, each folder before what it holds. Any other value is `LUMA_E_ARGUMENT`. |

A `LIST` event follows, with `value` the list's length in bytes. The list is text: one line for each file or folder, its path from `path` with `/` between parts, a folder's line ending in `/`, and each line ending in `\n`. Lines are sorted by bytes. Names outside the path rules, and links, are left out.

### list_package

As `list`, in your mod's own installed files.

```c
int32_t (*list_package)(const LumaMod* mod, const char* path, uint32_t recursive, LumaEventFn fn, void* user,
                        LumaHandle* out);
```

```lumascript
fs.list_package(path: text, recursive: u32, fn(e: fs.ListEvent)) -> fs.Request?
wait fs.list_package(path: text, recursive: u32) -> text?
```

### take_list

As `take_read`, for the list's text, inside the `LIST` event's call. No NUL is added. C only.

```c
int64_t (*take_list)(const LumaMod* mod, LumaHandle request, char* buf, uint64_t size);
```

### cancel

Ends a request. No event for it is delivered after this returns.

```c
int32_t (*cancel)(const LumaMod* mod, LumaHandle request);
```

```lumascript
fs.cancel(request: fs.Request)
```

A `read` or `list` that has not begun is not done. A `write`, `remove` or `move` still happens in its place in the order, so a later read sees a consistent folder. Returns `LUMA_OK`. A handle that is not your mod's open request is `LUMA_E_HANDLE`, reported.

## fs.exchange functions

`fs.exchange` has `read`, `take_read`, `write`, `remove`, `move`, `list`, `take_list` and `cancel`. Each works as the `fs` function of the same name, in the Exchange folder. `remove("")` is `LUMA_E_ARGUMENT`: the folder is the player's.

```c
typedef struct LumaFsExchangeService {
    LumaServiceTable base;
    int32_t (*read)(const LumaMod* mod, const char* path, LumaEventFn fn, void* user, LumaHandle* out);
    int64_t (*take_read)(const LumaMod* mod, LumaHandle request, void* buf, uint64_t size);
    int32_t (*write)(const LumaMod* mod, const char* path, const void* data, uint64_t bytes, LumaEventFn fn,
                     void* user, LumaHandle* out);
    int32_t (*remove)(const LumaMod* mod, const char* path, LumaEventFn fn, void* user, LumaHandle* out);
    int32_t (*move)(const LumaMod* mod, const char* from, const char* to, LumaEventFn fn, void* user,
                    LumaHandle* out);
    int32_t (*list)(const LumaMod* mod, const char* path, uint32_t recursive, LumaEventFn fn, void* user,
                    LumaHandle* out);
    int64_t (*take_list)(const LumaMod* mod, LumaHandle request, char* buf, uint64_t size);
    int32_t (*cancel)(const LumaMod* mod, LumaHandle request);
} LumaFsExchangeService;
```

```lumascript
use svc.fs.exchange

task export(data: bytes) {
    if wait exchange.write("my-mod/save.bin", data) == none {
        log "could not write to the Exchange folder"
    }
}
```

The 32 open requests are counted over both services together.

## Events

`handle` is the request's. Every request is closed after its event. None of the events has `data` of its own; the read and list data come through the take functions.

| Kind | Value | LumaScript record | `value` on `LUMA_OK` |
|---|---|---|---|
| `LUMA_FS_EVENT_READ` | 1 | `fs.ReadEvent`, with `data: bytes` | The file's length in bytes. |
| `LUMA_FS_EVENT_WRITE` | 2 | `fs.WriteEvent` | The bytes written. |
| `LUMA_FS_EVENT_REMOVE` | 3 | `fs.RemoveEvent` | The files removed. |
| `LUMA_FS_EVENT_MOVE` | 4 | `fs.MoveEvent` | 0. |
| `LUMA_FS_EVENT_LIST` | 5 | `fs.ListEvent`, with `data: text` | The list's length in bytes. |

## When a mod stops

Nothing more is delivered to it. Its queued writes, removes and moves still happen, in order.

## Example

This mod keeps the player's best time in its data folder.

```lumascript
use svc.fs

var best: u64 = 0

task load_best() {
    guard let data = wait fs.read("best.bin") else {
        return          // no file yet, or it could not be read
    }
    if data.len() == 8 {
        best = data.read_u64(0, big: false)
    }
}

fn save_best(frames: u64) {
    var data = b"\x00\x00\x00\x00\x00\x00\x00\x00"
    data.write_u64(0, frames, big: false)
    let r = fs.write("best.bin", data, fn(e: fs.WriteEvent) {
        if let a = e.status {
            log "best time not saved: {a}"
        }
    })
}

on start {
    start load_best()
}
```

## Mod log messages

The fs services have no messages of their own. They use the common messages: `Service.Argument`, `Service.Handle`, `Service.Limit` (`32` for requests, `67108864` for a file, `256 MiB` and `1 GiB` for full folders) and `Service.Thread`. For more information, see [Calling services](calling-services.md#mod-log-messages).

## Further reading

* [save service](save.md), for small values kept with the player's game file
* [Calling services](calling-services.md)
* [About permissions](../../concepts/permissions.md)
