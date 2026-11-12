/* SPDX-License-Identifier: GPL-3.0-only */
/* LumaCore's save services (contract services-save): a mod's own values kept in the player's game file (`save`,
   written when the game saves, copied and erased with the file), its values across files (`save.global`, written at
   once), and the game's own file handed out or offered back through the game's screen (`save.game`, permission
   `save.game`). A key is 1 to 64 of a-z 0-9 _ - .; a value 0 to 64 KiB. */
#ifndef LUMA_SAVE_H
#define LUMA_SAVE_H

#include <luma_services.h>

#ifdef __cplusplus
extern "C" {
#endif

#define LUMA_SAVE_NAME "save"
#define LUMA_SAVE_MAJOR 1u
#define LUMA_SAVE_MINOR 0u
#define LUMA_SAVE_GLOBAL_NAME "save.global"
#define LUMA_SAVE_GLOBAL_MAJOR 1u
#define LUMA_SAVE_GLOBAL_MINOR 0u
#define LUMA_SAVE_GAME_NAME "save.game"
#define LUMA_SAVE_GAME_MAJOR 1u
#define LUMA_SAVE_GAME_MINOR 0u

#define LUMA_SAVE_KEY_MOST 64u          /* bytes of a key */
#define LUMA_SAVE_VALUE_MOST 65536u     /* bytes of a value */
#define LUMA_SAVE_MOD_MOST 262144u      /* one mod's entries in one file, keys and values with their lengths */
#define LUMA_SAVE_GLOBAL_MOST 1048576u  /* one mod's global file */

typedef enum LumaSaveCharacter {
    LUMA_SAVE_MARIO = 0,
    LUMA_SAVE_LUIGI = 1
} LumaSaveCharacter;

typedef struct LumaSaveService {
    LumaServiceTable base;
    /* 1.0. Game thread. The value of `key` for this mod in the file the game has loaded now: copies min(length, size)
       bytes into buf and returns the value's whole length (0 or more), also when it is over size, so a caller can ask
       again with room; buf may be null with size 0. LUMA_E_NOT_FOUND when not set and LUMA_E_ABSENT when no file is
       loaded (title, file select): answers, not reported. A key outside the rule, a null buf with size above 0:
       LUMA_E_ARGUMENT, reported. size: 0 to 65536 used. */
    int64_t (*get)(const LumaMod* mod, const char* key, void* buf, uint64_t size);
    /* 1.0. Game thread. Sets the value, written by the game's next save of the file (never by this call). LUMA_OK;
       LUMA_E_ABSENT when no file is loaded, not reported; past 64 KiB a value, 256 KiB the mod's entries or 1 MiB all
       mods' in the file: LUMA_E_LIMIT, reported; a key outside the rule, a null value with bytes above 0:
       LUMA_E_ARGUMENT, reported. bytes: 0 to 65536. */
    int32_t (*set)(const LumaMod* mod, const char* key, const void* value, uint64_t bytes);
    /* 1.0. Game thread. Removes the value. LUMA_OK; LUMA_E_NOT_FOUND when not set and LUMA_E_ABSENT when no file is
       loaded: answers, not reported; a key outside the rule: LUMA_E_ARGUMENT, reported. */
    int32_t (*remove)(const LumaMod* mod, const char* key);
    /* 1.0. Game thread. The loaded file: 1 to 6 into *number and its character into *character. LUMA_OK;
       LUMA_E_ABSENT when none, not reported; a null out: LUMA_E_ARGUMENT, reported. */
    int32_t (*file)(const LumaMod* mod, uint32_t* number, LumaSaveCharacter* character);
} LumaSaveService;

typedef struct LumaSaveGlobalService {
    LumaServiceTable base;
    /* 1.0. Game thread. As save's get, for this mod's values across files (its global file's read starts as the mod
       begins; a first call before it ends waits for it). LUMA_E_NOT_FOUND when not set, not reported; LUMA_E_ARGUMENT
       as save's get. The file could not be read: LUMA_E_IO from every call, and nothing is written over it; no memory
       for the values: LUMA_E_MEMORY, the same. A file that does not parse: one Error Save.GlobalBroken, the values
       start empty. size: 0 to 65536 used. */
    int64_t (*get)(const LumaMod* mod, const char* key, void* buf, uint64_t size);
    /* 1.0. Game thread. Sets the value; the mod's global file is written whole on the file thread at the end of the
       frame. LUMA_OK; past 64 KiB a value or 1 MiB the mod's file: LUMA_E_LIMIT, reported; LUMA_E_ARGUMENT as save's
       set; LUMA_E_IO, LUMA_E_MEMORY as get. bytes: 0 to 65536. */
    int32_t (*set)(const LumaMod* mod, const char* key, const void* value, uint64_t bytes);
    /* 1.0. Game thread. Removes the value, written as set writes. LUMA_OK; LUMA_E_NOT_FOUND when not set, not
       reported; a key outside the rule: LUMA_E_ARGUMENT, reported; LUMA_E_IO, LUMA_E_MEMORY as get. */
    int32_t (*remove)(const LumaMod* mod, const char* key);
} LumaSaveGlobalService;

/* Events of save.game (LumaEvent.kind); `handle` is the request's. */
typedef enum LumaSaveGameEvent {
    LUMA_SAVE_GAME_EVENT_EXPORT = 1, /* data: none; value: the file's bytes, taken with take in the event's call;
                                        status: LUMA_OK, LUMA_E_NOT_FOUND (that user file is empty) */
    LUMA_SAVE_GAME_EVENT_OFFER = 2   /* data: none; value: the user file replaced (1 to 6), else 0; status: LUMA_OK,
                                        LUMA_E_DENIED (no, or the screen closed), LUMA_E_FORMAT (not a game file of
                                        this disc's form: no screen), LUMA_E_IO (the old file could not be kept, or
                                        saves are off: nothing replaced), LUMA_E_MEMORY */
} LumaSaveGameEvent;

typedef struct LumaSaveGameService {
    LumaServiceTable base;
    /* 1.0. Game thread. Asks for user file `file` as the game would write it now (its game data for the character
       last played, then its config). LUMA_PENDING with the request in *out; an EXPORT event follows at the next
       frame's start, closed after its event; a Note Save.Exported in the mod log. file: 1 to 6, else
       LUMA_E_ARGUMENT; a null fn or out: LUMA_E_ARGUMENT; a 5th open request: LUMA_E_LIMIT; all reported. */
    int32_t (*export_file)(const LumaMod* mod, uint32_t file, LumaEventFn fn, void* user, LumaHandle* out);
    /* 1.0. Game thread, inside the EXPORT event's call for that handle, once. Copies min(value, size) bytes into buf
       and returns value (the file's whole length). Outside the call, another handle or a second time: LUMA_E_HANDLE,
       reported; a null buf with size above 0: LUMA_E_ARGUMENT, reported. */
    int64_t (*take)(const LumaMod* mod, LumaHandle handle, void* buf, uint64_t size);
    /* 1.0. Game thread. Offers the file at `path` (in the mod's data folder; an export_file's bytes) to replace a
       user file: the player picks one on Nebula's offer screen and confirms. LUMA_PENDING with the request in *out;
       an OFFER event follows, closed after its event; the old file's bytes are kept in the save folder first. Another
       offer open (any mod's), a screen waiting on the player, a time hold or a cutscene: LUMA_E_BUSY; a path naming
       no file: LUMA_E_NOT_FOUND; both answers, not reported; LUMA_E_IO: the file could not be read; LUMA_E_ABSENT: a
       host without the offer screen; a path outside the data folder, a null fn or out: LUMA_E_ARGUMENT, reported. */
    int32_t (*offer)(const LumaMod* mod, const char* path, LumaEventFn fn, void* user, LumaHandle* out);
    /* 1.0. Game thread. Ends a request: no event for it after this returns; an open question closes as no, nothing
       replaced (a REPLACE the player already gave still goes through). LUMA_OK; a handle that is not this mod's open
       request: LUMA_E_HANDLE, reported. */
    int32_t (*cancel)(const LumaMod* mod, LumaHandle handle);
} LumaSaveGameService;

#ifdef __cplusplus
}
#endif

#endif /* LUMA_SAVE_H */
