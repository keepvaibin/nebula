/* SPDX-License-Identifier: GPL-3.0-only */
/* Providing a LumaCore service (services-registry contract 6.2, 7, 8): between the game module's kernel and the
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
       A mod the kernel does not know (or after its end): null, -1, 0. */
    const char* (*mod_id)(const LumaMod* mod);
    int32_t (*mod_order)(const LumaMod* mod);
    int32_t (*granted)(const LumaMod* mod, const char* permission);
    /* 1: known and not being taken out; 0: unknown, or after its stop. */
    int32_t (*mod_live)(const LumaMod* mod);
    /* 1 on the game thread. */
    int32_t (*game_thread)(void);
    /* A mistake of the mod: the row `code` (the provider's rows, or the registry's 100 to 119) with `args`; `where` is
       "<service>.<function>". Kept and said as LumaCore.report does: once in the log, kept for the Mods page, repeats
       counted. */
    void (*report)(const LumaMod* mod, const char* where, uint32_t code, const char* const* args, uint32_t arg_count);
    /* Queues `node` for its mod (from any thread). LUMA_OK: queued, or queued already; LUMA_E_CANCELLED: the mod is
       gone or unknown, nothing queued (the node stays the provider's); LUMA_E_ARGUMENT: a null node, mod or deliver. */
    int32_t (*post)(LumaEventNode* node);
    /* Game thread, from a node's deliver: calls fn(user, event) for the mod (through the guard, plan 1.6, which names
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

#ifdef __cplusplus
}
#endif

#endif
