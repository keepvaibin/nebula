---
title: LumaCore C API
shortTitle: LumaCore C API
intro: Look up the LumaCore table that a C mod uses to find game functions, hook them, read fields, define classes and reach services.
contentType: reference
---

## About the LumaCore C API

A C mod includes `luma.h`. LumaCore gives the mod a table of functions, LumaCore, which it uses to find the game's functions, hook them, read fields, define classes, keep values across a reload and reach services. LumaScript mods use the same table through the code LumaLink writes. For how to write a C mod, see [Writing code in C](../how-tos/writing-code-in-c.md).

From the header:

> LumaCore: what a mod's native code is written against. A mod's library links nothing. It exports one thing, `luma_mod` (LUMA_MOD), which lists what it asks the game for by name. The game fills each slot before the mod's first line runs. A name the game does not have stops the mod from loading, and the **Mods** page says which name; a slot marked LUMA_OPTIONAL is left empty instead. Names do not change between builds of the game. A function's signature is part of what a mod asks for: the same name with other parameters does not bind.

Header: `native/include/luma.h`.

## Constants

| Name | Value | Description |
|---|---|---|
| `LUMA_API` | `1` | The layout version. A struct with a `size` field grows only at its end, and each side reads only what both sizes cover. A struct without one (`LumaFunction`, `LumaCall`, `LumaImport`, `LumaExport`, `LumaOverride`, `LumaTable`) keeps its layout for every mod whose `LumaModInfo`.api is 1; a game that changes one reads and writes the old layout for such a mod. Enum values and LumaCore's order never change. |
| `LUMA_MAX_MODS` | `256` | The most mods with code that may be on at once. |
| `LUMA_OPTIONAL` | `1` |  |
| `LUMA_MAKE_DEAD` | `1` | Made, then dead (makeActorDead) until something makes it appear; actors only. |
| `LUMA_REPORT_OUT_OF_MEMORY` | `1` | `Hook.OutOfMemory`: args[0] the size; luma.h's `new` then stops the game. |
| `LUMA_EXPORT` | `__declspec(dllexport)` |  |
| `LUMA_EXPORT` | `__attribute__((visibility("default")))` |  |
| `LUMA_C_EXTERN` | `extern "C"` |  |
| `LUMA_C_EXTERN` | `extern` |  |
| `LUMA_OWN` | `__attribute__((visibility("hidden")))` |  |
| `LUMA_ZERO_NAME` | `__bzero` |  |
| `LUMA_ZERO_NAME` | `bzero` |  |
| `LUMA_ZERO` | `\` |  |
| `LUMA_MEMORY` | `\` |  |
| `LUMA_STACK_PROBE` | `\` |  |
| `LUMA_STACK_PROBE` | `\` |  |
| `LUMA_CTORS` | `\` |  |
| `LUMA_CTORS` | `\` |  |
| `LUMA_CTORS` | `\` |  |
| `LUMA_LIFE` | `\` |  |
| `LUMA_RUNTIME` | `\` |  |
| `LUMA_NEW` | `\` | Off Windows these are visible by the language's rule; the build's luma_mod.map exports luma_mod alone and -Bsymbolic binds the mod's own calls to them. |

## Types

| Name | Declaration | Description |
|---|---|---|
| `LumaHook` | `typedef int32_t (*LumaHook)(LumaCall* call, void* user);` |  |
| `LumaCtor` | `typedef void (*LumaCtor)(void);` | A mod's life. Its global constructors run in `begin`, after the core is known; the system's loader runs none: - Windows: the library has no C runtime, so nothing else runs `.CRT$XCU`; luma.h runs what lies between its own `.CRT$XCA` and `.CRT$XCZ` markers. - Linux and Android: the build's `-fno-use-init-array` and luma_ctors.ld put them in `luma_ctors` (always made, a null entry first), not `.init_array`; the bounds are strong, so a link without the script fails. - Apple: the build runs macho_ctors.py over each object, which makes the init section `__DATA,luma_ctors` of type S_REGULAR, so dyld runs none; ld64 gives `___dso_handle`. Destructors registered with `atexit` (Windows) or `__cxa_atexit` (elsewhere) run in `finish`, the last first; the list grows through the core's memory. Then `finish` runs the terminators the C runtime would have run at exit (a destructor attribute's function the compiler did not register with atexit), which the system would otherwise run never (Windows `.CRT$XT*`, ELF `.dtors`) or after the mod's memory is gone (Mach-O `__mod_term_func`): - Windows: what lies between luma.h's `.CRT$XTA` and `.CRT$XTZ` markers, last to first. - Linux and Android: luma_ctors.ld puts `.dtors` in `luma_dtors` (a null entry first), run first to last. On every system the order is GCC's: constructors by priority, 101 first; terminators the other way round. - Apple: macho_ctors.py makes a term section `__DATA,luma_dtors` of type S_REGULAR, run first to last. Apple's clang sends a destructor attribute through `__cxa_atexit`, so only assembly or another compiler makes one. |

## Enumerations

### Unnamed constants

| Name | Description |
|---|---|
| `LUMA_BEFORE = 0` | Runs first; returning non-zero skips the function (write the result first). |
| `LUMA_AFTER = 1` | Runs last, the last added first; may change the result. |
| `LUMA_WRAP = 2` | Runs around the function and calls core->proceed(call) when the rest should run. |
| `LUMA_REPLACE = 3` | A wrap only one mod may hold on a function; a second mod's is a conflict. |

### Unnamed constants

| Name | Description |
|---|---|
| `LUMA_IMPORT_FUNCTION = 0` | Slot: const `LumaFunction`**. |
| `LUMA_IMPORT_FIELD = 1` | Name "Type.field" of any record the game defines ("MarioModule::Data.mSpeed", "Vec3<f32>.x", a member of an anonymous type "StarPiece.mFlags.isGettable"; a base's field through the type). One name is one row: a field of a named type's field takes two ("LiveActor.mFlag" and "LiveActorFlag.mIsDead", offsets added). Signature: its type text as the self-description writes it ("f32"); slot: int32_t* (the offset in the object). A bit-field's signature is "<type> : <width>" and its slot gets its position in bits (`LUMA_BITS_GET`, `LUMA_BITS_SET`). |
| `LUMA_IMPORT_SIZE = 2` | Name any record ("Class", "Vec3<f32>"); slot: int32_t* (its size in bytes). |
| `LUMA_IMPORT_CLASS = 3` | Name "Class"; slot: int32_t* (its number, for is_a). |
| `LUMA_IMPORT_HOOK = 4` | A hook on the function `name` with `signature`: `hook`, `hook_kind`, `user`. |
| `LUMA_IMPORT_SERVICE = 5` | A service: `name`, `signature` its "major.minor"; slot: const void** (its table). |
| `LUMA_IMPORT_MOD_FUNCTION = 6` | Another mod's function (a library mod's export): `name` "<mod id>/<export name>", `signature` the export's ("lumi1:" and 16 hex digits, docs/modding/language/ compiled-form.md, "A mod's functions for other mods"); slot: const void**. That mod is among this one's `needs`, so it is loaded first. |
| `LUMA_IMPORT_GLOBAL = 7` | A global the game defines (types 1.2): `name` qualified ("SoundEngine::sEngine"), `signature` its type text; slot: void** (its address; const void** for a const one). |
| `LUMA_IMPORT_METHOD = 8` | A virtual call (objects 1): name "Class::function" (the declaring class's), signature as kind 0's; slot: const `LumaFunction`** (as LumaCore.method gives it). |

## Structures

### `LumaFunction`

A function of the game. `invoke` calls it: args[0] is `this` (null for a function without one), args[1..] point at the arguments, and `result` is where what it returns is put. Hooks on the function run. A parameter of reference type is the object itself: its args[n] is the object's address (for `const Vec3f &`, a `Vec3f *`); a pointer parameter's args[n] points at the pointer.

Some functions read a scene object (the player's holder, the camera's context, the scene's results) or a game system object made after the boot's first frames (`UserFile`: the save files, which every `GameDataFunction` reads; `CometScheduler`; `MarioActor`: the player's actor, set some way into a scene's making) with no test that it is there; the build finds them from the game's source and lists them in luma_functions.json (`needs`). When one of those objects is not there (at boot, between stages, on a title screen), a mod's `invoke` does not call the function: `result` gets its type's empty value (0, null, a default-made record; zero bytes for a record that has no default constructor or has a destructor; for a reference, the address of an object of zero bytes, never null; nothing for void), and the mod's log gets 8 `Call.NotNow` (found: the function, need: the object), once per function and object, then counted. The game's own calls of the function are not checked.

| Name | Description |
|---|---|
| `const char* name` | "Kuribo::exeWander"; "Game/Enemy/Kuribo.cpp#name" for one only its file sees. |
| `const char* signature` | "void(f32, const Vec3f &)", as the game's source writes it. |
| `void (*invoke)(void** args, void* result)` |  |
| `unsigned char* hooked` |  |

### `LumaCall`

One call of a hooked function.

| Name | Description |
|---|---|
| `const LumaFunction* function` |  |
| `void** args` | As `LumaFunction`.invoke takes them; a hook may change what they point at. |
| `void* result` | Where the result is, or goes; null for a constructor or destructor. Zero (a class: as its default constructor makes it; a reference: a null pointer) until a hook or the function writes it. |
| `uint32_t next` | The kernel's. |
| `uint32_t ran` | The kernel's: whether the function's own code ran, and the deepest wrap entered. |
| `const void* chain` | The kernel's: the hooks this call runs, fixed when it starts. |

### `LumaImport`

| Name | Description |
|---|---|
| `uint32_t kind` |  |
| `uint32_t flags` |  |
| `const char* name` |  |
| `const char* signature` |  |
| `void* slot` |  |
| `LumaHook hook` |  |
| `uint32_t hook_kind` |  |
| `void* user` |  |

### `LumaOverride`

A mod's own class: derived from a game class, made by the game's own factory, its overrides hooks that run for its objects only, with data of its own per object.

| Name | Description |
|---|---|
| `const char* name` | A virtual of the base class as `method` names it: "JetTurtle::control" or the class that declares it first, "LiveActor::control". |
| `const char* signature` | As `method`'s: "void()". |
| `LumaHook hook` | A wrap, run only when the object is one of this class's; core->proceed(call) runs the base class's own. |
| `void* user` |  |

### `LumaClassDef`

| Name | Description |
|---|---|
| `uint32_t size` | Sizeof(`LumaClassDef`). |
| `const char* name` | The class's name (1 to 63 bytes): a stage places its objects by it. No game class, factory row or other mod's class may have it; names compare with ASCII case folded, as the game's factory does. |
| `const char* base` | The game class it derives from ("JetTurtle"): polymorphic, with a row. "PlanetGravity": a gravity class, its objects made by LumaCore.make_gravity only (maker and replaces null; no stage places it, no alias names it). |
| `const char* maker` | The factory row that makes the base ("JetTurtle"); null: the one named as the base. |
| `const char* replaces` | Null, or a name the game's stages place: its objects are made as this class instead. |
| `uint32_t data_size` | Its own data per object, zeroed before `made`; 0: none. |
| `uint32_t data_align` | A power of two up to 4096; 0: 16. |
| `void (*made)(void* object, void* data, void* user)` | After the maker made the object and before its init, on the thread that makes the stage's objects. May be null. |
| `void* user` |  |
| `const LumaOverride* overrides` |  |
| `uint32_t override_count` |  |
| `void (*gone)(void* object, void* data, void* user)` | Once for each object whose `made` ran, before its data is freed: when it is destroyed (`object` is its address, its own parts' destructors already run: read `data`, not the object), or with `object` null when the scene's memory went with it, or when an object is made again at its address. On the thread that destroys it. Not called when the mod is taken out (its memory goes whole). May be null; a definition whose `size` ends before it has none. |

### `LumaMakePlace`

Where LumaCore.make puts an object, as a stage's placement row gives it (classes contract, "make").

| Name | Description |
|---|---|
| `uint32_t size` | Sizeof(`LumaMakePlace`); in an array, each place's (the first's is the stride). |
| `float pos[3]` | In the main stage, as a stage file's pos_x/y/z. |
| `float rot[3]` | Degrees, as dir_x/y/z. |
| `float scale[3]` | As scale_x/y/z. |
| `int32_t args[8]` | Obj_arg0 to 7; -1: not given (the placement file names what each class reads). |

### `LumaPlacement`

Where a stage placed an object (classes contract, "placement"): the scene's main stage, the zone (a stage's name; the main stage's own for its objects), the layer ("Common", "LayerA" to "LayerAE") and the object's index among that zone's placements of that layer (its Placement files, then its MapParts files, in the stage archive's order, each file's rows in order). The same on every machine with the same game and mods.

| Name | Description |
|---|---|
| `uint32_t size` | Sizeof(`LumaPlacement`). |
| `const char* stage` | Placement_of: valid while the scene lives. placed_at: null, or the main stage's name. |
| `const char* zone` |  |
| `const char* layer` |  |
| `uint32_t index` |  |

### `LumaGravityPlace`

A gravity LumaCore.make_gravity makes (classes contract, "gravity"): PlanetGravity's own fields, which the scene's gravity manager sorts and filters by.

| Name | Description |
|---|---|
| `uint32_t size` | Sizeof(`LumaGravityPlace`). |
| `float range` | MRange: below 0, everywhere; else what PlanetGravity::isInRangeDistance compares with (an override that wants a range calls it, as the game's gravities do). |
| `int32_t priority` | MPriority: only the highest priority that pulls at a point counts; equal ones add. |
| `uint32_t types` | MGravityType bits: 1 normal, 2 shadow, 4 magnet, 8 launcher; 0: 1. |
| `uint32_t inverse` | Nonzero: mIsInverse, the pull reversed. |

### `LumaTable`

A table of the game's data as LumaCore.table gives it.

| Name | Description |
|---|---|
| `const void* data` | As the game reads it now; written only through set_table. |
| `const char* type` | Its element's type, the compiler's spelling ("unsigned int", "const char *", "MR::ActorMoveParam"). |
| `uint32_t size` | All of it, in bytes. |
| `uint32_t count` | Its elements (an array's innermost), 1 for a record. |

### `LumaExport`

The kernel's own, said for a mod by LumaCore (not passed to report): 2 `Hook.NoResult` and 3 `Hook.SkipRefused` (a hook's result), 4 `Hook.ServiceNotGranted`, and the objects contract's 5 `Object.NoRow` (found: the class's type_info name as the ABI writes it), 6 `Object.NotReadable` (where: the call), 7 `Object.Ambiguous` (where, found: the class), and 8 `Call.NotNow` (found: the function, need: the object that is not there; see `LumaFunction`). One function a library mod gives the mods that need it. Names "<module>.<fn>" and the signature form are described in [The compiled form](lumascript/compiled-form.md); the kernel compares both strings and nothing else. Sorted by name.

| Name | Description |
|---|---|
| `const char* name` |  |
| `const char* signature` |  |
| `const void* address` |  |

### `LumaModInfo`

| Name | Description |
|---|---|
| `uint32_t size` |  |
| `uint32_t api` |  |
| `const LumaImport* imports` |  |
| `uint32_t import_count` |  |
| `int32_t (*start)(const LumaCore* core, const LumaMod* mod)` | After every slot is filled. Non-zero: the mod could not start and is left off. May be null. |
| `void (*stop)(void)` | When the mod is taken out, after its hooks are: other threads may still be inside one until the game's grace ends, so it frees nothing they read (memory from `alloc` and its globals go after the grace). From stop and finish (and after a start that refused), hook, define_class, alias_class, set_number, set_table and make_gravity refuse (-1, or null), said in the log: what the mod added then would outlive it. May be null. |
| `void (*begin)(const LumaCore* core, const LumaMod* mod)` | Luma.h's, filled by `LUMA_MOD`: before `start`, it keeps the core for `new` and runs the C++ globals' constructors (the system's loader runs none). |
| `void (*finish)(void)` | Luma.h's: after the grace, the destructors `atexit` took, the last first; then the game frees the mod's memory and closes its library. |
| `const LumaExport* exports` | A library mod's functions for the mods that need it (kind 6 imports); none for most mods. |
| `uint32_t export_count` |  |
| `void (*dependent_gone)(const LumaMod* dependent)` | A library mod is told that `dependent`, which imported from it, is being taken out: after the dependent's stop, before its grace, on the game thread. It drops what it keeps for that mod (callbacks into its code). May be null. |
| `int64_t (*layers)(const LumaCore* core, const LumaMod* mod, char* buf, uint64_t size)` | Which of the mod's layers with `"when": {"code": true}` are on. Called once a start, on the game thread, after every mod's begin and before any mod's start; not called when the mod is loaded again while the game runs. Writes the chosen layer names into buf, each with its NUL, and returns the bytes they need; first called with buf null and size 0, and when size is too small it writes nothing and is called again with the size it returned. 0 or less: none chosen. A name that is not one of the mod's code layers is left out and said in the mod log. The game's content is cooked with the answer: an answer that differs from the one the content was cooked with starts the game again once, before it reads any content. Null: none of its code layers is on. |
| `uint32_t runtime_layout` | The layout of the data LumaScript's runtime passes between mods (text, lists), as the LumaLink that built the mod numbers it; 0: the mod has no LumaScript runtime or was built before this field. A mod that calls another mod's functions (kind 6) is refused when both numbers are non-zero and differ, and an optional such import stays null: the older mod is rebuilt with a current LumaLink. Read only when `size` covers it. |
| `void (*cut)(const LumaCut* cut)` | The mod is told that a fault in another mod's code cut a call of its code short, and it stays on: once, right after the fault, on the faulting thread, before any other code of it runs there. It puts back what it records of calls in progress and what the cut call was changing. A fault in it turns it off. Null: none; the log then says its state may be half done. Read only when `size` covers it. Set it with `LUMA_MOD_CUT(imports, start, stop, cut)` or `LUMA_LIBRARY_MOD_CUT(imports, start, stop, exports, dependent_gone, cut)`. For more information, see [When a mod fails](../concepts/when-a-mod-fails.md). |

### `LumaCut`

What a mod's `cut` is told. The mod's frames below `stack` on the thread `cut` runs on were left without returning. Strings live until `cut` returns.

| Name | Description |
|---|---|
| `uint32_t size` | `sizeof(LumaCut)` as the game knows it. |
| `uint32_t boundary` | Where the cut call began (`LumaGuardBoundary` in `luma_guard.h`: 0 a before hook, ..., 8 a call from a library). |
| `const char* where` | That boundary's function, service or class, for example `MR::getRandom`. |
| `uintptr_t stack` | The boundary's stack pointer. |
| `const char* function` | The innermost cut function of this mod, by its export's name when it is one, else `""`. |
| `const char* place` | That place as the mod's file and offset, for example `my-lib/mod.dll+0x1a2b`. |
| `const LumaMod* caller` | The mod whose code called this mod's cut code; null when the game called it. |
| `const char* caller_id` | That mod's ID, or `""`. |
| `const char* off` | The ID of the mod turned off for the fault. |
| `const char* faulted` | The ID of the mod whose code failed. |

## LumaCore functions

* `uint32_t size;` 
* `uint32_t api;` 
### `log`

```c
void log(const LumaMod* mod, const char* text);
```

### `function`

```c
const LumaFunction* function(const char* name, const char* signature);
```

A function by name, after loading. `signature` may be null when the name has one function. Null: not found.

### `function_at`

```c
const LumaFunction* function_at(uint32_t index);
```

Every function: `index` from 0 until it returns null, by name.

### `hook`

```c
int32_t hook(const LumaMod* mod, const LumaFunction* function, uint32_t kind, LumaHook hook, void* user);
```

Adds a hook after loading. 0: added; -1: refused (a constructor or destructor takes before and after only, a function whose result has no default takes no skipping hook, and a virtual call from `method` is not one function: hook its rows' own functions, `overrider` names them); -2: another mod replaces the function already; -3: not on the game thread (nothing changes). A call already running keeps the hooks it started with. The same function, kind, hook and user added twice is two hooks: each runs, and unhook takes out one.

### `proceed`

```c
void proceed(LumaCall* call);
```

From a wrap or a replace: runs the wraps after it, then the function.

### `field`

```c
int32_t field(const char* class_name, const char* field, const char* type);
```

A field's offset in an object of any record the game defines (by its name or an alias's; a base's field through the record; a bit-field: its position in bits), or -1, as kind 1 binds it. `type` may be null; when given it must be the field's type text.

### `size_of`

```c
int32_t size_of(const char* class_name);
```

Any record's size, or -1.

### `class_of`

```c
int32_t class_of(const char* class_name);
```

A class's number, or -1: its record's index among the record rows sorted by name in byte order (objects O-3; kind 3 gives the same). is_a: 1 when the object's own class is that class or derives from it by one path (as_class's answer, with no report), else 0.

### `is_a`

```c
int32_t is_a(const void* object, int32_t class_number);
```

### `service`

```c
const void* service(const LumaMod* mod, const char* name, uint32_t major, uint32_t minor);
```

A service by name ("fs", "net", "draw"...): its table of functions, or null when this game has no such service, an older one than `minor`, or one whose permission the player has not granted this mod (logged, named). A `major` is a table of its own; within it a later `minor` only adds at the end.

### `game_thread`

```c
int32_t game_thread(void);
```

1 on the game thread (the thread that runs the game's frames and starts the mods), else 0. A hooked function runs its hooks on whatever thread calls it.

### `unhook`

```c
int32_t unhook(const LumaMod* mod, const LumaFunction* function, uint32_t kind, LumaHook hook, void* user);
```

Takes out a hook this mod added (in its imports or with `hook`): the same function, kind, hook and user. 0: taken out; -1: this mod has no such hook; -3: not on the game thread (nothing changes).

### `alloc`

```c
void* alloc(const LumaMod* mod, uint64_t size, uint64_t align);
```

Memory for the mod, `size` bytes aligned to `align` (a power of two up to 65536), counted on the **Mods** page and freed when the mod is taken out. Null: none left (the mod says so). From any thread.

### `free`

```c
void free(const LumaMod* mod, void* p);
```

Gives back what `alloc` gave this mod; null does nothing.

### `report`

```c
void report(const LumaMod* mod, const char* file, uint32_t line, uint32_t code, const char* const* args, uint32_t arg_count);
```

Something went wrong in the mod's code at `file`:`line`. `code` picks the words: 900 to 9999 lumart's (Script.S0901...), args {0}, {1}... in order; 1 to 99 the kernel's and luma.h's own (`LUMA_REPORT_`*). Said once in the game's log and kept for the **Mods** page in the player's language; repeats are counted. Args are UTF-8 and copied; at most 8 are kept. From any thread.

### `own_class`

```c
int32_t own_class(const LumaMod* mod, const void* object);
```

Objects (objects contract). The object's own class (the most derived), from the object or any of its polymorphic subobjects: its number (class_of's). -1: object is null. -2: its class has no row (no name a mod can write). -3: not a game object the kernel can read (the object or its vtable pointer not aligned as a pointer; on Windows, a locator that is not one). -2 and -3 are reported for `mod`, once each. Any thread.

### `as_class`

```c
const void* as_class(const LumaMod* mod, const void* object, int32_t class_number);
```

The address of the object's `class_number` subobject when its own class is that class or derives from it, else null. Null too when the class is in the object twice (an ambiguous base) (reported). Any thread.

### `method`

```c
const LumaFunction* method(const char* name, const char* signature);
```

The virtual call of a virtual function a record declares ("LiveActor::control", "void()"), as import kind 8 binds it: invoke(args, result) with args[0] the object as the declaring class (as_class gives it); the object's own class's function runs, and that function's hooks with it. `hooked` is null. Null: no such virtual (or one no mod can name).

### `overrider`

```c
const LumaFunction* overrider(const LumaMod* mod, const void* object, const char* name, const char* signature);
```

The function row the object's own class runs for that virtual, or null: no row (inline in a header, implicit), an unknown name, -2 or -3 as own_class, or two subobjects of the declaring class with different overriders and the object pointer at neither (reported, ambiguous). Any thread.

### `call_method`

```c
int32_t call_method(const LumaMod* mod, const void* object, const char* name, const char* signature, void** args, void* result);
```

Method() with the adjustment done: sets args[0] to `object` as the declaring class, then invokes (args may be null for a virtual with no parameters). 0: called; -1: no such virtual, or args null for one with parameters; -2: the object is not of that class (or ambiguous (reported)); -3: not readable (reported). The thread the call may run on is the called function's.

### `keep`

```c
int32_t keep(const LumaMod* mod, const void* bytes, uint32_t size);
```

State across a reload, the only thing carried from one load of the mod to the next. In `stop` only (`mod` the one start was given): the game holds a copy of `size` bytes, at most 1 MiB, for the mod's next load this session; the last call wins, and a size of 0 keeps nothing. 0: kept; -1: not in stop, more than 1 MiB, bytes null with a size, or no memory; -3: not on the game thread.

### `kept`

```c
const void* kept(const LumaMod* mod, uint32_t* size);
```

In `start` only: the bytes the mod's previous load kept this session, their count in *size, or null (none kept, or not in start; *size 0). Valid until start returns; once read, they are gone.

### `define_class`

```c
int32_t define_class(const LumaMod* mod, const LumaClassDef* def);
```

Classes. For more information, see [Defining classes](lumascript/defining-classes.md). Defines a mod's class: its number (>= 0; class_of, own_class, as_class and is_a know it), or -1 the definition is not whole (null, size, empty or long name, base not a polymorphic class with a row, data_align), -2 the name (or `replaces`) is taken, -3 not on the game thread, -4 an override names no virtual of the base, or one the base runs with no row (inline, implicit), or one whose hook is refused, -5 no factory row of the maker's name, or `replaces` is no name the factory makes. Every refusal is said in the log with the name it is about, and nothing of the class is left. Its hooks are the mod's: they go when it goes, and its objects are then plain objects of the base.

### `class_data`

```c
void* class_data(const LumaMod* mod, const void* object, int32_t class_number);
```

The object's data when it is one of the class's (from the object or any of its subobjects), else null. Any thread.

### `number`

```c
void* number(const LumaMod* mod, const char* name, const char* signature, uint32_t place, uint32_t* kind);
```

Numbers in the game's code: every number written in a function's code that the game reads at run time, named by the function's row name, its signature, and its place there (the k-th named number, from 1, in source order; luma_numbers.json lists them all), and every file's number constant by its name, with signature "" and place 0. A null signature asks for the only function of that name with a number at that place. number: its address and, in *kind, its kind (1 f32, 2 f64, 3 s8, 4 u8, 5 s16, 6 u16, 7 s32, 8 u32, 9 s64, 10 u64, 11 bool: a file's constant, 0 or 1), or null and kind 0: no such number, or several (give the signature); said in the log. Any thread.

### `set_number`

```c
int32_t set_number(const LumaMod* mod, const char* name, const char* signature, uint32_t place, double value);
```

Sets one to `value` for the mod. When the mod goes, every number it set is again what the mods still here set last, or the game's own (a write through number's address is not put back). 0: set; -1: no such number, several, or no memory (said); -2: the value is not one of its kind (a fraction for a whole number, out of range; said); -3: not on the game thread.

### `table`

```c
int32_t table(const LumaMod* mod, const char* name, LumaTable* out);
```

Tables of the game's data: every file-scope or class-static array or plain record with an initializer, by its name (`File.cpp#name` when only its file sees it; luma_numbers.json "tables" lists them with their types). table: 0 and `*out` filled (data as the game reads it now, its element type, the size in bytes, the count of elements, 1 for a record), or -1: no such table, or no memory (said). Any thread.

