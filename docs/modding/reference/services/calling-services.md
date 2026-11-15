---
title: Calling services
shortTitle: Calling services
intro: How a mod finds a service, how each C function of a service appears in LumaScript, and how statuses, results and events reach your code.
contentType: reference
---

## About calling services

Every service is a table of C functions declared in a header of Nebula's SDK, such as `luma_time.h` for `time`. C code calls the table directly. LumaScript reads the same header when you build and gives each function a LumaScript form, called its face. This page explains both sides and the rules that connect them.

Every service page in this reference shows each function in C and in LumaScript. The rules below explain how one becomes the other.

## Finding a service

### In LumaScript

Write `use svc.NAME` at the top of a module. The service is then in scope by the last part of its name, or by the name you give it with `as`.

```lumascript
use svc.time
use svc.draw.view
use svc.net.internet as web

on start {
    let now = time.now()
    log "frame {now.frame}"
}
```

In this module, `time` names the `time` service, `view` names `draw.view`, and `web` names `net.internet`.

LumaLink writes the import for each service your code uses, with the version the SDK's header gives. You don't write versions in LumaScript.

To use a service only when it is available, write `use optional svc.NAME`. The service is then `none` when the game has no such service, when the game's version of it is older than the one you build against, or when the player refused its permission. Test it before every call:

```lumascript
use optional svc.net.internet as web

on start {
    guard web != none else {
        log "no internet: the leaderboard is off"
        return
    }
    // Calls to web go here.
}
```

A call to an optional service with no such test before it is a compile error.

### In C

Import the service in your mod's import list with `LUMA_SERVICE(slot, name, version)`. The game fills the slot with the service's table before your `start` runs.

```c
#include "luma.h"
#include "luma_time.h"

static const LumaTimeService* time_svc;

static const LumaImport imports[] = {
    LUMA_SERVICE(time_svc, LUMA_TIME_NAME, "1.0"),
};
```

A service the game does not have stops the mod from loading, and the **Mods** page names the service. To make a service optional, write the row with the `LUMA_OPTIONAL` flag. The slot is then null when the service is not available.

```c
#include "luma_net.h"

static const LumaNetInternetService* web;

static const LumaImport imports[] = {
    {LUMA_IMPORT_SERVICE, LUMA_OPTIONAL, LUMA_NET_INTERNET_NAME, "1.0", &web, 0, 0, 0},
};
```

After `start`, you can also ask for a table by name:

```c
const LumaTimeService* t = (const LumaTimeService*)core->service(mod, "time", 1, 0);
```

`service` returns null when the game has no such service, has an older minor version than the one you ask for, or when the player has not granted the permission the service needs. A refusal for a permission is written to the mod log.

## Versions

Each service has a major and a minor version, written `MAJOR.MINOR`, such as `1.0`. The header defines both as `LUMA_<NAME>_MAJOR` and `LUMA_<NAME>_MINOR`.

* A later minor version only adds. It adds functions at the end of the table, fields at the end of structs, and new enum values and event kinds. It never changes what an earlier function does.
* A mod that asks for version 1.2 gets the table of any 1.x where x is at least 2. It reads only what 1.2 defines.
* A mod that asks for a minor version newer than the game's is refused, and the **Mods** page names the service and the version. An optional import is left empty instead.
* A different major version is a different table.

Every service of LumaCore v1 is major version 1.

The text of a version in an import row is `MAJOR.MINOR`, each a decimal number from 0 to 65535 with no leading zero. Anything else stops the mod from loading.

## Rules every service function keeps

* Every C function takes the calling mod as its first parameter, `const LumaMod* mod`, as `start` received it. LumaScript passes it for you.
* A function runs on the game thread only, unless its reference says "any thread". A call from another thread returns `LUMA_E_THREAD` and is reported.
* A function returns within the frame. Work that waits on the outside world, such as a file read or a connection, returns `LUMA_PENDING`, and its answer arrives later as an event.
* A handle that another mod holds, or one that is closed, is `LUMA_E_HANDLE`. A handle is never 0, and one session never gives the same handle twice.
* A service copies the text and data you give it before the call returns, and never keeps your pointer.
* Buffers and queues a service keeps for your mod are shown on the **Mods** page and freed when your mod stops.
* When your mod stops, its services cancel its requests and close its handles, and no event reaches it after its `stop`.
* A service starts no thread and opens no file, socket or device until a mod uses it.

