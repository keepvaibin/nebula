# Service: files

## More rules

### Services-fs (written with its code, R1)

Code: Header `native/include/luma_fs.h` (this page says what it decides and why). Provider `runtime/src/luma_fs.cpp`,
host calls `runtime/include/galaxy/luma_fs_service.h`.

### 1. What is decided here, in one list

1. Two services, one provider: `fs` 1.0 (no permission) and `fs.exchange` 1.0 (permission `fs.exchange`, registry
   5.1's row as it stands). Catalog rows in LumaServices.cpp after `save.game`.
2. Three folders:
   - data: `<Nebula's data folder>/mod-data/<id>`, the mod's own, read and written (`fs.read`, `write`,
     `remove`, `move`, `list`). Made at the first write. Nebula's data folder is `platform_paths.h`'s
     (`--data`, else LOCALAPPDATA/Library/XDG); a test run's is its own.
   - package: the mod's installed folder in the Mods folder, read only (`read_package`, `list_package`). Found
     at the mod's first package request (one copy of the session's list then; none for a mod that never asks).
   - Exchange: `<Nebula's data folder>/Exchange`, shared by the player and every mod granted `fs.exchange`.
     A file there may change or go between two requests.
3. Path rule (header top): relative, '/' between parts, 1-255 bytes, at most 16 parts of 1-64 bytes of ASCII
   letters, digits, space, `-`, `_`, `.`; no part starting or ending with space or `.` (so no `.`, `..`, hidden
   names); no Windows device name (CON, PRN, AUX, NUL, alone or before a dot, any case); no
   `.tmp.<digits and dots>` (atomic_file.cpp's half-written siblings). The same rule on every platform, so a mod
   that works on one works on all. Non-ASCII names are left for a later minor (normalisation differs by system).
4. Letter case never makes two names, on every system: a part that misses exactly is looked up by a scan of
   its folder up to ASCII case where the system keeps case (Linux, Android, a case-sensitive Apple volume); on
   Windows a miss is a miss of every case. A write onto an existing file keeps the case it has; a move that changes
   only the case renames it.
5. No link is followed: each part is `symlink_status`'d; a link, a junction (MSVC `file_type::junction`) or any
   other kind is LUMA_E_DENIED. remove of a folder removes a link inside as itself. The root folders themselves may
   be links (a player who moved the data folder).
6. Requests: every call is a request (registry 1 item 6: no call blocks): checked on the game thread
   (mistakes reported at once), done on one worker thread in the order made, answered at a frame's start, closed
   after its event. A write is copied before the call returns. So write-then-read in one frame reads the write.
   The worker starts at the first request (registry 10); none runs with no mod using fs.
7. cancel: no event after it. A read or list not begun is skipped; a write, remove or move keeps its place in
   the order and happens (else a later read in the same order could see either).
8. A mod gone: nothing is posted for it; its queued writes, removes and moves still land, in order.
9. Writes are whole and atomic: `galaxy::io::write_file_atomically` (sibling, flush, replace). No append in
   1.0 (a recorder writes whole tracks).
10. Limits: 32 open requests a mod over both services (LUMA_E_LIMIT, Service.Limit "32"); 64 MiB a file
    (LUMA_E_TOO_LARGE at once, Service.Limit "67108864"; a larger file on disk reads TOO_LARGE); 256 MiB a data
    folder, 1 GiB the Exchange folder, measured at each write without the file's old bytes (the player may change
    a folder meanwhile): an event with LUMA_E_LIMIT, said on the Mods page once a session per mod and folder
    (Service.Limit "256 MiB" / "1 GiB"). A list holds at most 65536 lines and 1 MiB (TOO_LARGE).
11. list gives text, one line a file or folder with `\n`, a folder's ending in '/', paths from the asked
    folder, sorted by bytes (so a folder's line comes before its contents), names outside the rule left out, links
    left out and not entered. Text, not structs, so LumaScript's `fs.list(...) -> text` is the take as D12.5 says.
12. LUMA_E_ABSENT (an answer): no data folder on this platform, or no installed folder for a package request.
13. No rows of its own: only the registry's 100-104. No Note: the Exchange folder is the permission's, and the
    data folder is the mod's own.

### 2. LumaScript

`use svc.fs`: `fs.read(path) -> pending<bytes?>`, `fs.write(path, data) -> pending<u64?>`, `fs.remove`,
`fs.move`, `fs.list(path, recursive) -> pending<text?>`, `fs.read_package`, `fs.list_package`; `use
svc.fs.exchange` the same over the Exchange folder. Each request's comment says "closed after its event", and the
takes say "inside the READ/LIST event's call" (D12.5). lumascript-programs' ghost example reads as written once its
calls are waited (`wait fs.read("bests.json")`); its `recursive: true` is the u32 parameter as a bool (G to say how
D12 maps a 0/1 u32; if it does not, the header's `recursive` stays u32 and the example passes 1).

### 3. What other services use

- `save.game`'s offer and `mods.install` read a file the mod names in its data folder: `find_data_file(id, path,
  &out)` (worker or other non-game thread), `path_in_rule` at the call. `mods.install` checks its path with
  `path_in_rule` plus a ".luma" ending. Both call find_data_file on the game thread (h/6.1-wire,
  h/2271-offer): a few metadata reads for a request the player answers on a screen that pauses the game; offer
  then reads the file (at most 4 MiB) there too.
- REMOVED MODS (services-mods 8), wired: `data_ids` adds a mod with only a data folder, `has_data_folder`
  and `data_bytes(id)` fill the line (`KeptMod.data_bytes`), FORGET calls `forget_data(id)` (queued on the worker
  after every earlier request). `kept()` may run on any thread.
- No exception reaches a mod: each request function is noexcept and answers LUMA_E_MEMORY if an allocation fails;
  a request that cannot be queued is answered LUMA_E_MEMORY through its event.
- The harness capabilities `services.fs` and `services.fs.exchange` are in tools/lumacore/capabilities.json.

### 4. Platforms

- Windows: as written, tested (2301-2312).
- Linux, macOS, iOS: the same code (std::filesystem); the case scan is compiled in. 8.1 runs the suite there.
- Android: the data folder is the app's private files folder, so the Exchange folder is not visible to the player
  there; 8.2 gives it through the system's document picker (a provider over `<data>/Exchange`). Until then Exchange
  works between mods only on Android.
- A player-visible "Open Exchange folder" on the Mods page:

### 6. Tests (deterministic mode)

| Test | Proves |
|---|---|
| 2301 | write twice in two cases (one file), read through a third case, take length-first, second take HANDLE |
| 2302 | list text, recursion, byte order, case kept, case-only move |
| 2303 | move, EXISTS onto a folder, NOT_FOUND old path, remove empty folder, read/write a folder, empty file |
| 2304 | remove "" counts files, list "" empty, NOT_FOUND cases, event count |
| 2305 | 24 path-rule and argument mistakes, 64 MiB + 1 TOO_LARGE, 16 parts taken, cancel/take HANDLE, each reported |
| 2306 | 33rd request LIMIT 32 |
| 2307 | cancelled write still lands, cancelled read no event |
| 2308 | read_package / list_package, data folder separate from package |
| 2309 | Exchange write/read/list, remove "" refused, remove a folder |
| 2310, 2310b | fs.exchange declined (consent class) / undeclared L506 |
| 2311 | data folders are per mod, Exchange is shared |
| 2312 | 256 MiB full LIMIT said once, remove "" |

Run hf1d: 12/13; 2310 is the consent class. Tip c8f5652d5 adds the later
commits above; they change no answer the tests check.