### `set_table`

```c
int32_t set_table(const LumaMod* mod, const char* name, uint32_t offset, const void* bytes, uint32_t size);
```

Writes `size` bytes at `offset` in the table for the mod. When the mod goes, the table is the game's again with the writes of the mods still here, in their order. 0: written; -1: no such table, or no memory (said); -2: the bytes are not inside the table, or none (said); -3: not on the game thread.

### `make`

```c
int32_t make(const LumaMod* mod, const char* name, uint32_t count, const LumaMakePlace* place, uint32_t flags, void** objects);
```

Objects (classes contract, "make"): `count` objects by a name the factory makes (a game object's, a mod class's, a mod's object: as a stage places it, its changes and replacements applied), each made and initialized as a placed one, at `place[i]` (null: the origin, unturned, scale 1, every argument -1). While the scene makes its objects (from a hook that runs then: after StageDataHolder::initPlacementMario, a class's init) or once it is made (during play: each object then takes its after-placement step in this call); they stand in the main stage and live as long as the scene; made inside another object's init, they join that placement's view group. Flags: `LUMA_MAKE_DEAD`. Each object's address in objects[i] (objects may be null). Returns the count made (fewer when a creator makes nothing, said), or -1 the factory makes nothing by that name, -2 its files are not in this game, -3 not on the game thread, no scene, or the scene is starting, -4 a bad argument (count not 1 to 1024, a place smaller than `LumaMakePlace` or not finite, an unknown flag) or no memory. Every refusal is said in the log with the name.

