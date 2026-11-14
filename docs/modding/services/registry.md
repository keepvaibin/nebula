# Services: the registry

## More rules

### Services-registry

Read with: lumalink/loader.md (services: kind 5, L505 `Name.Service`, L506, `service(mod, ...)`, `granted` in
`luma_load_mod`; `report`), format/luma.md (section 4 `permissions`), format/validator.md (L211, L505, L506),
language/language.md 12 and D4.14 (`use svc.<name>`, `optional`, events, `pending`), language/compiled-form.md 7.4
(tasks run from the frame handler).

### 1. What is decided here, in one list

1. One registry, in the game module's kernel, beside the loader (`native/petari/src/Game/System/LumaServices.cpp`).
   The kernel already holds what it needs: the mod records, the grants, the frame point, and (1.6) the guard.
2. Every service the game defines is in one catalog compiled into the module: name, major, minor, permission. A
   provider (the module itself, or the host) registers a table only for a catalogued service. The catalog is written
   into the self-description, so the validator and LumaScript check service imports without the game running (section
   9).
3. Every catalogued service is registered on every platform. What a platform or a device lacks is answered by the
   service's functions as absent (`LUMA_E_ABSENT`), never faked and never by a missing table. So an import of a
   service never fails because of the platform; it fails only because the game is too old (L505), the mod did not
   declare its permission (L506), or the player declined it (the mod is off before it starts, `Consent.OffDeclined`).
4. A permissioned service's name is its permission's name. One table, at most one permission, checked when the import
   binds (L506) and when the validator reads the import list. A service that needs two permissions for two kinds of
   use is two services with the same function layout (`net.local`, `net.internet`).
5. Every service function takes the calling mod first (`const LumaMod* mod`), runs on the game thread unless its
   contract says otherwise, and returns a status (section 4).
6. Anything that waits on the outside world is an event, delivered on the game thread at one point of the frame's
   start, per mod, in post order, through a queue that allocates nothing (section 6).
7. A provider starts no thread, opens no file, socket or device, and does no work per frame until a mod calls it
   (section 10).

### 2. Names and versions

- A service name is one or more parts joined by `.`, each part `[a-z][a-z0-9]*`; at most 31 bytes in all. Examples:
  `time`, `fs`, `fs.exchange`, `net.internet`, `save.game`.
