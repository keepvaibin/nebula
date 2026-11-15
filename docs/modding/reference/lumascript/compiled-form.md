---
title: The compiled form
shortTitle: The compiled form
intro: What LumaLink builds from a mod's LumaScript, in detail.
contentType: reference
---

## About the compiled form

LumaLink compiles LumaScript to C, then to machine code in your mod's own library. This page describes the C that LumaLink writes, the run-time support linked into the mod, and the run-time error codes. You do not need it to write a mod. It helps when you read generated code or mix LumaScript with C.

## The build

| Input | Where from |
|---|---|
| `source/**/*.lum`, `source/**/*.c`, `.cpp`, `.h` | the mod |
| `luma.json` (`id`, `options`, `permissions`, `code.platforms`) | the mod |
| the self-description of each game version in `luma.json`'s `nebula` range | carried by LumaLink, one per released game version; the newest also downloadable as a file the player names (never fetched silently) |

1. Front end: parse, check types and names against every self-description in range; all errors listed (S-codes).
2. Back end: one C file per module `lum/<module>.c` with `#line "<file>.lum" <n>` markers, `lum/imports.c`
  , `lum/mod.c` (`luma_mod`, `start`, `stop`). The C is C17, no compiler extensions but those named
   here, so any clang of the pinned version compiles it the same way.
3. Each platform of `code.platforms` is compiled and linked with LumaLink's own clang and lld (one pinned LLVM
   version per LumaLink release; LLVM 22.1.6 now, the version `crates/luma-validate/tests/data/build.sh` uses), with
   the run-time support and the mod's C sources, into `code/<platform>/mod.<ending>`.
4. The validator runs on the result before packing; a finding stops the pack, as for any mod.

## Targets and flags

Common: `-std=c17 -O2 -nostdlib -ffreestanding -fno-stack-protector -fvisibility=hidden -fno-builtin-malloc
-fno-common -ffile-prefix-map=<mod folder>=. -Wall -Werror=implicit-function-declaration`, and the platform's below.
Only `luma_mod` is exported (`LUMA_EXPORT`); every other symbol is hidden.

