/* SPDX-License-Identifier: GPL-3.0-only */
/* LumaCore's mods services (contract services-mods): the session's list of mods (`mods`), numbers in the game's
   numbered spaces by `<mod id>/<name>` (`mods.ids`), and a request to install a mod through the import screen
   (`mods.install`). None needs a permission. Calling another mod's functions is the loader's (import kind 6). */
#ifndef LUMA_MODS_H
#define LUMA_MODS_H

#include <luma_services.h>

#ifdef __cplusplus
extern "C" {
#endif

#define LUMA_MODS_NAME "mods"
#define LUMA_MODS_MAJOR 1u
#define LUMA_MODS_MINOR 0u
#define LUMA_MODS_IDS_NAME "mods.ids"
#define LUMA_MODS_IDS_MAJOR 1u
#define LUMA_MODS_IDS_MINOR 0u
#define LUMA_MODS_INSTALL_NAME "mods.install"
#define LUMA_MODS_INSTALL_MAJOR 1u
#define LUMA_MODS_INSTALL_MINOR 0u

typedef enum LumaModsKind {
    LUMA_MODS_LUMA = 0,        /* a .luma */
    LUMA_MODS_RIIVOLUTION = 1, /* a Riivolution mod */
    LUMA_MODS_TEXTURES = 2,    /* a texture pack */
    LUMA_MODS_FILES = 3        /* loose files */
} LumaModsKind;

typedef enum LumaModsState {
    LUMA_MODS_RUNNING = 0,     /* its code runs, or it has none and its content is in */
    LUMA_MODS_OFF = 1,         /* the player turned it off */
    LUMA_MODS_REFUSED = 2,     /* the check refused it (its reason on its Mods page) */
    LUMA_MODS_NOT_STARTED = 3, /* on, its code did not start (consent waiting, a required mod not running) */
    LUMA_MODS_TAKEN_OUT = 4    /* taken out while the game runs (reload 2) */
} LumaModsState;

typedef struct LumaModsInfo {
    uint32_t size;       /* sizeof(LumaModsInfo) as the mod was built */
    uint32_t order;      /* its place in the list from the top, from 1 */
    LumaModsKind kind;   /* 4 bytes: a C enum on MSVC, GCC and Clang */
    LumaModsState state;
    uint32_t has_code;   /* 1: code for this platform, 0: none */
    uint32_t reserved;   /* 0 */
    char id[68];         /* NUL-terminated UTF-8, cut at a character's end (registry 3) */
    char name[132];      /* as the Mods page shows it: text.<the player's language>.name, else name, else the id */
    char version[36];    /* luma.json `version`, "" when it has none */
} LumaModsInfo;          /* 1.0: 260 bytes */

typedef struct LumaModsService {
    LumaServiceTable base;
    /* 1.0. Game thread. The number of mods in the session's list (fixed for the session; only `state` follows
       take-out and reload). */
    uint32_t (*count)(const LumaMod* mod);
    /* 1.0. Game thread. The mod at `index` (0 the top) into *out. LUMA_OK; LUMA_E_NOT_FOUND past the end (an answer,
       not reported); a null out: LUMA_E_ARGUMENT (Service.Argument), a size below 1.0's: LUMA_E_ARGUMENT
       (Service.StructSize), both reported. */
    int32_t (*at)(const LumaMod* mod, uint32_t index, LumaModsInfo* out);
    /* 1.0. Game thread. The mod with id `id`, as `at`; LUMA_E_NOT_FOUND when none (not reported); a null id:
       LUMA_E_ARGUMENT, reported. */
    int32_t (*find)(const LumaMod* mod, const char* id, LumaModsInfo* out);
    /* 1.0. Game thread. The calling mod's own option `key` (luma.json `options[].key`) as the player has it now, as
       UTF-8 text with a NUL into buf (director's DECISION 11-11 23:0x, contract section 8):
         choice: the chosen value ("off" when the player turned an optional choice off);
         number: the number as the Mods page shows it, shortest decimal ("0.5", "12");
         toggle: "1" on, "0" off;
         text:   the player's text (the manifest's `default` until the player types one);
         key:    the control's name as input's store has it (plan 2.5; the manifest's `default` until the player
                 binds another), one of the names luma.json takes (input_bindings kTargetIds).
       Returns the text's length in bytes without the NUL. A null buf with size 0 asks the length only (not reported).
       Mistakes, each reported and nothing written: a null key, or a key that is not one of this mod's options:
       LUMA_E_NOT_FOUND (Mods.OptionUnknown); a key naming another mod's option ("<mod id>/<key>"): LUMA_E_DENIED
       (Mods.OptionNotOwn: a mod reads only its own, item 4); a buf too short for the text and its NUL:
       LUMA_E_TOO_LARGE (Mods.OptionBufferShort, naming the length needed). */
    int64_t (*option)(const LumaMod* mod, const char* key, char* buf, uint64_t size);
    /* 1.0. Game thread. `fn` gets an OPTION event (at the start of a frame, never inside this call) each time the
       player changes one of this mod's options while it runs: one event an option changed, in luma.json order, after
       the Mods page applied it (Apply, or leaving the page). The value read by `option` in that call is the new one.
       A later call replaces fn and user; a null fn stops the events. LUMA_OK. */
    /* events: OPTION(value) */
    int32_t (*watch_options)(const LumaMod* mod, LumaEventFn fn, void* user);
    /* 1.0. Game thread. `fn` gets a STOPPED event (at the start of a frame) each time another mod stops while this
       one runs: switched off on the Mods page, removed, reloaded, or stopped by the guard; after its stop, before its
       code is unloaded. A library mod removes what it made for that mod (LumaKit rule 4). A later call replaces fn
       and user; a null fn stops the events. LUMA_OK. */
    /* events: STOPPED(data) */
    int32_t (*watch_mods)(const LumaMod* mod, LumaEventFn fn, void* user);
    /* Kit data (contract section 9, lumakit rule 11): the files `kit/<module>/<name>.json` a .luma ships, which a library
       mod reads for each mod that needs it. `of` is a mod as LumaScript's caller() gives it (its LumaMod pointer as a
       number); `mod` may read the kit data of `of` when `of` is running and is `mod` itself or names `mod` in its
       luma.json `needs`; any other `of` (not running, not a mod, not needing it): LUMA_E_DENIED, reported
       (Mods.KitNotNeeded). `module` and `name`: 1 to 64 of a-z 0-9 _ - ., not starting with a dot, no ".." (no path
       part can be in them); another: LUMA_E_ARGUMENT, reported. A null length, or a null buf with size above 0:
       LUMA_E_ARGUMENT, reported. A null buf with size 0 asks the length only.
       kit_names: 1.0. Game thread. The names (file stems) of the `kit/<module>/<name>.json` files in `of`, sorted byte for byte, each
       followed by a 0 byte, into buf; *length the bytes they take (0: none). A folder mod reads like a .luma. LUMA_OK;
       LUMA_E_TOO_LARGE when size is below *length (an answer, not reported; nothing written). */
    int32_t (*kit_names)(const LumaMod* mod, uint64_t of, const char* module, char* buf, uint64_t size, uint64_t* length);
    /* 1.0. Game thread. The bytes of `kit/<module>/<name>.json` in `of` into buf, as the installed .luma (or folder mod) has them (read
       once, kept until `of` stops); *length their count. LUMA_OK; LUMA_E_NOT_FOUND when there is no such file (an
       answer, not reported); LUMA_E_TOO_LARGE when size is below *length (an answer; nothing written); a file past
       LUMA_MODS_KIT_FILE_MOST: LUMA_E_FORMAT, reported (Mods.KitTooLarge); LUMA_E_IO when it could not be read. */
    int32_t (*kit_read)(const LumaMod* mod, uint64_t of, const char* module, const char* name, void* buf, uint64_t size,
                        uint64_t* length);
    /* 1.0. Game thread. The running mods (with code) whose luma.json `needs` names `mod`, in the Mods list's order, as
       caller() numbers into out[] (up to capacity), and how many there are into *count (may exceed capacity).
       LUMA_OK; a null count, or a null out with capacity above 0: LUMA_E_ARGUMENT, reported. */
    int32_t (*needing)(const LumaMod* mod, uint64_t* out, uint32_t capacity, uint32_t* count);
    /* 1.0. Game thread. The mod `which` names (a caller() number: its LumaMod pointer as a number) into *out, as `at`
       gives it (its place in the list in `order`), so a library can put what it holds for each mod in the Mods
       list's order. LUMA_OK; LUMA_E_NOT_FOUND when `which` is no mod the game runs now (0, any other number, a mod
       stopped): an answer, not reported. A null out: LUMA_E_ARGUMENT, a size below 1.0's: LUMA_E_ARGUMENT
       (Service.StructSize), both reported. */
    int32_t (*of_mod)(const LumaMod* mod, uint64_t which, LumaModsInfo* out);
} LumaModsService;

/* The largest kit file kit_read gives: 4 MiB. */
#define LUMA_MODS_KIT_FILE_MOST (4u * 1024u * 1024u)

/* Events of mods (LumaEvent.kind) to watch_options' fn; `handle` is 0. */
typedef enum LumaModsEvent {
    LUMA_MODS_EVENT_OPTION = 1, /* data: none; value: the option's place in luma.json `options` (0 the first); status: LUMA_OK */
    LUMA_MODS_EVENT_STOPPED = 2 /* to watch_mods' fn: data: the stopped mod's id, UTF-8 text with a NUL (valid
                                   during the call); data_size: its length without the NUL; value: 0; status:
                                   LUMA_OK */
} LumaModsEvent;

/* The spaces of 1.0 (contract section 3): numbers 0x01000000 to 0x01FFFFFF in each. */
#define LUMA_MODS_SPACE_MESSAGE "message" /* a message one mod's actors send and another's receive (receiveOtherMsg) */
#define LUMA_MODS_SPACE_SENSOR "sensor"   /* a sensor type of a mod's actors (the character group) */

typedef struct LumaModsIdsService {
    LumaServiceTable base;
    /* 1.0. Game thread. The number for `<the mod's id>/<name>` in `space` into *out: the same in every session and on
       every machine with the same mods (derived from the key; a number two keys would share goes to the one that
       asked first, Mods.IdMoved). LUMA_OK; an unknown space, a name outside 1 to 64 of a-z 0-9 _ - ., a null out:
       LUMA_E_ARGUMENT, reported; a full space: LUMA_E_LIMIT, reported (Service.Limit). */
    int32_t (*get)(const LumaMod* mod, const char* space, const char* name, uint32_t* out);
    /* 1.0. Game thread. Another mod's number, read only: LUMA_OK when `<mod_id>/<name>` is in the record (that mod
       asked for it in this or an earlier session); LUMA_E_NOT_FOUND when not (not reported); a bad space, mod id or
       name, a null out: LUMA_E_ARGUMENT, reported. */
    int32_t (*of)(const LumaMod* mod, const char* space, const char* mod_id, const char* name, uint32_t* out);
} LumaModsIdsService;

/* Events of mods.install (LumaEvent.kind); `handle` is the request's. */
typedef enum LumaModsInstallEvent {
    LUMA_MODS_INSTALL_EVENT_DONE = 1 /* data: none; value: 0; status: LUMA_OK, LUMA_E_DENIED, LUMA_E_FORMAT, LUMA_E_EXISTS */
} LumaModsInstallEvent;

typedef struct LumaModsInstallService {
    LumaServiceTable base;
    /* 1.0. Game thread. Asks the player, on the import screen, to install the .luma at `path` in the mod's data
       folder (contract section 1 item 9; an installed mod starts the next time Nebula starts). LUMA_PENDING and *out:
       a DONE event follows (to fn), closed after its event, never inside this call; LUMA_E_BUSY, LUMA_E_NOT_FOUND, LUMA_E_DENIED (don't ask again):
       answers, not reported; LUMA_E_ABSENT: a host without the install screen; a path outside the mod's data folder or not ending ".luma",
       a null fn or out: LUMA_E_ARGUMENT, reported. */
    int32_t (*request)(const LumaMod* mod, const char* path, LumaEventFn fn, void* user, LumaHandle* out);
    /* 1.0. Game thread. Ends a request: the screen closes as Not now when still open; no event after this returns.
       LUMA_OK; a handle that is not this mod's open request: LUMA_E_HANDLE, reported. */
    int32_t (*cancel)(const LumaMod* mod, LumaHandle handle);
} LumaModsInstallService;

#ifdef __cplusplus
}
#endif

#endif /* LUMA_MODS_H */