- A service is found by `(name, major, minor)`. The registry gives the registered table of that name and that
  major whose minor is at least the asked minor.
  - Asked 1.2, the game has 1.3: the 1.3 table. It is the same table a 1.3 mod gets; the 1.2 mod reads only what 1.2
    defines (section 3's size rule). A minor never changes what an earlier minor's functions do: it only appends
    functions at the table's end, appends fields at the end of structs, and appends enum values and event kinds.
  - Asked 1.4, the game has 1.3: refused: L505 `kind` `P:Name.Service`, `found` the name, `sig` "1.4",
    or null for `LUMA_OPTIONAL` and for `core->service()`.
  - Asked 2.0, the game has only 1.x: refused the same way. A major is a table of its own: a game may carry 1.x and
    2.x side by side, and a 1.x mod keeps working on it. Every service of LumaCore v1 is major 1.
- Two registrations of one `(name, major)` are a host bug: `NEBULA_PANIC` naming the service.
- The version text of an import row of kind 5 is `<major>.<minor>`, each a decimal number
  from 0 to 65535 with no leading zero (`0` itself allowed), nothing else. Anything else is L505 with `sig` as found,
  in the validator and in the loader alike.
- Names LumaScript can resolve. In LumaScript a service is a dotted path (`use svc.net.internet`, a call
  `net.internet.connect(...)`, an event `on net.internet.received(...)`), resolved to the longest catalogued name
  that leaves exactly one part. So: no function, event kind or constant of a service `a` has the name of the next
  part of another catalogued service `a.<x>` (`fs` has no function `exchange`, since `fs.exchange` is a service).
  The catalog's check (section 9) refuses a catalog that breaks it.

### 3. The mod's header: `native/include/luma_services.h`

New, included by every service's header. Exact text:

```c
/* SPDX-License-Identifier: GPL-3.0-only */
/* LumaCore services: what every service's table shares (services-registry contract). A service is found through
   luma.h: an import row LUMA_SERVICE(slot, "fs", "1.0"), or core->service(mod, "fs", 1, 0) after start.

   Every service function takes the calling mod first, runs on the game thread unless its contract says otherwise, and
   returns a status: 0 or more is success, less than 0 an error. A mistake of the mod (LUMA_E_ARGUMENT, LUMA_E_THREAD,
   LUMA_E_HANDLE, LUMA_E_LIMIT, a struct too small) is also reported on the Mods page; an answer of the world (a file
   that is not there, a refused connection) is only returned. No call blocks: what waits on the outside world returns
   LUMA_PENDING and arrives later as a LumaEvent at the frame's start.

   Every struct a mod passes to a service, or a service fills for a mod, starts with `uint32_t size` set to sizeof as
   the mod was built: a service reads and writes only that much, so a mod built against an earlier minor keeps
   working. */
#ifndef LUMA_SERVICES_H
#define LUMA_SERVICES_H

#include <stdint.h>

#include "luma.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Every service table starts with this. `size` is the table's size in the game that runs: a later minor only adds at
   the end, so a mod built against an earlier minor reads only what it knows. */
typedef struct LumaServiceTable {
    uint32_t size;
    uint16_t major;
    uint16_t minor;
} LumaServiceTable;

/* What a service function returns. Values are only ever added. */
enum {
    LUMA_OK = 0,
    LUMA_PENDING = 1,       /* accepted: the answer comes as an event */
    LUMA_E_ARGUMENT = -1,   /* an argument the function does not take: the mod's mistake, reported */
    LUMA_E_THREAD = -2,     /* called off the game thread where the function does not allow it: reported */
    LUMA_E_HANDLE = -3,     /* a handle that is closed, not this mod's, or never was: reported */
    LUMA_E_LIMIT = -4,      /* a limit the service's contract states was reached: reported */
    LUMA_E_DENIED = -5,     /* outside what the player granted (a host the manifest does not name) */
    LUMA_E_ABSENT = -6,     /* this platform or this device has no such thing */
    LUMA_E_NOT_FOUND = -7,  /* no such file, host, device, name */
    LUMA_E_EXISTS = -8,     /* it exists already */
    LUMA_E_IO = -9,         /* the system failed to read, write, open or send */
    LUMA_E_TIMEOUT = -10,   /* no answer within the time the call or the contract gives */
    LUMA_E_CLOSED = -11,    /* the other side closed, or the thing was closed meanwhile */
    LUMA_E_CANCELLED = -12, /* the mod cancelled it, or the game is taking the mod out */
    LUMA_E_MEMORY = -13,    /* no memory for it */
    LUMA_E_BUSY = -14,      /* not now: a queue is full or the device is in use; try again on a later frame */
    LUMA_E_FORMAT = -15,    /* the data is not of the form the function reads */
    LUMA_E_TOO_LARGE = -16, /* more than the function or the contract allows at once */
    LUMA_E_UNREACHABLE = -17 /* the network cannot reach the other side */
};

/* A handle to something a service made for the mod (a file, a socket, a sound). Never 0; never given twice in one
   session, so a closed handle stays recognisably closed. */
typedef uint64_t LumaHandle;

/* An answer from the outside world, delivered on the game thread at the frame's start. Everything it points at is
   valid only during the call: copy what you keep. */
typedef struct LumaEvent {
    uint32_t size;     /* sizeof(LumaEvent) in the game that runs */
    uint32_t kind;     /* the service's own event kinds (its contract) */
    int32_t status;    /* LUMA_OK or a LUMA_E_* */
    uint32_t flags;    /* the kind's */
    LumaHandle handle; /* what it is about: the handle or the request id the call gave */
    const void* data;  /* the kind's bytes, or null */
    uint64_t data_size;
    uint64_t value; /* a number the kind defines */
} LumaEvent;

/* The mod's function for events, given with the call that asks; `user` comes back as it was given. */
typedef void (*LumaEventFn)(void* user, const LumaEvent* event);

/* The most event deliveries (nodes) one mod is given in one frame; the rest wait for the next frames, in order. */
#define LUMA_EVENTS_PER_FRAME 256u

#ifdef __cplusplus
}
#endif

#endif
```

One form for every service header, so LumaLink reads each table, its functions, structs and event kinds
from the header itself (rule 4) and LumaScript can call it; a header that breaks the form fails the crates' mirror
test:
- the table is `typedef struct Luma<Name>Service { LumaServiceTable base; ... } Luma<Name>Service;`, `<Name>` the
  dotted name in CamelCase (`net.internet` -> `LumaNetInternetService`);
- each member is a function pointer whose parameters all have names, the first `const LumaMod* mod`;
- a non-const pointer parameter after `mod` to a number or a struct is an out: written by the service, never read
  (LumaScript returns it); a buffer is a `void*`/`char*` with its size in the next parameter;
- a function callable from any thread has the comment `any thread` on its member, exactly that phrase;
- event kinds are enum values `LUMA_<NAME>_EVENT_<KIND>` (`LUMA_NET_INTERNET_EVENT_RECEIVED`), each with a comment
  naming its `data` type and what `value` means;
- each struct starts with `uint32_t size`;
- a `char` array field of a struct a service fills is NUL-terminated UTF-8, cut at a character's end (LumaScript
  reads it as `text`);
- every set of values is a named enum, `typedef enum Luma<Name><Thing> {...} Luma<Name><Thing>;`, and the fields and
  parameters that take one use that type (4 bytes as C enums on MSVC, GCC and Clang); a provider reads such a
  parameter's 4 bytes as they came and reports a value outside the enum;