| Platform | Target | Link |
|---|---|---|
| `windows-x64` | `x86_64-pc-windows-msvc` | lld-link `/DLL /NOENTRY /NODEFAULTLIB /NOIMPLIB /Brepro` |
| `windows-arm64` | `aarch64-pc-windows-msvc` | the same |
| `linux-x64`, `linux-arm64` | `x86_64-` / `aarch64-unknown-linux-gnu`, `-fPIC` | ld.lld `-shared --build-id=none -z pack-relative-relocs` |
| `android-arm64`, `android-x64` | `aarch64-` / `x86_64-linux-android31` (the game's `minSdk = 31`), `-fPIC` | ld.lld `-shared --build-id=none -z max-page-size=16384` |
| `macos-arm64`, `macos-x64` | `arm64-` / `x86_64-apple-macos<the game's deployment target>` | ld64.lld `-dylib -undefined error -no_uuid` |
| `ios-arm64` | `arm64-apple-ios<the game's deployment target>` | the same; the device signs it when the mod is installed |

- The libraries import nothing but what `luma.h` and the loader (lumalink/loader.md) allow (the compiler's `memcpy`,
  `memset`, `memmove` and, on the MSVC targets, the stack probe, all given by `luma.h`'s freestanding helpers; F,
  1.1). A LumaScript mod with C sources is held to the same: its C may not call the C library either (no `malloc`,
  no `printf`); it has the run-time support's functions and `luma.h`.
- The Apple deployment targets are 1.1's to state from the game's build (the game's CMake does not set one today);
  until then this row is "not settled", and LumaLink refuses those platforms naming why.

## Reproducible

The same sources, the same LumaLink release and the same self-descriptions give byte-identical libraries on every host
system LumaLink runs on: fixed flags above, no time, no path, no UUID, no build id, `/Brepro`; generated C in a
fixed order (modules by name, functions in source order).

## Names in the generated C

| LumaScript | C name |
|---|---|
| function `f` of module `m` | `lm_m__f` (a `__` cannot occur in a LumaScript name) |
| struct `S`, enum `E` of module `m` | `lm_m__S`, `lm_m__E` |
| a hook | `lm_hook_<n>` (n: the hook's place in the module, from 0) |
| a task | `lm_task_<n>` and its frame struct `lm_task_<n>_frame` |
| `export c fn f` | `f`, hidden visibility: callable from the mod's own C, never from the game |
| run-time support | `lumart_*` |

## The import list (`imports.c`)

Every game name the script uses is one `LumaImport` row, built by the compiler, never written by hand:

| Script use | Row |
|---|---|
| a call `MR::getPlayerPos` | `LUMA_IMPORT_FUNCTION`, name and signature from the self-description, slot `static const LumaFunction* lm_fn_<n>` |
| a field `self.mVelocity` | `LUMA_IMPORT_FIELD` `"LiveActor.mVelocity"` with its type text; slot `int32_t lm_off_<n>` |
| a class in a test `is_a`, a `ref<C>`, a `class X : C` | `LUMA_IMPORT_CLASS`; slot `int32_t lm_class_<n>` |
| a size (`class X : C`, game structs by value) | `LUMA_IMPORT_SIZE` |
| `hook` / `wrap` / `replace` | `LUMA_IMPORT_HOOK` with kind 0 / 2 / 3; `after` is kind 1 |
| `optional MR::foo` | the row with `LUMA_OPTIONAL`; a null slot is `none` to the script |
| a service `use svc.net.local` | a `LUMA_IMPORT_SERVICE` row (`LUMA_OPTIONAL` with `use optional`) |

Rows are sorted (name, kind); one name used many times is one row. The overload a script call chooses
 is the row's signature, so the loader and the validator bind exactly it.

## Hooks

Each hook form becomes one `LumaHook` (`int32_t (LumaCall* call, void* user)`) and one row. The arguments the
script names are `LUMA_ARG(call, n, T)` (n from 1; `call->args[0]` is `this`, `self` to the script), the result
`LUMA_RESULT(call, T)`.

| Script | Emitted |
|---|---|
| `hook before F(...) { ... }` | kind `LUMA_BEFORE`; returns 0, or after `skip v` writes `v` to `call->result` and returns 1 |
| `hook after F(...) -> r { ... }` | kind `LUMA_AFTER`; `r` is read from `call->result`, `result = x` writes it back |
| `wrap F(...) { ... proceed ... }` | kind `LUMA_WRAP`; `proceed` is `core->proceed(call)`; a written argument is written through `call->args[n]` before it |
| `replace F(...) { ... }` | kind `LUMA_REPLACE`; never proceeds; the body writes `call->result` |
| `hook ... every override of C::f` | one row per override the self-description lists, each with the same hook |
| a hook in `class X : C` | the row on `C::f`; the hook starts with `if (!core->is_a(self, lm_class_X)) return 0;` (a wrap proceeds) |

- Order within one mod. The kernel runs `before` hooks in the order added and `after` hooks the last added first
  (luma.h). The language says a mod's hooks run in the order written. So all of one mod's hooks of one
  kind on one function are one `LumaHook` that runs the bodies in written order: one row per (function, kind). Several
  `wrap`s of one mod on one function are one wrap whose body k's `proceed` runs body k+1, the last one's calling
  `core->proceed`.
- A hook calling its own function: each (function, mod) pair the mod hooks has one flag `lm_inside_<n>` in the mod. A
  hook sets it on entry and clears it on every way out; a hook entered while its flag is set returns 0 at once (a wrap
  calls `core->proceed(call)` first). So a call the mod makes from inside its own hook on the same function runs other
  mods' hooks and the game's code, not its own again.
- A result of pointer type written by `after`: the hook has a `static` slot of the pointed type; `result = v` stores
  `v` in it and writes the slot's address to `call->result`.
- Constructors and destructors: before and after only (luma.h; validator L512 refuses a wrap); `call->result` is null.
- A script error inside a hook: the hook's body returns early through the status path: a
  `before` returns 0 (no skip), an `after` leaves the result as the game made it, a wrap that has not proceeded calls
  `core->proceed(call)`, a `replace` that has not written its result writes the game's: it calls
  `call->function->invoke` with the mod's flag for that function set (so it does not re-enter itself). A non-void
  `replace` with no result to give is a compile error ("a replace must give a result on every path").

## Values across the boundary

| Language type (language 3, 7.2) | C in the generated code | To or from the game |
|---|---|---|
| `bool` | `uint8_t` | the game's `bool` (1 byte on every target of 1.1.1) |
| integers, `f32`, `f64` | `int8_t` ... `uint64_t`, `float`, `double` | the same |
| `vec2 vec3 vec4 quat` | `struct { float x, y(, z(, w)); }` | `TVec3f` / `Vec3f` by value or `const &`: the address of a copy; a `Vec3f *` result: copied when the call returns |
| `mat34` | `float[3][4]` | `MtxPtr`, `const TPos3f &` |
| `color` | `struct { float r, g, b, a; }` | not a game type; converted by the service that takes it |
| `text` | `lumart_text` (pointer to a counted, NUL-ended UTF-8 buffer) | `const char *`: the buffer's bytes for the call; a `const char *` result: copied into a new `text` |
| `bytes`, containers | `lumart_bytes`, `lumart_list` ... (pointer to counted storage, copy on write) | do not cross to the game |
| `T?` | `struct { uint8_t some; T value; }`; a view's `none` is a null pointer | a game pointer that may be null |
| a view of a game class | `void *` | the object's address; fields at `base + lm_off_<n>` |
| `ref<C>` | `lumart_ref` | never crosses; `get` gives a view |
| tuples, structs, enums of the mod | C structs and tagged unions in source order | `c struct` only |

A game function is called through its slot: the compiler fills `void* args[1 + n]` with `this` (or null) and the
address of each argument (a copy for values), calls `lm_fn_<n>->invoke(args, &result)`, and converts the result.
Hooks on the function run, as for any caller.

## Script errors

- Every generated function that can fail returns `int32_t` (0 ok, 1 a script error was raised) and gives its value
  through a last pointer parameter. A failing operation (index out of range, an empty handle, integer overflow,
  division by zero, a `!` on `none`) calls `lumart_error(file_id, line, code, args...)`, which records it, and
  returns 1; each caller returns 1 at once. No `setjmp`/`longjmp` (no unwinding past game frames, nothing
  platform-specific), no C++ exceptions.
- At the boundary (a hook, an event handler, a task step, `on start`), a 1 ends that run as described above, and the
  run-time support reports it once per frame per (file, line, code): the mod, `file.lum:line`, the text in the
  player's language. The report goes to the mod log through the kernel's report call;
  until it exists, through `core->log(mod, text)` with the same text, and the mod log reads it from there.
- Overflow checks use clang's `__builtin_*_overflow` (in the pinned compiler on every target; the one compiler
  builtin the generated C uses, besides `__builtin_trap`).

## The run-time support (`lumart`)

One C17 source set in LumaLink, compiled with each mod for each platform (so a mod carries its own copy and never
depends on another mod's), freestanding: no C library, no OS call, nothing imported.

## Memory

- Where it comes from: LumaCore has no allocator today, and a library that imports nothing cannot reach the
  system's. LumaCore gives `void* (*alloc)(const LumaMod* mod, uint64_t size, uint64_t align)` and
  `void (*free)(const LumaMod* mod, void* p)` at the end of `LumaCore` (its `size` field makes the addition safe),
  counted per mod so the Mods screen can show each mod's memory and the kernel frees what is left when a mod is
  taken out. Until then a LumaScript mod cannot be built: the compiler refuses with
  "the game gives no memory to mods yet (lumascript-abi 7.1)".
- `lumart` keeps its own small-block pools on top (sizes 16 to 512 bytes in steps), taking 64 KiB pages from
  `alloc`; larger blocks go straight to `alloc`.
- Out of memory (`alloc` returns null) is a script error ("out of memory: <n> bytes") at the line that asked;
  inside `lumart` with no line to give (growing the scratch) it is reported against the hook or task
  running, and if even the report cannot be made, `__builtin_trap`: a fault, which the guard catches and
  turns into "the mod was switched off", naming it.
- Counted values (text, bytes, containers): a count in the block; copy on write; no cycles can be
  made, so no collector.

## Scratch

A per-mod buffer (64 KiB to start, doubled when needed, never shrunk in a session) for text handed to the game for
one call, emptied by `lumart` at the start of the mod's frame handler.

As built: emptied sooner than the frame. Chunks (64 KiB, then each twice the one before or as large as one text needs)
are kept until the mod stops (`lumart_scratch_free_all` in its stop); uses nest as a stack: `lumart_scratch_mark`
before a use, `lumart_scratch_release` once the text is used (logged, copied into its own block, or after the game
call it went to), and every generated function that uses the scratch gives back to its own mark on its way out and at
a task's park, so a script error between loses nothing and a mod with no frame handler never grows it. A builder being
written is the top; one left below bytes a script error kept moves past them. Uses: every log and fact line, an
interpolated text before its copy (one allocation, not two), and a `const char *` C-6 marks `in` (`FnTy::transient`:
that text is never kept).

## Handles (`ref<C>`)

- `lumart_ref` is `{ uint32_t slot; uint32_t generation; }` into the mod's handle table (slot -> object address,
  generation). `ref(view)` finds or adds the object's slot.
- An object's death is learnt from the destructor rows (`LUMA_TELL` on `NameObj::~NameObj` and every derived
  destructor): the mod's library adds a `before` hook on `NameObj::~NameObj` only while its table holds a slot
  (added with `core->hook` when the first `ref` is made, the rule "no cost when a mod holds no handle"), which clears
  the object's slot and bumps its generation. Removing a hook after loading needs a call luma.h does not have today,
  `int32_t (*unhook)(const LumaMod*, const LumaFunction*, uint32_t kind, LumaHook, void* user)`; until
  then the hook stays once added and returns at once when the table is empty.
- `get` on an empty handle gives `none`; `h!.get` on one is a script error.

## Tasks (`task`, `wait`, states)

- A task is a frame struct (its arguments, the locals that live across a `wait`, the step number) and a step
  function `int32_t lm_task_<n>(lm_task_<n>_frame*, uint8_t* done)`: a `switch` on the step at the top, each `wait`
  storing the next step and returning. No stack is kept, no assembly, no thread: the same C on every platform,
  iOS included.
- `lumart` keeps the mod's live tasks in a list and runs each from the mod's frame handler (service `time`'s frame
  event, after `on frame`), in the order started. `wait N frames` counts frames; `wait N seconds` reads `time`'s game
  time; `wait until` checks each frame; `wait <pending>` checks the service call's state each frame and takes its
  result when ready.
- `states` compile to the same form: a state is a step range; `go` sets the step.

## Start and stop

`luma_mod.start(core, mod)`: stores `core` and `mod`; for each service the script uses, `core->service(name,
major, minor)` (until 1.1's import rows), a null result returning non-zero after `core->log` names the service and
the version wanted; runs the module-level `let`s and every `on start` in module order. A script error in `on start`
returns non-zero (the mod stays off, the log line names the line). `stop`: stops every task, removes every hook it
added with `core->hook` (1.1's `unhook`), releases every value (the kernel then frees what `alloc` gave, 7.1).

## C in the same mod

The mod's C and C++ sources are compiled with the same flags (C++: `-std=c++20 -fno-exceptions -fno-rtti`, no
standard library), linked into the same library. They see `luma.h`, `lumart.h` (text and bytes as `(const char*,
uint64_t)`, `lumart_ref_get`, `lumart_alloc`) and the header the compiler writes for every `export c fn`. A fault in
that C is the guard's, not a script error.

## Run-time error codes

Section 6 said only that the codes are lumart's; S0901 was the one named. This is the list. Each is a row
`Script.<code>` in NebulaText.tsv in 14 languages, which the kernel's `report(mod, file,
line, code, args, arg_count)` renders. The kernel adds the mod and `file.lum:line`; the row is only what went wrong.

Arguments are `const char*`, positional: `{0}`, `{1}`... in the row. Every argument is code or a value:
a name as the source spells it, a number, a type name, an expression's source text, or a condition in code form.
None is English prose, so nothing a mod or LumaKit passes needs translating. A row's arguments may appear in a
different order in another language (Japanese puts S0904's `{1}` first).

| Code | Raised when | Arguments | English |
|---|---|---|---|
| S0901 | a hook was called off the game thread | hook name | `{0} was called off the game thread, so this hook did not run there` |
| S0902 | `list[i]` out of range | index, length | `index {0} is outside a list of {1}` |
| S0903 | `h!` or a call through an empty `ref<C>` | class name | `the handle to {0} is empty` |
| S0904 | integer overflow (`__builtin_*_overflow`) | type, expression source | `{0} overflow in {1}` |
| S0905 | `/` or `%` by zero | expression source | `division by zero in {0}` |
| S0906 | `!` on `none` | expression source | `{0} is none` |
| S0907 | `map[k]` with no key `k` | key, printed | `the map has no key {0}` |
| S0908 | `as` that does not fit (`n as u8`) | value, target type | `{0} does not fit in {1}` |
| S0909 | `text.slice` off a code point boundary | byte offset | `byte {0} is inside a character of the text` |
| S0910 | `bytes.read_*`/`write_*` out of range | first, end, length | `bytes {0} to {1} are outside {2} bytes` |
| S0911 | lumart's heap cannot give a block (7.1; the mod goes on) | size | `out of memory: {0} bytes` |
| S0912 | a call that needs a value that is `none` now | function, the needed name in code (`player`) | `{0} needs {1}, which is none now, so it was not called` |
| S0913 | a call on an ended LumaKit handle | handle's kind (`kit.hud.meter`) | `{0} was ended, so it can no longer be used` |
| S0914 | a LumaKit call with an argument outside what it takes | function, parameter, value, condition (`>= 1`, `1..=100`, `!= ""`) | `{0}: {1} is {2}; it must be {3}` |

- S0914 replaces the prose form in lumakit.md's examples ("kit.hud.meter: max is 0; a meter needs max of 1 or more"):
  `kit.hud.meter: max is 0; it must be >= 1`. A LumaKit module raises it through lumart, never its own text.
- S0911 is the script heap's refusal, which the script can meet and go on from. `Hook.OutOfMemory` ({mod}, {size}) is
  luma.h's C++ `new` with no memory left, which stops the game; the row is written in the same batch.
- A new code is added here and in the table together; the compiler test checks that every code lumart can
  raise has a row with the same number of arguments in every language.

---

## A mod's functions for other mods

1. What is exported. Every `pub fn` at a module's top level, and every `pub fn` of a `pub` type (a method), of
   any mod with LumaScript code. Another mod reaches them only through `use <mod id>.<module>` with that mod in its
   `needs` (kind 6 refused otherwise, L505).
   - The C symbol stays hidden. `luma_mod` stays the only symbol a library exports.
   - The list goes into `LumaModInfo.exports` as `{name, signature, address}`, sorted by name, so the kernel
     can binary-search it.
2. Names. An export's `name` is its path inside its mod: `<module>.<fn>` or `<module>.<Type>.<fn>`, as the
   source spells it (`hud.meter`, `player.form.define`, `item.ItemId.spawn`). A kind 6 import's name is
   `<mod id>/<that path>` (`lumakit/hud.meter`). The kernel compares the strings byte for byte.
3. Signature. `lumi1:` and 16 lowercase hex digits: the first 8 bytes of SHA-256 over `lumart<layout>:` and the
   function's canonical text, where `<layout>` is the runtime layout (2; see item 9). That text is
   `<path>(<param types>)-><result type>`:
   - every type by its full LumaScript name (`lumakit.hud.Meter`, `u32`, `list<lumakit.item.ItemId>`,
     `fn(MarioActor)?`);
   - parameters separated by `,`, with no spaces and no parameter names or defaults.
   The text names types; it does not lay them out. So a struct gaining a field with a default, or an open enum
   gaining a case, keeps every hash, and an older caller still binds. A type's layout across the
   boundary follows item 5.
4. The C form of an exported function, as for any function, with three parameters first:
   `int32_t lm_<module>__<fn>(const LumaMod* caller, const char* file, uint32_t line, <params>..., <result>* out)`.
   - `caller` is the calling mod. A library records what it makes under it and makes service
     calls for it.
   - `file` and `line` are the call's place in the caller's source. A library that raises a script error reports it
     itself, through `core->report(caller, file, line, code, args)`, so the error is at the caller's line (lumakit
     rule 8), and then returns 2.
   - The caller's code treats 2 as 1 (it ends that run), except that nothing is left to report.
     Section 6's codes become: 0 ok, 1 an error this mod's lumart holds, 2 an error already reported.
5. Values across. As for any function, with one rule for a library's own types:
   - a `struct` from the library's interface goes by pointer, with its size first (`uint32_t`, the caller's
     `sizeof`). The library reads the fields within that size and takes the defaults for the rest. Fields are only
     ever added at the end.
   - An enum goes as its case number (`uint32_t`) and a payload by pointer. A case the library does not know is
     S0914 at the caller's line.
   - LumaLink's build of a library mod compares its new interface file with the one in the version it replaces
     (the newest published `.lumi` given, or none for 1.0). Anything other than added functions, fields at the end
     and cases in open enums stops the build. The message names the type or function and says "a new major
     version is needed". So rule 12's promise is checked, not trusted.
6. Closures. A closure given to another mod (`on_spin`) is passed as `{fn, env, owner}`. Its `fn` is a
   trampoline in the owner's library that acts as the boundary for it: it reports the owner's own errors and
   returns 2. A library that gets 1 or 2 from a closure stops that call only, and the closure's mod gets the report.
   The library drops a mod's closures in `dependent_gone`, so no closure is called after that mod's
   `stop`.
7. The interface file `<mod id>.lumi`, written by the build next to the libraries and packed in the `.luma`:
   - the module tree;
   - every exported `pub` item with its full LumaScript declaration (types with fields, defaults and cases; functions
     with parameter names and defaults, which the caller's compiler fills in);
   - the `lumi1` hashes;
   - the runtime layout it was built with, as the header line `// runtime layout <n>` (none: 1).
   It is UTF-8 LumaScript declarations only: no bodies, nothing that runs. The compiler of a mod that writes
   `use <mod>.<module>` reads it from the needed mod's `.luma` (LumaLink's mod folder) and checks the call against
   it. A different hash at load is then L505 `Name.FunctionSig`, the library rebuilt against another interface.
