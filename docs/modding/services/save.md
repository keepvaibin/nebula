# Service: saves

## More rules

### Services-save

The `save` services: a mod's own values kept with the player's game file (following its saves, copy and erase), a
mod's own values kept for the player across files (written at once), and the game's own save file handed out
(export) or offered back (import, only through the game's screen and the player's answer). Inherits
services-registry whole.

### 1. What is decided here, in one list

1. Three services: `save` 1.0 (per-file values; no permission), `save.global` 1.0 (values across files; no
   permission), `save.game` 1.0 (the game's file out and in; permission `save.game`, registry 5.1: "Reads your save
   files and may offer one to replace; you confirm each replacement.").
2. Per-file values live inside the game's file. Each user file's game data (GameDataHolder: Mario's and Luigi's,
   SaveFileId kinds 0 and 1) gains one chunk, signature `'LUMA'`, registered last in GameDataHolder's chunk holder.
   So a value follows exactly what the game's progress follows: written when the game saves (its own save points,
   never by the mod), copied when the player copies the file on the file-select screen (SaveDataHandler
   copyUserFileMemory copies the whole binary), cleared when the file is erased or started new (`initializeData`),
   lost when the player quits without saving, as a star would be. The game without the chunk (an older Nebula)
   skips it and drops it at its next save:
   said in the release notes, not a format break.
3. The chunk's form, version 1, big-endian as the file's other chunks (SaveBinary.hpp): `u16 version`, `u16
   mods`, then per mod `u8 id_length, id bytes, u32 bytes, then its entries`: per entry `u8 key_length, key bytes,
   u32 length, value bytes`. Mods in list order at the save, keys in the order first set. Header hash
   (`makeHeaderHashCode`) = fnv1a32("LUMA" "1"). A chunk that does not parse is skipped whole and the mod log has one
   Error (`Save.ChunkBroken`, file number), the file's game data loads as it is.