- a thing a table opens and only that table's functions take has its handle type, `typedef LumaHandle
  Luma<Name><Thing>;` (`LumaTimeTimer`); a request's handle stays a bare `LumaHandle`;
- each member's comment names every status it returns, LUMA_OK and LUMA_PENDING included; the statuses every member
  shares are not repeated: LUMA_E_ARGUMENT for a null or unknown mod, LUMA_E_CANCELLED for a mod being taken out,
  LUMA_E_THREAD for a game-thread member called off it (section 4);
- fixed phrases in member comments, as `any thread` is: a request says `a <KIND> event follows`
  (`an` before a vowel) and, when its one event ends it, `closed after its event`; a take function says `inside the
  <KIND> event's call`; a handler setter says `set by on <x>` (`on frame`); a range a literal can be checked against
  is `<param>: <a> to <b>` (`rate: 1 to 16000`);
- a colour is `LumaColor` (luma_services.h, `uint32_t` 0xRRGGBBAA).

The size rule for structs. Every struct a mod passes to a service, and every struct a service fills for a mod, starts
with `uint32_t size`, which the mod sets to `sizeof` as it was built:
- a struct filled out: the service writes whole fields only, and none past `size`.
`LumaEvent.size` is the game's; a mod built against an earlier minor reads only the fields it knows.

1. The first parameter is the mod, `const LumaMod* mod`, as `LumaCore.start` gave it. A null or unknown mod:
   the call returns `LUMA_E_ARGUMENT` and logs one line per `<service>.<function>`, repeats counted and said again at
   10, 100, 1000 (as the kernel's reports are; it has no mod to report to), through the kernel's `unknown_mod`. A mod that is being taken out
   (after its `stop`) gets `LUMA_E_CANCELLED` and nothing is done. "Unknown" is found by looking the pointer's
   address up among the registry's live records, comparing addresses only: nothing is read through a pointer the
   registry does not hold. It is the lookup the record needs anyway, so it costs nothing more.
2. Threads. The game thread only, unless the function's contract says "any thread". A call from another thread
   returns `LUMA_E_THREAD` and is reported (`Service.Thread`). The check is one thread-local read.
3. A mod's mistakes are reported, the world's answers are returned. `LUMA_E_ARGUMENT`, `LUMA_E_THREAD`,
   `LUMA_E_HANDLE`, `LUMA_E_LIMIT` and a struct-size error are mistakes in the mod: each is returned and reported
   through the kernel's report (loader.md `report`: kept for the Mods page and the mod log in the player's language, said
   once in the log, repeats counted), with the service, the function and what was wrong. Every other error is an
   answer from the world (a file that is not there, a refused connection, an absent device): returned, and named by
   the service's event or result, never reported by itself, since the mod is expected to handle it. LumaScript's
   generated code reports an error result its script does not handle (language/compiled-form.md), so no error is silent.
4. No call blocks. A function returns within the frame. Work that waits (a file read, a connection, a device)
   returns `LUMA_PENDING` and a request id or handle, and its answer is an event. A contract may allow a small
   synchronous form only where it states a size cap.
5. Handles are this mod's only. Another mod's handle, or a closed one, is `LUMA_E_HANDLE`, reported. Everything a
   mod holds (handles, registrations) is closed by the service when the mod is taken out (section 8).
6. Text is UTF-8 with a length or a terminating 0 as the function says; a service never keeps a pointer the mod
   gave beyond the call (it copies).