## Statuses

A C function returns a status of type `int32_t`. A value of 0 or more is success. A value below 0 is an error.

| Status | Value | Meaning | Reported |
|---|---|---|---|
| `LUMA_OK` | 0 | Done. | |
| `LUMA_PENDING` | 1 | Accepted. The answer arrives as an event. | |
| `LUMA_E_ARGUMENT` | -1 | An argument the function does not take, a null or unknown mod, or a struct whose `size` is too small. | Yes |
| `LUMA_E_THREAD` | -2 | Called off the game thread, where the function does not allow it. | Yes |
| `LUMA_E_HANDLE` | -3 | A handle that is closed, not the mod's, or never was. | Yes |
| `LUMA_E_LIMIT` | -4 | A limit the service states was reached. | Yes |
| `LUMA_E_DENIED` | -5 | Outside what the player granted, such as a host that `luma.json` does not name. | |
| `LUMA_E_ABSENT` | -6 | This platform or this device has no such thing. | |
| `LUMA_E_NOT_FOUND` | -7 | No such file, host, device or name. | |
| `LUMA_E_EXISTS` | -8 | It exists already. | |
| `LUMA_E_IO` | -9 | The system failed to read, write, open or send. | |
| `LUMA_E_TIMEOUT` | -10 | No answer within the time the call or the service gives. | |
| `LUMA_E_CLOSED` | -11 | The other side closed, or the thing was closed meanwhile. | |
| `LUMA_E_CANCELLED` | -12 | The mod canceled it, or the game is taking the mod out. | |
| `LUMA_E_MEMORY` | -13 | No memory for it. | |
| `LUMA_E_BUSY` | -14 | Not now: a queue is full or the device is in use. Try again on a later frame. | |
| `LUMA_E_FORMAT` | -15 | The data is not of the form the function reads. | |
| `LUMA_E_TOO_LARGE` | -16 | More than the function or the service allows at once. | |
| `LUMA_E_UNREACHABLE` | -17 | The network cannot reach the other side. | |
| `LUMA_E_STATE` | -18 | Not valid now, such as a draw call outside a draw point's function. | |

### Mistakes and answers

Statuses fall into two groups.

* Mistakes are errors in the mod: `LUMA_E_ARGUMENT`, `LUMA_E_THREAD`, `LUMA_E_HANDLE`, `LUMA_E_LIMIT`, and a struct that is too small. The service returns the status and also reports it. The report goes to the game's log once, and to the mod log and the **Mods** page in the player's language. Repeats are counted. A mistake is never a value your LumaScript sees.
* Answers come from the world: a file that is not there, a refused connection, an absent device. The service returns them and reports nothing, because your code is expected to handle them. In LumaScript an answer is a value of the enum `svc.Answer`.

`LUMA_E_CANCELLED` never reaches running LumaScript code.

### svc.Answer

`svc.Answer` is one enum for every service. Its cases are the answers:

| Case | C status |
|---|---|
| `Denied` | `LUMA_E_DENIED` |
| `Absent` | `LUMA_E_ABSENT` |
| `NotFound` | `LUMA_E_NOT_FOUND` |
| `Exists` | `LUMA_E_EXISTS` |
| `Io` | `LUMA_E_IO` |
| `Timeout` | `LUMA_E_TIMEOUT` |
| `Closed` | `LUMA_E_CLOSED` |
| `Memory` | `LUMA_E_MEMORY` |
| `Busy` | `LUMA_E_BUSY` |
| `Format` | `LUMA_E_FORMAT` |
| `TooLarge` | `LUMA_E_TOO_LARGE` |
| `Unreachable` | `LUMA_E_UNREACHABLE` |
| `State` | `LUMA_E_STATE` |

As a type, write it through a service your module uses, such as `web.Answer`. Shown in text, a case is its name: `"failed: {a}"` gives `failed: Denied`.

## How C becomes LumaScript

### Names