8. Tests: a library mod and a caller built separately bind on Windows and Android. A field added
   with a default keeps the binding, and the old caller sees the default. A removed field stops the library's build,
   named. A script error in the library is reported at the caller's line, once. A closure's error is reported for
   its owner.
9. The runtime layout. Text blocks, lists and closures cross between two mods' libraries as lumart's own blocks, so
   two libraries built with different layouts of them must never bind. The layout is 2 since a text block may be a
   row of the game's messages, and 1 for every build before. Every signature is taken over it (item 3): a bind
   between two layouts is refused at load as L505 `Name.FunctionSig`, both ways. The build of a mod whose needed
   mod's interface file carries another layout stops with S0160, naming that mod and asking for it to be rebuilt
   with a current LumaLink. When the SDK's `LumaModInfo` has `runtime_layout`, the mod's record carries the layout
   too.

## Reference and pointer arguments

- `args[n]` of a reference parameter (`T &`, `const T &`) is the object's address, a `T *`, as for a value parameter
  (the copy's address). Of a pointer parameter (`T *`) it is the address of the pointer, a `T `. This is what the
  game's instrumented code reads (`(void*)&param`) and what luma.h says; the compiler and lumart use nothing else.
- Calling: a script value given to a `const T &` parameter is copied into a temporary of the calling
  function and `args[n]` is its address; given to a `T &` parameter it must be written `var x` and
  `args[n]` is `x`'s own storage, so the game's writes reach `x`; a `T *` parameter takes a pointer value and `args[n]`
  is the address of a local holding it.
- Hooks: `LUMA_ARG(call, n, T)` of a reference parameter is the caller's object itself. A written argument
  of a value parameter changes the copy the function receives; of a `T &` parameter, the caller's object. A `const T &`
  parameter is read-only to the script: writing it is a compile error.
- Tested in 4.8: a call and a hook each on a `const Vec3f &`, a `Vec3f &` and a `Vec3f *` parameter.

- A `T &&` parameter is as `T &` (luma_hook.h's Bare strips both): `args[n]` is the object's address. A script
  call gives it a temporary of the calling function, as for `const T &` (it needs no `var`); a hook may read and write
  it as the caller's object.
- `args[n]` is a `T `, and a call takes a pointer value for it. The game's rows keep the parameter as written
  (`ParticleEmitter::setGlobalSRTMatrix "void(const Mtx)"`, `Mtx` being `f32[3][4]`), so the checker reads a parameter
  whose type names an array, through any alias, as a pointer to its element: `const Mtx` there is `const f32 (*)[4]`.
  The same holds for a hook's `LUMA_ARG` of it.
- Tested in 4.8 with the cases of addition 3: a call and a hook on a `T &&` parameter and on `const Mtx`.

## Imports for globals and bit-fields

The compiler never writes an offset, a size or an address taken from the self-description: every place in the game
is reached through the import row the loader binds, so a mod built against one build of the game loads
on another that has the same names and types.

| Use | Row (luma.h) | Kind | Slot the loader fills | Read and written as |
|---|---|---|---|---|
| A game global `G` | `LUMA_IMPORT_GLOBAL`, name `G`, signature its type text | 7 | `void *`, or `const void *` for a const global | `*(T *)slot` |
| A bit-field `Type.f` | kind 1 row, signature `"<type> : <width>"` | 1 | its position in bits from the object's start | only `LUMA_BITS_GET` / `LUMA_BITS_SET`; never addressed, never a `var` argument |

- A const global is read only; writing it is a compile error.
- A bit-field read gives its declared type, zero- or sign-extended by that type; a write keeps the low `width` bits
  and is a script error when the value does not fit (`wrap(...)` keeps the low bits on purpose, as for `as`).

## Text across the boundary

- Text a script gives the game (a `const char *` parameter, or a `text` written where the game takes one) lives
  as long as the mod's library. The game may keep the pointer (names, labels, a layout's message id), and nothing in
  a row says which parameters it keeps, so the compiler gives every such argument a pointer that outlives any call:
  - a string literal, or a module `let` the compiler folds to one, is passed as the literal in the library's image;
  - any other text (made while running: interpolation, a container's or a service's text) is passed through
    `lumart_text_keep(text) -> const char *`, which copies it NUL-terminated into the mod's text store, once per
    distinct text: a second call with equal bytes gives the first copy. The store is freed with the library, never
    while the mod is loaded, so a pointer the game kept stays valid as a literal's does.
  - The build report lists each call that passes a made text (file, line, function), since each distinct text stays
    in memory until the mod is unloaded.
  - Text holding a NUL byte cannot cross as `const char *`: a script error naming the function and the parameter,
    and the call is not made.
  - When the self-description marks a parameter not kept, such text goes to the frame's
    scratch instead, and the report drops it.
- Text the game gives a script (a `const char *` result, field or global read as `text`) is copied into a script
  `text` when read: the script never holds a pointer into the game's memory, so text the game frees later is never
  read.
- Addition 5, corrected: a bit-field write that should keep the low bits uses `low_bits<T>(...)`;
  `wrap(...)` is not a conversion.

The scratch is only for a parameter C-6 marks not kept. When 1.11 adds
unloading, the loader frees a reloaded mod's image and text store only at the next stage change (where the game's
objects are rebuilt) or at the game's end, never at the unload itself; the reload test reads a kept label after the
reload.

