/* SPDX-License-Identifier: GPL-3.0-only */
/* LumaCore: what a mod's native code is written against.

   A mod's library links nothing. It exports one thing, `luma_mod` (LUMA_MOD), which lists what it asks the game for by
   name. The game fills each slot before the mod's first line runs. A name the game does not have stops the mod from
   loading, and the Mods screen says which name; a slot marked LUMA_OPTIONAL is left empty instead.

   Names do not change between builds of the game. A function's signature is part of what a mod asks for: the same
   name with other parameters does not bind. */
#ifndef LUMA_H
#define LUMA_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* The layout version. A struct with a `size` field grows only at its end, and each side reads only what both sizes
   cover. A struct without one (LumaFunction, LumaCall, LumaImport, LumaExport, LumaOverride, LumaTable) keeps its
   layout for every mod whose LumaModInfo.api is 1; a game that changes one reads and writes the old layout for
   such a mod. Enum values and LumaCore's order never change. */
#define LUMA_API 1u
/* The most mods with code that may be on at once (loader contract 2). */
#define LUMA_MAX_MODS 256u

/* A function of the game. `invoke` calls it: args[0] is `this` (null for a function without one), args[1..] point at
   the arguments, and `result` is where what it returns is put. Hooks on the function run. A parameter of reference
   type is the object itself: its args[n] is the object's address (for `const Vec3f &`, a `Vec3f *`); a pointer
   parameter's args[n] points at the pointer. */
typedef struct LumaFunction {
    const char* name;      /* "Kuribo::exeWander"; "Game/Enemy/Kuribo.cpp#name" for one only its file sees */
    const char* signature; /* "void(f32, const TVec3f &)", as the game's source writes it */
    void (*invoke)(void** args, void* result);
    unsigned char* hooked;
} LumaFunction;

/* One call of a hooked function. */
typedef struct LumaCall {
    const LumaFunction* function;
    void** args;  /* as LumaFunction.invoke takes them; a hook may change what they point at */
    void* result; /* where the result is, or goes; null for a constructor or destructor. Zero (a class: as its
                     default constructor makes it; a reference: a null pointer) until a hook or the function writes it
                     (loader contract 4) */
    uint32_t next;      /* the kernel's */
    uint32_t ran;       /* the kernel's: whether the function's own code ran, and the deepest wrap entered */
    const void* chain;  /* the kernel's: the hooks this call runs, fixed when it starts */
} LumaCall;

typedef int32_t (*LumaHook)(LumaCall* call, void* user);

/* A va_list as a function receives it (contract varargs 2, 3): what args[i] points at for a `va_list` parameter, and
   for the rest of a function with `...`. On x86-64 outside Windows the va_list is an array, received as a pointer to
   its element; elsewhere it is itself. A hook reads a copy, never the one it is given:
       va_list mine; LUMA_VA_COPY(mine, call->args[2]); ... va_arg(mine, int) ...; va_end(mine); */
#if defined(__x86_64__) && !defined(_WIN32)
/* The element's type taken from the compiler's own va_list: a `struct __va_list_tag` written here would be a new
   struct of that name, not the compiler's, and a va_list would not convert to a pointer to it. */
typedef __typeof__(((__builtin_va_list*)0)[0][0])* luma_va_arg;
#define LUMA_VA_COPY(mine, arg) __builtin_va_copy(mine, *(luma_va_arg*)(arg))
#elif defined(_MSC_VER) && !defined(__clang__)
/* MSVC (the Windows host): its va_list is a char pointer, copied with va_copy. */
#include <stdarg.h>
typedef va_list luma_va_arg;
#define LUMA_VA_COPY(mine, arg) va_copy(mine, *(luma_va_arg*)(arg))
#else
typedef __builtin_va_list luma_va_arg;
#define LUMA_VA_COPY(mine, arg) __builtin_va_copy(mine, *(luma_va_arg*)(arg))
#endif

enum {
    LUMA_BEFORE = 0, /* runs first; returning non-zero skips the function (write the result first) */
    LUMA_AFTER = 1,  /* runs last, the last added first; may change the result */
    LUMA_WRAP = 2,   /* runs around the function and calls core->proceed(call) when the rest should run */
    LUMA_REPLACE = 3 /* a wrap only one mod may hold on a function; a second mod's is a conflict */
};

enum {
    LUMA_IMPORT_FUNCTION = 0, /* slot: const LumaFunction** */
    LUMA_IMPORT_FIELD = 1,    /* name "Type.field" of any record the game defines ("MarioModule::Data.mSpeed",
                                 "Vec3<f32>.x", a member of an anonymous type "StarPiece.mFlags.isGettable"; a base's
                                 field through the type). One name is one row: a field of a named type's field takes two
                                 ("LiveActor.mFlag" and "LiveActorFlag.mIsDead", offsets added). Signature: its type
                                 text as the self-description writes it ("f32");
                                 slot: int32_t* (the offset in the object). A bit-field's signature is "<type> : <width>"
                                 and its slot gets its position in bits (LUMA_BITS_GET, LUMA_BITS_SET) */
    LUMA_IMPORT_SIZE = 2,     /* name any record ("Class", "Vec3<f32>"); slot: int32_t* (its size in bytes) */
    LUMA_IMPORT_CLASS = 3,    /* name "Class"; slot: int32_t* (its number, for is_a) */
    LUMA_IMPORT_HOOK = 4,     /* a hook on the function `name` with `signature`: `hook`, `hook_kind`, `user` */
    LUMA_IMPORT_SERVICE = 5,  /* a service: `name`, `signature` its "major.minor"; slot: const void** (its table) */
    LUMA_IMPORT_MOD_FUNCTION = 6, /* another mod's function (loader D3-3): `name` "<mod id>/<export name>", `signature`
                                    the export's ("lumi1:" and 16 hex digits, lumascript-abi R-2); slot: const void**.
                                    That mod is among this one's `needs`, so it is loaded first */
    LUMA_IMPORT_GLOBAL = 7,   /* a global the game defines (types 1.2): `name` qualified ("SoundEngine::sEngine"),
                                 `signature` its type text; slot: void** (its address; const void** for a const one) */
    LUMA_IMPORT_METHOD = 8    /* a virtual call (objects 1): name "Class::function" (the declaring class's), signature as
                                 kind 0's; slot: const LumaFunction** (as LumaCore.method gives it) */
};
#define LUMA_OPTIONAL 1u

typedef struct LumaImport {
    uint32_t kind;
    uint32_t flags;
    const char* name;
    const char* signature;
    void* slot;
    LumaHook hook;
    uint32_t hook_kind;
    void* user;
} LumaImport;

typedef struct LumaMod LumaMod;

