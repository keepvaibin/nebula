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
    LUMA_IMPORT_HOOK = 4,     /* a hook on the function `name` with `signature`: `hook`, `hook_kind`, `user` */
    LUMA_IMPORT_SERVICE = 5   /* a service: `name`, `signature` its "major.minor"; slot: const void** (its table) */
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
} LumaCore;

/* report's own codes (D3-1). */
#define LUMA_REPORT_OUT_OF_MEMORY 1u /* Hook.OutOfMemory: args[0] the size; luma.h's `new` then stops the game */

typedef struct LumaModInfo {
    uint32_t size;
    uint32_t api;
    const LumaImport* imports;
    uint32_t import_count;
    /* After every slot is filled. Non-zero: the mod could not start and is left off. May be null. */
    int32_t (*start)(const LumaCore* core, const LumaMod* mod);
    /* When the mod is taken out, after its hooks are: other threads may still be inside one until the game's grace
       ends, so it frees nothing they read (memory from `alloc` and its globals go after the grace). May be null. */
    void (*stop)(void);
    /* luma.h's, filled by LUMA_MOD: before `start`, it keeps the core for `new` and runs the C++ globals' constructors
       (the system's loader runs none: loader contract 8, LR-6). */
    void (*begin)(const LumaCore* core, const LumaMod* mod);
    /* luma.h's: after the grace, the destructors `atexit` took, the last first; then the game frees the mod's memory
       and closes its library. */
    void (*finish)(void);
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
#define LUMA_MEMORY                                                                                                   \
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
    LUMA_C LUMA_EXPORT const LumaModInfo luma_mod = {sizeof(LumaModInfo), LUMA_API, imports, sizeof(imports) / sizeof(imports[0]), start, stop, luma_begin, luma_finish}
/* A mod that binds no name of the game (it finds what it needs through the core at start). */
#define LUMA_MOD_NO_IMPORTS(start, stop) \
    LUMA_RUNTIME                         \
    LUMA_C LUMA_EXPORT const LumaModInfo luma_mod = {sizeof(LumaModInfo), LUMA_API, 0, 0, start, stop, luma_begin, luma_finish}

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