4. Limits: a key 1 to 64 of `a-z 0-9 _ - .`; a value 0 to 64 KiB; one mod's entries in one file at most 256 KiB;
   the whole chunk in one file at most 1 MiB, so Nebula.sav (twelve game-data files written whole at each save)
   stays a few MiB. Past either: `LUMA_E_LIMIT`, reported (Service.Limit, naming the file).
   The save handler grows with the chunk: (a) the chunk's `serialize` never writes past the
   capacity it is given and returns 0 when it does not fit (one Error, `Save.ChunkTooBig`, the file saved without
   mods' data, never corrupted); (b) SaveDataHandler grows `mMake` before each make to the fixed part plus the
   chunk's `size()` (the four make sites and storeUserFile); (c) a Nebula.sav longer than the read buffer
   (`NEBULA_SAVE_TOO_LARGE` from the host's read, which SaveRequest maps to Result_Corrupted today) regrows `mBuffer`
   to the length the read gave and reads once more; a second TOO_LARGE is the corrupted file it claims.
5. A removed mod's values stay in the chunk, inert (6.5): carried on each save, never given to another mod. A mod
   is removed when its folder is gone. The one REMOVED MODS line of services-mods 8 names it with its numbers
   and its bytes (the chunks' entries over the six user files in memory plus `luma/<id>.bin`); its one FORGET drops
   the entries in memory (each file loses them at its next save by the game; never a save of Nebula's own, which
   would write the loaded file's unsaved progress) and deletes `luma/<id>.bin` at once; one Note `Mods.Forgotten`
   (services-mods 162). The id rule: a mod with a removed mod's id is that mod installed again and reads
   its values; that is the player's install of a file with that id (validator, the import screen naming it), never
   something a mod can claim by itself at run time.
6. A renamed mod (luma.json `takes_over: ["<old id>"]`): the move is the
   player's answer: the import screen says "Takes over what {old} kept" under the mod, and INSTALL is the answer.
   It happens only when the old id is not installed. The old id installed and on: the validator refuses the new mod
   (an Error at import). Installed and off: no move, a Note on the new mod's page, "Takes over {old}, which
   is still installed". At the new mod's first start the host moves the old id's entries in each
   file's chunk in memory to the new id (the game's next save writes them), renames `luma/<old>.bin`, and moves the
   mod-ids.json keys (services-mods 8); one Note `Mods.TookOver` (services-mods 164). Consent answers never move: the
   new mod's code is asked as any new mod's. This service reads entries by the mod's current id only.
7. Global values (`save.global`): `<save root>/<code>/luma/<mod id>.bin` (beside Nebula.sav, so a backup of the
   saves has them), the same entry form as item 3 for one mod, written whole and renamed into place
   (galaxy/atomic_file.h) on the file thread at the end of the frame a value was set (one write a frame a mod at
   most). Limit 1 MiB a mod. Read once at the mod's first get. A write the system refuses is tried again after a
   wait that doubles from 60 frames to 3600, with one Error `Save.GlobalWriteFailed` for each run of refusals; the
   mod's stop writes at once whatever the wait. A file that does not read is first copied to
   `luma/<mod id>.bin.broken` (the Error `Save.GlobalBroken` names it), and `luma/<mod id>.bin` is written over only
   after that copy is down; if the copy is refused, every call answers `LUMA_E_IO` and the file is never written over.
8. The game's file (`save.game`): `export(mod, file, fn, user, &h)`: the event gives the bytes of user file `file`
   (1-6) as the game would write them now (its GameDataHolder binary for the active character, then config),
   `take` in the event's call as clipboard's take. `offer(mod, path, fn, user, &h)`: a file in the mod's data folder
   (fs 2.3) in the same form, parsed before any screen (a bad file: `LUMA_E_FORMAT`, never shown). The offer screen
   is a Nebula screen over the game (the game's file select is reached only from the title): opened as the
   import screen is, the game paused as by Start; the offered file's summary at the top ("{mod} offers this file":
   character, stars, play time), the six files with theirs from the user files in memory, the loaded one shown "In
   play" and not pickable; the player picks a file, then "Replace file {n}? Its progress goes." REPLACE / KEEP.
   `LUMA_OK` (value: the file) replaced, `LUMA_E_DENIED` no, Escape, B or the menu closed. Before replacing, the old
   file's bytes go to `<save root>/<code>/luma/replaced/<date>-file<n>.bin` (the confirm's help says the old file is
   kept in the save folder). The replacement goes through the module: `SaveDataHandler::setFile` of each of
   that user file's kinds (Mario, Luigi, config) with the offered bytes, as `copyUserFileMemory` does, then
   `requestSaveSaveData`, queued to the start of a frame when no save is running (`mSaveRequest->isDone()`) and the
   game's own save sequence (SaveDataHandleSequence) does not hold the handler: requestSaveSaveData `init()`s the
   one SaveRequest, so a write made under the sequence would replace the game's. Checked in
   SaveDataHandler.cpp: requestSaveSaveData
   writes `mFiles` as they are and only `storeUserFile` puts a loaded file's holders in, so the loaded file's unsaved
   progress is not written and the game's next save, which stores only the loaded file, keeps the new one.
   One offer at a time across mods; `LUMA_E_BUSY` while any screen waiting on the player is up (services/mods.md
   9). No LUMA_E_CANCELLED: `cancel`, a mod's stop and the game's end send nothing.
9. Deterministic mode: values work as written; the game's saves go where the harness says (its own save folder
   per run); `offer` answers through the harness's screen step.
10. No mod, no cost: the chunk with no entries is 16 bytes in each file (its 12-byte header, `version` and
    `mods` 0), written for every player, mods or not; nothing else runs. Nebula.sav is Nebula's own file: the Wii's
    save (NAND) and its tools never see the chunk.

### 2. The header: `native/include/luma_save.h`

```c
#define LUMA_SAVE_NAME "save"
#define LUMA_SAVE_GLOBAL_NAME "save.global"
#define LUMA_SAVE_GAME_NAME "save.game"

typedef struct LumaSaveService {
    LumaServiceTable base;
    /* 1.0. Game thread. The value of `key` in the file the game has loaded now, for this mod. Copies min(length,
       size) bytes into buf and returns the value's whole length (>= 0), also when it is over `size`, so a caller can
       ask again with room; buf may be null with size 0. LUMA_E_NOT_FOUND when not set, LUMA_E_ABSENT when
       no file is loaded (title, file select). A bad key, or a null buf with size > 0: LUMA_E_ARGUMENT, reported. */
    int64_t (*get)(const LumaMod* mod, const char* key, void* buf, uint64_t size);
    /* 1.0. Game thread. Sets the value (written by the game's next save). LUMA_OK; LUMA_E_ABSENT with no file
       loaded; LUMA_E_LIMIT past item 4's limits, reported; a bad key or a null value with bytes: LUMA_E_ARGUMENT. */
    int32_t (*set)(const LumaMod* mod, const char* key, const void* value, uint64_t bytes);
    /* 1.0. Game thread. LUMA_OK removed, LUMA_E_NOT_FOUND not set. */
    int32_t (*remove)(const LumaMod* mod, const char* key);
    /* 1.0. Game thread. The loaded file: 1-6 in *number and 0 Mario / 1 Luigi in *character (outs);
       LUMA_E_ABSENT when none; a null out: LUMA_E_ARGUMENT, reported. */
    int32_t (*file)(const LumaMod* mod, uint32_t* number, uint32_t* character);
} LumaSaveService;

typedef struct LumaSaveGlobalService {
    LumaServiceTable base;
    /* 1.0. Game thread. As save's get/set/remove, for the mod's values across files (item 7). */
    int64_t (*get)(const LumaMod* mod, const char* key, void* buf, uint64_t size);
    int32_t (*set)(const LumaMod* mod, const char* key, const void* value, uint64_t bytes);
    int32_t (*remove)(const LumaMod* mod, const char* key);
} LumaSaveGlobalService;

/* Events of save.game (LumaEvent.kind); `handle` is the request's. */
typedef enum LumaSaveGameEvent {
    LUMA_SAVE_GAME_EVENT_EXPORT = 1, /* data: none; status OK: value = the bytes' length, taken with take in the call */
    LUMA_SAVE_GAME_EVENT_OFFER = 2   /* data: none; status OK: value = the file number replaced (1-6); else value 0 */
} LumaSaveGameEvent;
/* Statuses: EXPORT LUMA_OK, LUMA_E_NOT_FOUND (that file is empty); OFFER LUMA_OK, LUMA_E_DENIED (no, or the screen
   closed), LUMA_E_FORMAT (not a game file of this disc's form). Never LUMA_E_CANCELLED (item 8). */

typedef struct LumaSaveGameService {
    LumaServiceTable base;
    /* 1.0. Game thread. Asks for user file `file` (1-6) as the game would write it now (item 8). LUMA_PENDING with
       the request in *out; the answer is an EXPORT event to fn at the next frame's start; a Note Save.Exported in
       the mod log. A file outside 1-6, a null fn or out: LUMA_E_ARGUMENT; a 5th open request: LUMA_E_LIMIT; both
       reported. */
    int32_t (*export_file)(const LumaMod* mod, uint32_t file, LumaEventFn fn, void* user, LumaHandle* out);
    /* 1.0. Game thread, inside the EXPORT event's call for that handle only, once. Copies min(value, size) bytes
       into buf and returns value (the file's whole length). Outside the call, another handle or a second time:
       LUMA_E_HANDLE, reported; a null buf with size > 0: LUMA_E_ARGUMENT, reported. The request is closed after the
       call whether or not this was called. */
    int64_t (*take)(const LumaMod* mod, LumaHandle handle, void* buf, uint64_t size);
    /* 1.0. Game thread. Offers the file at `path` (in the mod's data folder, fs 2.3) to replace a user file: the
       host opens the offer screen (item 8); the answer is an OFFER event. LUMA_PENDING with *out; another offer
       open (any mod's), any screen waiting on the player, a time hold or a cutscene now: LUMA_E_BUSY (an answer,
       not reported); a path naming no file: LUMA_E_NOT_FOUND, not reported; LUMA_E_IO the file could not be read;
       LUMA_E_ABSENT a host without the offer screen; a path outside the data folder, a null fn or out:
       LUMA_E_ARGUMENT, reported. */
    int32_t (*offer)(const LumaMod* mod, const char* path, LumaEventFn fn, void* user, LumaHandle* out);
    /* 1.0. Game thread. Ends a request: no event for it after this returns (registry 4.8); an open question closes
       as no, nothing replaced. LUMA_OK; a handle that is not this mod's open request: LUMA_E_HANDLE, reported. */
    int32_t (*cancel)(const LumaMod* mod, LumaHandle handle);
} LumaSaveGameService;
```

### 3. Implementation

- Module: `Game/System/LumaSaveChunk.hpp/.cpp` (the chunk: BinaryDataChunkBase, entries by mod id in memory, serialize
  and deserialize item 3), `Game/System/LumaSave.cpp` (the three providers; `save.global` and `save.game`'s file work
  through host calls in luma_services_facts.h). GameDataHolder.cpp: `addChunk` of the chunk after mAllGalaxyStorage,
  and its `initializeData` in `resetAllData`, which names each chunk by hand. SaveDataHandler.cpp and SaveRequest.cpp:
  item 4's (b) and (c), and the offer's queued write (item 8);
- Host: `<save root>/<code>/luma/` through galaxy::saves (`save_files.h`: a `luma` sub-folder, plain names `<mod
  id>.bin`), the file thread's writer.
- Screens: the offer screen (8) and the REMOVED MODS line (5) drawn; the import screen's takes_over line (6).

### 4. Messages (codes 180-199)

| Code | Row | English | Level |
|---|---|---|---|
| 180 | `Save.ChunkBroken` | Mods' data in file {file} could not be read; the file's game data loaded. | Error |
| 181 | `Save.Exported` | {mod} read your file {file}. | Note |
| 182 | `Save.Offered` | {mod} offered a file to replace file {file}. | Note |
| 184 | `Save.ChunkTooBig` | Mods' data did not fit in file {file}; the file was saved without it. | Error |
| 183 | `Save.Replaced` | File {file} was replaced with the one {mod} offered. | Note |

### 5. Tests (builds and runs)

| Test | Must show |
|---|---|
| 2261 | set then get in a loaded file: the value; at the title: LUMA_E_ABSENT |
| 2262 | set, the game saves (a save point step), a new run loads the file: the value; set without a save, a new run: not set |
| 2263 | file 1 copied to file 2 on the file-select screen (harness keys): the value in file 2 |
| 2264 | file 2 erased: its values gone, file 1's kept |
| 2265 | Luigi's file and Mario's of the same user file keep their own values |
| 2266 | a chunk with a broken entry: Save.ChunkBroken, the file's stars load |
| 2267 | a mod removed: its values carried through a save (the chunk's bytes), not given to another mod; installed again: found |
| 2268 | limits: a 65 KiB value, a 257th KiB, the chunk's 1 MiB over five mods: LUMA_E_LIMIT, reported; a save with 900 KiB of mods' data in file 1 (past the old read buffer) then a new run: the values and the stars load, no damaged-save screen |
| 2269 | save.global: set, next run: the value without any game save; the file written whole (no partial file after a kill mid-write is a by-person check) |
| 2270 | save.game export without the permission declared: L506; declined: Consent.OffDeclined; granted: the bytes equal Nebula.sav's user file as the game makes it |
| 2271 | save.game offer: the screen (fact), the loaded file not pickable, B: LUMA_E_DENIED; REPLACE: file N replaced (loaded in the same run on file select: its stars; and in the next run), the old bytes in luma/replaced, Save.Offered and Save.Replaced in the log; a broken file: LUMA_E_FORMAT, no screen |
| 2272 | takes_over: the old id removed, the new mod installed: its entries, global file and ids under the new id, Mods.TookOver; the old one installed and off: no move, the Note; installed and on: L213 at import |
| 2273 | FORGET of a removed mod: luma/<id>.bin gone at once, its entries gone after the next save, its numbers gone from mod-ids.json, Mods.Forgotten |

### 6. More rules

- `LumaSaveGameEvent` kinds: EXPORT = 1 (value: the length) and OFFER = 2 (value: the file replaced).
- `get` returns the value's whole length when it is over `size`; a null buf with size 0 asks the length.
- The 16 bytes of item 10 are in every player's file; the Wii's save never sees them.
- The offer screen is Nebula's own, over the game. There is no CANCELLED status. `takes_over` is the player's answer:
  only when the old id is gone, its entries, global file and ids move to the new mod, never through consent.
- `export_file`, `take`, `offer` and `cancel` are commented in the header: version, thread, each result, and whether
  an event follows.
- One REMOVED MODS line (item 5). The replacement goes through the module and the save handler's write from memory;
  the replaced file is kept, and row 183 `Save.Replaced` says so. BUSY holds across mods. Tests 2272 (takes_over) and
  2273 (FORGET).
- The service holds bytes; LumaScript types them (`json` data, `get<bytes>` raw).
- The write buffer grows as a mod's values grow, and what is written is bounded; the read buffer grows again on
  TOO_LARGE, so a grown file never reads as damaged. A file holds at most 1 MiB of mods' data (item 4; over it,
  `Save.ChunkTooBig`, 184).
- Item 8: the replacement is queued to a frame's start when no save runs and the game's save sequence is idle; 2271
  loads file N in the same run too.

- Item 8, the export form: one framed file of the whole user file: "NSAV", u16 version 1, u16 parts 3, then Mario's
  game data, Luigi's game data and the config, each a u32 length and its bytes; the frame's numbers little-endian,
  each part exactly as Nebula.sav holds it (save scalars in host order, version 3). The loaded file's own game data
  (its character's) and its config are made at the EXPORT event's frame start as the game's next save would write them
  (forgotten mods' values out first); every other part is the stored one; a part with no stored bytes has length 0. A
  file not created gives LUMA_E_NOT_FOUND. offer reads this form.
- EXPORT's status adds LUMA_E_MEMORY (no memory for the bytes). The limit for export_file is 4 open requests a mod
  (and 32 in all: Service.Limit names whichever was reached).
- The chunk's numbers (item 3) are host order like every chunk of a version-3 save, not big-endian.

### 7. The offer

- The host gives `data_file`, `ask_offer` and `close_offer` (a host without them answers LUMA_E_ABSENT). The file is read at the call on
  the game thread; at most 4 MiB, else it is not a game file.
- Parsed before any screen (SaveDataHandler::splitExport: "NSAV", version 1, 3 parts framed exactly; then the config
  and the last character's game data must load, and the other character's when it has bytes). A bad file: the call is
  LUMA_PENDING and the event LUMA_E_FORMAT at the next frame's start, no screen (as the header lists FORMAT among the
  event's statuses).
- The screen's six files: from the files in memory (the loaded one from its live game data, In play); a file in memory
  that does not load shows as existing with 0 stars. No per-file play time exists in this game's save (the
  achievements' play time is global), so the screen shows each file's last save instead: LumaOfferFile.saved, the
  config's last-modified time as Unix seconds UTC (the local clock's offset from the host's UTC clock now, to the
  quarter hour; 0 never saved).
- BUSY also for any cutscene (the game itself never pauses inside one), not only an unskippable one.
- REPLACE: Note Save.Offered (182) at the answer. The old file (if created) is exported as stored and written to
  luma/replaced/<YYYY-MM-DD-HHMMSS>-file<n>.bin through the host's save worker; it is logged by name once down. The
  copy refused: the event is LUMA_E_IO and nothing is replaced. Then, once no sequence runs, the save was read at the
  start and no request is with the host (as a FORGET's save waits), SaveDataHandler::replaceUserFile sets the three
  parts and startQuietSave writes the save; Note Save.Replaced (183); event LUMA_OK, value n. Saves off (nothing read
  at the start, no-save mode, a damaged save): LUMA_E_IO, nothing replaced. The file loaded meanwhile: LUMA_E_DENIED.
- cancel and the mod's stop: the open screen closes (close_asks) with no event; after REPLACE the replacement still
  goes through, without an event. One handle count for export and offer requests.
- Test 2271-offer: a missing file, a broken one, busy, REPLACE twice (the second keeps the old file), cancel. An
  offer of a missing file is LUMA_E_NOT_FOUND.