/* A mod's own class (docs/modding/language/classes.md): derived from a game class, made by the game's own factory, its
   overrides hooks that run for its objects only, with data of its own per object. */
typedef struct LumaOverride {
    const char* name;      /* a virtual of the base class as `method` names it: "JetTurtle::control" or the class that
                              declares it first, "LiveActor::control" */
    const char* signature; /* as `method`'s: "void()" */
    LumaHook hook;         /* a wrap, run only when the object is one of this class's; core->proceed(call) runs the
                              base class's own */
    void* user;
} LumaOverride;

typedef struct LumaClassDef {
    uint32_t size;            /* sizeof(LumaClassDef) */
    const char* name;         /* the class's name (1 to 63 bytes): a stage places its objects by it. No game class,
                                 factory row or other mod's class may have it; names compare with ASCII case
                                 folded, as the game's factory does */
    const char* base;         /* the game class it derives from ("JetTurtle"): polymorphic, with a row. "PlanetGravity":
                                 a gravity class, its objects made by LumaCore.make_gravity only (maker and replaces
                                 null; no stage places it, no alias names it) */
    const char* maker;        /* the factory row that makes the base ("JetTurtle"); null: the one named as the base */
    const char* replaces;     /* null, or a name the game's stages place: its objects are made as this class instead */
    uint32_t data_size;       /* its own data per object, zeroed before `made`; 0: none */
    uint32_t data_align;      /* a power of two up to 4096; 0: 16 */
    /* After the maker made the object and before its init, on the thread that makes the stage's objects. May be null. */
    void (*made)(void* object, void* data, void* user);
    void* user;
    const LumaOverride* overrides;
    uint32_t override_count;
    /* Once for each object whose `made` ran, before its data is freed: when it is destroyed (`object` is its address,
       its own parts' destructors already run: read `data`, not the object), or with `object` null when the scene's
       memory went with it, or when an object is made again at its address. On the thread that destroys it. Not called
       when the mod is taken out (its memory goes whole). May be null; a definition whose `size` ends before it has
       none. */
    void (*gone)(void* object, void* data, void* user);
} LumaClassDef;

/* Where LumaCore.make puts an object, as a stage's placement row gives it (classes contract, "make"). */
typedef struct LumaMakePlace {
    uint32_t size;    /* sizeof(LumaMakePlace); in an array, each place's (the first's is the stride) */
    float pos[3];     /* in the main stage, as a stage file's pos_x/y/z */
    float rot[3];     /* degrees, as dir_x/y/z */
    float scale[3];   /* as scale_x/y/z */
    int32_t args[8];  /* Obj_arg0 to 7; -1: not given (the placement file names what each class reads) */
} LumaMakePlace;
#define LUMA_MAKE_DEAD 1u /* made, then dead (makeActorDead) until something makes it appear; actors only */

/* Where a stage placed an object (classes contract, "placement"): the scene's main stage, the zone (a stage's name;
   the main stage's own for its objects), the layer ("Common", "LayerA" to "LayerAE") and the object's index among
   that zone's placements of that layer (its Placement files, then its MapParts files, in the stage archive's order,
   each file's rows in order). The same on every machine with the same game and mods. */
typedef struct LumaPlacement {
    uint32_t size;      /* sizeof(LumaPlacement) */
    const char* stage;  /* placement_of: valid while the scene lives. placed_at: null, or the main stage's name */
    const char* zone;
    const char* layer;
    uint32_t index;
} LumaPlacement;

/* A gravity LumaCore.make_gravity makes (classes contract, "gravity"): PlanetGravity's own fields, which the scene's
   gravity manager sorts and filters by. */
typedef struct LumaGravityPlace {
    uint32_t size;     /* sizeof(LumaGravityPlace) */
    float range;       /* mRange: below 0, everywhere; else what PlanetGravity::isInRangeDistance compares with (an
                          override that wants a range calls it, as the game's gravities do) */
    int32_t priority;  /* mPriority: only the highest priority that pulls at a point counts; equal ones add */
    uint32_t types;    /* mGravityType bits: 1 normal, 2 shadow, 4 magnet, 8 launcher; 0: 1 */
    uint32_t inverse;  /* nonzero: mIsInverse, the pull reversed */
} LumaGravityPlace;

/* A table of the game's data as LumaCore.table gives it (docs/modding/language/numbers.md). */
typedef struct LumaTable {
    const void* data;  /* as the game reads it now; written only through set_table */
    const char* type;  /* its element's type, the compiler's spelling ("unsigned int", "const char *", "MR::ActorMoveParam") */
    uint32_t size;     /* all of it, in bytes */
    uint32_t count;    /* its elements (an array's innermost), 1 for a record */
} LumaTable;