## Marked pointer parameters

C-6 says what LumaScript does with a marked parameter; this is how the checker types it and how the compiler and
lumart pass it. Unmarked and `kept` pointer parameters stay refused (S0060 with why PointerToValue or Kept).

- Typing (checker, `Game::call_signature`). A row's marks come from the functions file (luma_functions.json); a build
  without it has none. A marked parameter must be a pointer (or an array parameter) to a number, value (vector, quat,
  matrix) or enum; anything else, a write way (`out`, `inout`) through a `const` pointer, or a count parameter that is
  no integer of one width on every platform, is refused with why MarkNotFit (the generator stops on these first; this
  is the second line).
  - `in`: the pointee's type, a value: `MR::f(v)`. `out`, `inout`: `var x` of the pointee's type.
  - with `count` or `fixed`: `list<T>` of the pointee's type; `var xs` for out and inout, a value for in.
  - `nullable`: also `none`.
  - A count parameter is not written by the script: the call has one argument fewer. Messages about an argument
    (S0060, S0062, S0063, S0133) number the parameter by its place in the row's text, so the number matches the
    signature the build report lists.
  - S0133 (build time): a list literal (`[a, b]`) for a `fixed` parameter with another number of elements.
- Passing (compiler 4.3, lumart). `args[n]` of a pointer parameter is the address of a local holding the pointer
  (addition 3); the local holds:
  - `in`: the address of a copy of the value in the calling function's frame; `out`: the address of a frame local of
    the pointee's type, zero-filled before the call (a function that writes on some paths only leaves zero), copied
    into `x` after the call; `inout`: the same local, first filled from `x`, copied back after.
  - `count`: the address of the list's first element (copy-on-write storage made unique before the call when the way
    writes), its length in the count parameter's local, converted to the count's type. A length the count's type
    cannot hold is S0916 and the call is not made. `out` writes up to the list's current length, so the script sizes
    the list first; the list's length never changes.
  - `fixed` n: `out`: the list is set to n zero elements before the call, whatever its length was (so `var xs` of any
    length works and holds the n values after). `in` and `inout`: a list of another length is S0915 and the call is
    not made (a literal of another length is S0133 at build time).
  - `nullable` and `none`: a null pointer, and a count of 0.
  - Nothing of the script's is kept: every address is valid only during the call, which C-6's `in`, `out`, `inout`
    promise.
- Run-time codes (in the table of run-time error codes):

| Code | Raised when | Arguments | English |
|---|---|---|---|
| S0915 | a list of another length for a `fixed` in or inout parameter | function, parameter number, n, length | `{0}: parameter {1} takes exactly {2} elements; the list has {3}, so it was not called` |
| S0916 | a list longer than its count parameter's type holds | function, parameter number, length, count type | `{0}: parameter {1}'s list has {2} elements, more than its count, a {3}, holds, so it was not called` |

- Text (addition 6): C-6 marks no `const char *`, so made text still goes through `lumart_text_keep`.
- Tested in 4.8 with C-6's first marks: `MR::getPlayerFrontVec(var front)` (out), `MR::calcScreenPosition`
  (out, both overloads), and once the headers mark them, one count, one fixed and one nullable function; each
  S0915 and S0916 raised once. The checker's half is tested now (tests/body.rs: marked_pointer_parameters,
  a_fixed_counts_list_literal_is_that_many, a_hidden_count_keeps_the_parameters_numbers). Written, not yet proven
  in a run.

## Further reading

* [LumaScript reference](index.md)
