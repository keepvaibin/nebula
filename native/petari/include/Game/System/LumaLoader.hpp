#pragma once

// Binds a mod's native library to the game by name (luma.h): its functions, fields, classes and hooks.

#include <luma_host.h>
#include <nebula_types.h>

// The host's lookup of a service by name (LumaCore.service), given to the module once before any mod loads. Null: no
// such service, or an older minor. *permission: the permission a mod needs for it, or null when it needs none.
typedef const void* (*LumaServiceFinder)(const char* name, uint32_t major, uint32_t minor, const char** permission);

namespace luma {
    const LumaCore* core();

    // Binds a mod whole or not at all (docs/modding/language/compiled-form.md): every name, then every hook as `hook`
    // would take it, then the slots, its begin and its start; only then its hooks. False: refused, pRefusal says why
    // (the validator's code and parameters). The kernel closes pLibrary with `close` when none of its code can run any
    // more.
    bool loadMod(const LumaModInfo* pInfo, const char* pId, const char* pFile, s32 order, void* pLibrary, LumaCloseLibrary close,
                 const char* const* pGranted, u32 grantedCount, LumaRefusal* pRefusal);
    // loadMod in two steps, for the start: beginMod binds the slots, runs its begin and then its layers call, and keeps
    // the mod waiting (later mods bind to its exports); startMod runs its start and publishes its hooks; dropMod ends a
    // waiting mod that will not start (a mod it needs did not start), as a refused start ends. The layers call's answer
    // stays readable through chosenLayers until the mod ends. False: refused, pRefusal says why.
    bool beginMod(const LumaModInfo* pInfo, const char* pId, const char* pFile, s32 order, void* pLibrary, LumaCloseLibrary close,
                  const char* const* pGranted, u32 grantedCount, LumaRefusal* pRefusal);
    bool startMod(const LumaModInfo* pInfo, LumaRefusal* pRefusal);
    void dropMod(const LumaModInfo* pInfo);
    // The names a waiting or started mod's layers call answered, each with its NUL, and their bytes; null when it has no
    // layers call, chose none, or is not loaded.
    const char* chosenLayers(const LumaModInfo* pInfo, u64* pBytes);
    // Takes its hooks out and runs its stop; after the grace, its finish, its memory and its library.
    void unloadMod(const LumaModInfo* pInfo);
    // Once a frame on the game thread: replaced hook lists and taken-out mods whose grace is over are let go.
    void frame();
    // Any thread: the mod taken out at the next frame's start (reload.md 2). False: not queued, said in the log.
    bool requestTakeOut(const char* pId);
    // At a frame's start on the game thread, before frame() and the services' deliveries: each queued mod with the
    // mods that use it, the last started first. One load when nothing is queued.
    void takeOutRequested();

    // The guard's view of a mod (LumaGuard.cpp): its id, its library's file name, its library's image (0, 0: none), and
    // its fault flag (atomic; 1 once the guard caught a fault in its code).
    const char* modId(const LumaMod* pMod);
    const char* modFile(const LumaMod* pMod);
    void modCode(const LumaMod* pMod, uintptr_t* pBase, uintptr_t* pBegin, uintptr_t* pEnd);
    u32* modFaulted(LumaMod* pMod);
    // Its code-time slot in the guard (LumaGuard.cpp): 0 none, else the slot's index + 1.
    u32* modCostSlot(LumaMod* pMod);
}
