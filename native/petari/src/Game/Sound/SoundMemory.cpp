#include "Game/Sound/SoundMemory.hpp"
#include "Game/System/NebulaArena.hpp"
#include <cstring>

namespace {
    // Before each block, the game's own: the retired list and the flag that frees it.
    struct Header {
        Header* mNext;
        const NebulaAtomic< bool >* mReleased;
    };
    const u32 cHeaderBytes = 64;
    static_assert(sizeof(Header) <= cHeaderBytes, "a block's header fits before it");

    NebulaArena* sHeap = nullptr;
    Header* sRetired = nullptr;
}  // namespace

void SoundMemory::setHeap(NebulaArena* pHeap) {
    sHeap = pHeap;
}

void* SoundMemory::alloc(u32 bytes) {
    u8* memory = static_cast< u8* >(NebulaArena::alloc(cHeaderBytes + bytes, 64, sHeap));
    if (memory == nullptr) {
        NEBULA_PANIC("no memory for the sound system (%u bytes)", bytes);
    }
    std::memset(memory, 0, cHeaderBytes + bytes);
    return memory + cHeaderBytes;
}

void SoundMemory::retire(void* pMemory, const NebulaAtomic< bool >& rReleased) {
    Header* header = reinterpret_cast< Header* >(static_cast< u8* >(pMemory) - cHeaderBytes);
    header->mReleased = &rReleased;
    header->mNext = sRetired;
    sRetired = header;
}

void SoundMemory::collect() {
    Header** link = &sRetired;
    while (*link != nullptr) {
        Header* header = *link;
        if (*header->mReleased) {
            *link = header->mNext;
            NebulaArena::free(header, sHeap);
        } else {
            link = &header->mNext;
        }
    }
}
