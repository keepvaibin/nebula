# Service: assets

## More rules

### Services-assets (written with its code, R1)

Code: Header `native/include/luma_assets.h` (this page says what it decides and why). Provider
`runtime/src/luma_assets.cpp`, host call `runtime/include/galaxy/luma_assets_service.h` (`set_store`, from host_main
once game.pak is open).

Layer 1 is reading (1.0). Layer 2, providers (1.1): a mod's files given to the game for a path at run time, through
the host's asset functions (host_main `svc_asset_*`). Layer 3, cook (1.1): a file as the disc has it cooked at run time
by the game's own cooker (nebula_import) and provided. The cooker's refusal is the check of
the content (the one validator checks packages at install; a file cooked at run time is checked by the code that cooks
it). Code: `h/2.8-provide`.

### 1. What is decided here, in one list

1. One service, `assets` 1.0, no permission: every mod may read the game. Catalog row after `net.local`;
   harness capability `services.assets`.
2. What it reads: the host's AssetStore (game.pak with the mods' store attached over it). The store opens before
   the first mod and the mods' store is attached before the game starts; neither changes after, so every answer
   holds for the whole session.
3. Paths: the game's, from the store's root, any letter case, "." and ".." steps. A disc path is the store's
   (`assets::store_path`: "X.arc" is the folder "X", "StageData/S/SScenario.arc" is "StageData/SScenario", ".szs"
   likewise), so a mod names files as the disc and every Wii tool do. `<id>:<path>` is a mod's own asset
   (luma-format 5: `layers/<L>/assets/<path>`, the store's `Luma/<id>/<path>`), any mod's, so mods can share assets.
   At most 1023 bytes.
4. Merged by default: what the game reads at a path (a mod's file over the game's; a language a mod adds falls
   back to the game's own). `LUMA_ASSETS_GAME_ONLY`: the game's own, under any mod's (a mod that changes a file and
   wants the original). `from_mod` says which.
5. find: what is there (file or folder, archive, kind, from_mod, entries, bytes). Nothing there is an answer
   (LUMA_E_NOT_FOUND, not reported).
6. bytes: a pointer to the file's bytes in the mapped store: no copy, read only, valid until the game ends and
   usable from any thread (the call itself is the game thread's). The bytes are the game's as the game reads them
   (the importer's cooked form where it cooks a kind; LumaAssetKind says which). A folder: LUMA_E_FORMAT.
7. entry: a folder's entries by index; a game folder's own first (store order), then the ones mods add; a mod's
   folder or a replaced archive only its own. Each entry's info is what the game reads at that path. A short name
   buffer is LUMA_E_TOO_LARGE with the size needed (name null and size 0 asks only that).
8. Struct growth: LumaAssetInfo starts with its size; the service writes only the bytes both sides know.
9. Nothing waits: each call is an index lookup and a mapped view. The first touch of a cold file's bytes reads
   from the drive in the touching thread, as the game's own first read does.
10. Rows: only the registry's (100 thread, 101 argument, 104 limit). No player text.
11. provide (1.1): the mod's bytes for a path, copied, in the form the game reads (what `bytes` gives: a copy of
    a game file, or cook's output). One file (a count of 1 and an empty inner path) or a whole archive (its files by
    paths inside it; sub-folders from the paths; the files side by side in one block at 32-byte steps, as the cooker
    places them, so the game's AssetFolder reads it as it reads the store's). The game's next look at the path finds
    it before the store's (`svc_asset_find`); its entry numbers start at 0x60000000, above every store's, and lead
    `entry`, `child`, `map` and `read` to it. `map_private` answers null for it, so the game reads it into its own
    memory (GameAssets::load does so already when a view is refused). A file the game holds already (a loaded
    stage's archives) changes when the game loads it again.
12. Where a provide is refused (answers, not reported): inside an archive (the game's, a mods' store's or a
    provided one: the game reads archives whole and never looks the path up), a file where the game has a folder,
    an archive where it has a file, at a plain folder (its files are provided one by one): LUMA_E_FORMAT. A path
    another mod provides, or a path under which one is provided: LUMA_E_EXISTS. The mod's own path again: its
    second provide replaces the first. A path new to the game is found by its path; folders' entries do not list it
    (`added_child` answers nothing under a provided path either: a provide replaces the game's whole).
13. Nothing given is taken back. An entry once given keeps its index, its name and its bytes until the process
    ends, withdrawn or not: the game may hold an index, a name pointer or a view on its loading threads. So a mod's
    provided bytes are capped over its run, withdrawn ones included (LUMA_ASSETS_PROVIDE_MOST, 256 MiB; LIMIT, row
    104); at most 4096 files in one provide. A mod's stop withdraws everything it provides; its end forgets its
    count (the bytes stay).
14. withdraw (1.1): the game's own again at the path; NOT_FOUND when the mod provides nothing there (an
    answer).
15. cook (1.1): the bytes (copied) cooked on one thread of the service's own, one file at a time in the order
    asked, then provided as 11-12 on the game thread when the answer is delivered (so the rules hold for the game as
    it is then). LUMA_PENDING and a request; LUMA_ASSETS_EVENT_COOKED with that handle: LUMA_OK (value: the mod's
    provided bytes now), LUMA_E_FORMAT with the cooker's words as data, or provide's answers. ABSENT when this
    game's importer has no run-time cook; BUSY at 8 cooks waiting or running for the mod. A mod's stop cancels its
    cooks: no event comes. The cooker is told the path as the mod gave it (".arc" and all): its name decides how
    a file is read, as at install.
16. Threads: find, bytes, entry, provide, withdraw and cook are the game thread's. The host's functions the game
    calls read the provided table from any thread (a shared lock; one atomic load while no mod provides anything).

### 2. Why

- A mod that reads the game's own data (a level's objects, a model to copy, a table) needs no copy of it in the
  .luma and no second loader: the bytes are the ones the game uses.
- A pointer into the mapped store is the cheapest possible read, and safe because the store never moves or closes
  while the game runs.
- Disc paths keep every existing tool's names; `<id>:<path>` is the format's own name for mod assets.

### 3. Tests

| id | what it proves |
|---|---|
| 2801-assets-read | ObjectData/Kuribo.arc by disc path, other case, store form and GAME_ONLY agree; walk to a file, its bytes and length; FORMAT for a folder's bytes and a file's entries; NOT_FOUND for nothing and past the end; TOO_LARGE with the needed size; an 8-byte struct keeps the rest |
| 2802-assets-mistakes | null and 1024-byte path, flags 2, null out, out->size 4, null data, null name with a size: ARGUMENT, each in the mod log |
| 2804-assets-provide | a file at a new path (any spelling, its bytes, GAME_ONLY NOT_FOUND, replaced by the mod's second provide, withdrawn, withdrawn again NOT_FOUND); the Goomba's archive copied from the game's own (GAME_ONLY) and provided: from_mod, entries, an entry's name and bytes, the game's still under it; FORMAT inside an archive, at a plain folder, under a provided archive, a file where an archive is |
| 2805-assets-provide-mistakes | provide: null files, count 0 and 4097, null data with bytes, a ".." part, a path twice (other case), a file where a folder is, an empty inner path among two, null and root path: ARGUMENT; bytes over the most: LIMIT; withdraw null path, cook null fn: ARGUMENT; each said with its count |
| 2806-assets-provide-two | two mods, one path: EXISTS for the second, which cannot withdraw the first's and reads its bytes; after the first withdraws, the second's provide is taken |
| 2807-assets-cook | a text file cooked as it is and provided (bytes the same, value > 0); a cut J3D model refused (FORMAT, the cooker's words) and nothing provided; two requests, two handles. Needs section 4 in nebula_import |
| 2803-assets-own | the mod's layers/base/assets files as `assets-own:hello.txt` and `Luma/assets-own/hello.txt`: from_mod 1, bytes "hello luma", GAME_ONLY NOT_FOUND, its folder's 2 entries, sub/deep.txt by entry |

### 4. The cooker's run-time calls

The service resolves these from nebula_import (Windows `nebula_import.dll` and Linux/Android `libnebula_import.so` by
name, Apple linked in, weak). Without all five, cook answers ABSENT. They are `cook::cook_entry` behind a C call:

```c
typedef struct LumaCooked LumaCooked;
/* One file as the disc has it (compressed or not, an archive or one file), cooked as the store holds it. `name` is
   its path as the mod gave it ("ObjectData/Kuribo.arc"): it names textures and picks the readers that go by
   extension, as at install. Never null but for want of memory. */
LumaCooked* luma_cook_entry(const char* name, const uint8_t* bytes, uint64_t size);
/* Null: it cooked. Else the cooker's words (English UTF-8, terminated), valid until free. */
const char* luma_cooked_error(const LumaCooked* cooked);
/* Its items in the store's order (cook_entry's, sorted by `order`). */
uint32_t luma_cooked_count(const LumaCooked* cooked);
/* Item `index`: *path its path inside ("" the file itself, or an empty archive), *folder 1 for a folder (bytes
   null, *size 0), else its bytes and *size. Valid until free. */
const uint8_t* luma_cooked_item(const LumaCooked* cooked, uint32_t index, const char** path, uint64_t* size,
                                uint32_t* folder);
void luma_cooked_free(LumaCooked* cooked);
```

Calls come from one thread, one at a time, while the importer's other calls (the validator) may run
on others: whatever cook_entry touches that is global (the texture naming file, the pipeline list, the program list)
must be safe for that, or luma_cook_entry takes the importer's own lock. A file whose cook needs what only an install
gives (new pipelines for the renderer's cache, a program the game was not built with) is refused with words saying
so, never cooked half.

State: 1.0 WRITTEN (compiles; mods built). 1.1 WRITTEN (h/2.8-provide: luma_assets.cpp, host_main svc_asset_*,
catalog assets 1.1, capabilities services.assets.provide/.cook; mods built, tests 2804-2807 written, unrun). 2807 also
waits on section 4 in nebula_import. PROVEN waits on a run on luma-int.