typedef struct LumaCore {
    uint32_t size;
    uint32_t api;

    void (*log)(const LumaMod* mod, const char* text);

    /* A function by name, after loading. `signature` may be null when the name has one function. Null: not found. */
    const LumaFunction* (*function)(const char* name, const char* signature);
    /* Every function: `index` from 0 until it returns null, by name. */
    const LumaFunction* (*function_at)(uint32_t index);
    /* Adds a hook after loading. 0: added; -1: refused (a constructor or destructor takes before and after only, a
       function whose result has no default takes no skipping hook, and a virtual call from `method` is not one
       function: hook its rows' own functions, `overrider` names them); -2: another mod replaces the function already;
       -3: not on the game thread (nothing changes). A call already running keeps the hooks it started with. The same
       function, kind, hook and user added twice is two hooks: each runs, and unhook takes out one. */
    int32_t (*hook)(const LumaMod* mod, const LumaFunction* function, uint32_t kind, LumaHook hook, void* user);
    /* From a wrap or a replace: runs the wraps after it, then the function. */
    void (*proceed)(LumaCall* call);

    /* A field's offset in an object of any record the game defines (by its name or an alias's; a base's field through
       the record; a bit-field: its position in bits), or -1, as kind 1 binds it. `type` may be null; when given it
       must be the field's type text. */
    int32_t (*field)(const char* class_name, const char* field, const char* type);
    /* Any record's size, or -1. */
    int32_t (*size_of)(const char* class_name);
    /* A class's number, or -1: its record's index among the record rows sorted by name in byte order (objects O-3; kind 3
       gives the same). is_a: 1 when the object's own class is that class or derives from it by one path (as_class's
       answer, with no report), else 0. */
    int32_t (*class_of)(const char* class_name);
    int32_t (*is_a)(const void* object, int32_t class_number);

    /* A service by name ("fs", "net", "draw"...): its table of functions, or null when this game has no such service,
       an older one than `minor`, or one whose permission the player has not granted this mod (logged, named). A
       `major` is a table of its own; within it a later `minor` only adds at the end. */
    const void* (*service)(const LumaMod* mod, const char* name, uint32_t major, uint32_t minor);

    /* 1 on the game thread (the thread that runs the game's frames and starts the mods), else 0. A hooked function
       runs its hooks on whatever thread calls it. */
    int32_t (*game_thread)(void);
    /* Takes out a hook this mod added (in its imports or with `hook`): the same function, kind, hook and user. 0: taken
       out; -1: this mod has no such hook; -3: not on the game thread (nothing changes). */
    int32_t (*unhook)(const LumaMod* mod, const LumaFunction* function, uint32_t kind, LumaHook hook, void* user);
    /* Memory for the mod, `size` bytes aligned to `align` (a power of two up to 65536), counted on the Mods screen and
       freed when the mod is taken out. Null: none left (the mod says so). From any thread. */
    void* (*alloc)(const LumaMod* mod, uint64_t size, uint64_t align);
    /* Gives back what `alloc` gave this mod; null does nothing. */
    void (*free)(const LumaMod* mod, void* p);
    /* Something went wrong in the mod's code at `file`:`line` (loader contract 6, D3-1). `code` picks the words: 900 to
       9999 lumart's (Script.S0901...), args {0}, {1}... in order; 1 to 99 the kernel's and luma.h's own (LUMA_REPORT_*).
       Said once in the game's log and kept for the Mods page in the player's language; repeats are counted. Args are
       UTF-8 and copied; at most 8 are kept. From any thread. */
    void (*report)(const LumaMod* mod, const char* file, uint32_t line, uint32_t code, const char* const* args,
                   uint32_t arg_count);

    /* Objects (objects contract). The object's own class (the most derived), from the object or any of its
       polymorphic subobjects: its number (class_of's). -1: object is null. -2: its class has no row (no name a mod
       can write). -3: not a game object the kernel can read (the object or its vtable pointer not aligned as a
       pointer; on Windows, a locator that is not one). -2 and -3 are reported for `mod`, once each. Any thread. */
    int32_t (*own_class)(const LumaMod* mod, const void* object);
    /* The address of the object's `class_number` subobject when its own class is that class or derives from it, else
       null. Null too when the class is in the object twice (an ambiguous base), reported. Any thread. */
    const void* (*as_class)(const LumaMod* mod, const void* object, int32_t class_number);
    /* The virtual call of a virtual function a record declares ("LiveActor::control", "void()"), as import kind 8
       binds it: invoke(args, result) with args[0] the object as the declaring class (as_class gives it); the object's
       own class's function runs, and that function's hooks with it. `hooked` is null. Null: no such virtual (or one
       no mod can name). */
    const LumaFunction* (*method)(const char* name, const char* signature);
    /* The function row the object's own class runs for that virtual, or null: no row (inline in a header, implicit),
       an unknown name, -2 or -3 as own_class, or two subobjects of the declaring class with different overriders and
       the object pointer at neither (reported, ambiguous). Any thread. */
    const LumaFunction* (*overrider)(const LumaMod* mod, const void* object, const char* name, const char* signature);
    /* method() with the adjustment done: sets args[0] to `object` as the declaring class, then invokes (args may be
       null for a virtual with no parameters). 0: called; -1: no such virtual, or args null for one with parameters;
       -2: the object is not of that class (or ambiguous, reported); -3: not readable (reported). The thread the call
       may run on is the called function's. */
    int32_t (*call_method)(const LumaMod* mod, const void* object, const char* name, const char* signature, void** args,
                           void* result);

    /* State across a reload (reload.md 4), the only thing carried from one load of the mod to the next. In `stop`
       only (`mod` the one start was given): the game holds a copy of `size` bytes, at most 1 MiB, for the mod's next
       load this session; the last call wins, and a size of 0 keeps nothing. 0: kept; -1: not in stop, more than 1 MiB,
       bytes null with a size, or no memory; -3: not on the game thread. */
    int32_t (*keep)(const LumaMod* mod, const void* bytes, uint32_t size);
    /* In `start` only: the bytes the mod's previous load kept this session, their count in *size, or null (none kept,
       or not in start; *size 0). Valid until start returns; once read, they are gone. */
    const void* (*kept)(const LumaMod* mod, uint32_t* size);

    /* Classes (docs/modding/language/classes.md). Defines a mod's class: its number (>= 0; class_of, own_class, as_class and
       is_a know it), or -1 the definition is not whole (null, size, empty or long name, base not a polymorphic class
       with a row, data_align), -2 the name (or `replaces`) is taken, -3 not on the game thread, -4 an override names
       no virtual of the base, or one the base runs with no row (inline, implicit), or one whose hook is refused, -5 no
       factory row of the maker's name, or `replaces` is no name the factory makes. Every refusal is said in the log
       with the name it is about, and nothing of the class is left. Its hooks are the mod's: they go when it goes, and
       its objects are then plain objects of the base. */
    int32_t (*define_class)(const LumaMod* mod, const LumaClassDef* def);
    /* The object's data when it is one of the class's (from the object or any of its subobjects), else null. Any
       thread. */
    void* (*class_data)(const LumaMod* mod, const void* object, int32_t class_number);

    /* Numbers in the game's code (docs/modding/language/numbers.md): every number written in a function's code that the game
       reads at run time, named by the function's row name, its signature, and its place there (the k-th named number,
       from 1, in source order; luma_numbers.json lists them all), and every file's number constant by its name, with
       signature "" and place 0. A null signature asks for the only function of that name with a number at that place.
       number: its address and, in *kind, its kind (1 f32, 2 f64, 3 s8, 4 u8, 5 s16, 6 u16, 7 s32, 8 u32, 9 s64,
       10 u64, 11 bool: a file's constant, 0 or 1), or null and kind 0: no such number, or several (give the
       signature); said in the log. Any thread. */
    void* (*number)(const LumaMod* mod, const char* name, const char* signature, uint32_t place, uint32_t* kind);
    /* Sets one to `value` for the mod. When the mod goes, every number it set is again what the mods still here set
       last, or the game's own (a write through number's address is not put back). 0: set; -1: no such number, several,
       or no memory (said); -2: the value is not one of its kind (a fraction for a whole number, out of range; said);
       -3: not on the game thread. */
    int32_t (*set_number)(const LumaMod* mod, const char* name, const char* signature, uint32_t place, double value);

    /* Tables of the game's data (docs/modding/language/numbers.md): every file-scope or class-static array or plain record
       with an initializer, by its name (`File.cpp#name` when only its file sees it; luma_numbers.json "tables" lists
       them with their types). table: 0 and *out filled (data as the game reads it now, its element type,
       the size in bytes, the count of elements, 1 for a record), or -1: no such table, or no memory (said). Any
       thread. */
    int32_t (*table)(const LumaMod* mod, const char* name, LumaTable* out);
    /* Writes `size` bytes at `offset` in the table for the mod. When the mod goes, the table is the game's again with
       the writes of the mods still here, in their order. 0: written; -1: no such table, or no memory (said); -2: the
       bytes are not inside the table, or none (said); -3: not on the game thread. */
    int32_t (*set_table)(const LumaMod* mod, const char* name, uint32_t offset, const void* bytes, uint32_t size);

    /* Objects (classes contract, "make"): `count` objects by a name the factory makes (a game object's, a mod class's,
       a mod's object: as a stage places it, its changes and replacements applied), each made and initialised as a
       placed one, at `place[i]` (null: the origin, unturned, scale 1, every argument -1). While the scene makes its
       objects (from a hook that runs then: after StageDataHolder::initPlacementMario, a class's init) or once it is
       made (during play: each object then takes its after-placement step in this call); they stand in the main stage
       and live as long as the scene; made inside another object's init, they join that placement's view group. Flags: LUMA_MAKE_DEAD. Each object's address in objects[i] (objects may be null).
       Returns the count made (fewer when a creator makes nothing, said), or -1 the factory makes nothing by that
       name, -2 its files are not in this game, -3 not on the game thread, no scene, or the scene is starting,
       -4 a bad argument (count not 1 to 1024, a place smaller than LumaMakePlace or not finite, an unknown flag) or no
       memory. Every refusal is said in the log with the name. */
    int32_t (*make)(const LumaMod* mod, const char* name, uint32_t count, const LumaMakePlace* place, uint32_t flags,
                    void** objects);
    /* Another name for a mod's class (classes contract, "alias"): a stage that places `name`, and make by it, make an
       object of the live mod class `class_name` (any mod's), whose placement argument i (Obj_arg0 to 7) reads as
       args[i] while it initialises, where args[i] is not -1 (-1: the placement's own). The alias is the calling mod's:
       it goes when that mod goes or the class does. Called again for a name the mod aliased, it changes that alias.
       0: the alias stands; -1 a bad argument (null, a name empty or over 63 bytes, null args), -2 the name is taken (a
       game class's, a factory row's, a mod class's or another mod's alias), -3 not on the game thread, -5 class_name
       is no live mod class. Every refusal is said in the log with the name. */
    int32_t (*alias_class)(const LumaMod* mod, const char* name, const char* class_name, const int32_t args[8]);
    /* The placement a stage made `object` from (classes contract, "placement"): 0 and *out, or -1 none (no scene, an
       object made by code or by make, or not one the stage's placements made), -3 not on the game thread, -4 a null
       object or out, or out smaller than LumaPlacement (said). The scene's objects live as long as the scene. */
    int32_t (*placement_of)(const LumaMod* mod, const void* object, LumaPlacement* out);
    /* The object the scene made from a placement (`place`'s zone, layer, index; its stage null or the main stage's
       name; names compare with ASCII case folded), or null: no such placement in this scene, its creator made
       nothing, it is not made yet, not on the game thread, or a null place or one smaller than LumaPlacement (said). */
    void* (*placed_at)(const LumaMod* mod, const LumaPlacement* place);
    /* A gravity of the live mod class `class_name`, whose base is PlanetGravity (classes contract, "gravity"): made
       with `place`'s fields, its data zeroed and its `made` run, then registered with the scene's gravity manager,
       which asks its calcOwnGravityVector (the class's override: pDest the pull's direction, pScalar the distance
       it falls off with) at every point any object asks its gravity at. It lives as long as the scene (`gone` with
       a null object when the scene goes); when its mod goes it pulls nowhere. Null: not on the game thread, or said
       (no such live class, not a gravity class, no scene with gravities, no memory, a null place or one smaller than
       LumaGravityPlace, a range that is not a number, a type bit above 8). */
    void* (*make_gravity)(const LumaMod* mod, const char* class_name, const LumaGravityPlace* place);
} LumaCore;

