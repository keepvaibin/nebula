#pragma once

// Binds a mod's native library to the game by name (luma.h): its functions, fields, classes and hooks.

#include <luma_host.h>
#include <nebula_types.h>

namespace luma {
    const LumaCore* core();

    // Binds a mod whole or not at all (loader contract 2, LR-2): every name, then every hook as `hook` would take it,
    // then the slots, its begin and its start; only then its hooks. False: refused, pRefusal says why (the
    // validator's code and parameters). The kernel closes pLibrary with `close` when none of its code can run any more.
    bool loadMod(const LumaModInfo* pInfo, const char* pId, const char* pFile, s32 order, void* pLibrary, LumaCloseLibrary close,
                 LumaRefusal* pRefusal);
    // Takes its hooks out and runs its stop; after the grace, its finish, its memory and its library (LR-5).
    void unloadMod(const LumaModInfo* pInfo);
    // Once a frame on the game thread: replaced hook lists and taken-out mods whose grace is over are let go.
    void frame();
}
