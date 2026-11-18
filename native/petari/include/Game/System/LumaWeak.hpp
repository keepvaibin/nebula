#pragma once

// Weak references to the game's objects (LumaCore.weak and weak_get, N-4003): one table in the kernel for every mod,
// emptied as an object goes. The game tells it directly, on whatever thread an object goes: NameObj's destructor
// (gone), NebulaArena::free (freed) and NebulaArena::destroy (released, the arena's whole range, whose objects' own
// destructors mostly do not run). No hook is added anywhere; while no handle is held each of those costs one load.

#include <luma.h>
#include <stdint.h>

namespace luma {
    namespace weak {
        // A handle for the object (as its NameObj; any subobject of a game object may be given): nonzero, the same one
        // for every mod while the object lives; 0 for null, an object that is no NameObj, or no memory (said for pMod).
        uint64_t make(const LumaMod* pMod, const void* pObject);
        // The object (as its NameObj) while it lives; null once it went, for 0, or for a handle never given.
        const void* get(uint64_t handle);

        void gone(const void* pObject);                        // NameObj::~NameObj: `this`
        void freed(const void* pData);                         // NebulaArena::free: the block
        void released(const void* pStart, const void* pEnd);  // NebulaArena::destroy: its memory
    }  // namespace weak
}  // namespace luma