/* report's own codes (D3-1). */
#define LUMA_REPORT_OUT_OF_MEMORY 1u /* Hook.OutOfMemory: args[0] the size; luma.h's `new` then stops the game */
/* The kernel's own, said for a mod by LumaCore (not passed to report): 2 Hook.NoResult and 3 Hook.SkipRefused (a hook's
   result), 4 Hook.ServiceNotGranted, and the objects contract's 5 Object.NoRow (found: the class's type_info name as
   the ABI writes it), 6 Object.NotReadable (where: the call), 7 Object.Ambiguous (where, found: the class). */

/* One function a library mod gives the mods that need it (loader D3-3). Names "<module>.<fn>" and the signature form
   are lumascript-abi R-2's; the kernel compares both strings and nothing else. Sorted by name. */
typedef struct LumaExport {
    const char* name;
    const char* signature;
    const void* address;
} LumaExport;

typedef struct LumaModInfo {
    uint32_t size;
    uint32_t api;
    const LumaImport* imports;
    uint32_t import_count;
    /* After every slot is filled. Non-zero: the mod could not start and is left off. May be null. */
    int32_t (*start)(const LumaCore* core, const LumaMod* mod);
    /* When the mod is taken out, after its hooks are: other threads may still be inside one until the game's grace
       ends, so it frees nothing they read (memory from `alloc` and its globals go after the grace). From stop and
       finish (and after a start that refused), hook, define_class, alias_class, set_number, set_table and make_gravity
       refuse (-1, or null), said in the log: what the mod added then would outlive it. May be null. */
    void (*stop)(void);
    /* luma.h's, filled by LUMA_MOD: before `start`, it keeps the core for `new` and runs the C++ globals' constructors
       (the system's loader runs none: loader contract 8, LR-6). */
    void (*begin)(const LumaCore* core, const LumaMod* mod);
    /* luma.h's: after the grace, the destructors `atexit` took, the last first; then the game frees the mod's memory
       and closes its library. */
    void (*finish)(void);
    /* A library mod's functions for the mods that need it (kind 6 imports); none for most mods. */
    const LumaExport* exports;
    uint32_t export_count;
    /* A library mod is told that `dependent`, which imported from it, is being taken out: after the dependent's stop,
       before its grace, on the game thread. It drops what it keeps for that mod (callbacks into its code). May be
       null. */
    void (*dependent_gone)(const LumaMod* dependent);
    /* Which of the mod's layers with `"when": {"code": true}` are on. Called once a start, on the game thread, after
       every mod's begin and before any mod's start; not called when the mod is loaded again while the game runs.
       Writes the chosen layer names into buf, each with its NUL, and returns the bytes they need; first called with
       buf null and size 0, and when size is too small it writes nothing and is called again with the size it returned.
       0 or less: none chosen. A name that is not one of the mod's code layers is left out and said in the mod log.
       The game's content is cooked with the answer: an answer that differs from the one the content was cooked with
       starts the game again once, before it reads any content. Null: none of its code layers is on. */
    int64_t (*layers)(const LumaCore* core, const LumaMod* mod, char* buf, uint64_t size);
    /* The layout of the data LumaScript's runtime passes between mods (text, lists), as the LumaLink that built the
       mod numbers it; 0: the mod has no LumaScript runtime or was built before this field. A mod that calls
       another mod's functions (kind 6) is refused when both numbers are non-zero and differ (L513), and an optional
       such import stays null: the older mod is rebuilt with a current LumaLink. Read only when `size` covers it. */
    uint32_t runtime_layout;
} LumaModInfo;