| C | LumaScript | Example |
|---|---|---|
| The table `Luma<Name>Service` | The service | `LumaTimeService` is `time` |
| A member function | `<service>.<member>` | `time.now` |
| A struct `Luma<Name><T>` | `<service>.<T>` | `LumaTimeNow` is `time.Now` |
| A named enum `Luma<Name><T>` | `<service>.<T>`, each case without the common prefix, in CamelCase | `LUMA_TIME_FRAMES` is `time.Clock.Frames`, `LUMA_SYSTEM_D3D12` is `system.Renderer.D3d12` |
| A handle type `typedef LumaHandle Luma<Name><K>` | `<service>.<K>` | `LumaTimeTimer` is `time.Timer` |
| A handle type named as the service itself | The service's last word | `LumaMovie` is `movie.Movie`, `LumaContentModel` is `content.model.Model` |
| A bare `LumaHandle` | `<service>.Request` | `audio.Request` |
| `#define LUMA_<NAME>_<C>` of a whole number | The constant `<service>.<C>` | `LUMA_TIME_TIMERS_MOST` is `time.TIMERS_MOST` |
| A case of an unnamed `enum { }` | The constant `<service>.<C>` | `LUMA_INPUT_TEXT_NAME` is `input.TEXT_NAME` |
| The event enum `Luma<Name>Event` | Events `<service>.<kind>`, in lower case | `LUMA_NET_INTERNET_EVENT_RECEIVED` is `received` |
| `LumaColor` | `color` | Packed as 0xRRGGBBAA, each channel `round(c * 255)` |

The `MAJOR`, `MINOR` and `NAME` defines are not constants of the face. A struct of a header that no service owns takes the name of the service it is grouped with: `LumaNetHeader` is `net.internet.Header` when you use `net.internet`.

### Parameters

The first parameter, `mod`, is never written in LumaScript. Every other parameter maps as follows:

| C parameter | LumaScript |
|---|---|
| A number or `bool` | The same type: `uint32_t` is `u32`, `float` is `f32`, `int64_t` is `i64` |
| A named enum of the service | That enum |
| `const char* x` with no size after it | `text`. A NUL inside the text is a script error. |
| `const char*`, `const void*` or `const uint8_t*`, then `uint64_t` | One argument: `text` for `char`, otherwise `bytes`. LumaScript passes the length. |
| `const void*` or `const uint8_t*`, then `uint32_t size` | `bytes`, its length checked to fit 32 bits |
| `uint32_t w, uint32_t h, const void* rgba8` | `w`, `h`, and `bytes` of w x h x 4 |
| `const float* x` whose comment says "two", "three" or "four floats" | `vec2`, `vec3` or `vec4` |
| `const T x[N]` | `vec2` to `vec4` for 2 to 4 floats, otherwise a tuple of N values |
| `const Luma<Name><T>*` | The struct by value. LumaScript sets `size` and zeroes `reserved` fields. It is `T?` when the comment says the pointer may be null. |
| `const T* x, uint32_t count` | One `list<T>` argument |
| Numbers counted by another function, such as `audio.stream_push`'s samples | One `list` of numbers. LumaScript works out the count. |
| `LumaEventFn fn, void* user` | One closure argument. See [Requests and events](#requests-and-events). |
| `X fn, void* user`, where the header declares `typedef void (*X)(void* user, ...)` | One `fn(args)` value the service calls back. It is kept until the mod ends or you replace it. |
| A handle type | The handle kind, such as `time.Timer` |
| A non-const pointer to a number, enum, handle or struct | An out. It is not an argument. It is part of the result. |
| `char*`, `void*` or `uint8_t*`, then a size | An out buffer. The result is `text` or `bytes`. |
| `T* out, uint32_t capacity, uint32_t* count` | A `list<T>` out |
| `const void** data, uint64_t* bytes` | `bytes` the service lends, copied into your own |

A parameter of any other form makes the function callable from C only. `lumalink services` lists every service function and whether LumaScript can call it. Every function of every service is callable from LumaScript except those of `draw.native`, which are for C mods by design.

### Results

LumaScript turns each status and out into a result:

| C function | LumaScript result |
|---|---|
| `int32_t` status with no answers in its comment | The outs, or nothing. A mistake gives zeros or nothing, and was reported. |
| `int32_t` status with answers | `T?` of the outs, `none` for any answer or mistake. With no outs, `bool`: `true` for `LUMA_OK`. |
| `int64_t`, `uint32_t` or `uint64_t` that is not a status | The number |
| `int64_t` length with answers and an out buffer | The buffer as `T?` |
| One out | That type |
| Several outs | A tuple, in the order of the C parameters |
| An out buffer | `text` or `bytes`. LumaScript asks the length first, then fills it. |
| A handle the call opens at once, with no event | An out like any other, such as `content.fx.Effect`. A mistake gives the zero handle. |

For example, `time.step` returns `LUMA_OK` or the answer `LUMA_E_BUSY`, so in LumaScript it returns `bool`. `time.now` has no answers and one out, so it returns `time.Now`.

```lumascript
use svc.time

on start {
    let now = time.now()          // LumaTimeNow* out becomes the result
    time.hold()                   // a mistake would be reported, not returned
    if not time.step(2) {         // LUMA_E_BUSY is false
        log "another mod holds the world too"
    }
}
```

## Requests and events

A function that waits on the outside world takes `LumaEventFn fn, void* user`, returns `LUMA_PENDING` (or `LUMA_OK` for a stream that opens at once), and gives a handle through its last parameter. The answer arrives later as a `LumaEvent`:

```c
typedef struct LumaEvent {
    uint32_t size;     /* sizeof(LumaEvent) in the game that runs */
    uint32_t kind;     /* the service's own event kinds */
    int32_t status;    /* LUMA_OK or a LUMA_E_* */
    uint32_t flags;    /* the kind's */
    LumaHandle handle; /* the handle or the request id the call gave */
    const void* data;  /* the kind's bytes, or null */
    uint64_t data_size;
    uint64_t value;    /* a number the kind defines */
} LumaEvent;

typedef void (*LumaEventFn)(void* user, const LumaEvent* event);
```

Events are delivered on the game thread at the start of each frame:

* Mods receive events in the order of the **Mods** list, top first.
* Each mod receives its events in the order they were posted.
* Each mod receives at most `LUMA_EVENTS_PER_FRAME` (256) events a frame. The rest wait for the next frames, in order.
* After every mod's events, each mod's `on frame` handler and tasks run, so a task waiting for an answer sees it in the frame it arrives.
* An event's `data` is valid only during the call. Copy what you keep.
* A function that faults inside an event call is handled by the guard, which names the service and the event kind. For more information, see [When a mod fails](../../concepts/when-a-mod-fails.md).
* Once a `cancel` or `close` returns, no event for that handle reaches the mod.

In LumaScript, a request is called in one of two ways.

### With a closure

Pass a closure as the last argument. The call returns the handle as `<service>.<K>?`, which is `none` when the call was refused. The closure runs for each of the request's events, at a frame's start.

```lumascript
use svc.time

on start {
    let t = time.timer(time.Clock.Frames, 60, 60, fn(e: time.TimerEvent) {
        log "another second of frames ({e.value} due)"
    })
    if t == none {
        log "no timer"
    }
}
```

The closure takes the event's record, `<service>.<Kind>Event`. It has these fields:

| Field | Type | Meaning |
|---|---|---|
| `status` | `svc.Answer?` | `none` for `LUMA_OK`, otherwise the answer |
| `value` | `u64` | The number the event kind defines |
| `flags` | `u32` | The kind's flags |
| `data` | `text` or `bytes` | The event's data, for a kind that has data |

### By waiting

A request that its reference says is "closed after its event" has one event. In a task, you can wait for it without a closure. The call then returns `pending<R?>`, and `wait` gives `R`, or `none` for any answer, including a refusal.

```lumascript
use svc.fs

task load() {
    guard let data = wait fs.read("best.bin") else {
        log "no best time yet"
        return
    }
    log "read {data.len()} bytes"
}

on start {
    start load()
}
```

`R` is the request's data when its event has a take function, such as `fs.read`'s `bytes` or `fs.list`'s `text`. Otherwise `R` is the event's value, a `u64`.

`wait` is allowed in a task, in a state's body, in `enter`, and in an `on` handler, which the compiler then runs as a task. For more information, see [Tasks](../lumascript/tasks.md).

### Take functions

Some events carry data that the mod copies with a take function, such as `fs.take_read`. A take function is called inside its event's call. In LumaScript you never call it: the compiler takes the data for you and gives it to the closure as `data`, or to `wait` as its result.

### Streams

A stream is a handle with many events of several kinds, such as a socket or a playing sound. Its reference lists them on an `events:` line, for example `events: ENDED(socket, status)`.

In LumaScript, a stream takes `fn(e: <service>.Event)`. `<service>.Event` is one enum for the service, with a case for each kind any of its streams gives. Each case carries the fields its line lists, in that order. Read it with `match`:

```lumascript
use svc.audio

on start {
    let v = audio.play("Ding", none, fn(e: audio.Event) {
        match e {
            audio.Event.Ended(voice, status) => {
                if let a = status { log "dropped: {a}" } else { log "ended" }
            }
            else => return
        }
    })
}
```

| Field | Type | Meaning |
|---|---|---|
| `socket` | The stream's handle kind | The handle the event is about |
| `data` | `bytes`, `text`, or a struct of the service | The event's data |
| `from` | A struct of the service | Where it came from, such as `net.Peer` |
| `status` | `svc.Answer?` | `none` for `LUMA_OK` |
| `value` | `u64` | The number the kind defines |
| `flags` | `u32` | The kind's flags |

When the reference says the function takes a null `fn`, the closure is optional: pass `none` for no events.

### Mod-wide events

An event kind that no request or stream names belongs to the whole service. Handle it with `on <service>.<kind>(e) { }`. An `on` handler for an event that goes to a request's or a stream's closure is a compile error.

### Handlers set by on

Some C functions set a handler for the whole mod, such as `time.on_frame`. In LumaScript you write `on frame { }` or `on step { }` instead. Calling the function by name is a compile error.

## Structs

Write a struct of a service as a literal, such as `audio.StreamDesc { rate: 48000, channels: 2, bus: 1, volume: 1.0 }`.

* `size` and `reserved` fields are not written. LumaScript sets `size` to the struct of the version the mod imports, and zeroes `reserved`.
* A field that is a pointer in C is a value in LumaScript:

  | C field | LumaScript field |
  |---|---|
  | `const char* x` | `text` |
  | `const void* x` (or `const uint8_t*`), then `uint64_t` size | `bytes`. The size field is not written. |
  | `const Luma<Name><T>* x`, then `uint32_t` count | `list<<service>.<T>>`. The count field is not written. |
  | `uint32_t x_count`, then `const float* xs` | `list<f32>` |
  | A `char` array | `text` |
  | `float x[2]` to `float x[4]` | `vec2` to `vec4` |

* A pointer field you leave out is null in C. Every other field must be written.
* A `char` array that a service fills is NUL-terminated UTF-8.

In C, set `size` to `sizeof` the struct before you pass it. A service reads and writes only `size` bytes, so a mod built against an earlier minor version keeps working.

## Permissions

A service that reaches outside the game needs a permission in `luma.json`. The service's name is its permission's name: `net.internet` needs the permission `net.internet`.

* A `use svc.NAME` of a permissioned service with no permission in `luma.json` is a check error, at the line of the `use`.
* A required permission the player refuses keeps the mod off.
* An optional service whose permission the player refuses is `none` in LumaScript, and a null slot in C.

For more information, see [About permissions](../../concepts/permissions.md).

## Mod log messages

Every service reports the mod's mistakes with these messages. `{mod}` is the mod's ID and `{where}` is `<service>.<function>`.

| Code | ID | Message |
|---|---|---|
| 100 | `Service.Thread` | `{mod} called {where} off the game thread; it did nothing.` |
| 101 | `Service.Argument` | `{mod}: {where} was given a wrong {argument}; it did nothing.` |
| 102 | `Service.Handle` | `{mod}: {where} was given a handle that is closed or not its own.` |
| 103 | `Service.StructSize` | `{mod}: {where} was given a {struct} of {found} bytes; it needs at least {expected}.` |
| 104 | `Service.Limit` | `{mod}: {where} reached its limit of {count}; it did nothing.` |

Each service page lists its own messages. For more information, see [Reading the mod log](../../how-tos/reading-the-mod-log.md).

## Further reading

* [About services](../../concepts/services.md)
* [Services in LumaScript](../lumascript/services.md)
* [Writing code in C](../../how-tos/writing-code-in-c.md)
