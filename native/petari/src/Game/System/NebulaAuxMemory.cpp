#include "Game/System/NebulaAuxMemory.hpp"

#include <string.h>

#include "Game/Util/RenderSignals.hpp"

namespace {
    u8* sBase;
    u32 sSize;
    u32 sNext;
}  // namespace

namespace NebulaAuxMemory {
    void init(void* pBase, u32 size) {
        sBase = static_cast< u8* >(pBase);
        sSize = size;
        sNext = cReserved;
    }

    u8* base() {
        return sBase;
    }

    u32 size() {
        return sSize;
    }

    u32 alloc(u32 amount) {
        const u32 offset = sNext;
        sNext += amount;
        return offset;
    }

    // The renderer may hold images it took from either side: it learns of the rewrite.
    void write(u32 offset, const void* pSource, u32 length) {
        memcpy(sBase + offset, pSource, length);
        NebulaRender::memoryRewritten(sBase + offset, length);
    }

    void read(void* pDestination, u32 offset, u32 length) {
        memcpy(pDestination, sBase + offset, length);
        NebulaRender::memoryRewritten(pDestination, length);
    }
}  // namespace NebulaAuxMemory