#if defined(_WIN32)
#define LUMA_EXPORT __declspec(dllexport)
#else
#define LUMA_EXPORT __attribute__((visibility("default")))
#endif

#define LUMA_FUNCTION(slot, name, signature) {LUMA_IMPORT_FUNCTION, 0, name, signature, &slot, 0, 0, 0}
#define LUMA_FIELD(slot, class_and_field, type) {LUMA_IMPORT_FIELD, 0, class_and_field, type, &slot, 0, 0, 0}
#define LUMA_SIZE(slot, class_name) {LUMA_IMPORT_SIZE, 0, class_name, 0, &slot, 0, 0, 0}
#define LUMA_CLASS(slot, class_name) {LUMA_IMPORT_CLASS, 0, class_name, 0, &slot, 0, 0, 0}
#define LUMA_HOOK_ON(name, signature, kind, hook) {LUMA_IMPORT_HOOK, 0, name, signature, 0, hook, kind, 0}
#define LUMA_SERVICE(slot, name, major_minor) {LUMA_IMPORT_SERVICE, 0, name, major_minor, &slot, 0, 0, 0}
#define LUMA_GLOBAL(slot, name, type) {LUMA_IMPORT_GLOBAL, 0, name, type, &slot, 0, 0, 0}
#define LUMA_METHOD(slot, name, signature) {LUMA_IMPORT_METHOD, 0, name, signature, &slot, 0, 0, 0}
/* A bit-field bound by kind 1 (its slot is its position in bits): read and write `width` bits of `object`. */
#define LUMA_BITS_GET(object, position, width) luma_bits_get((const void*)(object), (position), (width))
#define LUMA_BITS_SET(object, position, width, value) luma_bits_set((void*)(object), (position), (width), (value))
static inline uint32_t luma_bits_get(const void* object, int32_t position, uint32_t width) {
    const unsigned char* at = (const unsigned char*)object + position / 8;
    uint64_t word = 0;
    for (uint32_t i = 0; i < (position % 8 + width + 7) / 8; i++) word |= (uint64_t)at[i] << (8 * i);
    return (uint32_t)((word >> (position % 8)) & ((width >= 32 ? 0xFFFFFFFFull : (1ull << width) - 1)));
}
static inline void luma_bits_set(void* object, int32_t position, uint32_t width, uint32_t value) {
    unsigned char* at = (unsigned char*)object + position / 8;
    const uint32_t bytes = (position % 8 + width + 7) / 8;
    uint64_t word = 0;
    for (uint32_t i = 0; i < bytes; i++) word |= (uint64_t)at[i] << (8 * i);
    const uint64_t mask = (width >= 32 ? 0xFFFFFFFFull : (1ull << width) - 1) << (position % 8);
    word = (word & ~mask) | (((uint64_t)value << (position % 8)) & mask);
    for (uint32_t i = 0; i < bytes; i++) at[i] = (unsigned char)(word >> (8 * i));
}
#define LUMA_MOD_FUNCTION(slot, mod_and_name, signature) {LUMA_IMPORT_MOD_FUNCTION, 0, mod_and_name, signature, &slot, 0, 0, 0}
#define LUMA_EXPORT_FUNCTION(name, signature, function) {name, signature, (const void*)(function)}

/* A library with no C library that uses floats needs this one symbol the Windows compiler expects. */
#if defined(_WIN32)
#define LUMA_FLOATS __declspec(selectany) int _fltused = 0;
#else
#define LUMA_FLOATS
#endif

/* What the compiler calls in a library with no C library: copies, zeroing and comparing (a struct copy, `= {0}`), and
   on Windows the stack probe of a frame over 4 KiB (loader contract 8). The library defines them itself, once, with
   luma_mod; they are not exported. */
#if defined(__cplusplus)
#define LUMA_C extern "C"
#define LUMA_C_EXTERN extern "C"
#else
#define LUMA_C
#define LUMA_C_EXTERN extern
#endif
#if defined(_WIN32)
#define LUMA_OWN
#else
#define LUMA_OWN __attribute__((visibility("hidden")))
#endif
/* Apple's code generator zeroes memory (`= {0}`, a memset of 0) by calling the system's own entry, not memset: bzero
   on arm64, __bzero on x86-64. */
#if defined(__APPLE__) && defined(__x86_64__)
#define LUMA_ZERO_NAME __bzero
#elif defined(__APPLE__)
#define LUMA_ZERO_NAME bzero
#endif
#if defined(LUMA_ZERO_NAME)
#define LUMA_ZERO                                                                                                     \
    LUMA_C LUMA_OWN __attribute__((no_builtin, used)) void LUMA_ZERO_NAME(void* to, __SIZE_TYPE__ n) {                \
        unsigned char* t = (unsigned char*)to;                                                                        \
        while (n-- != 0) *t++ = 0;                                                                                    \
    }
#else
#define LUMA_ZERO
#endif
#define LUMA_MEMORY                                                                                                   \
    LUMA_ZERO                                                                                                         \
    LUMA_C LUMA_OWN __attribute__((no_builtin, used)) void* memcpy(void* to, const void* from, __SIZE_TYPE__ n) {     \
        unsigned char* t = (unsigned char*)to;                                                                        \
        const unsigned char* f = (const unsigned char*)from;                                                          \
        while (n-- != 0) *t++ = *f++;                                                                                 \
        return to;                                                                                                    \
    }                                                                                                                 \
    LUMA_C LUMA_OWN __attribute__((no_builtin, used)) void* memmove(void* to, const void* from, __SIZE_TYPE__ n) {    \
        unsigned char* t = (unsigned char*)to;                                                                        \
        const unsigned char* f = (const unsigned char*)from;                                                          \
        if (t < f) {                                                                                                  \
            while (n-- != 0) *t++ = *f++;                                                                             \
        } else {                                                                                                      \
            while (n-- != 0) t[n] = f[n];                                                                             \
        }                                                                                                             \
        return to;                                                                                                    \
    }                                                                                                                 \
    LUMA_C LUMA_OWN __attribute__((no_builtin, used)) void* memset(void* to, int value, __SIZE_TYPE__ n) {            \
        unsigned char* t = (unsigned char*)to;                                                                        \
        while (n-- != 0) *t++ = (unsigned char)value;                                                                 \
        return to;                                                                                                    \
    }                                                                                                                 \
    LUMA_C LUMA_OWN __attribute__((no_builtin, used)) int memcmp(const void* a, const void* b, __SIZE_TYPE__ n) {     \
        const unsigned char* x = (const unsigned char*)a;                                                             \
        const unsigned char* y = (const unsigned char*)b;                                                             \
        for (; n != 0; n--, x++, y++) {                                                                               \
            if (*x != *y) return *x < *y ? -1 : 1;                                                                    \
        }                                                                                                             \
        return 0;                                                                                                     \
    }

