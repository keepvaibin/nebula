#pragma once

// Binds a mod's native library to the game by name (luma.h): its functions, fields, classes and hooks.

#include <luma.h>
#include <nebula_types.h>

namespace luma {
    const LumaCore* core();

    // Fills every slot the mod's library lists, adds its hooks at `order` (its place in the player's list) and runs its
    // start. False when a name it needs is missing or its start refuses: nothing of it is left bound, and pMessage
    // holds the missing names, or why.
    bool loadMod(const LumaModInfo* pInfo, const char* pName, s32 order, char* pMessage, u32 messageSize);
    // Runs the mod's stop and takes its hooks out.
    void unloadMod(const LumaModInfo* pInfo);
}
