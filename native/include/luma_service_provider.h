/* SPDX-License-Identifier: GPL-3.0-only */
/* Providing a LumaCore service (docs/modding/reference/services/calling-services.md): between the game module's kernel and the
   services it or the host provides. Not for mods. */
#ifndef LUMA_SERVICE_PROVIDER_H
#define LUMA_SERVICE_PROVIDER_H

#include <stdint.h>

#include "luma.h"
#include "luma_services.h"

#ifdef __cplusplus
extern "C" {
#endif

/* One thing that can have an answer waiting for a mod (a request, a socket, a device), owned by its provider. The
   kernel's queue links nodes and allocates nothing; a node is queued at most once at a time. */
typedef struct LumaEventNode {
    struct LumaEventNode* next; /* the kernel's */
    uint32_t state;             /* the kernel's: 0 idle, 1 queued */
    uint32_t reserved;
    const LumaMod* mod; /* the provider's: whose event it is */
    /* Game thread, at the frame's start, the node already idle again: builds the LumaEvent(s) and gives them to the
       mod through LumaServiceKernel.call. A post from inside queues the node for the next frame. */
    void (*deliver)(struct LumaEventNode* node);
    /* Game thread: the mod is gone and the node will not be delivered; the node is idle again and the provider's. */
    void (*drop)(struct LumaEventNode* node);
} LumaEventNode;

/* One row a service reports through: its code in the service's block (services-registry 11), the row id in
   NebulaText.tsv, and the names of its arguments in order ({mod} the kernel fills; unused names null). */
typedef struct LumaServiceRow {
    uint32_t code;
    const char* id;
    const char* names[4];
} LumaServiceRow;

/* What the kernel gives a provider. Any thread unless said. */
typedef struct LumaServiceKernel {
    uint32_t size;
    /* The mod's id ("gold-flower"), its place in the Mods list (top 0), and 1 when the player granted it `permission`.
       A mod the kernel does not know (or after its end): null, -1, 0. The id is valid until the mod's mod_end. */
    const char* (*mod_id)(const LumaMod* mod);
    int32_t (*mod_order)(const LumaMod* mod);
    int32_t (*granted)(const LumaMod* mod, const char* permission);
    /* 1: known and not being taken out; 0: unknown, or after its stop. */
    int32_t (*mod_live)(const LumaMod* mod);
    /* 1 on the game thread. */
    int32_t (*game_thread)(void);
    /* A mistake of the mod: the row `code` (the provider's rows, or the registry's 100 to 119); `where` is
       "<service>.<function>", the row's first argument {where}; `args` are the row's arguments after it, in order (at
       most 7). Kept and said as LumaCore.report does: once in the log, kept for the Mods page, repeats counted. */
    void (*report)(const LumaMod* mod, const char* where, uint32_t code, const char* const* args, uint32_t arg_count);
    /* Queues `node` for its mod (from any thread). LUMA_OK: queued, or queued already; LUMA_E_CANCELLED: the mod is
       gone or unknown, nothing queued (the node stays the provider's); LUMA_E_ARGUMENT: a null node, mod or deliver. */
    int32_t (*post)(LumaEventNode* node);
    /* Game thread, from a node's deliver: calls fn(user, event) for the mod (through the guard, which names
       "<service> event <kind>" if fn faults). LUMA_OK: called; LUMA_E_CANCELLED: the mod is gone (not called);
       LUMA_E_THREAD, LUMA_E_ARGUMENT: a provider bug, logged. */
    int32_t (*call)(const LumaMod* mod, const char* service, LumaEventFn fn, void* user, const LumaEvent* event);
    /* Memory counted against the mod (LumaCore.alloc and free on its behalf): shown on the Mods page, freed when the mod
       ends if the provider has not freed it. */
    void* (*alloc)(const LumaMod* mod, uint64_t size, uint64_t align);
    void (*free)(const LumaMod* mod, void* p);
    /* A service function given a null mod, or one mod_live does not know: the function returns LUMA_E_ARGUMENT and
       calls this with "<service>.<function>". One line in the game's log per place, repeats counted and said again at
       10, 100, 1000 (there is no mod to report to). */
    void (*unknown_mod)(const char* where);
    /* Game thread: calls the mod's own fn(a0, a1) through the guard with nothing between them, for a mod function
       whose two parameters are words (pointers or integers) and that is no LumaEventFn. A provider function between
       the guard and the mod's would be a game frame there, and a fault in the mod's code could not be resumed. The
       guard names "<service> event <kind>". Answers as `call`. A kernel built before it has no field (size). */
    int32_t (*call_words)(const LumaMod* mod, const char* service, uint32_t kind, const void* fn, uintptr_t a0, uintptr_t a1);
    /* A mod's memory, which a provider reads and writes only through these: a pointer the mod gave that cannot be read
       (copy_in) or written (copy_out) for `size` bytes, being unmapped, freed, too short or protected, answers
       LUMA_E_ARGUMENT and the game goes on, where a read in the provider's own code would end it (the guard resumes a
       fault only in a mod's code). The provider answers the mod as for any argument it cannot use. A null pointer
       with size 0: LUMA_OK, nothing copied. Any thread. A kernel built before them has no fields (size). */
    int32_t (*copy_in)(const LumaMod* mod, void* dest, const void* source, uint64_t size);
    int32_t (*copy_out)(const LumaMod* mod, void* dest, const void* source, uint64_t size);
    /* A text the mod gave, with its 0, into out (cap bytes, the 0 counted). LUMA_OK, *length its bytes without the 0;
       LUMA_E_ARGUMENT: null, or unreadable before its 0; LUMA_E_TOO_LARGE: no 0 in its first cap - 1 bytes (out holds
       those, and a 0). length may be null. Any thread. */
    int32_t (*copy_text)(const LumaMod* mod, const char* text, char* out, uint64_t cap, uint64_t* length);
    /* 1 when fn lies in the code of `mod` or of a mod it uses, directly or through others (a library acting for it):
       a function the mod may give a service to call back. 0 for anything else, which would fault at an address in no
       mod's code, where the guard cannot resume. Any thread. */
    int32_t (*mod_code)(const LumaMod* mod, const void* fn);
} LumaServiceKernel;

/* A service's table and what the kernel tells its provider. */
typedef struct LumaServiceProvider {
    uint32_t size;
    const char* name;              /* a catalogued name, or one starting with "test." (a host extension's) */
    const LumaServiceTable* table; /* major and minor are read from it; they must be the catalog's */
    const char* permission;        /* a "test." service's own (null: none); a catalogued service's comes from the catalog */
    const LumaServiceRow* rows;    /* its report rows, in its code block */
    uint32_t row_count;
    void* user;
    /* Game thread, before the mod's begin and start: the mod may call the service from then on. May be null. */
    void (*mod_begin)(void* user, const LumaMod* mod);
    /* Game thread, after the mod's stop (or its refused start): cancel what it asked, close what it holds, stop
       posting for it. When this returns no thread of the provider posts for the mod or asks the kernel about it again.
       May be null. */
    void (*mod_gone)(void* user, const LumaMod* mod);
    /* Game thread, after the mod's grace and its finish, before its memory and library go: free what the provider
       kept for it. May be null. */
    void (*mod_end)(void* user, const LumaMod* mod);
    /* Game thread, once, when luma_provide_service accepts the provider: the kernel it uses from then on (a provider
       outside the game module, a host extension's, has no other way to it). May be null. */
    void (*registered)(void* user, const LumaServiceKernel* kernel);
    /* Game thread, at each frame's start before the frame's events are given, in the harness's deterministic mode only
       (the kernel calls it nowhere else): a provider whose answers come from another thread brings in what this
       process already sent itself (one socket to another), so it is given on the same frame every run. Returns when
       that is posted, or after a bound it names in the log. May be null; a provider built before it has no field. */
    void (*frame_start)(void* user);
} LumaServiceProvider;

/* The game module's exports for the host, found by name as luma_load_mod is.
   luma_services_open: game thread, before the first mod loads and before luma_provide_service: checks the catalog,
   registers the module's own services (once; a second call only answers) and gives the kernel for the host's
   providers.
   luma_provide_service: game thread, before the first mod loads. 0: registered. A name neither catalogued nor
   "test.", a second registration of one name and major, a table whose major or minor differs from the catalog's, a
   call after the first mod loaded, or off the game thread: the game stops (NEBULA_PANIC) naming it. The provider and
   everything it points at stay valid for the process's life. */
typedef const LumaServiceKernel* (*LumaServicesOpen)(void);
typedef int32_t (*LumaProvideService)(const LumaServiceProvider* provider);

/* luma_services_describe: any thread, needs no attach and no luma_services_open. The services catalog (services-registry
   9) as JSON, given to `write` in pieces in order: {"services": [{"name", "major", "minor", "permission"}, ...]}, by
   name in byte order, then major; `permission` null or the permission the service needs. Every provider's table is
   checked against these rows when it registers, so they are the services a mod can import on every platform. The
   host's --luma-services and the build (native/tools/luma_describe.c --services) write it as luma_services.json, the
   file the validator reads beside luma_types.json. */
typedef void (*LumaServicesDescribeWrite)(void* user, const char* text, uint32_t length);
typedef void (*LumaServicesDescribe)(LumaServicesDescribeWrite write, void* user);

/* The save half of the Mods page's REMOVED MODS line (services-save 5, services-mods 8): the game module's exports,
   found by name.
   luma_save_kept: any thread. fn(user, id, bytes) once for each mod id with values in the user files in memory (their
   LUMA chunks as last read, copied, started or saved), `bytes` its entries' bytes over all of them; an id being
   forgotten is not given. fn is called after the module's lock is let go (the pairs copied first): it may call the
   module.
   luma_save_forget: any thread. At the next frame's start the id's values leave the user files in memory (the stored
   ones and the loaded file's), and when a stored file held some, the save file is written at once without a window
   (waiting while a save or load runs, and for the start's read); every later save this session leaves them out too.
   From the call on luma_save_kept no longer gives the id. An id outside the id rule is ignored; past 256 ids in one
   session an id is logged and not forgotten. */
typedef void (*LumaSaveKeptFn)(void* user, const char* id, uint64_t bytes);
typedef void (*LumaSaveKept)(LumaSaveKeptFn fn, void* user);
typedef void (*LumaSaveForget)(const char* id);

#ifdef __cplusplus
}