/* __chkstk as the Windows ABI has it: x64 takes the frame's size in rax and keeps every register; arm64 takes it in x15
   in 16-byte units. Each touches the frame's pages from the top down, so the guard page grows the stack in order. */
#if defined(_WIN32) && defined(__x86_64__)
#define LUMA_STACK_PROBE                                                                                              \
    __asm__(".text\n.globl __chkstk\n.p2align 4\n__chkstk:\n"                                                         \
            "pushq %rcx\npushq %rax\ncmpq $0x1000, %rax\nleaq 24(%rsp), %rcx\njb 1f\n"                                \
            "2:\nsubq $0x1000, %rcx\ntestq %rcx, (%rcx)\nsubq $0x1000, %rax\ncmpq $0x1000, %rax\nja 2b\n"             \
            "1:\nsubq %rax, %rcx\ntestq %rcx, (%rcx)\npopq %rax\npopq %rcx\nretq\n");
#elif defined(_WIN32) && defined(__aarch64__)
#define LUMA_STACK_PROBE                                                                                              \
    __asm__(".text\n.globl __chkstk\n.p2align 2\n__chkstk:\n"                                                         \
            "lsl x16, x15, #4\nmov x17, sp\n"                                                                         \
            "1:\nsub x17, x17, #4096\nsubs x16, x16, #4096\nldr xzr, [x17]\nb.gt 1b\nret\n");
#else
#define LUMA_STACK_PROBE
#endif

/* A mod's life (loader contract 8, LR-6, Draft 2 review). Its global constructors run in `begin`, after the core is
   known; the system's loader runs none:
   - Windows: the library has no C runtime, so nothing else runs `.CRT$XCU`; luma.h runs what lies between its own
     `.CRT$XCA` and `.CRT$XCZ` markers.
   - Linux and Android: the build's `-fno-use-init-array` and luma_ctors.ld put them in `luma_ctors` (always made, a
     null entry first), not `.init_array`; the bounds are strong, so a link without the script fails.
   - Apple: the build runs macho_ctors.py over each object, which makes the init section `__DATA,luma_ctors` of type
     S_REGULAR, so dyld runs none; ld64 gives `___dso_handle`.
   Destructors registered with `atexit` (Windows) or `__cxa_atexit` (elsewhere) run in `finish`, the last first; the
   list grows through the core's memory. Then `finish` runs the terminators the C runtime would have run at exit (a
   destructor attribute's function the compiler did not register with atexit), which the system would otherwise run
   never (Windows `.CRT$XT*`, ELF `.dtors`) or after the mod's memory is gone (Mach-O `__mod_term_func`):
   - Windows: what lies between luma.h's `.CRT$XTA` and `.CRT$XTZ` markers, last to first.
   - Linux and Android: luma_ctors.ld puts `.dtors` in `luma_dtors` (a null entry first), run first to last.
   On every system the order is GCC's: constructors by priority, 101 first; terminators the other way round.
   - Apple: macho_ctors.py makes a term section `__DATA,luma_dtors` of type S_REGULAR, run first to last. Apple's clang
     sends a destructor attribute through `__cxa_atexit`, so only assembly or another compiler makes one. */
typedef void (*LumaCtor)(void);
#if defined(_WIN32)
#define LUMA_CTORS                                                                                                    \
    LUMA_C __attribute__((section(".CRT$XCA"), used)) const LumaCtor luma_ctors_a = 0;                                \
    LUMA_C __attribute__((section(".CRT$XCZ"), used)) const LumaCtor luma_ctors_z = 0;                                \
    static void luma_run_ctors(void) {                                                                                \
        const LumaCtor* at = &luma_ctors_a;                                                                           \
        const LumaCtor* end = &luma_ctors_z;                                                                          \
        __asm__("" : "+r"(at), "+r"(end)); /* two objects' addresses: the compiler may not reason across them */      \
        for (at++; at < end; at++) {                                                                                  \
            if (*at != 0) (*at)();                                                                                    \
        }                                                                                                             \
    }                                                                                                                 \
    LUMA_C __attribute__((section(".CRT$XTA"), used)) const LumaCtor luma_dtors_a = 0;                                \
    LUMA_C __attribute__((section(".CRT$XTZ"), used)) const LumaCtor luma_dtors_z = 0;                                \
    static void luma_run_dtors(void) {                                                                                \
        /* The last first: the compiler places priority 101's in .CRT$XTA00101, before the plain ones in .CRT$XTX, */ \
        /* so this gives the order ELF's .dtors gives (101's terminator last). */                                     \
        const LumaCtor* first = &luma_dtors_a;                                                                        \
        const LumaCtor* at = &luma_dtors_z;                                                                           \
        __asm__("" : "+r"(first), "+r"(at));                                                                          \
        for (at--; at > first; at--) {                                                                                \
            if (*at != 0) (*at)();                                                                                    \
        }                                                                                                             \
    }                                                                                                                 \
    LUMA_C int atexit(void (*fn)(void)) { return luma_add_exit(0, 0, fn); }
#elif defined(__APPLE__)
#define LUMA_CTORS                                                                                                    \
    LUMA_C __attribute__((section("__DATA,luma_ctors"), used)) const LumaCtor luma_ctors_zero = 0;                    \
    LUMA_C_EXTERN LumaCtor luma_ctors_start[] __asm("section$start$__DATA$luma_ctors");                               \
    LUMA_C_EXTERN LumaCtor luma_ctors_end[] __asm("section$end$__DATA$luma_ctors");                                   \
    static void luma_run_ctors(void) {                                                                                \
        for (LumaCtor* at = luma_ctors_start; at != luma_ctors_end; at++) {                                           \
            if (*at != 0) (*at)();                                                                                    \
        }                                                                                                             \
    }                                                                                                                 \
    LUMA_C __attribute__((section("__DATA,luma_dtors"), used)) const LumaCtor luma_dtors_zero = 0;                    \
    LUMA_C_EXTERN LumaCtor luma_dtors_start[] __asm("section$start$__DATA$luma_dtors");                               \
    LUMA_C_EXTERN LumaCtor luma_dtors_end[] __asm("section$end$__DATA$luma_dtors");                                   \
    static void luma_run_dtors(void) {                                                                                \
        for (LumaCtor* at = luma_dtors_start; at != luma_dtors_end; at++) {                                           \
            if (*at != 0) (*at)();                                                                                    \
        }                                                                                                             \
    }                                                                                                                 \
    LUMA_C LUMA_OWN int __cxa_atexit(void (*fn)(void*), void* arg, void* dso) {                                       \
        (void)dso;                                                                                                    \
        return luma_add_exit(fn, arg, 0);                                                                             \
    }