### `alias_class`

```c
int32_t alias_class(const LumaMod* mod, const char* name, const char* class_name, const int32_t args[8]);
```

Another name for a mod's class (classes contract, "alias"): a stage that places `name`, and make by it, make an object of the live mod class `class_name` (any mod's), whose placement argument i (Obj_arg0 to 7) reads as args[i] while it initialises, where args[i] is not -1 (-1: the placement's own). The alias is the calling mod's: it goes when that mod goes or the class does. Called again for a name the mod aliased, it changes that alias. 0: the alias stands; -1 a bad argument (null, a name empty or over 63 bytes, null args), -2 the name is taken (a game class's, a factory row's, a mod class's or another mod's alias), -3 not on the game thread, -5 class_name is no live mod class. Every refusal is said in the log with the name.

### `placement_of`

```c
int32_t placement_of(const LumaMod* mod, const void* object, LumaPlacement* out);
```

The placement a stage made `object` from (classes contract, "placement"): 0 and `*out`, or -1 none (no scene, an object made by code or by make, or not one the stage's placements made), -3 not on the game thread, -4 a null object or out, or out smaller than `LumaPlacement` (said). The scene's objects live as long as the scene.

### `placed_at`

```c
void* placed_at(const LumaMod* mod, const LumaPlacement* place);
```

The object the scene made from a placement (`place`'s zone, layer, index; its stage null or the main stage's name; names compare with ASCII case folded), or null: no such placement in this scene, its creator made nothing, it is not made yet, not on the game thread, or a null place or one smaller than `LumaPlacement` (said).

### `make_gravity`

```c
void* make_gravity(const LumaMod* mod, const char* class_name, const LumaGravityPlace* place);
```

A gravity of the live mod class `class_name`, whose base is PlanetGravity (classes contract, "gravity"): made with `place`'s fields, its data zeroed and its `made` run, then registered with the scene's gravity manager, which asks its calcOwnGravityVector (the class's override: pDest the pull's direction, pScalar the distance it falls off with) at every point any object asks its gravity at. It lives as long as the scene (`gone` with a null object when the scene goes); when its mod goes it pulls nowhere. Null: not on the game thread, or said (no such live class, not a gravity class, no scene with gravities, no memory, a null place or one smaller than `LumaGravityPlace`, a range that is not a number, a type bit above 8).

### `call_mod`

```c
int32_t call_mod(const LumaMod* mod, const void* fn, uint64_t a0, uint64_t a1, uint64_t a2, uint64_t a3, uint64_t* result);
```

A library's call of code another mod gave it (a callback it keeps). `fn(a0, a1, a2, a3)` is a function in a mod's library with up to four integer or pointer arguments; its integer result goes to `*result` (may be null). The mod whose library holds `fn` runs under a boundary of its own, so when its code fails that mod is turned off and the call returns: the library goes on to its next user in the same frame. When `fn` is `mod`'s own code, or the code of a mod `mod` needs, it is a plain call.

| Result | Meaning |
|---|---|
| `0` | Called; it returned. |
| `-1` | Its code failed; the mod that gave it is off until the next start (`*result` 0). |
| `-2` | Not called; the mod that gave it is off already or being taken out (`*result` 0). |
| `-4` | `fn` is in no running mod's code, or `mod` is null (said in the log; not called). |

Any thread. A call costs a guarded call's boundary more than a plain call; the measured cost is in `docs/plan/PROGRESS-AGENT-2.md`.

## Further reading

* [When a mod fails](../concepts/when-a-mod-fails.md)
* [Writing code in C](../how-tos/writing-code-in-c.md)
* [Calling services](services/calling-services.md)
* [Defining classes](lumascript/defining-classes.md)
