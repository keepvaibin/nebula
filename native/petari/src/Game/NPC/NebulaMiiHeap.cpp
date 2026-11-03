#include <MiiInternalHeap.h>

#include "Game/System/NebulaArena.hpp"

// The Mii library's blocks come from the game's own allocator (the current heap, as its `new` uses), 32-byte
// aligned for the texture and vertex data they hold, each as large as it needs: no work buffer of its own.
extern "C" void* NebulaMiiAlloc(u32 size) {
    return NebulaArena::alloc(size, 32, nullptr);
}

extern "C" void NebulaMiiFree(void* block) {
    if (block != nullptr) {
        NebulaArena::free(block, nullptr);
    }
}