#else
#define LUMA_CTORS                                                                                                    \
    LUMA_C_EXTERN __attribute__((visibility("hidden"))) LumaCtor __start_luma_ctors[];                                \
    LUMA_C_EXTERN __attribute__((visibility("hidden"))) LumaCtor __stop_luma_ctors[];                                 \
    static void luma_run_ctors(void) {                                                                                \
        /* .ctors order: the last first; the script's null entry is skipped. */                                       \
        for (LumaCtor* at = __stop_luma_ctors; at != __start_luma_ctors;) {                                           \
            --at;                                                                                                     \
            if (*at != 0) (*at)();                                                                                    \
        }                                                                                                             \
    }                                                                                                                 \
    LUMA_C_EXTERN __attribute__((visibility("hidden"))) LumaCtor __start_luma_dtors[];                                \
    LUMA_C_EXTERN __attribute__((visibility("hidden"))) LumaCtor __stop_luma_dtors[];                                 \
    static void luma_run_dtors(void) {                                                                                \
        /* .dtors order: the first first; the script's null entry is skipped. */                                      \
        for (LumaCtor* at = __start_luma_dtors; at != __stop_luma_dtors; at++) {                                      \
            if (*at != 0) (*at)();                                                                                    \
        }                                                                                                             \
    }                                                                                                                 \
    LUMA_C LUMA_OWN void* __dso_handle = &__dso_handle;                                                               \
    LUMA_C LUMA_OWN int __cxa_atexit(void (*fn)(void*), void* arg, void* dso) {                                       \
        (void)dso;                                                                                                    \
        return luma_add_exit(fn, arg, 0);                                                                             \
    }
#endif

#define LUMA_LIFE                                                                                                     \
    LUMA_C LUMA_OWN const LumaCore* luma_rt_core = 0;                                                                 \
    LUMA_C LUMA_OWN const LumaMod* luma_rt_self = 0;                                                                  \
    typedef struct {                                                                                                  \
        void (*fn)(void*);                                                                                            \
        void* arg;                                                                                                    \
        void (*plain)(void);                                                                                          \
    } LumaExit;                                                                                                       \
    static LumaExit luma_exits_first[16];                                                                             \
    static LumaExit* luma_exits = luma_exits_first;                                                                   \
    static unsigned luma_exit_count = 0;                                                                              \
    static unsigned luma_exit_room = 16;                                                                              \
    static int luma_add_exit(void (*fn)(void*), void* arg, void (*plain)(void)) {                                     \
        if (luma_exit_count == luma_exit_room) {                                                                      \
            /* 64 more at a time from the core (D2-4); none: this one is refused and said. */                         \
            LumaExit* more = luma_rt_core != 0 ? (LumaExit*)luma_rt_core->alloc(luma_rt_self,                         \
                                 (luma_exit_room + 64u) * sizeof(LumaExit), 16) : 0;                                  \
            if (more == 0) {                                                                                          \
                if (luma_rt_core != 0) luma_rt_core->log(luma_rt_self, "no memory for one more destructor; it will not run"); \
                return -1;                                                                                            \
            }                                                                                                         \
            for (unsigned i = 0; i < luma_exit_count; i++) more[i] = luma_exits[i];                                   \
            if (luma_exits != luma_exits_first) luma_rt_core->free(luma_rt_self, luma_exits);                         \
            luma_exits = more;                                                                                        \
            luma_exit_room += 64u;                                                                                    \
        }                                                                                                             \
        luma_exits[luma_exit_count].fn = fn;                                                                          \
        luma_exits[luma_exit_count].arg = arg;                                                                        \
        luma_exits[luma_exit_count].plain = plain;                                                                    \
        luma_exit_count++;                                                                                            \
        return 0;                                                                                                     \
    }                                                                                                                 \
    LUMA_CTORS                                                                                                        \
    static void luma_begin(const LumaCore* core, const LumaMod* mod) {                                                \
        luma_rt_core = core;                                                                                          \
        luma_rt_self = mod;                                                                                           \
        luma_run_ctors();                                                                                             \
    }                                                                                                                 \
    static void luma_finish(void) {                                                                                   \
        while (luma_exit_count != 0) {                                                                                \
            luma_exit_count--;                                                                                        \
            if (luma_exits[luma_exit_count].plain != 0) {                                                             \
                luma_exits[luma_exit_count].plain();                                                                  \
            } else {                                                                                                  \
                luma_exits[luma_exit_count].fn(luma_exits[luma_exit_count].arg);                                      \
            }                                                                                                         \
        }                                                                                                             \
        luma_run_dtors();                                                                                             \
    }                                                                                                                 \

#define LUMA_RUNTIME \
    LUMA_FLOATS      \
    LUMA_MEMORY      \
    LUMA_STACK_PROBE \
    LUMA_LIFE        \
    LUMA_NEW

#define LUMA_MOD(imports, start, stop) \
    LUMA_RUNTIME                       \
    LUMA_C LUMA_EXPORT const LumaModInfo luma_mod = {sizeof(LumaModInfo), LUMA_API, imports, sizeof(imports) / sizeof(imports[0]), start, stop, luma_begin, luma_finish, 0, 0, 0, 0, 0}
/* A mod that binds no name of the game (it finds what it needs through the core at start). */
#define LUMA_MOD_NO_IMPORTS(start, stop) \
    LUMA_RUNTIME                         \
    LUMA_C LUMA_EXPORT const LumaModInfo luma_mod = {sizeof(LumaModInfo), LUMA_API, 0, 0, start, stop, luma_begin, luma_finish, 0, 0, 0, 0, 0}
/* A library mod (loader D3-3): its exports (LUMA_EXPORT_FUNCTION rows, sorted by name) and what it does when a mod that
   needs it is taken out. */
#define LUMA_LIBRARY_MOD(imports, start, stop, exports, dependent_gone) \
    LUMA_RUNTIME                                                        \
    LUMA_C LUMA_EXPORT const LumaModInfo luma_mod = {sizeof(LumaModInfo), LUMA_API, imports, sizeof(imports) / sizeof(imports[0]), start, stop, luma_begin, luma_finish, exports, sizeof(exports) / sizeof(exports[0]), dependent_gone, 0, 0}

