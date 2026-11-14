# How the game loads a mod

## More rules

### Loader

What the game's loader does with a mod's code library, what `luma.h` promises a library, and how a refusal is said.
`native/include/luma.h`, `native/petari/src/Game/System/LumaLoader.cpp`, `runtime/src/mod_plugins.cpp`), audited by G.
It gathers what earlier audits carried here: Z-2, Z-3, Z-9 to Z-12, the validator's L504/L506/L512/L805 inputs,
lumascript-abi 7.1/7.3 and language 16's requests, audit 4.1's A-2 and A-4.

### 1. Order: one rule

The Mods list, top first. The mod higher in the list wins a file, a cell and a replace (DECISIONS; table-keys 12;
validator r2 item 9). The loader therefore binds mods from the top of the list down:

- a function's `LUMA_REPLACE`: the first bound (the highest mod) holds it; a lower mod's replace of the same function
  is refused with L805, naming the higher mod; the lower mod stays off (its other hooks unbound: a mod binds whole
  or not at all, as today);
- `LUMA_BEFORE` hooks run in bind order: the higher mod's first;
- `LUMA_AFTER` hooks run last-bound first: the higher mod's last, so its result stands;
- `LUMA_WRAP`: the higher mod's wrap is outermost (it sees the call first and the result last);
- within one mod: befores in the order of its import rows, afters in the reverse order, wraps the first row outermost.

Today the base binds bottom-up (mods.cpp 1666, run 3e): 1.1 turns it, and run 3e's rep1/rep2 case is the test.

### 2. Binding

- `LumaModInfo.api` 0 is refused: "not built with luma.h" (L512, its `why` phrase `Lib.NotLumaH`). `api`
  greater than the game's `LUMA_API`: L508. `size` smaller than the struct as `api` defines it: L512.
- Import rows bind in order; the first that cannot bind refuses the mod with that row's code: L505 (no such function,
  field, class, size or hook target, the name and signature given), L512 (a malformed row; a wrap or replace on a
  constructor or destructor), L805 (section 1), L506 (a service the mod's permissions do not grant). `LUMA_OPTIONAL`
  rows that do not bind leave their slot null and refuse nothing.
- `LUMA_IMPORT_SERVICE = 5`: `name` the service ("net", "draw", ...), `signature` its `major.minor` as text,
  `slot` a `const void` that receives its table. Absent, or older than `minor`, or not granted: L506 unless
  optional. `LumaCore.service()` stays for a mod that asks after start.
- The most mods with code that may be on is one constant, `LUMA_MAX_MODS` in luma.h (256), and the refusal's words
  take it as a parameter.

### 3. Refusals are said once, translated

The loader produces (code, parameters), never English sentences: the same codes and templates as the
validator (section 5, the `Validator.L5xx`/`L805` rows). The game renders them through the
validator's own text (the rows are not in the game's table: nebula_text.py `NOT_THE_GAMES`), so request to G:
`luma_validate.h` gains `const char* luma_validate_message(uint16_t code, const char* const* names, const char* const*
values, uint32_t count, const char* language)` (the text valid until the next call from that thread). The log gets
the English line; the Mods page the player's language. The host adds no words of its own.

### 4. A hook's call

- The result before any hook runs. For a function with a result, the kernel value-initialises the result's
  storage before the chain runs (zero for scalars and plain structs; the default constructor for a class that has
  one). So a `before` that skips, or a wrap or replace that does not proceed, without writing the result, gives the
  caller a zero/default value, never uninitialised bytes. A function whose class result has no default constructor
  is marked in its row (`noskip`); a skipping before, a wrap or a replace on it is refused at bind (L512, `why`
  `Hook.NoDefaultResult`).
- `proceed` from a wrap runs the next wrap, then the function; from a replace, the function's own code. It is the
  only way a hook runs what is below it (not `invoke`).
- Threads. A hooked function's chain runs on whatever thread calls it. New at the end of
  `LumaCore`: `int32_t (*game_thread)(void)`, 1 when the caller is the game thread (one thread-local read). Compiled
  LumaScript hooks return at once off the game thread (logging once per hook); C hooks decide for themselves.
