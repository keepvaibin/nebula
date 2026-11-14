# Service: other mods

## More rules

### Services-mods

The `mods` services: the list of mods in this session as a mod's code may read it, a request to install a mod
(always through the import screen and the player's answer), and numbers allocated in the game's numbered spaces by
`<mod id>/<name>`, unique across mods and the same in every session. Inherits services-registry whole:
calling rules (4), events (6), a mod's life (8), no cost with no mod (10), messages (11), permissions (5).

Reviewers: F (the host's session list and import path, the loader's mod exports this does not duplicate), G (the
self-description rows, LumaScript's `mods`, the validator for spaces named in luma.json), J (the import screen's
request path 6.1, the Mods page's line for reserved numbers, the mod log rows).

### 1. What is decided here, in one list

1. Three services, each its own catalog row: `mods` 1.0 (the list; no permission), `mods.ids` 1.0 (numbers; no
   permission), `mods.install` 1.0 (no permission: the import screen is the player's answer, consent 2.1 "Import", and
   nothing installs without it).
2. Not here: calling another mod. A mod's functions for other mods are the loader's. That path is the one that records
   `uses`, so a mod taken out goes after the mods calling it (reload 2). A run-time lookup of an export here would
   bypass it and leave a pointer into a closed library; there is none.
3. The list is the session's: the mods as this session started them (the host's list after its start, in list order),
   not the Mods page's draft. Fixed for the session, `state` too: it is read once, at the first call, and a mod taken
   out later still reads `RUNNING`; no call gives `LUMA_MODS_TAKEN_OUT`. A mod learns that another stopped from
   `watch_mods` (its STOPPED event names the mod).
4. What a mod may read of another: id, name (as the Mods page shows it: luma.json `text.<the player's language>.name`,
   else `name`, else the folder name), version, its place in the list, its kind (`.luma`, Riivolution, texture pack,
   loose files), whether it has code for this platform, and its state (running, off, refused, not started, taken out).
   Not its options, files, permissions or answers: those are the player's settings, not another mod's business.
5. Numbers (`mods.ids`) are allocated in a space the host defines (section 3), for a key `<mod id>/<name>` (the
   calling mod's id; name 1 to 64 of `a-z 0-9 _ - .`). The first ask allocates; every later ask of the same key (this
   session, the next, after a reinstall) gives the same number. Two keys never share a number in a space.
6. The same number on every machine: the first value tried is derived from the key, `lo + (fnv1a32(space + "\0" + key)
   mod (hi - lo + 1))`, then the next free one upward (wrapping) when taken. Spaces are 2^24 wide, so two keys meet
   only by chance (about n^2 / 2^25 for n keys) and, short of that, every machine with the same mods gives the same
   numbers whatever order they asked in. A collision is resolved by the record (item 7) and said once in the log
   (`Mods.IdMoved`, a Note naming both keys). After a meeting, which key moved depends on which asked first on that
   machine, so a mod sending numbers to another machine (co-op) sends keys and asks `get` or `of` on each side, never
   a raw number.
7. The record: `<settings>/mod-ids.json` (beside mods.ini; `<settings>/mods` is the mods folder itself), `{"format":
   1, "spaces": {"<space>": {"<key>": number}}}`, written whole then renamed into place by the host's settings writer
   (file thread) after each new allocation; read once, at the first ask of the session. A record that is unreadable is
   kept aside as `mod-ids.json.bad` (one Error in the mod log, `Mods.IdsUnreadable`) and the session starts a new one,
   so numbers stay derived (item 6) even then.
8. Never reused: an allocation stays in the record while its mod is removed; the number is not given to another key. A
   mod is removed when its folder is gone (off or taken out is not). The Mods page shows, under REMOVED MODS, one line
   per removed mod for everything it keeps (with services-save 5): "{mod}: kept for when it returns", its help "{n}
   numbers, {size} of saved data", and one FORGET for all of it behind a confirm (the player's act; one Note
   `Mods.Forgotten`). A save naming such a number keeps working when the mod comes back. A renamed mod (luma.json
   `takes_over`, services-save 6) takes its old id's keys as `<new id>/<name>`, same numbers, at the same moment and
   under the same rule as the save entries.
