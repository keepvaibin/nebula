/* SPDX-License-Identifier: GPL-3.0-only */
/* LumaCore's assets service (contract services-assets; plan 2.8). Layer 1: the game's files as the game reads them,
   by path: what is there, its bytes, a folder's entries, with the mods' files over the game's or the game's own under
   them. No permission: every mod may read the game. Every call is the game thread's and returns at once (the store is
   mapped for the whole session: no call waits on the drive; the first read of a file's bytes may, as the game's own
   first read of them does). Later layers: a mod's files given to the game at run time (providers), run-time content
   through the cooker and the validator. */
#ifndef LUMA_ASSETS_H
#define LUMA_ASSETS_H

#include <luma_services.h>

#ifdef __cplusplus
extern "C" {
#endif

#define LUMA_ASSETS_NAME "assets"
#define LUMA_ASSETS_MAJOR 1u
#define LUMA_ASSETS_MINOR 0u

/* A path (the game's, from the store's root): '/' between parts, letters in either case, "." and ".." steps; a disc
   path is taken as the game's (an archive "ObjectData/Kuribo.arc" is the folder "ObjectData/Kuribo", a file inside it
   "ObjectData/Kuribo.arc/kuribo.bdl" that folder's file; ".szs" likewise). A mod path `<id>:<path>` (luma-format 5) is
   the file a mod's layer has at assets/<path>, any mod's. At most LUMA_ASSETS_PATH_MOST bytes. */
#define LUMA_ASSETS_PATH_MOST 1023u

/* flags for find, bytes and entry. */
#define LUMA_ASSETS_GAME_ONLY 1u /* the game's own file or folder, under any mod's that replaces it */

/* What a file holds (the importer's kind of it). Values are only ever added. */
typedef enum LumaAssetKind {
    LUMA_ASSET_OTHER = 0,
    LUMA_ASSET_MODEL = 1,             /* bdl, bmd */
    LUMA_ASSET_MOTION = 2,            /* bck, bca */
    LUMA_ASSET_TEXTURE_SRT = 3,       /* btk */
    LUMA_ASSET_COLOR_KEY = 4,         /* bpk */
    LUMA_ASSET_TEXTURE_PATTERN = 5,   /* btp */
    LUMA_ASSET_CLUSTER_KEY = 6,       /* blk */
    LUMA_ASSET_REGISTER_COLOR = 7,    /* brk */
    LUMA_ASSET_SOUND_ANIMATION = 8,   /* bas */
    LUMA_ASSET_MATERIALS = 9,         /* bmt */
    LUMA_ASSET_VISIBILITY = 10,       /* bva */
    LUMA_ASSET_MATERIAL_TABLE = 11,   /* banmt */
    LUMA_ASSET_TEXTURE = 12,          /* bti */
    LUMA_ASSET_LAYOUT = 13,           /* brlyt */
    LUMA_ASSET_LAYOUT_ANIMATION = 14  /* brlan */
} LumaAssetKind;

/* What is at a path. */
typedef struct LumaAssetInfo {
    uint32_t size;      /* sizeof(LumaAssetInfo) as the mod was built: the service fills only what both know */
    uint32_t folder;    /* 1: a folder (an archive is a folder of its files); 0: a file */
    uint32_t archive;   /* 1: a folder that was an archive on the disc (the mods replace it whole) */
    uint32_t kind;      /* a file's LumaAssetKind; a folder's 0 */
    uint32_t from_mod;  /* 1: a mod's file or folder, over the game's or new; 0: the game's own */
    uint32_t entries;   /* a folder's entries (entry's indices 0 to entries - 1); a file's 0 */
    uint64_t bytes;     /* a file's length; a folder's 0 */
} LumaAssetInfo; /* 1.0: 32 bytes */

typedef struct LumaAssetsService {
    LumaServiceTable base;
    /* 1.0. What is at `path` into *out (out->size set by the mod). LUMA_OK; nothing there: LUMA_E_NOT_FOUND (an
       answer, not reported). A null or too long path, a null out or out->size under 8, flags other than
       LUMA_ASSETS_GAME_ONLY: LUMA_E_ARGUMENT, reported. */
    int32_t (*find)(const LumaMod* mod, const char* path, uint32_t flags, LumaAssetInfo* out);
    /* 1.0. The bytes of the file at `path`: *data and *bytes. The bytes are the game's, read only, and stay where they
       are until the game ends (a pointer kept is good all session, from any thread). LUMA_OK; nothing there:
       LUMA_E_NOT_FOUND, a folder: LUMA_E_FORMAT (answers, not reported); a file of 0 bytes: LUMA_OK, *data not null.
       A null path, data or bytes, wrong flags: LUMA_E_ARGUMENT, reported; the system could not map it: LUMA_E_IO. */
    int32_t (*bytes)(const LumaMod* mod, const char* path, uint32_t flags, const void** data, uint64_t* bytes);
    /* 1.0. The index-th entry of the folder at `path`: its name (with its terminating 0) into name (name_size bytes)
       and what it is into *out (either may be null). The game's entries come first, in the store's order, then the
       ones the mods add; with LUMA_ASSETS_GAME_ONLY the game's alone (and the game's folder). LUMA_OK; index at or past
       the end: LUMA_E_NOT_FOUND; no folder there: LUMA_E_NOT_FOUND, a file there: LUMA_E_FORMAT (answers); a name
       longer than name_size - 1: LUMA_E_TOO_LARGE, name_size the needed size in *needed when needed is not null
       (an answer; *out is filled). A null path, wrong flags, name null with name_size not 0: LUMA_E_ARGUMENT,
       reported. */
    int32_t (*entry)(const LumaMod* mod, const char* path, uint32_t flags, uint32_t index, char* name,
                     uint32_t name_size, uint32_t* needed, LumaAssetInfo* out);
} LumaAssetsService;

#ifdef __cplusplus
}
#endif

#endif /* LUMA_ASSETS_H */