7. Memory a service keeps for a mod (buffers, queues) is counted against the mod: allocated through the kernel's
   per-mod memory (`LumaCore.alloc` on the mod's behalf), shown on the Mods page, freed when the mod ends.
8. Cancel (must). Every function that returns `LUMA_PENDING` with a request id has a `cancel(mod, id)` in
   its table. Once `cancel` returns, `fn` is never called for that id: an answer already queued is dropped at
   delivery. A stream subscription is ended the
   same way by its unsubscribe, and a handle's `close` gives the same promise for every event about that handle. So
   a LumaScript task that gives up a `pending<T>` (its state left, its mod stopping) frees the slot `user` points at
   with no call into freed memory a frame later.

### 5.1 The closed list (v1)

| Permission | Owner | Short name (row `Permission.<name>`) | What the player reads (row `Permission.<name>.Text`) |
|---|---|---|---|
| `native` | luma-format, not a service | Native libraries | Runs libraries of its own with full access to this device. |
| `fs.exchange` | `fs` | Exchange folder | Reads and writes files in the Exchange folder, which you and other mods can open. |
| `net.local` | `net` | Local network | Connects to devices on your network and to programs on this device. |
| `net.internet` | `net` | Internet | Connects to the internet: {hosts}. |
| `input.inject` | `input` | Controls | Presses buttons and moves the controls as if it were you. |
| `input.devices` | `input` | Other devices | Uses Bluetooth, USB and MIDI devices connected to this device. |
| `audio.microphone` | `audio` | Microphone | Hears your microphone. |
| `capture.camera` | `capture` | Camera | Sees your camera. |
| `system.clipboard` | `system` | Clipboard | Reads and writes the clipboard. |
| `system.links` | `system` | Web pages | Opens web pages in your browser. |
| `save.game` | `save` | Game saves | Reads your save files and may offer one to replace; you confirm each replacement. |

- `net.internet`'s `{hosts}` is the manifest's `hosts`, joined by ", "; `*` alone shows the row
  `Permission.net.internet.Any` ("any address"). A host is a name (`api.example.org`), a name under a domain
  (`*.example.org`, the domain itself not included), or `*`. Addresses typed by the player at run time (a co-op
  partner's) need `*`.
- What a host is: a name (`api.example.org`), a name under a domain (`*.example.org`, the domain itself not included),
  an IPv4 address in dotted digits (a mod's own server may have no name), or `*` alone. No port (a host grants every
  port); no IPv6 literal (`*` covers them). Names are lowercased before the consent digest (consent 1.2) and before
  `net`'s check, so `API.example.org` is not a second host; `net` (2.7) compares exactly so.
- `native` is not a service: it is checked by the validator (L509) and by the host before it loads a mod's extra
  libraries, never at bind. A mod that declares it and is not granted it does not start (its extra libraries load
  before its entry library).
- `hosts` holds at most 32 entries; more is L211.
- No permission is needed for: a mod's own folders (`fs`), the game's clock and state (`time`), facts about the system
  (`system`), other mods (`mods`), a mod's own save values (`save`), the game's controls as read (`input`), playing
  sound and reading the game's own mix (`audio`), drawing and reading the game's own picture (`draw`), the game's
  layouts (`ui`), the game's files and the mod's own (`assets`). Reading the game's own picture or mix shows nothing
  outside the game, so it needs none (the prompt's `capture.screen` is therefore not a permission: there is no capture
  of anything but the game).

### 5.2 Where a permission is declared, granted and checked

- Declared in `luma.json` `permissions` (luma-format 4): `name` from the list above, `why` one line; `hosts` with
  `net.internet` only (required there). The validator (L211) takes the names of this list; the old `net` is
  refused with L211 naming the list; `hosts` entries as section 5.1 says.
- Checked statically by the validator: an import row of kind 5 whose service is permissioned needs that permission in
  `permissions` (L506), read from the catalog in the self-description (section 9). A permission declared and never
  imported is the Note L218: `{mod} asks for the "{permission}" permission, but no code of it imports a service that
  needs it.` (for `native`: declared with no `code.libraries` entry). It stays a Note, never an Error: a C library may
  ask `core->service()` by name at run time, which has no row. LumaScript's compiler gives the same rule at `use
  svc.<name>` (language 12).
- Granted by the player on the consent screen. The host passes the granted names to `luma_load_mod` (`granted`).
- Checked at bind: L506 (already in 1.1) for a permission not declared. A required permission the player declined
  never reaches the bind: the mod is off before the start check (start-check 1a, `Consent.OffDeclined`). Checked at
  run time by `core->service()` and, for `net.internet`'s hosts, by `net` at each connection.

### 6.1 For mods

A function that answers later takes `LumaEventFn fn, void* user` and returns `LUMA_PENDING` with a request id or a
handle. A service that reports a stream of happenings (data arriving on a socket, a device connected) takes the pair
when the mod subscribes. The answer arrives as one call `fn(user, &event)`:
- on the game thread, at the frame's start: in `GameSystem::update`, right after `luma::frame()`;
- mods in list order (the Mods list, top first, the loader's `order`); each mod's nodes in the order they were posted;
  at most `LUMA_EVENTS_PER_FRAME` (256) nodes delivered per mod per frame, the rest kept, in order, ahead of the next
  frame's.
- after every mod's events, `time`'s frame handlers (`on frame`, lumart's tasks) run, in list order, so a task's `wait
  <pending>` sees an answer in the frame it arrived. A task resumed by an answer runs in that frame's `time` pass,
  after every mod's events, so a script never sees a half-delivered frame;
- an event's `data` is valid only during the call; the mod copies what it keeps;
- a call into the mod goes through the guard (when it exists): a fault in `fn` is the guard's. The guard's entry in
  the mod log names what was running: its `file` is "<service> event <kind>" (the service's name and the event's kind
  number), so a player sees which part of the mod failed. `LumaServiceKernel.call` gives the guard the service and
  kind with the call.
LumaScript makes a `pending<T>` from the request and fills it in its `fn` (language D4.14), and an `on <svc>.<event>`
handler from a subscription.

### 6.2 For providers: the queue

A provider owns one event node per thing that can have an answer waiting (a socket, a device). The
kernel's queue links nodes; it allocates nothing.

```c
typedef struct LumaEventNode {
    struct LumaEventNode* next; /* the kernel's */
    uint32_t state;             /* the kernel's: 0 idle, 1 queued */
    uint32_t reserved;
    const LumaMod* mod;         /* the provider's: whose event it is */
    void (*deliver)(struct LumaEventNode* node); /* game thread, at the frame's start: builds the LumaEvent(s) and
                                                    calls the mod through LumaServiceKernel.call */
    void (*drop)(struct LumaEventNode* node);    /* game thread: the mod is gone; the node is not delivered */
} LumaEventNode;
```

- `post(node)`, from any thread: one compare-and-swap of `state` from 0 to 1 (acquire-release). Only the thread that
  wins it pushes the node, so two threads posting one node at once queue it once. If the node is idle it is
  queued at the end of its mod's queue; if it is already queued nothing changes (its `deliver` will see everything that arrived meanwhile: a socket's node is posted once
  however many packets arrive before the frame). Returns `LUMA_OK`, or `LUMA_E_CANCELLED` when the mod is gone (the
  node is not queued; the provider keeps it and frees it as section 8 says).
- At delivery, for each node in order, exactly: the node is unlinked from the kernel's list (`next` read and
  cleared), then `state` is set to 0 (release), then `deliver` is called. So a post during `deliver` queues it again
  for the next frame (no answer is lost), and a node is never in two lists.
- A node the kernel holds over the frame's 256 (or taken from the stack and not yet delivered) keeps `state` 1: a
  post meanwhile changes nothing, and its `deliver` sees what arrived.
- The queue per mod is a lock-free stack (push by compare-and-swap); delivery takes the whole stack with one
  exchange and reverses it, which is post order; what is over the frame's 256 is kept, in order, ahead of the next
  frame's.
- One kernel-wide flag says whether any node was posted since the last delivery. With nothing posted, the frame's
  start costs one atomic load.

### 7. The provider's side: `native/include/luma_service_provider.h`

Between the host and the game module, and between the module's own services and its kernel; not for mods. Exact text:

```c
/* SPDX-License-Identifier: GPL-3.0-only */
/* Providing a LumaCore service (docs/modding/services/registry.md): between the game module's kernel and the
   services it or the host provides. Not for mods. */
#ifndef LUMA_SERVICE_PROVIDER_H
#define LUMA_SERVICE_PROVIDER_H

#include <stdint.h>

#include "luma.h"
#include "luma_services.h"

#ifdef __cplusplus
extern "C" {
#endif

/* One thing that can have an answer waiting for a mod (a request, a socket, a device), owned by its provider. The
   kernel's queue links nodes and allocates nothing; a node is queued at most once at a time. */
typedef struct LumaEventNode {
    struct LumaEventNode* next; /* the kernel's */
    uint32_t state;             /* the kernel's: 0 idle, 1 queued */
    uint32_t reserved;
    const LumaMod* mod; /* the provider's: whose event it is */
    /* Game thread, at the frame's start, the node already idle again: builds the LumaEvent(s) and gives them to the
       mod through LumaServiceKernel.call. A post from inside queues the node for the next frame. */
    void (*deliver)(struct LumaEventNode* node);
    /* Game thread: the mod is gone and the node will not be delivered; the node is idle again and the provider's. */
    void (*drop)(struct LumaEventNode* node);
} LumaEventNode;

/* One row a service reports through: its code in the service's block (services-registry 11), the row id in
   NebulaText.tsv, and the names of its arguments in order ({mod} the kernel fills; unused names null). */
typedef struct LumaServiceRow {
    uint32_t code;
    const char* id;
    const char* names[4];
} LumaServiceRow;

/* What the kernel gives a provider. Any thread unless said. */
typedef struct LumaServiceKernel {
    uint32_t size;
    /* The mod's id ("gold-flower"), its place in the Mods list (top 0), and 1 when the player granted it `permission`.
       A mod the kernel does not know (or after its end): null, -1, 0. The id is valid until the mod's mod_end. */
    const char* (*mod_id)(const LumaMod* mod);
    int32_t (*mod_order)(const LumaMod* mod);
    int32_t (*granted)(const LumaMod* mod, const char* permission);
    /* 1: known and not being taken out; 0: unknown, or after its stop. */
    int32_t (*mod_live)(const LumaMod* mod);
    /* 1 on the game thread. */
    int32_t (*game_thread)(void);
    /* A mistake of the mod: the row `code` (the provider's rows, or the registry's 100 to 119) with `args`; `where` is
       "<service>.<function>". Kept and said as LumaCore.report does: once in the log, kept for the Mods page, repeats
       counted. */
    void (*report)(const LumaMod* mod, const char* where, uint32_t code, const char* const* args, uint32_t arg_count);
    /* Queues `node` for its mod (from any thread). LUMA_OK: queued, or queued already; LUMA_E_CANCELLED: the mod is
       gone or unknown, nothing queued (the node stays the provider's); LUMA_E_ARGUMENT: a null node, mod or deliver. */
    int32_t (*post)(LumaEventNode* node);
    /* Game thread, from a node's deliver: calls fn(user, event) for the mod (through the guard, which names
       "<service> event <kind>" if fn faults). LUMA_OK: called; LUMA_E_CANCELLED: the mod is gone (not called);
       LUMA_E_THREAD, LUMA_E_ARGUMENT: a provider bug, logged. */
    int32_t (*call)(const LumaMod* mod, const char* service, LumaEventFn fn, void* user, const LumaEvent* event);
    /* Memory counted against the mod (LumaCore.alloc and free on its behalf): shown on the Mods page, freed when the mod
       ends if the provider has not freed it. */
    void* (*alloc)(const LumaMod* mod, uint64_t size, uint64_t align);
    void (*free)(const LumaMod* mod, void* p);
    /* A service function given a null mod, or one mod_live does not know: the function returns LUMA_E_ARGUMENT and
       calls this with "<service>.<function>". One line in the game's log per place, repeats counted and said again at
       10, 100, 1000 (there is no mod to report to). */
    void (*unknown_mod)(const char* where);
} LumaServiceKernel;

/* A service's table and what the kernel tells its provider. */
typedef struct LumaServiceProvider {
    uint32_t size;
    const char* name;              /* a catalogued name, or one starting with "test." (a host extension's) */
    const LumaServiceTable* table; /* major and minor are read from it; they must be the catalog's */
    const char* permission;        /* a "test." service's own (null: none); a catalogued service's comes from the catalog */
    const LumaServiceRow* rows;    /* its report rows, in its code block */
    uint32_t row_count;
    void* user;
    /* Game thread, before the mod's begin and start: the mod may call the service from then on. May be null. */
    void (*mod_begin)(void* user, const LumaMod* mod);
    /* Game thread, after the mod's stop (or its refused start): cancel what it asked, close what it holds, stop
       posting for it. When this returns no thread of the provider posts for the mod or asks the kernel about it again.
       May be null. */
    void (*mod_gone)(void* user, const LumaMod* mod);
    /* Game thread, after the mod's grace and its finish, before its memory and library go: free what the provider
       kept for it. May be null. */
    void (*mod_end)(void* user, const LumaMod* mod);
    /* Game thread, once, when luma_provide_service accepts the provider: the kernel it uses from then on (a provider
       outside the game module, a host extension's, has no other way to it). May be null. */
    void (*registered)(void* user, const LumaServiceKernel* kernel);
} LumaServiceProvider;

/* The game module's exports for the host, found by name as luma_load_mod is.
   luma_services_open: game thread, before the first mod loads and before luma_provide_service: checks the catalog,
   registers the module's own services (once; a second call only answers) and gives the kernel for the host's
   providers.
   luma_provide_service: game thread, before the first mod loads. 0: registered. A name neither catalogued nor
   "test.", a second registration of one name and major, a table whose major or minor differs from the catalog's, a
   call after the first mod loaded, or off the game thread: the game stops (NEBULA_PANIC) naming it. The provider and
   everything it points at stay valid for the process's life. */
typedef const LumaServiceKernel* (*LumaServicesOpen)(void);
typedef int32_t (*LumaProvideService)(const LumaServiceProvider* provider);

#ifdef __cplusplus
}
#endif

#endif
```

Module exports (in `LumaServices.cpp`): `NEBULA_GAME_API const LumaServiceKernel* luma_services_open(void)` and
`NEBULA_GAME_API int32_t luma_provide_service(const LumaServiceProvider*)`. The host calls both from `start_plugins`
(mod_plugins.cpp, through `galaxy::luma_services::provide_all`, section 13), next to `luma_set_service_finder`, before
the first `luma_load_mod`: open first (the module's own services register inside it), then the host's providers,
then the extensions'.

### 8. A mod's life, for services

The loader tells the registry at three moments, all on the game thread:

| Moment | Where in LumaLoader.cpp | The registry |
|---|---|---|
| begin | `loadMod`, pass 3, after the slots are filled and before `begin`/`start` | makes the mod's record (id, order, granted copied); every provider's `mod_begin` |
| gone | `unloadMod` after `stop` and `tellDependentGone`; the start-refused path after `tellDependentGone`; both before `endMod` | marks the mod gone (`mod_live` 0, every call `LUMA_E_CANCELLED`); every provider's `mod_gone`, in reverse registration order; then every node still queued for the mod is given to its `drop` |
| end | `finishMod`, after `finish` and before `freeAll` and the library's close | takes the mod's queue once more (a post that raced `mod_gone`: given to `drop`); every provider's `mod_end`; frees the record |

A mod refused before pass 3 has no record: neither gone nor end does anything for it. With these, no event is
delivered to a mod after its `stop`, no provider thread posts for it after `mod_gone`, and nothing a provider holds
for it outlives its memory.

The registry's records are a fixed array of 2 x LUMA_MAX_MODS slots (a mod taken out keeps its slot through its
grace while the mod that replaces it begins). A modBegin that finds no free slot stops the game (NEBULA_PANIC naming the mod
and the slot count): the loader's limit of LUMA_MAX_MODS mods on, and the host taking a mod out only after its
dependents, make it a host bug. modBegin writes a slot's record whole, then its mod address (release);
modEnd clears the address (release) before it frees anything. Every kernel function a provider may call off the
game thread finds its mod by acquire loads of the addresses, comparing addresses only, and reads the record only
while the mod is live, which mod_gone's promise guarantees for the provider's own calls. A pointer kept past mod_end
is a provider bug: its address may be a later mod's.

### 9. The catalog

`LumaServices.cpp` holds the catalog, one row per service the game defines: name, major, minor, permission (or
none), the contract that defines it. The catalog is the only list (rule 4, "generated, not listed", is kept by
making the self-description and the validator read this one list rather than a copy).

- The self-description gains `"services": [{"name": "fs", "major": 1, "minor": 0, "permission": null}, ...]` , exactly
  so:
  - one row per catalogued service, sorted by name in byte order; `permission` null or the service's own name;
  - The key is always present, empty while the catalog is. Absent means an older game: kind 5 stays
    `Missing.Services`, not checked. Present and empty means no service, so every non-optional kind 5 import is L505;
  - The validator's L505 for kind 5: no row of that name and major, or a row whose minor is lower than asked; L506:
    the row's permission is not in `permissions`;
  - LumaScript's `use svc.<name>` and consent's `required` read the same rows.
- The catalog's own check (a test over `cCatalog` in the module's unit tests, and the generator of the
  self-description) refuses a catalog with two rows of one name and major, a name that breaks section 2's grammar, a
  permissioned row whose permission is not its own name, or a name that breaks section 2's resolution rule against
  another row's table (the header's member names, read from the header form of section 3).
- At start, after the host's registrations and before the first mod loads, a catalogued service with no registered
  table is a build fault: `NEBULA_PANIC` naming it (section 1, item 3: every service exists on every platform).
- `services-time.md` adds `time` and so on.

### 10. No cost with no mod

- Registration at start: a few table writes, no thread, no file, no socket, no device opened.
- The frame's start with nothing posted: one atomic load.
- No call: nothing. A provider opens its thread, socket, device or file at the first call that needs it, and closes
  them when the last mod using them is gone.

### 11. Messages

| Codes | Owner |
|---|---|
| 100-119 | the registry |
| 120-139 `time`, 140-159 `system`, 160-179 `mods`, 180-199 `save` | 2.2 |
| 200-249 `fs` | 2.3 |
| 250-299 `net` | 2.7 |
| 300-349 `input` | 2.5 |
| 350-399 `audio`, 400-419 `capture` | 2.6 |
| 420-469 `assets` | 2.8 |
| 470-569 `draw`, 570-619 `ui` | 2.4 |
| 620-699 | content at run time (`effects`; 3.12, 3.5) |
| 700-899 | free, given by revision |

| Code | Row | English | Args |
|---|---|---|---|
| 100 | `Service.Thread` | `{mod} called {where} off the game thread; it did nothing.` | where |
| 101 | `Service.Argument` | `{mod}: {where} was given a wrong {argument}; it did nothing.` | where, argument |
| 102 | `Service.Handle` | `{mod}: {where} was given a handle that is closed or not its own.` | where |
| 103 | `Service.StructSize` | `{mod}: {where} was given a {struct} of {found} bytes; it needs at least {expected}.` | where, struct, found, expected |
| 104 | `Service.Limit` | `{mod}: {where} reached its limit of {count}; it did nothing.` | where, count |

`{where}` is `<service>.<function>` as the contract names it; it is a name, not a translated word. A row has at most
four arguments (`LumaServiceRow.names[4]`; row 103 uses all four).

L506 is the permission not declared (items 3, 5.2, test 2105).

Filling a row: `report(mod, where, code, args, count)`'s `where` fills
`{where}`; `args[0..count)` fill the row's other names in their order, `{where}` skipped (row 101's `{argument}` is
`args[0]`, row 103's `{struct}`, `{found}`, `{expected}` are `args[0..2]`). `{mod}` is the mod's id. The kernel's
record, the mod log's CODE entries (names and values as the record gives them, `where` first by the name `where`)
and the self-description's row list read rows so.

The provider gives the kernel its rows (`LumaServiceProvider.rows`); the kernel's report path finds a code's row
there. (section 12, d).

### 12. Where the kernel calls the registry

`LumaServices.hpp` declares `modBegin`, `modGone`, `modEnd`, `rowOf`, `find`, `frameStart` and `provideOwn` in
`namespace luma::services`; the loader and the game's frame call them at these points:

(a) A mod's load (`LumaLoader.cpp`, `loadMod`, pass 3), just before its `begin` runs:
`modBegin(mod, name, order, granted, grantedCount)`.
(b) A mod whose start is refused, and a mod taken out (`unloadMod`), after its dependents are told it is gone and
before it ends: `modGone(mod)`.
(c) A mod's finish (`finishMod`), after its `finish()` and before its memory is freed: `modEnd(mod)`.
(d) A report's row (`rowOf`): a code from 100 to 899 is the services' and its row comes from
`luma::services::rowOf(code, id, size, names)`, before the kernel's own codes from 900. A row has up to 4 names, and
the Mods page renders through the same lookup.
(e) A service import (`findService`): the registry is asked first (`luma::services::find`, which also gives the
permission); the host's finder after it, for the harness's extension services, which need no permission.
(f) The frame (`GameSystem::update`), right after `luma::frame()`: `frameStart()`, which delivers the services'
events, then time's frame handlers; one load when nothing waits.

### 13. The host's side

- `mod_plugins.cpp` `start_plugins`: after `luma_set_service_finder`, one call
  `galaxy::luma_services::provide_all(find_symbol, log, own)` (`runtime/include/galaxy/luma_services_host.h`, in the
  `galaxy_host_extension` library) registers each host provider and then each extension's `test.` provider through
  `luma_provide_service` (found by name like `luma_load_mod`), once a process, before the loop that loads mods. With
  no mod in the list `start_plugins` is not called (host_main `svc_mods_start` returns first), so nothing is
  registered and nothing costs. Host providers live in
  `runtime/src/luma_services/` (one file per service, platform parts behind `platform/` and the existing runtime
  layers: never `#ifdef _WIN32` in shared service code outside the platform layer).
- The granted list `luma_load_mod` takes comes from the consent store, not `nullptr, 0u` as
  today. Until 6.4 is built, none is granted, as today.
- The harness's own service (`galaxy::extensions::service`) keeps working: the registry does not know its name, so
  `findService` falls through to it.

### 14. How it is tested

Test services. The registry's own cases need services whose versions and permission a test controls. A host
extension (the harness's host half, `--extension`) may provide services whose name starts with `test.`: never in
the catalog, permission none or a name starting with `test.`. `nebula_host_extension.h` gives them
at the end of `NebulaHostExtension`: `const LumaServiceProvider* const* luma_providers; uint32_t
luma_provider_count;`, registered by the host through `luma_provide_service` before the first mod loads, exactly as
its own (a name not starting with `test.` from an extension: the extension is refused at attach, its error naming the extension and the service, and the run does not start). The harness's host half
provides `test.svc` 1.3 (no permission) and `test.secret` 1.0 (permission `test.secret`), and the mod entry's own `permissions` key (harness 3.1.1: `"allow"`, `"deny"` or per name) decides, and the
runner writes each declared permission's consent line into the run's mod settings before the start, as the
player's answer would be, so the test takes the real granted path. A
`test.` permission is declared in the test mod's luma.json like any other (validator L211 accepts `test.` names,
With no extension, nothing named `test.` exists. Each case is a conformance mod (C) and its
facts:

| # | Case | Expected |
|---|---|---|
| 2101 | import `test.svc` "1.3" | binds; table `size`, `major` 1, `minor` 3 as the provider set |
| 2102 | import `test.svc` "1.1" (older minor) | binds to the same table (pointer equal to 2101's) |
| 2103 | import `test.svc` "1.4" (newer minor) | refused: L505 `Name.Service`, `found` test.svc, `sig` 1.4; the Mods page's text in two languages |
| 2104 | import `test.svc` "2.0" (other major) | refused: L505, `sig` 2.0 |
| 2105 | import `test.secret` "1.0", required, declined | off before it starts: `Consent.OffDeclined` {permission} test.secret; no L506 |
| 2106 | the same, granted (the harness passes the grant) | binds |
| 2107 | `LUMA_OPTIONAL` import of "1.4" | starts; slot null |
| 2108 | `core->service(mod, "test.secret", 1, 0)` ungranted | null; one `Hook.ServiceNotGranted` report |
| 2109 | a test function called off the game thread | `LUMA_E_THREAD`; one `Service.Thread` report; a second call counted, not said again |
| 2110 | events: the test provider posts 300 events for one mod from another thread in one frame | 256 delivered in that frame and 44 the next, in post order; `on frame` after them |
| 2111 | a node posted twice before the frame | delivered once |
| 2112 | a post during `deliver` | delivered the next frame |
| 2113 | two mods, events for both | the higher mod's first |
| 2114 | a mod taken out with 10 events queued (needs 1.11's stop step) | none delivered; `drop` called 10 times; `mod_gone` before `mod_end`; no call after its stop |
| 2115 | no mod | the frame's start reads one flag (by reading the code path) and the interleaved run shows no cost |
| 2116 | a struct with `size` 4 where the first minor's is 16 | `LUMA_E_ARGUMENT`, `Service.StructSize` with 4 and 16 |
| 2117 | the self-description's `services` | equals the catalog, row for row |
| 2118 | the validator on a mod importing the first catalogued permissioned service without declaring it | L506 in LumaLink check and at import, the same text |