- Rule. The loader calls every other mod entry on the game thread only: `luma_mod_start`,
  `luma_mod_stop`, reload's stop and start, the frame callback, service events, a made class's constructor,
  destructor and virtuals, and the guard's switch-off. LumaScript's runtime (lumart pools, keep table) has no lock and
  relies on it; `lumart_on_game_thread` checks it. A hook is the one entry that can run elsewhere, and a compiled
  LumaScript hook leaves before it touches lumart. A new entry that must run off the game thread changes this rule
  first.
- `report` from any thread. `LumaCore.report` may be called from any thread: a mod's kept reports and
  their counts are under that mod's lock (LumaLoader record, two passes, the text copied outside it), and the line
  goes through the host's log, which takes lines from every thread. So a compiled hook off the game thread may report
  (code 901, once per place, repeats counted) and leave.
- `hook` and `unhook` (abi 7.3). New: `int32_t (*unhook)(const LumaMod*, const LumaFunction*, uint32_t
  kind, LumaHook, void* user)` (0 removed, -1 no such hook of that mod). Both are called on the game thread only
  (others: -3, nothing changes). A chain is never changed in place: the kernel builds the new chain and publishes it
  with one atomic pointer store; the old one is kept on a retire list and freed when the mod list changes (stop,
  reload) or the game ends, so a call already inside it on any thread finishes safely. Hook and unhook are rare (a
  handle table's destructor hook is added once); the retire list is counted in the Mods page's memory.

### 5. Memory for mods (lumascript-abi 7.1)

New at the end of `LumaCore`: `void* (*alloc)(const LumaMod* mod, uint64_t size, uint64_t align)` and
`void (*free)(const LumaMod* mod, void* p)`. Counted per mod (the Mods page shows it); null when out of memory (the
caller reports it); `align` a power of two up to 4096; everything a mod still holds is freed when it is taken out
(stop, reload). From any thread.

### 6. Script errors and the mod log (language 16)

New at the end of `LumaCore`: `void (*report)(const LumaMod* mod, const char* file, uint32_t line, uint32_t code,
const char* const* args, uint32_t arg_count)`. The kernel renders `Script.<code>` with the args, puts the line in the
mod log (3.12) and the game's log with `file:line`, and counts repeats of one (mod, file, line, code) instead of
repeating them.

### 8. What a mod's library may import (L504)

| Platform | Allowed imports | Evidence |
|---|---|---|
| Windows x64/arm64 | none | `luma.h` gives `memcpy`, `memset`, `memmove`, `memcmp` and the stack probe `__chkstk` (x64 and arm64) as its own functions, so a frame over 4 KiB imports nothing. To prove in 1.1's run: a mod with a 64 KiB local array, its import table read (must be empty) and the mod run |
| Linux, Android | none | `-nostdlib -ffreestanding`; the same functions from luma.h |
| macOS, iOS | none | read from the bytes on this machine: a `-nostdlib -dynamiclib` image linked by lld with `-undefined,error` has no dylib load command and no undefined symbol on macos-arm64, macos-x64 and ios-arm64, once `luma.h` gives `bzero` and `__bzero` (clang lowers zeroing to them on Apple targets). L504 is checked there as everywhere. Not yet loaded on a Mac (8.1 b) |

Today (f/build_mod.sh's flags): a mod whose `start` has a 64 KiB local buffer that escapes
 does not link: `lld-link: error: undefined symbol: __chkstk`. (A buffer the
compiler can shrink links, which is why small tests never showed it.)

`__chkstk` on x64 is a naked function in luma.h touching each 4 KiB page from the stack pointer down to the requested
size (rax), preserving every register but rax's use as the size; arm64's takes the size in x15 (in 16-byte units)
and returns it unchanged, as the MSVC ABI says. Both are emitted only where clang would call them.

Apple deployment targets for mod libraries: those of the game, `platform/apple/toolchain.cmake`: macOS 12.0, iOS
15.0 (LumaLink's build flags take them from here; "the game's CMake sets none": it does).

### 9. Fields and classes (carried, not 1.1)

Z-4 (a field's type checked against the mod's) is 1.2's; Z-7 (`is_a` on an object that is not a NameObj) is 1.3's;
Z-5 (the module's 38,697 exports) is 1.10's. Listed so nothing is lost.

### 10. luma.h's `LUMA_API` and size

Every addition above goes at the end of `LumaCore`; `LumaCore.size` grows; a mod built against an older luma.h reads
only what its size covers. This is LumaCore v1's first release (11-10: no "added in version N"), so the
additions are simply part of v1 and `LUMA_API` stays 1.

One conformance mod per item, each a recorded run with facts: order (rep1 above rep2 and below it: who holds the
replace, L805 naming the higher); befores/afters/wraps of two mods in both orders (the facts give the call order and
the result the caller received); a skipping before on a `TVec3f`-result function with no write (the caller gets 0,0,0);
`noskip` refused; api 0 refused; a service import present, absent, optional; alloc/free counts; report (a line in the
mod log, the repeat counted); unhook; a hook from a second thread (game_thread 0); the released row (handle `none`
after a stage change); a 64 KiB frame mod with no imports; every refusal's text in two languages on the Mods page.

- Section 7, the released row, exactly: told at the start of every `NebulaArena::destroy` call, with that arena's own
  reserved range `[start, end)`: a parent arena is told first, then its `ArenaOwned` objects' destructors run, then
  each child arena is destroyed and told in turn. A handle whose object lies in a told range is emptied; an object
  deleted one by one still comes through its destructor row. The harness's game half does the same today with a
  before-hook on `NebulaArena::destroy` and `NebulaArena::findFromRoot`; 1.1's row replaces that with the range, no
  per-object lookup.
- One name: `game_thread`.

- Section 4, Z-2, noSkip (changed). A bind cannot know whether a before will skip, so instead: (a) the kernel learns
  noSkip from the result type itself: each function's invoke answers when called with `luma::kAskNoSkip` as its
  arguments (`Result<R>::kNoSkip`); noSkip is a reference result (145 hookable rows), a class with no default
  constructor, or one with a destructor (the function's value is built over the storage, so a default there would
  never be destroyed); (b) a wrap or replace on a noSkip function is refused at bind (L512 `Hook.NoDefaultResult`);
  (c) a before that returns skip on one is not taken: the function runs, and the kernel says so once per (function,
  mod). Every other result is zeroed and then default-constructed before the chain runs (zeroing first: TVec3f's
  default constructor sets nothing, and section 11's test wants 0,0,0).
- Section 4, a call's chain (added). `LumaCall` gains `const void* chain` (the kernel's): a call runs the list it
  started with, so a hook added or taken out while it runs changes nothing for it.
- Section 4, the retire list. "freed when the mod list changes (stop, reload) or the game ends". A call on another
  thread (the sound thread's hooked functions) may be inside an old list at that moment. Part 1 frees at a mod's
  unload, as written; proposal: free only at the game's end and at a stage change where every other game thread is
  known to be idle, and count the list's size on the Mods page meanwhile.
- Section 7, the released row's name (changed). `LumaCore` is luma.h's C struct, so the row is
  `lumacore::released(const void *, const void *)`: an ordinary function row (before and after hooks), told at the
  start of `NebulaArena::destroy` with `mStart`, `mEnd`. It does nothing itself.
- Section 8, luma.h (added, proven to link). `LUMA_MOD` and `LUMA_MOD_NO_IMPORTS` define, once per library: `memcpy`,
  `memmove`, `memset`, `memcmp` (`no_builtin`, so they are not compiled into calls to themselves; hidden off Windows)
  and on Windows `__chkstk` (x64: rax, every register kept; arm64: x15 in 16-byte units).
- Sections 1 and 2, the order (as written).
- Still to write in 1.1: refusals as (code, parameters) (waits on `luma_validate_message`); `report` (waits on the
  `Script.<code>` rows); the conformance mods of section 11 and the run.

Each chain has `gameUsers` (plain, the game thread's calls in it); other threads count `sReaders[epoch & 1]` (seq_cst)
before reading a chain; the slot store is seq_cst. `luma::reclaim()` runs on the game thread each frame
(GameSystem::update, after lumascript::beginFrame) and at a mod's unload: up to two flips, each only when the counter
it leaves has drained; a retired chain is freed when two flips came after it was retired and its `gameUsers` is 0.
Nothing retired: one pointer test a frame. `luma::retiredCount()` is the fact LR-8's 600-frame test reads.
- LR-1, section 4 replaced. A function's invoke, asked with `luma::kAskNoSkip`, answers its result's kind: plain
  (zeroed, then default-constructed before the chain), reference (the slot is a pointer, null before the chain), or
  fixed (a class with no default constructor, or with a destructor). After the befores and the wraps, a reference
  result still null means no hook wrote it and none proceeded: the kernel calls the function itself, the caller gets
  its own result, and the kernel reports once per (function, mod) naming the hook's mod (the skipping before's, else
  the outermost wrap's) and the function. Only fixed results keep the bind refusal of a wrap or replace (L512, `why`
  `P:Hook.NoDefaultResult`), and a before's skip on one is not taken (reported once). The rows of each kind are
  counted as a fact in 1.1's run. "{mod}: a hook on {found} gave no result, so the function ran"; `Hook.SkipRefused`
  likewise for a fixed result).
- LR-2, section 2 (how "whole or not at all" is made). Bind is three passes on the game thread: (1) every row
  resolved; (2) every hook row checked against what `hook` would refuse (a constructor's wrap, a fixed result, a
  replace another mod holds, a kind out of range); (3) the slots filled and `start` run. Only when all three pass are
  the mod's hook rows published (one chain store per function). A refusal in any pass publishes nothing; hooks `start`
  added itself through `core->hook` are taken out and wait for the grace.
- LR-5, sections 4 and 5 (a mod's end). Stop is: unbind (chains without the mod published), the mod's `stop` (on the
  game thread; other threads may still be in its hooks until the grace ends, so `stop` frees nothing they read), then
  the grace (the retire list's: two flips after the unbind and no chain retired by it still held), then its C++
  globals' destructors (`LumaModInfo.finish`), then everything `alloc` gave it, then its library is closed. The kernel
  closes the library: `luma_load_mod` takes the library handle and the host's close function and calls it at once when
  nothing of the mod was ever published, else after the grace. The host never closes a LumaCore library itself.
  Refusals after `start` ran take the same path. `alloc` with `size` 0, `align` 0, not a power of two or over 4096
  returns null and logs one line naming the mod and the arguments; `free` of a pointer the mod does not hold (another
  mod's, freed twice, not from `alloc`: each block carries its mod and a check word) logs one line and does nothing.
  At the game's end nothing is unloaded (the process ends).
- LR-6, section 8 (C++ in a mod). Proven to link and run: a C++ file with a global with a constructor and destructor,
  a static local, a virtual destructor and `new`/`delete` links with no imports for Windows x64 and arm64 (MSVC
  target) and Android arm64 and x86_64 (NDK 28 clang, lld), and a loader on Windows shows its constructor not run at
  load, run by luma.h's runner, and `new`/`delete` working. How each goes:
  - `-shared -nostdlib -ffreestanding -fno-exceptions -fno-rtti -fno-threadsafe-statics -fno-stack-protector
    -fvisibility=hidden`, lld; Windows `/NOENTRY`; ELF `-fno-use-init-array -Wl,-T,luma_ctors.ld -Wl,-z,defs` where
    luma_ctors.ld (shipped with luma.h) is `SECTIONS { luma_ctors : { KEEP(*(SORT_BY_INIT_PRIORITY(.ctors.*)))
    KEEP(*(.ctors)) } } INSERT AFTER .data;`. Statics are set up on the game thread, so no thread-safe guards are
    needed.
  - Global constructors run after the core is known. The system loader runs none: ELF's go to `luma_ctors`, not
    `.init_array` (no `INIT_ARRAY` tag, checked); Windows has no CRT entry, so `.CRT$XCU` is run by nobody else.
    luma.h's runner (`LumaModInfo.begin`, filled by `LUMA_MOD`) stores the core and the mod for `new`, then runs them:
    Windows between its own `.CRT$XCA`/`.CRT$XCZ` markers, ELF from `__stop_luma_ctors` down to `__start_luma_ctors`.
    The kernel calls `begin` before `start`. A constructor may therefore call `new` and the core.
  - `new`/`delete` (all forms: array, sized, aligned, nothrow) and placement `new` are luma.h's, over
    `core->alloc`/`core->free`; luma.h declares `std::align_val_t`/`std::nothrow_t` only when `<new>` is not included.
    A throwing `new` that gets null logs the size and stops the game with a crash report naming the mod (there are no
    exceptions to throw); the nothrow forms return null.
  - `__cxa_atexit`/`__dso_handle` (ELF, Mach-O) and `atexit` (Windows) are luma.h's: a fixed list in the mod (256
    entries; one more is a refusal of the 257th registration, logged), run last-first by `LumaModInfo.finish` after
    the grace.
  - `LumaModInfo` gains `begin` and `finish` at its end (filled by `LUMA_MOD`; a mod built without them has `size`
    smaller and the kernel skips them). Apple: `__mod_init_func` is run by dyld at load; not checkable here (no Mac),
    and L504 stays "not checked" there until a Mac build exists.
  - Test: a C++ mod with `new`, a static local and a global with a destructor: no imports on the four targets, and in
    1.1's run the constructor's fact before `start` and the destructor's fact after its stop's grace.
- Section 3, the host's side. `luma_load_mod(info, mod_id, file, order, library, close, refusal)`: the refusal is
  `LumaRefusal { uint16_t code; uint32_t count; const char* names[8]; char values[8][256]; }` with the validator's
  parameter names and `T:`/`P:` values, the place as `mod` and `file`. The host renders it with
  `luma_validate_message` (English, the log) and keeps the struct for the Mods page (the player's language, its own
  call). On Windows the host loads `nebula_import.dll` (the crate's cdylib, beside the exe) for it; when it is
  missing, the log gets the code and every parameter as given, never nothing.

### , as built (not built)

- Build flags (section 8), final. These flags go with the ones above:
  - ELF: `-Wl,--version-script=luma_mod.map` (shipped beside luma.h: `{ global: luma_mod; local: *; }; `) and
    `-Wl,-Bsymbolic`. Off Windows, C++'s replaceable `operator new`/`delete` are visible by the language's rule and
    cannot be marked hidden. Without the script a mod exported eight of them. Without -Bsymbolic its own `new` could
    bind to the host's.
  - Checked on android arm64/x86_64: the dynamic symbols are `luma_mod` alone, no NEEDED, no INIT_ARRAY, FLAGS
    SYMBOLIC BIND_NOW, and the constructors are in `luma_ctors`.
  - luma.h's `new`/`delete` exist only where `__STDC_HOSTED__` is 0 (the mod's `-ffreestanding`). The game's own code
    includes luma.h for its types and keeps its C++ runtime.
- `LumaModInfo.size`: the kernel takes `size >= offsetof(LumaModInfo, begin)` (a mod built before `begin` and `finish`
  existed, such as the harness's luma-test.dll, binds and has neither). `LumaCall.ran` lies in what was padding, so
  the call's layout is unchanged.
- Refusal parameters, exactly. Every refusal carries `mod` and `file` (both T:). Each code adds:
  - L500: `why` P:Lib.TooManyMods, `count`
  - L508: `found`, `expected`
  - L512: `why` with
    - P:Lib.InfoUnreadable
    - P:Lib.NotLumaH
    - P:Lib.InfoTooSmall
    - P:Lib.ImportsUnreadable
    - P:Lib.ImportRowBad (plus `row`)
    - P:Lib.HookOnConstructor (plus `found`)
    - P:Hook.NoDefaultResult (plus `found`)
    - P:Lib.StartRefused (plus `found`, the number start returned)
  - L505: `kind`, `found`, `sig`
  - L805: `found`, `sig`, `other`
  - The first refusal found is the one given.
  - Rows G adds (14 languages): `Validator.Lib.NotLumaH`, `Validator.Lib.TooManyMods` ({count}),
    `Validator.Lib.StartRefused` ({found}), `Validator.Hook.NoDefaultResult` ({found}). The run-time lines (a
    reference no hook wrote, a skip not taken) go to the log in English until `report` and its rows exist.
- Loading off the game thread is the host's programming error, so it stops the game with a crash report
  (`NEBULA_PANIC`) rather than refusing.
- The frame point: `luma::frame()` (GameSystem::update, after lumascript::beginFrame). It runs `reclaim` and then ends
  the mods whose grace is over. With nothing retired and no mod waiting, it is one test.

### D3-1 `report` (section 6), exactly
- `LumaCore.report(mod, file, line, code, args, arg_count)` is at the end of LumaCore and can be called from any
  thread. The `code` picks a row:
  - 900 to 9999: `Script.S%04u` (lumart's, lumascript-abi), with args `{0}`, `{1}`, ... in order.
  - 1 to 99: the kernel's and luma.h's own rows, args by the row's names. 1 is `Hook.OutOfMemory`: args[0] is
    `{size}`, and the kernel fills `{mod}`.
  - Any other code is a bug in the caller: one log line naming the code, and it is counted.
- A missing arg shows as `{name}`. Args are UTF-8, copied, and at most 8 are kept; a ninth on is shown as `...`
  and counted, never dropped silently.
- Each distinct (file, line, code) of a mod is one entry: its args from the first time, and a count. The game's log
  gets one English line, `[mod <id>] <file>:<line>: <text>`, the first time and again at 10, 100, 1000, ...
  (" (seen N times)"). The entry list is kept per mod (at most 256; past that, one entry counts the rest) for the
  Mods page and the mod log (3.12, 6.2), which show it in the player's language. It is freed with the mod.
- The kernel's run-time lines (Hook.SkipRefused, Hook.NoResult) go through the same path with `{mod}`/`{found}`.
  Their `file` is the hooked function's name and their line is 0.
- luma.h: a throwing `new` with no memory calls `report(mod, "luma.h", 0, LUMA_REPORT_OUT_OF_MEMORY, {size})` and
  then traps. The trap stops the game with its crash report, as agreed.

### D3-2 Services (section 2), exactly
- The game keeps one table of services: name, major, minor, table, and permission (null when none is needed).
  Phase 2's services add themselves before any mod loads. Today the table is empty.
- An import of kind 5 (`LUMA_IMPORT_SERVICE`) gives `name` and `signature` "major.minor", and its slot receives
  the table:
  - no such service, a different major, or an older minor: L505 with `kind` `P:Name.Service`, `found` the
    name and `sig` the asked "major.minor". Request to G: the row `Validator.Name.Service` ("service").
  - a permissioned service the player has not granted: L506 {service} {permission}.
  - `LUMA_OPTIONAL`: the slot stays null and nothing is refused.
- `luma_load_mod` gains `const char* const* granted, uint32_t granted_count`: the permission names the player
  granted this mod. Until phase 6's consent screen the host grants none, so every permissioned service is refused
  by name, never allowed by omission. `LumaCore.service()` checks the same grants.

### D3-4 Small
- luma.h's `alloc` comment says "up to 65536". It said 4096.
- Section 11 gains the tests:
  - a report of each kind, repeats counted (1, 10, 100), args cut at 8 and named;
  - a service import refused for being absent and for having no grant;
  - kind 6 bound, refused for being absent and for its signature;
  - the needs order with the list order kept for hooks;
  - dependent_gone before the library's grace.

- D3-2, services come from the host's finder (0.3's `luma_set_service_finder`), not from a table in the game.
  - The finder now also gives each service's permission: `(name, major, minor, const char permission)`, null when none
    is needed. The harness's service needs none.
  - `LumaCore.service` takes the mod first: `service(mod, name, major, minor)`, so it can check the grants.
    luma_test.c passes its mod. Nothing is released, so the change costs only a rebuild of the harness's DLL, which
    the 1.1 build does anyway.
- D3-1, the kernel's codes are fixed:
  - 1 Hook.OutOfMemory {size};
  - 2 Hook.NoResult {found};
  - 3 Hook.SkipRefused {found};
  - 4 Hook.ServiceNotGranted {service} {permission}: a service() call for a permission not granted. Row request to G
    (14 languages): "{mod} may not use {service}: the \"{permission}\" permission was not granted".
  - For codes 2 and 3, {found} is "<function> <signature>", and the line has no file:line part.
- T-2 confirmed: finishMod runs `finish` only for a mod whose `begin` ran (`begun`). A refusal in the bind passes runs
  none, and a refusal in `start` runs it.
- Until its row table is regenerated, the game's log shows the row's id with every arg. The stand-in core showed
  `report luma.h:0 code 1 n 1 arg0 549755813888` from a failed `new`, then the trap.

Run 18 (RUNS) proves the tests named below; nothing else here is claimed as run.

- luma.h. `LumaExport { const char* name; const char* signature; const void* address; }`. `LumaModInfo` ends with
  `exports`, `export_count`, `dependent_gone`, after `finish`. `LUMA_MOD` and `LUMA_MOD_NO_IMPORTS` write 0 for all
  three, so a mod that exports nothing is built as before with a larger `size`. New macros:
  `LUMA_MOD_FUNCTION(slot, "<mod id>/<export>", signature)` (a kind 6 row, slot `const void*`),
  `LUMA_EXPORT_FUNCTION(name, signature, fn)` (one export row) and
  `LUMA_LIBRARY_MOD(imports, start, stop, exports, dependent_gone)`. The C form of an exported function (`caller`,
  `file`, `line` first, abi item 4) is the library's ABI; the kernel only binds the address.
- Binding (kernel). The name splits at its first `/`. The mod is found among the loaded mods by its whole id, the
  export by its whole name read in order (the validator checks the sort), then the two signatures are compared as
  strings. No such mod loaded, a mod built before `exports`, or no such export: L505 `kind` P:Name.ModFunction (1125).
  Another signature: L505 P:Name.FunctionSig (1126). `LUMA_OPTIONAL`: the mod starts and the slot is null (1127). Pass
  1 checks every row before pass 3 writes any slot, as for the game's functions.
- Who is told. A mod keeps the mods it bound from, each once, in bind order. `dependent_gone(mod)` is called on each
  of them, the last bound first, on the game thread: when the mod is taken out (its hooks go, its `stop` runs, then
  they are told, then the grace, finish, memory, library), and when its `start` refuses after binding (its hooks go,
  they are told, then the same end; it never ran `stop`). 1124: the library is told once, with the dependent's own
  LumaMod (`gone_remembered`), and the refusal reads "its start refused to run (it returned 7)". A mod built before
  `dependent_gone`, or one that leaves it 0, is not called.
- Taking out a needed mod first is a panic naming both mods: "LumaCore: <lib> was taken out while <mod>, which uses
  it, is on". Not run: it needs a harness step that stops a mod (1.11, with the reload step).
- The host (runtime/src/mod_plugins.cpp).
  - `PluginFile` carries the manifest's `needs` ids (mods.cpp).
  - Start order: the first mod in list order whose listed needed mods have all started goes next, again from the top.
    With no `needs` this is the list order exactly. A need on the mod itself orders nothing. `index` stays the list
    position, so hooks follow the list (1122: trace 12, 1123: trace 21; `lt.ready` reads 1 in both, so the library
    started first either way).
  - A needed id that is not in the list is the host's existing missing-mod line (mods.cpp), "lt-uses: THIS MOD NEEDS:
    lt-lib", and the mod is not started (1128).
  - A mod left unplaced is in a cycle, or needs one in a cycle. One in a cycle gets L800 through the validator's text:
    `mod`, `file` luma.json, `other` the next mod in the cycle, `expected` empty, `found` P:Need.Cycle, `chain` the
    shortest cycle from the mod back to it, breadth first as luma-validate's need_cycle ("a -> b -> a"). 1129: each of
    the two is named with its own chain. A mod not in a cycle but needing one gets "<mod> (<file>) not started: it
    needs <id>, which did not start".
  - Not built yet: the host never takes a mod out. No path calls `luma_unload_mod` today. The stop order
    (dependents first, the reverse of the start order) comes with 1.11's reload, which adds the step.
- Refusals' words. `refusal_text` asks the importer's `luma_validate_message` (nebula_import.dll beside the game,
  loaded from the application folder and System32) in the game's language: the renderer hosts set it from the
  language setting (`set_message_language`); a headless host has none and keeps UsEnglish. If the library is missing
  or lacks the function, the log says once "refusals are given as codes: <why>" and each refusal is its code and
  values; the runner fails any test whose log has that line. The host's own lines ("not started: ...") are the
  log's English. The Mods page's refusal text in the player's language, with pictures, is 1.1's open item.
