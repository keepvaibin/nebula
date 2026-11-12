/* SPDX-License-Identifier: GPL-3.0-only */
/* LumaCore's file services (contract services-fs): the mod's data folder and its own installed files (`fs`), and the
   Exchange folder the player and every mod with the permission share (`fs.exchange`, permission fs.exchange).

   A path is relative to its folder: parts joined by '/', 1 to 255 bytes in all, at most 16 parts, each 1 to 64 bytes
   of ASCII letters, digits, ' ', '-', '_' and '.', not starting or ending with ' ' or '.', not a name Windows keeps for
   a device (CON, PRN, AUX, NUL, COM1 to COM9, LPT1 to LPT9, alone or before a '.', in any case) and not ending ".tmp."
   and digits (the game's own half-written files). "" names the folder itself where a call says so. Letter case does
   not make two names: "Runs/A.bin" and "runs/a.bin" are one file on every system, and a write keeps the case the
   file already has. A path through a link or a junction is refused like a path outside the rule. */
#ifndef LUMA_FS_H
#define LUMA_FS_H

#include <luma_services.h>

#ifdef __cplusplus
extern "C" {
#endif

#define LUMA_FS_NAME "fs"
#define LUMA_FS_MAJOR 1u
#define LUMA_FS_MINOR 0u
#define LUMA_FS_EXCHANGE_NAME "fs.exchange"
#define LUMA_FS_EXCHANGE_MAJOR 1u
#define LUMA_FS_EXCHANGE_MINOR 0u

/* Events of fs and fs.exchange (LumaEvent.kind); `handle` is the request's. Every request is closed after its event. */
typedef enum LumaFsEvent {
    LUMA_FS_EVENT_READ = 1,   /* data: none; status OK: value = the file's bytes; take_read in the call */
    LUMA_FS_EVENT_WRITE = 2,  /* data: none; status OK: value = the bytes written */
    LUMA_FS_EVENT_REMOVE = 3, /* data: none; status OK: value = the files removed */
    LUMA_FS_EVENT_MOVE = 4,   /* data: none; status OK: value: 0 */
    LUMA_FS_EVENT_LIST = 5    /* data: none; status OK: value = the list's bytes; take_list in the call */
} LumaFsEvent;
/* Statuses an event may carry: LUMA_OK; LUMA_E_NOT_FOUND (no such file or folder); LUMA_E_EXISTS (move: the new
   path is taken; write: a folder has that path); LUMA_E_TOO_LARGE (a file over 64 MiB, a list over 1 MiB or 65536
   entries); LUMA_E_LIMIT (the folder's space is full: 256 MiB a mod's data folder, 1 GiB the Exchange folder; said
   once on the Mods page per mod and folder a session, Service.Limit); LUMA_E_DENIED (a path through a link or
   junction, or a file the system refuses to open); LUMA_E_IO (the system failed to read or write it).
   Every request below answers LUMA_E_ABSENT (not reported) where its folder cannot be: no data folder on this
   platform, or a mod with no installed folder for read_package and list_package. */

/* The most requests one mod has open in fs and fs.exchange together; one more: LUMA_E_LIMIT, reported. */
#define LUMA_FS_REQUESTS_MOST 32u
/* The largest file read or written, in bytes (64 MiB). */
#define LUMA_FS_FILE_MOST 67108864ull

typedef struct LumaFsService {
    LumaServiceTable base;
    /* 1.0. Game thread. Reads the file at `path` in the mod's data folder. LUMA_PENDING with the request in *out; a
       READ event follows (to fn), closed after its event. A path outside the rule (or ""), a null path, fn or out:
       LUMA_E_ARGUMENT; a 33rd open request: LUMA_E_LIMIT; all reported. */
    int32_t (*read)(const LumaMod* mod, const char* path, LumaEventFn fn, void* user, LumaHandle* out);
    /* 1.0. Game thread. As read, from the mod's own installed files (its folder in the Mods folder, read only). */
    int32_t (*read_package)(const LumaMod* mod, const char* path, LumaEventFn fn, void* user, LumaHandle* out);
    /* 1.0. Game thread, inside the READ event's call for that handle only, once. Copies min(value, size) bytes into buf
       and returns value (the whole file's bytes); buf may be null with size 0 to ask the length first, and asking so
       does not count as the take. Outside the call, another handle or a second take: LUMA_E_HANDLE, reported; a null
       buf with a size above 0: LUMA_E_ARGUMENT, reported. The request is closed after the call whether or not this
       was called. */
    int64_t (*take_read)(const LumaMod* mod, LumaHandle request, void* buf, uint64_t size);
    /* 1.0. Game thread. Writes `bytes` bytes (copied before this returns) as the whole file at `path` in the mod's
       data folder, making its folders; the file is replaced whole or not at all, and requests run in the order made,
       so a read after it gives these bytes. LUMA_PENDING; a WRITE event follows, closed after its event. A path
       outside the rule (or ""), a null path, fn or out, null data with bytes above 0: LUMA_E_ARGUMENT; over 64 MiB:
       LUMA_E_TOO_LARGE; a 33rd open request: LUMA_E_LIMIT; all reported. */
    int32_t (*write)(const LumaMod* mod, const char* path, const void* data, uint64_t bytes, LumaEventFn fn,
                     void* user, LumaHandle* out);
    /* 1.0. Game thread. Removes the file at `path` in the mod's data folder, or the folder with everything in it; ""
       empties the data folder. LUMA_PENDING; a REMOVE event follows, closed after its event. A path outside the rule,
       a null path, fn or out: LUMA_E_ARGUMENT; a 33rd open request: LUMA_E_LIMIT; all reported. */
    int32_t (*remove)(const LumaMod* mod, const char* path, LumaEventFn fn, void* user, LumaHandle* out);
    /* 1.0. Game thread. Gives the file or folder at `from` in the mod's data folder the path `to`, making to's
       folders; a taken `to` is not replaced (EXISTS). LUMA_PENDING; a MOVE event follows, closed after its event. A
       path outside the rule (or ""), `to` inside `from`, a null path, fn or out: LUMA_E_ARGUMENT; a 33rd open
       request: LUMA_E_LIMIT; all reported. */
    int32_t (*move)(const LumaMod* mod, const char* from, const char* to, LumaEventFn fn, void* user, LumaHandle* out);
    /* 1.0. Game thread. Lists the folder at `path` in the mod's data folder ("" the folder itself): one line a file or
       folder, its path from `path` ('/' between parts), a folder's ending in '/', each ending in '\n', sorted by
       bytes; recursive 1: everything under it, a folder before what it holds. Names outside the rule are left out.
       LUMA_PENDING; a LIST event follows, closed after its event. A path outside the rule, a null path, fn or out,
       recursive not 0 or 1: LUMA_E_ARGUMENT; a 33rd open request: LUMA_E_LIMIT; all reported. */
    int32_t (*list)(const LumaMod* mod, const char* path, uint32_t recursive, LumaEventFn fn, void* user,
                    LumaHandle* out);
    /* 1.0. Game thread. As list, in the mod's own installed files. */
    int32_t (*list_package)(const LumaMod* mod, const char* path, uint32_t recursive, LumaEventFn fn, void* user,
                            LumaHandle* out);
    /* 1.0. Game thread, inside the LIST event's call for that handle only, once. As take_read, for the list's text
       (no NUL is added). */
    int64_t (*take_list)(const LumaMod* mod, LumaHandle request, char* buf, uint64_t size);
    /* 1.0. Game thread. Ends a request: no event for it is given after this returns (registry 4.8). LUMA_OK; a
       handle that is not this mod's open request: LUMA_E_HANDLE, reported. A read or list not yet begun is not done;
       a write, remove or move still happens in its place in the order. */
    int32_t (*cancel)(const LumaMod* mod, LumaHandle request);
} LumaFsService;

/* fs.exchange: as fs's functions of the same names, in the Exchange folder. The player puts and takes files there;
   other mods with the permission do too, so a file may change or go between two requests. */
typedef struct LumaFsExchangeService {
    LumaServiceTable base;
    /* 1.0. As fs.read, in the Exchange folder. */
    int32_t (*read)(const LumaMod* mod, const char* path, LumaEventFn fn, void* user, LumaHandle* out);
    /* 1.0. Game thread, inside the READ event's call for that handle only, once. As fs.take_read. */
    int64_t (*take_read)(const LumaMod* mod, LumaHandle request, void* buf, uint64_t size);
    /* 1.0. As fs.write, in the Exchange folder. */
    int32_t (*write)(const LumaMod* mod, const char* path, const void* data, uint64_t bytes, LumaEventFn fn,
                     void* user, LumaHandle* out);
    /* 1.0. As fs.remove, in the Exchange folder; "" is LUMA_E_ARGUMENT (the folder is the player's). */
    int32_t (*remove)(const LumaMod* mod, const char* path, LumaEventFn fn, void* user, LumaHandle* out);
    /* 1.0. As fs.move, in the Exchange folder. */
    int32_t (*move)(const LumaMod* mod, const char* from, const char* to, LumaEventFn fn, void* user, LumaHandle* out);
    /* 1.0. As fs.list, in the Exchange folder. */
    int32_t (*list)(const LumaMod* mod, const char* path, uint32_t recursive, LumaEventFn fn, void* user,
                    LumaHandle* out);
    /* 1.0. Game thread, inside the LIST event's call for that handle only, once. As fs.take_list. */
    int64_t (*take_list)(const LumaMod* mod, LumaHandle request, char* buf, uint64_t size);
    /* 1.0. As fs.cancel. */
    int32_t (*cancel)(const LumaMod* mod, LumaHandle request);
} LumaFsExchangeService;

#ifdef __cplusplus
}
#endif

#endif
