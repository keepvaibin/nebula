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

#define LUMA_API 1u
/* The most mods with code that may be on at once (loader contract 2). */
#define LUMA_MAX_MODS 256u

/* A function of the game. `invoke` calls it: args[0] is `this` (null for a function without one), args[1..] point at
   the arguments, and `result` is where what it returns is put. Hooks on the function run. */
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
                     default constructor makes it) until a hook or the function writes it (loader contract 4) */
    uint32_t next;      /* the kernel's */
    const void* chain;  /* the kernel's: the hooks this call runs, fixed when it starts */
} LumaCall;

typedef int32_t (*LumaHook)(LumaCall* call, void* user);

enum {
    LUMA_BEFORE = 0, /* runs first; returning non-zero skips the function (write the result first) */
    LUMA_AFTER = 1,  /* runs last, the last added first; may change the result */
    LUMA_WRAP = 2,   /* runs around the function and calls core->proceed(call) when the rest should run */
    LUMA_REPLACE = 3 /* a wrap only one mod may hold on a function; a second mod's is a conflict */
};

enum {
    LUMA_IMPORT_FUNCTION = 0, /* slot: const LumaFunction** */
    LUMA_IMPORT_FIELD = 1,    /* name "Class.field", signature its type; slot: int32_t* (the offset in the object) */
    LUMA_IMPORT_SIZE = 2,     /* name "Class"; slot: int32_t* (its size in bytes) */
    LUMA_IMPORT_CLASS = 3,    /* name "Class"; slot: int32_t* (its number, for is_a) */
    LUMA_IMPORT_HOOK = 4      /* a hook on the function `name` with `signature`: `hook`, `hook_kind`, `user` */
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

typedef struct LumaCore {
    uint32_t size;
    uint32_t api;

    void (*log)(const LumaMod* mod, const char* text);

    /* A function by name, after loading. `signature` may be null when the name has one function. Null: not found. */
    const LumaFunction* (*function)(const char* name, const char* signature);
    /* Every function: `index` from 0 until it returns null, by name. */
    const LumaFunction* (*function_at)(uint32_t index);
    /* Adds a hook after loading. 0: added; -1: refused (a constructor or destructor takes before and after only, and a
       function whose result has no default takes no skipping hook); -2: another mod replaces the function already;
       -3: not on the game thread (nothing changes). A call already running keeps the hooks it started with. */
    int32_t (*hook)(const LumaMod* mod, const LumaFunction* function, uint32_t kind, LumaHook hook, void* user);
    /* From a wrap or a replace: runs the wraps after it, then the function. */
    void (*proceed)(LumaCall* call);

    /* A field's offset in its object, or -1. `type` may be null; when given it must be the field's type. */
    int32_t (*field)(const char* class_name, const char* field, const char* type);
    /* A class's size, or -1. */
    int32_t (*size_of)(const char* class_name);
    /* A class's number, or -1; is_a says whether an object of the game is of that class or one made from it. */
    int32_t (*class_of)(const char* class_name);
    int32_t (*is_a)(const void* object, int32_t class_number);

    /* A service by name ("fs", "net", "draw"...): its table of functions, or null when this game has no such service
       or an older one than `minor`. A `major` is a table of its own; within it a later `minor` only adds at the end. */
    const void* (*service)(const char* name, uint32_t major, uint32_t minor);

    /* 1 on the game thread (the thread that runs the game's frames and starts the mods), else 0. A hooked function
       runs its hooks on whatever thread calls it. */
    int32_t (*game_thread)(void);
    /* Takes out a hook this mod added (in its imports or with `hook`): the same function, kind, hook and user. 0: taken
       out; -1: this mod has no such hook; -3: not on the game thread (nothing changes). */
    int32_t (*unhook)(const LumaMod* mod, const LumaFunction* function, uint32_t kind, LumaHook hook, void* user);
    /* Memory for the mod, `size` bytes aligned to `align` (a power of two up to 4096), counted on the Mods screen and
       freed when the mod is taken out. Null: none left (the mod says so). From any thread. */
    void* (*alloc)(const LumaMod* mod, uint64_t size, uint64_t align);
    /* Gives back what `alloc` gave this mod; null does nothing. */
    void (*free)(const LumaMod* mod, void* p);
} LumaCore;

typedef struct LumaModInfo {
    uint32_t size;
    uint32_t api;
    const LumaImport* imports;
    uint32_t import_count;
    /* After every slot is filled. Non-zero: the mod could not start and is left off. May be null. */
    int32_t (*start)(const LumaCore* core, const LumaMod* mod);
    /* Before the library is taken out (a reload while making the mod). May be null. */
    void (*stop)(void);
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
#else
#define LUMA_C
#endif
#if defined(_WIN32)
#define LUMA_OWN
#else
#define LUMA_OWN __attribute__((visibility("hidden")))
#endif
#define LUMA_MEMORY                                                                                                   \
    LUMA_C LUMA_OWN __attribute__((no_builtin, used)) void* memcpy(void* to, const void* from, __SIZE_TYPE__ n) {     \
        unsigned char* t = (unsigned char*)to;                                                                       \
        const unsigned char* f = (const unsigned char*)from;                                                         \
        while (n-- != 0) *t++ = *f++;                                                                                \
        return to;                                                                                                   \
    }                                                                                                                \
    LUMA_C LUMA_OWN __attribute__((no_builtin, used)) void* memmove(void* to, const void* from, __SIZE_TYPE__ n) {    \
        unsigned char* t = (unsigned char*)to;                                                                       \
        const unsigned char* f = (const unsigned char*)from;                                                         \
        if (t < f) {                                                                                                 \
            while (n-- != 0) *t++ = *f++;                                                                            \
        } else {                                                                                                     \
            while (n-- != 0) t[n] = f[n];                                                                            \
        }                                                                                                            \
        return to;                                                                                                   \
    }                                                                                                                \
    LUMA_C LUMA_OWN __attribute__((no_builtin, used)) void* memset(void* to, int value, __SIZE_TYPE__ n) {           \
        unsigned char* t = (unsigned char*)to;                                                                       \
        while (n-- != 0) *t++ = (unsigned char)value;                                                                \
        return to;                                                                                                   \
    }                                                                                                                \
    LUMA_C LUMA_OWN __attribute__((no_builtin, used)) int memcmp(const void* a, const void* b, __SIZE_TYPE__ n) {    \
        const unsigned char* x = (const unsigned char*)a;                                                            \
        const unsigned char* y = (const unsigned char*)b;                                                            \
        for (; n != 0; n--, x++, y++) {                                                                              \
            if (*x != *y) return *x < *y ? -1 : 1;                                                                   \
        }                                                                                                            \
        return 0;                                                                                                    \
    }

/* __chkstk as the Windows ABI has it: x64 takes the frame's size in rax and keeps every register; arm64 takes it in x15
   in 16-byte units. Each touches the frame's pages from the top down, so the guard page grows the stack in order. */
#if defined(_WIN32) && defined(__x86_64__)
#define LUMA_STACK_PROBE                                                                                              \
    __asm__(".text\n.globl __chkstk\n.p2align 4\n__chkstk:\n"                                                       \
            "pushq %rcx\npushq %rax\ncmpq $0x1000, %rax\nleaq 24(%rsp), %rcx\njb 1f\n"                               \
            "2:\nsubq $0x1000, %rcx\ntestq %rcx, (%rcx)\nsubq $0x1000, %rax\ncmpq $0x1000, %rax\nja 2b\n"              \
            "1:\nsubq %rax, %rcx\ntestq %rcx, (%rcx)\npopq %rax\npopq %rcx\nretq\n");
#elif defined(_WIN32) && defined(__aarch64__)
#define LUMA_STACK_PROBE                                                                                              \
    __asm__(".text\n.globl __chkstk\n.p2align 2\n__chkstk:\n"                                                       \
            "lsl x16, x15, #4\nmov x17, sp\n"                                                                           \
            "1:\nsub x17, x17, #4096\nsubs x16, x16, #4096\nldr xzr, [x17]\nb.gt 1b\nret\n");
#else
#define LUMA_STACK_PROBE
#endif

#define LUMA_RUNTIME \
    LUMA_FLOATS      \
    LUMA_MEMORY      \
    LUMA_STACK_PROBE

#define LUMA_MOD(imports, start, stop) \
    LUMA_RUNTIME                       \
    LUMA_C LUMA_EXPORT const LumaModInfo luma_mod = {sizeof(LumaModInfo), LUMA_API, imports, sizeof(imports) / sizeof(imports[0]), start, stop}
/* A mod that binds no name of the game (it finds what it needs through the core at start). */
#define LUMA_MOD_NO_IMPORTS(start, stop) \
    LUMA_RUNTIME                         \
    LUMA_C LUMA_EXPORT const LumaModInfo luma_mod = {sizeof(LumaModInfo), LUMA_API, 0, 0, start, stop}

/* An argument of a hooked call, and its result. */
#define LUMA_ARG(call, n, type) (*(type*)(call)->args[n])
#define LUMA_THIS(call, type) ((type*)(call)->args[0])
#define LUMA_RESULT(call, type) (*(type*)(call)->result)

#ifdef __cplusplus
}
#endif

#endif