9. Install: `mods.install.request(mod, path, fn, user, &handle)`: `path` names a `.luma` file in the calling mod's own
   data folder (fs 2.3's `data/`; until fs exists, `LUMA_E_ABSENT`). A path naming no file at the call:
   `LUMA_E_NOT_FOUND`, not reported. The answer comes as the event: `LUMA_OK` installed (on or off as the player
   chose, consent 2.1 "Import"), and it starts the next time Nebula starts: the session's list (item 3) does not
   change and the screen says so (a start in the running game by reload would be a status of a later minor, not a
   change to LUMA_OK). `LUMA_E_DENIED`: the player said no, or Escape, B or closing the menu. `LUMA_E_FORMAT`: the
   validator refused it (the screen shows why), or the file changed or went while the import read it. `LUMA_E_EXISTS`:
   that id is installed at the same or a newer version (an older one installed: the screen's button reads UPDATE and
   the line under it names both versions, Mods.InstallUpdates). No other status: `cancel` closes the screen with no
   event, a mod's stop ends its requests with no event (registry 8), the game's end ends everything. `LUMA_E_BUSY` (an
   answer, not reported): a request of this mod open; 60 s after its last `LUMA_E_DENIED`; any screen waiting on the
   player up, any mod's (the consent question, an import screen, a drop or picker import running); the game held by a
   mod's time hold or in a cutscene the game marks unskippable. The request screen has a third choice, DON'T ASK
   AGAIN: kept as the player's answer for that mod in consent's store, key `install.ask`, shown on its page as "May
   ask to install mods"; after it `request` gives `LUMA_E_DENIED` at once, no screen, not reported. A Note
   `Mods.InstallAsked` naming the file's name only, never its path.
10. No mod, no cost: nothing is read or allocated until a mod calls; the record is not opened in a session where no
    mod asks for a number.

### 2. The header: `native/include/luma_mods.h`

```c
#define LUMA_MODS_NAME "mods"
#define LUMA_MODS_IDS_NAME "mods.ids"
#define LUMA_MODS_INSTALL_NAME "mods.install"

typedef enum LumaModsKind { LUMA_MODS_LUMA = 0, LUMA_MODS_RIIVOLUTION = 1, LUMA_MODS_TEXTURES = 2, LUMA_MODS_FILES = 3 } LumaModsKind;
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
    LumaModsKind kind;   /* 4 bytes, a C enum on MSVC, GCC and Clang (G-SM5) */
    LumaModsState state;
    uint32_t has_code;   /* 1: code for this platform, 0: none */
    uint32_t reserved;   /* 0 */
    char id[68];         /* registry 3: NUL-terminated UTF-8, cut at a character's end (R-5c) */
    char name[132];
    char version[36];    /* luma.json `version`, "" when it has none */
} LumaModsInfo;          /* 1.0: 260 bytes */

typedef struct LumaModsService {
    LumaServiceTable base;
    /* 1.0. Game thread. The number of mods in the session's list. */
    uint32_t (*count)(const LumaMod* mod);
    /* 1.0. Game thread. The mod at `index` (0 the top). LUMA_OK; LUMA_E_NOT_FOUND past the end; a null out or a size
       below 1.0's: LUMA_E_ARGUMENT, reported (Service.Argument, Service.StructSize). */
    int32_t (*at)(const LumaMod* mod, uint32_t index, LumaModsInfo* out);
    /* 1.0. Game thread. The mod with id `id`, as `at`; LUMA_E_NOT_FOUND when none. */
    int32_t (*find)(const LumaMod* mod, const char* id, LumaModsInfo* out);
} LumaModsService;

/* The spaces of 1.0 (section 3). */
#define LUMA_MODS_SPACE_MESSAGE "message"
#define LUMA_MODS_SPACE_SENSOR "sensor"

typedef struct LumaModsIdsService {
    LumaServiceTable base;
    /* 1.0. Game thread. The number for `<the mod's id>/<name>` in `space`: LUMA_OK and *out; an unknown space or a name
       outside the rule: LUMA_E_ARGUMENT, reported; a full space: LUMA_E_LIMIT, reported (Service.Limit). */
    int32_t (*get)(const LumaMod* mod, const char* space, const char* name, uint32_t* out);
    /* 1.0. Game thread. Another mod's number, read only: LUMA_OK when `<mod_id>/<name>` is in the record (that mod
       asked for it in this or an earlier session), LUMA_E_NOT_FOUND when not. */
    int32_t (*of)(const LumaMod* mod, const char* space, const char* mod_id, const char* name, uint32_t* out);
} LumaModsIdsService;