/* For a provider in C++: a mod's struct, which starts with its size (uint32_t), read and written through the kernel's
   copy_in and copy_out, so a pointer the mod got wrong is an answer, not the end of the game. */

/* The size field of the mod's struct at p. LUMA_OK and *size; LUMA_E_ARGUMENT (*size 0): p null or unreadable. */
inline int32_t luma_struct_size(const LumaServiceKernel* kernel, const LumaMod* mod, const void* p, uint32_t* size) {
    *size = 0;
    return p == nullptr ? LUMA_E_ARGUMENT : kernel->copy_in(mod, size, p, sizeof(*size));
}

/* The mod's struct at p into *out: the bytes the mod gave (its size, at most sizeof(T)), the rest zero, out->size the
   mod's. The caller has checked the size against the least it takes. LUMA_E_ARGUMENT: unreadable. */
template <typename T>
inline int32_t luma_struct_in(const LumaServiceKernel* kernel, const LumaMod* mod, const void* p, uint32_t size, T* out) {
    *out = T{};
    const int32_t status = kernel->copy_in(mod, out, p, size < sizeof(T) ? size : sizeof(T));
    if (status != LUMA_OK) {
        *out = T{};
    }
    return status;
}

/* value to the mod's struct at p, whose size field is `size` (checked by the caller): the first min(size, sizeof(T))
   bytes, the size field left as the mod wrote it. LUMA_E_ARGUMENT: p cannot be written. */