/* An argument of a hooked call, and its result. */
#define LUMA_ARG(call, n, type) (*(type*)(call)->args[n])
#define LUMA_THIS(call, type) ((type*)(call)->args[0])
#define LUMA_RESULT(call, type) (*(type*)(call)->result)

#ifdef __cplusplus
}

/* C++'s `new` and `delete` over the core's memory (LR-6), in a mod's library (built freestanding, loader contract 8;
   the game's own code, built hosted, includes luma.h for its types only). Include <new> before luma.h or not at all:
   without it, luma.h declares what `new` needs. `new` of 0 bytes takes 1; alignments go up to 65536. A throwing `new`
   the system has no memory for logs and stops the game (no exceptions); the nothrow forms give null. Placement `new`
   is the usual one. */
#if !__STDC_HOSTED__
#if !defined(_NEW_) && !defined(_LIBCPP_NEW) && !defined(_NEW) && !defined(__NEW__) && !defined(__NOTHROW_T_DEFINED)
namespace std {
    enum class align_val_t : __SIZE_TYPE__ {};
    struct nothrow_t {
        explicit nothrow_t() = default;
    };
    extern const nothrow_t nothrow;
}
inline void* operator new(__SIZE_TYPE__, void* p) noexcept { return p; }
inline void* operator new[](__SIZE_TYPE__, void* p) noexcept { return p; }
inline void operator delete(void*, void*) noexcept {}
inline void operator delete[](void*, void*) noexcept {}
void* operator new(__SIZE_TYPE__, const std::nothrow_t&) noexcept;
void* operator new[](__SIZE_TYPE__, const std::nothrow_t&) noexcept;
void* operator new(__SIZE_TYPE__, std::align_val_t, const std::nothrow_t&) noexcept;
void* operator new[](__SIZE_TYPE__, std::align_val_t, const std::nothrow_t&) noexcept;
void operator delete(void*, const std::nothrow_t&) noexcept;
void operator delete[](void*, const std::nothrow_t&) noexcept;
void operator delete(void*, std::align_val_t, const std::nothrow_t&) noexcept;
void operator delete[](void*, std::align_val_t, const std::nothrow_t&) noexcept;
#endif
extern "C" const LumaCore* luma_rt_core;
extern "C" const LumaMod* luma_rt_self;
/* Off Windows these are visible by the language's rule; the build's luma_mod.map exports luma_mod alone and
   -Bsymbolic binds the mod's own calls to them (loader contract 8). */
#define LUMA_NEW                                                                                                      \
    namespace std {                                                                                                   \
        const nothrow_t nothrow{};                                                                                    \
    }                                                                                                                 \
    static void* luma_new(__SIZE_TYPE__ size, __SIZE_TYPE__ align, bool must) {                                       \
        void* const p = luma_rt_core != 0 ? luma_rt_core->alloc(luma_rt_self, size != 0 ? size : 1, align < 16 ? 16 : align) : 0; \
        if (p == 0 && must) {                                                                                         \
            /* Hook.OutOfMemory through report (D3-1), then the game stops with its crash report (D2-7). */           \
            if (luma_rt_core != 0) {                                                                                  \
                char digits[24];                                                                                      \
                char* at = digits + sizeof(digits) - 1;                                                               \
                *at = 0;                                                                                              \
                unsigned long long n = size;                                                                          \
                do {                                                                                                  \
                    *--at = (char)('0' + n % 10u);                                                                    \
                    n /= 10u;                                                                                         \
                } while (n != 0);                                                                                     \
                const char* args[1] = {at};                                                                           \
                luma_rt_core->report(luma_rt_self, "luma.h", 0, LUMA_REPORT_OUT_OF_MEMORY, args, 1);                  \
            }                                                                                                         \
            __builtin_trap();                                                                                         \
        }                                                                                                             \
        return p;                                                                                                     \
    }                                                                                                                 \
    static void luma_delete(void* p) {                                                                                \
        if (p != 0 && luma_rt_core != 0) luma_rt_core->free(luma_rt_self, p);                                         \
    }                                                                                                                 \
    void* operator new(__SIZE_TYPE__ n) { return luma_new(n, 16, true); }                                             \
    void* operator new[](__SIZE_TYPE__ n) { return luma_new(n, 16, true); }                                           \
    void* operator new(__SIZE_TYPE__ n, std::align_val_t a) { return luma_new(n, (__SIZE_TYPE__)a, true); }           \
    void* operator new[](__SIZE_TYPE__ n, std::align_val_t a) { return luma_new(n, (__SIZE_TYPE__)a, true); }         \
    void* operator new(__SIZE_TYPE__ n, const std::nothrow_t&) noexcept { return luma_new(n, 16, false); }            \
    void* operator new[](__SIZE_TYPE__ n, const std::nothrow_t&) noexcept { return luma_new(n, 16, false); }          \
    void* operator new(__SIZE_TYPE__ n, std::align_val_t a, const std::nothrow_t&) noexcept {                         \
        return luma_new(n, (__SIZE_TYPE__)a, false);                                                                  \
    }                                                                                                                 \
    void* operator new[](__SIZE_TYPE__ n, std::align_val_t a, const std::nothrow_t&) noexcept {                       \
        return luma_new(n, (__SIZE_TYPE__)a, false);                                                                  \
    }                                                                                                                 \
    void operator delete(void* p) noexcept { luma_delete(p); }                                                        \
    void operator delete[](void* p) noexcept { luma_delete(p); }                                                      \
    void operator delete(void* p, __SIZE_TYPE__) noexcept { luma_delete(p); }                                         \
    void operator delete[](void* p, __SIZE_TYPE__) noexcept { luma_delete(p); }                                       \
    void operator delete(void* p, std::align_val_t) noexcept { luma_delete(p); }                                      \
    void operator delete[](void* p, std::align_val_t) noexcept { luma_delete(p); }                                    \
    void operator delete(void* p, __SIZE_TYPE__, std::align_val_t) noexcept { luma_delete(p); }                       \
    void operator delete[](void* p, __SIZE_TYPE__, std::align_val_t) noexcept { luma_delete(p); }                     \
    void operator delete(void* p, const std::nothrow_t&) noexcept { luma_delete(p); }                                 \
    void operator delete[](void* p, const std::nothrow_t&) noexcept { luma_delete(p); }                               \
    void operator delete(void* p, std::align_val_t, const std::nothrow_t&) noexcept { luma_delete(p); }               \
    void operator delete[](void* p, std::align_val_t, const std::nothrow_t&) noexcept { luma_delete(p); }
#endif /* !__STDC_HOSTED__ */
#endif /* __cplusplus */
#ifndef LUMA_NEW
#define LUMA_NEW
#endif

#endif