/* Events of mods.install (LumaEvent.kind); `handle` is the request's. */
typedef enum LumaModsInstallEvent {
    LUMA_MODS_INSTALL_EVENT_DONE = 1 /* data: none; value: 0; status: LUMA_OK, LUMA_E_DENIED, LUMA_E_FORMAT, LUMA_E_EXISTS */
} LumaModsInstallEvent;

typedef struct LumaModsInstallService {
    LumaServiceTable base;
    /* 1.0. Game thread. As section 1 item 9. LUMA_PENDING and *out (the event never comes inside this call);
       LUMA_E_BUSY, LUMA_E_NOT_FOUND, LUMA_E_DENIED (don't ask again): answers, not reported; LUMA_E_ABSENT: no fs
       yet; a path outside the mod's data folder or not ending ".luma", a null fn or out: LUMA_E_ARGUMENT, reported. */
    int32_t (*request)(const LumaMod* mod, const char* path, LumaEventFn fn, void* user, LumaHandle* out);
    /* 1.0. Game thread. The screen closes as Not now when still open; no event after. LUMA_E_HANDLE, reported. */
    int32_t (*cancel)(const LumaMod* mod, LumaHandle handle);
} LumaModsInstallService;
```

### 3. The spaces of 1.0

| Space | The game's own | Mods' range | What a number is for |
|---|---|---|---|
| `message` | `ACTMES_*` 0x00-0xF3 (ActorSensorUtil.hpp), u32 in `receiveMessage` | 0x01000000-0x01FFFFFF | A message one mod's actors send and another's receive. Above every attack range: it reaches a receiver's `receiveOtherMsg`, never `receiveMsgPlayerAttack`/`EnemyAttack` (LiveActor::receiveMessage), so the game's actors ignore it. A new kind of attack the game's enemies react to is a hook on their receivers, not a number. |
| `sensor` | `ATYPE_*` 0x00-0x84 (ActorSensorUtil.hpp), `HitSensor::mType` | 0x01000000-0x01FFFFFF | A sensor type of a mod's actors that no game check (`isSensorPlayer`, `isSensorEnemy`, ...) takes for its own. Every such check is a bounded range, so SensorHitChecker::initGroup puts the sensor in the character group (its last `else`): it meets the player, ride, eye and map-object groups and other characters (SensorHitChecker::movement's pairs), as a character's sensor does. |

No `switch` space. Global stage switches (numbers 1000 up, StageSwitch.cpp) are game data: the game has
no by-number path (`SwitchIdInfo(s32, const TableIter&)` is its only constructor; `StageSwitchContainer::
mGlobalSwitches` is a ZoneSwitch), and a service for switches would be engine code knowing a concept. A mod reaches
them by the game's functions and fields by name (1.5); a missing row for `mGlobalSwitches` is 1.5's gap (F takes
it). The census of the global numbers the discs' stages use is still written for the docs (section 7), not for 1.0.

Not spaces at all, and where they are: event flags and save-flag names are named, saved by a 16-bit hash of the name
(GameEventFlagTable getSaveHash): a mod's own flags are `save`'s per-file values (services-save); effect and sound
names are content (3.5, 3.7) and assets (2.8); a state (a nerve) is an object, not a number; collision codes are
bits of a stage's collision data (3.9). A space is added to this table with its census row, never by a mod.

### 4. Implementation

- Host: `runtime/src/luma_mods.cpp` (the three providers, the record, the session list), provided by
  `luma_services_host.cpp`'s provide_all beside the system providers; the session list from `galaxy::mods` as started;
  `state` from `plugins_not_started()` when the list is made (L5xx records REFUSED, L800 and host sentences
  NOT_STARTED), never TAKEN_OUT.
- `void galaxy::mods::ask_install(const std::filesystem::path& file, std::string_view asker,
  std::function<void(int32_t status)> done)`, any thread; `done` exactly once, on the game thread, never inside
  ask_install, so `request` returns LUMA_PENDING first.
- H exports for the REMOVED MODS line: `void galaxy::mods::kept(std::vector<KeptMod>& out)` (per removed id: its
  numbers in the record, its bytes in the six user files' chunks in memory plus `luma/<id>.bin`) and `void
  galaxy::mods::forget(std::string_view id)` (any thread: the record's keys and the global file at once on the file
  thread; the chunks' entries in memory, written at each file's next save by the game, never a save of its own).
- `LumaServices.cpp` catalog: `mods` 1.0, `mods.ids` 1.0, `mods.install` 1.0, no permission each.
- fnv1a32 of the bytes of `space`, a 0 byte, then `key`; the remainder taken in unsigned 64-bit arithmetic.

### 5. Messages (codes 160-179)

| Code | Row | English | Level |
|---|---|---|---|
| 160 | `Mods.IdMoved` | {key} was given {number} in {space}; {other} already held its first choice. | Note |
| 161 | `Mods.IdsUnreadable` | The record of mods' numbers could not be read; it was kept as mod-ids.json.bad and a new one started. | Error |
| 162 | `Mods.Forgotten` | {mod}'s kept data was forgotten. | Note |
| 163 | `Mods.InstallAsked` | {mod} asked to install {file}. | Note |
| 164 | `Mods.TookOver` | {mod} took over what {old} kept. | Note |
| 165 | `Mods.OptionUnknown` | {mod} asked for an option it does not have: {key}. | Error |
| 166 | `Mods.OptionNotOwn` | {mod} asked for {key}, another mod's option; a mod reads only its own. | Error |
| 167 | `Mods.OptionBufferShort` | {mod} gave {size} bytes for its option {key}, which needs {needed}. | Error |

### 6. Tests (builds and runs: DECISIONS 7)

| Test | Must show |
|---|---|
| 2241 | `count` and `at` over three mods: order 1-3, ids, names, versions, kinds, has_code, state RUNNING |
| 2242 | `find` of a mod off and of one refused: OFF, REFUSED; an id not installed: LUMA_E_NOT_FOUND |
| 2243 | `at` with a size of 8: LUMA_E_ARGUMENT, Service.StructSize with 8 and 260 |
| 2244 | `ids.get("message", "breath")` twice: the same number, in range, equal to the derived one; the record written |
| 2245 | the next run with the record: the same number; with the record deleted: the same number again (derived) |
| 2246 | two keys whose derived numbers meet (names found by the test generator): the second gets the next number, Mods.IdMoved once |
| 2247 | a mod removed: its key kept in the record; another mod's ask never gets that number |
| 2248 | `ids.of` another mod's key: its number; a key never asked: LUMA_E_NOT_FOUND |
| 2249 | a bad space, a bad name: LUMA_E_ARGUMENT, reported |
| 2250 | an unreadable record: mod-ids.json.bad kept, Mods.IdsUnreadable, numbers still derived |
| 2251 | `install.request` without fs: LUMA_E_ABSENT |
| 2252 | a message number sent between two mods' actors arrives at the receiver's `receiveOtherMsg`; a game enemy given it does nothing; a sensor of a mod's number meets the player's sensor once (the actor's `attackSensor` called with the player's) |
| 2253 | `option` of each kind (choice, number, toggle, text, key) at the manifest's defaults: the texts of section 8; the length with a null buf |
| 2254 | the player changes a choice and a toggle on the Mods page (harness keys): two OPTION events, values 0 and 2, in luma.json order; `option` inside the event gives the new texts; after `watch_options(null)` a change gives none |
| 2255 | mistakes: an unknown key, "<other mod>/<key>", a short buf: NOT_FOUND, DENIED, TOO_LARGE, each its row, nothing written |
| 2256 | hostile text values typed on the page: 256 code points, every script the fonts carry, a NUL typed, control characters: `option` gives the page's text byte for byte |

### 7. Owed (none blocks 1.0)

- For the docs (section 3): the census of global switch numbers the three discs' stages use (every placement's
  SW_* fields at 1000 and up), by a script over the extracted stages (`lumalink extract`), its output in census/.
- J: the import screen's entry for a request from code (6.1), and the REMOVED MODS line.
- G: the self-description rows for the three services; whether LumaScript's `mods` reads `LumaModsInfo` as a record.

- G-SM1: `LumaModsInstallEvent` with `LUMA_MODS_INSTALL_EVENT_DONE = 1` (section 2), commented in the form.
- G-SM2: LUMA_E_CANCELLED dropped. Closing the screen without answering is the player's no (`LUMA_E_DENIED`);
  `cancel`, a mod's stop and the game's end send nothing (item 9).
- G-SM3: the sensor row names the character group and the pairs it meets; 2252 shows a hit with the player's sensor.
- G-SM4: item 6 says numbers after a meeting depend on asking order; co-op sends keys.
- G-SM5: `kind` and `state` typed `LumaModsKind` and `LumaModsState`; `has_code` stays `uint32_t` 0 or 1.
- J, all in: J-SM1 ask_install's exact form (section 4); J-SM2 LUMA_OK starts at the next start; J-SM3 BUSY across
  mods; J-SM4 DON'T ASK AGAIN (`install.ask`); J-SM5 copied, NOT_FOUND unreported, FORMAT when changed; J-SM6 no
  CANCELLED; J-SM7 one REMOVED MODS line, kept/forget calls, 162 `Mods.Forgotten`; J-SM8 IdMoved's English; J-SM9 the
  name in the player's language; J-SM10 the platform clause out; J-SM11 the file's name only; J-SM12 takes_over moves
  the keys (164 `Mods.TookOver`, shared with services-save).

- F-SM1:
- F-SM2: no `switch` space; switches are game data by name (1.5); the census stays for the docs (sections 3, 7).
- F-SM3: item 3 says `state` changes between frames only.

### As written

- The record is `<settings>/mod-ids.json`, not `<settings>/mods/ids.json`: the host's settings folder holds the mods
  folder as `mods/` (host_main set_mods_folder), so the record would have sat among the mods.
- `kept`, `forget`, `take_over_ids` and `set_save_half` are galaxy::luma_mods calls (luma_mods_service.h); the save
  half gives its byte count and its forget when services-save lands.

### 8. A mod's own options (replaces "a mod does not read its options")

1. `mods.option(mod, key, buf, size)`: the calling mod's own option, as text (header). Choice: the value id; number:
   shortest decimal as the Mods page lists it; toggle "1"/"0"; text: UTF-8 as typed; key: the control's name. A mod
   still reads no other mod's options (item 4): a key with "/" is another mod's and refused (Mods.OptionNotOwn).
2. `mods.watch_options(mod, fn, user)`: one OPTION event an option changed, value = its place in luma.json, at the
   next frame's start after the Mods page applied the change. Layers chosen by `when.options` still change at the next
   start (the layer set is fixed for a session); the event says the value changed, not that content moved.
3. Key options are input actions: registered with the manifest default, bound only in input's store; `option` asks
   that store (`binding_name`) and gives the default until 2.5 exists.
4. At most `max_length` code points; a stored value longer than that, or not UTF-8, gives the default and one Note.
5. Host: luma.cpp reads text and key options (kind, default, max_length) beside the choice kinds; the session keeps
   each running mod's values; the page calls `galaxy::luma_mods::options_applied(id)` after Apply, which compares and
   posts the events. Costs nothing with no watcher (registry 10).

### 9. Other mods stopping (for LumaKit rule 4)

`mods.watch_mods(mod, fn, user)` (mods 1.0): `fn` gets `LUMA_MODS_EVENT_STOPPED` (kind 2) at a frame's start each
time another mod stops while the watcher runs (switched off, removed, reloaded, or stopped by the guard), after its
stop and before its code is unloaded: `data` the stopped mod's id (UTF-8, NUL-ended, valid during the call),
`data_size` its length, `value` 0. Ids stopped in one frame come in order, one event each. A later call replaces fn
and user; a null fn ends the watch; the watcher's own stop ends it. LumaScript takes it as `on mods.stopped`.
Code: `runtime/src/luma_mods.cpp` (`mods_watch_mods`, `options_mod_gone`).

### 10. Kit data

Three calls at the end of `mods` 1.0's table (the project is at v1, so nothing is "added in" a later minor; the table
only grows at its end, registry 3).
- `kit_names(mod, of, module, buf, size, &length)`: the stems of `kit/<module>/<name>.json` in `of`, sorted byte for
  byte, each ended by a 0 byte; `kit_read(mod, of, module, name, buf, size, &length)`: that file's bytes;
  `needing(mod, out, capacity, &count)`: the running mods with code whose luma.json `needs` names `mod`, in the Mods
  list's order. LumaScript: `mods.kit_names(mod: u64, module: text) -> list<text>`, `mods.kit_read(mod: u64, module:
  text, name: text) -> bytes?`, `mods.needing() -> list<u64>`.
- `of` is a mod as `caller()` gives it: its LumaMod pointer as a u64. The kernel only compares it with its records, so
  any number is safe; one that is no running mod is refused like a mod that does not need the asker.
- Who may ask: `of` running, and `of` is the asking mod itself or needs it. Otherwise LUMA_E_DENIED and
  Mods.KitNotNeeded (168) once in the asker's log (the kernel counts repeats); LumaScript gives empty / none. Inside a
  library's export, services see the caller as the mod (lumascript-abi 6), so a module reading the data of `caller()`
  there reads that mod's own; at the library's own frame or start, it reads the mods `needing()` gives.
- `module` and `name`: 1 to 64 of `a-z 0-9 _ - .`, not starting with a dot, no `..`: no path part can be in them;
  another is LUMA_E_ARGUMENT, Service.Argument (a script error at the caller's line). A file whose stem is not such a
  name is not listed.
- Bytes come from the installed .luma's folder, or a folder mod's (the test harness runs folders), read at the first
  `kit_read` of that file and kept until `of` stops (mods' mod_gone; a reload is a stop). The index of mods (folder,
  needs) is made again after any mod stops.
- Sizes: a null buf with size 0 asks the length; a short buf is LUMA_E_TOO_LARGE with `*length` set (an answer;
  nothing written). A file past 4 MiB (`LUMA_MODS_KIT_FILE_MOST`) is LUMA_E_FORMAT and Mods.KitTooLarge (169). A
  missing file: LUMA_E_NOT_FOUND (an answer).
- No cost with no mod: nothing runs until a call; mod_begin keeps one pointer per mod with code.
- Rows 168 Mods.KitNotNeeded `{mod}: {where} asked about {other}, which does not need it; it got nothing.` and 169
  Mods.KitTooLarge `{mod}: {where} found {file} of {size} bytes, past the 4 MiB a kit file may have; it read
  nothing.`, both Error, 14 languages in NebulaText.tsv.

### 11. A caller's place in the list (lumakit 2.12 rule 7; h/mods-of on h/roll4)

`of_mod(mod, which, out)`, at the end of mods 1.0 (unreleased, so in place as section 10): `which` is a caller()
number (the LumaMod pointer as LumaScript holds it); *out is that mod's LumaModsInfo as `at` gives it, so
a library orders what it holds for each mod by `order`. The number is compared with the kernel's records only
(mod_id), never read through: 0, a number that is no mod, or a mod that has stopped is LUMA_E_NOT_FOUND, an answer.
A null out or a short struct: ARGUMENT / StructSize, reported. Game thread. Test 2253-of-mod.