template <typename T>
inline int32_t luma_struct_out(const LumaServiceKernel* kernel, const LumaMod* mod, void* p, uint32_t size, T value) {
    value.size = size;
    return kernel->copy_out(mod, p, &value, size < sizeof(T) ? size : sizeof(T));
}

/* A text the mod gave into out, with its 0. LUMA_OK; LUMA_E_ARGUMENT: null, or unreadable before its 0;
   LUMA_E_TOO_LARGE: longer than N - 1 bytes. length (may be null): its bytes without the 0. */
template <uint64_t N>
inline int32_t luma_text_in(const LumaServiceKernel* kernel, const LumaMod* mod, const char* text, char (&out)[N],
                            uint64_t* length = nullptr) {
    out[0] = 0;
    return kernel->copy_text(mod, text, out, N, length);
}

/* Whether the mod's out pointer p can take a T, leaving it as it was (its bytes read and written back): checked
   before a call does anything, so a call whose out is wrong does nothing. LUMA_E_ARGUMENT: null, or p cannot be read
   and written. */
template <typename T>
inline int32_t luma_out_ready(const LumaServiceKernel* kernel, const LumaMod* mod, T* p) {
    unsigned char bytes[sizeof(T)];
    if (p == nullptr || kernel->copy_in(mod, bytes, p, sizeof(T)) != LUMA_OK) {
        return LUMA_E_ARGUMENT;
    }
    return kernel->copy_out(mod, p, bytes, sizeof(T));
}

/* One value (a handle, a count) to the mod's out pointer p. LUMA_E_ARGUMENT: null, or p cannot be written. */
template <typename T>
inline int32_t luma_value_out(const LumaServiceKernel* kernel, const LumaMod* mod, T* p, T value) {
    return p == nullptr ? LUMA_E_ARGUMENT : kernel->copy_out(mod, p, &value, sizeof(T));
}
#endif

#endif
